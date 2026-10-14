/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: Harmony's pure engine (cr_harmony.h): the map, the listener, the suggestion engine. See the
 * Harmony Preview Edge Case Spec for the decisions the suggestion engine ports to code: the key-inference score and
 * hysteresis, the candidate tables and their allocation order, the two-tier duplicate-collapse fallback, and the
 * voice-leading cost function (the map reuses the last). */
#include "cr_harmony.h"

/* ============================================================== scales === */
/* Scale membership, as a 12-bit mask relative to the tonic (bit i set: scale degree i semitones above the
 * tonic is in the scale). Major: 0,2,4,5,7,9,11. Natural minor: 0,2,3,5,7,8,10. */
#define CR_HARM_MAJOR_MASK 0x0AB5u
#define CR_HARM_MINOR_MASK 0x05ADu

static int crh_popcount12(unsigned m)
{
    int n = 0;
    m &= 0x0FFFu;
    while (m) { n += (int)(m & 1u); m >>= 1; }
    return n;
}

/* a scale's membership mask rotated so bit i means "pitch class (tonic + i) mod 12 is in the scale" */
static unsigned crh_scale_mask_at(uint8_t tonic_pc, uint8_t scale)
{
    unsigned base = scale == CR_SCALE_MINOR ? CR_HARM_MINOR_MASK : CR_HARM_MAJOR_MASK;
    unsigned rot = (unsigned)(tonic_pc % 12u);
    return ((base << rot) | (base >> (12u - rot))) & 0x0FFFu;
}

/* ===================================================== a chord's pitch classes === */
/* The pitch-class set of (quality, ext) relative to its own root (root_pc contributes bit 0 of the rotation
 * done by the caller): built once via cr_chord_base_ex at a safe, unclipped root so every interval the table
 * carries actually lands, then reduced mod 12. Shared by key-scoring, qualifying-history checks, and the
 * display "common pitch classes" figure -- one source of truth, not three. */
static unsigned crh_pcset_shape(uint8_t quality, uint8_t ext)
{
    uint8_t tmp[CR_CHORD_MAX];
    int n = cr_chord_base_ex(60, quality, ext, 0, tmp);
    unsigned mask = 0;
    int i;
    for (i = 0; i < n; i++) mask |= 1u << ((unsigned)(tmp[i] - 60) % 12u);
    return mask;
}
static unsigned crh_pcset_at(uint8_t root_pc, uint8_t quality, uint8_t ext)
{
    unsigned shape = crh_pcset_shape(quality, ext);
    unsigned rot = (unsigned)(root_pc % 12u);
    return ((shape << rot) | (shape >> (12u - rot))) & 0x0FFFu;
}
static unsigned crh_pcset_of_notes(const uint8_t *notes, int n)
{
    unsigned mask = 0;
    int i;
    for (i = 0; i < n; i++) mask |= 1u << (unsigned)(notes[i] % 12);
    return mask;
}

/* ===================================================================== history === */
void cr_harm_hist_clear(cr_harm_hist_t *h) { h->n = 0; }

int cr_harm_hist_push(cr_harm_hist_t *h, uint8_t root_pc, uint8_t quality, uint8_t ext)
{
    unsigned pcset = crh_pcset_at(root_pc, quality, ext);
    int i;
    if (crh_popcount12(pcset) < 3) return 0;
    if (h->n > 0) {
        const cr_harm_chord_t *top = &h->e[h->n - 1];
        if (top->root_pc == root_pc && top->quality == quality && top->ext == ext) return 1;
    }
    if (h->n < CR_HARM_HIST) {
        h->e[h->n].root_pc = root_pc; h->e[h->n].quality = quality; h->e[h->n].ext = ext;
        h->n++;
    } else {
        for (i = 0; i < CR_HARM_HIST - 1; i++) h->e[i] = h->e[i + 1];
        h->e[CR_HARM_HIST - 1].root_pc = root_pc;
        h->e[CR_HARM_HIST - 1].quality = quality;
        h->e[CR_HARM_HIST - 1].ext = ext;
    }
    return 1;
}

/* ==================================================================== listener === */
void cr_harm_listen_clear(cr_harm_listen_t *L)
{
    int i;
    for (i = 0; i < CR_HARM_LISTEN_N; i++) L->f[i].live = 0;
    L->next_ord = 0;
}

static int crh_same_id(const cr_harm_chord_t *a, const cr_harm_chord_t *b)
{
    return a->root_pc == b->root_pc && a->quality == b->quality && a->ext == b->ext;
}

/* sounded at least the settle time between t0 and t (a t stamped before t0 -- a report drained after the caller's
 * clock was read -- counts as no time at all, never as a wrap-around eternity) */
static int crh_settled(uint32_t t0, uint32_t t) { return (int32_t)(t - t0) >= (int32_t)CR_HARM_SETTLE_MS; }

static int crh_commit(cr_harm_follow_t *f, cr_harm_hist_t *h)
{
    f->done = f->cur;
    f->committed = 1;
    return cr_harm_hist_push(h, f->cur.root_pc, f->cur.quality, f->cur.ext) ? 1 : 0;
}

static cr_harm_follow_t *crh_follow_find(cr_harm_listen_t *L, uint16_t gid)
{
    int i;
    for (i = 0; i < CR_HARM_LISTEN_N; i++) if (L->f[i].live && L->f[i].gid == gid) return &L->f[i];
    return 0;
}

/* a slot for a new gesture: a free one, else the oldest committed one (already in history: only its final-state
 * check is lost), else the oldest of all (a chord that had not sounded long enough yet) */
static cr_harm_follow_t *crh_follow_alloc(cr_harm_listen_t *L)
{
    int i, best = -1, pass;
    for (i = 0; i < CR_HARM_LISTEN_N; i++) if (!L->f[i].live) return &L->f[i];
    for (pass = 0; pass < 2 && best < 0; pass++)
        for (i = 0; i < CR_HARM_LISTEN_N; i++) {
            if (pass == 0 && !L->f[i].committed) continue;
            if (best < 0 || (int16_t)(L->f[i].ord - L->f[best].ord) < 0) best = i;
        }
    return &L->f[best];
}

int cr_harm_listen_event(cr_harm_listen_t *L, cr_harm_hist_t *h, uint32_t gid, int16_t root, uint8_t quality,
                         uint8_t ext, int on, uint32_t t_ms)
{
    cr_harm_follow_t *f;
    cr_harm_chord_t id;
    if (ext & CR_GX_BASS) return 0;                     /* a Split Bass bass-side note: not a chord */
    id.root_pc = (uint8_t)(((int)root % 12 + 12) % 12);
    id.quality = quality;
    id.ext = ext;
    if (on == 1) {
        f = crh_follow_alloc(L);
        f->live = 1; f->committed = 0;
        f->gid = (uint16_t)gid; f->ord = L->next_ord++;
        f->t0 = t_ms;
        f->cur = id;
        return 0;
    }
    f = crh_follow_find(L, (uint16_t)gid);
    if (!f) return 0;                                   /* not followed (evicted, or begun before a clear) */
    f->cur = id;                                        /* key-up / end: the chord as it is now (its final state) */
    if (on == CR_G_KEYUP) {                             /* latched: it sounds on; only a changed chord commits now */
        if (f->committed && !crh_same_id(&f->cur, &f->done)) return crh_commit(f, h);
        return 0;
    }
    /* the end: commit what sounded long enough and is not already committed as it is */
    {
        int n = 0;
        if (f->committed ? !crh_same_id(&f->cur, &f->done) : crh_settled(f->t0, t_ms)) n = crh_commit(f, h);
        f->live = 0;
        return n;
    }
}

int cr_harm_listen_tick(cr_harm_listen_t *L, cr_harm_hist_t *h, uint32_t now_ms)
{
    int n = 0;
    for (;;) {                                          /* the oldest settled, uncommitted gesture first */
        int i, best = -1;
        for (i = 0; i < CR_HARM_LISTEN_N; i++) {
            const cr_harm_follow_t *f = &L->f[i];
            if (!f->live || f->committed || !crh_settled(f->t0, now_ms)) continue;
            if (best < 0 || (int16_t)(f->ord - L->f[best].ord) < 0) best = i;
        }
        if (best < 0) return n;
        n += crh_commit(&L->f[best], h);
    }
}

/* ========================================================================= key === */
void cr_harm_key_clear(cr_harm_key_t *key) { key->held = 0; key->tonic_pc = 0; key->scale = 0; key->score = 0; }

void cr_harm_key_explicit(cr_harm_key_t *key, uint8_t tonic_pc, uint8_t scale)
{
    key->held = 1; key->tonic_pc = (uint8_t)(tonic_pc % 12u); key->scale = scale; key->score = 0;
}

static int crh_quality_matches_mode(uint8_t quality, uint8_t scale)
{
    return scale == CR_SCALE_MINOR ? quality == CR_Q_MIN : quality == CR_Q_MAJ;
}

void cr_harm_key_infer(cr_harm_key_t *key, const cr_harm_hist_t *hist)
{
    unsigned pcset[CR_HARM_HIST];
    int n[CR_HARM_HIST], cand, j;
    uint16_t best_score = 0; int best_match_rank = 99, best_tonic = -1, best_scale = -1;
    /* acquisition needs 3 qualifying entries, not 2 (raised 2026-10-13): with only 2, the newest chord's
     * weight-4 tonic-quality bonus can transiently outweigh the evidence for the musically correct key -- a
     * plain i-iv alternation (e.g. Cm then Fm) briefly acquired F minor before self-correcting to C minor on
     * the 3rd chord. Waiting for a 3rd entry avoids that transient outright; the switching margin below
     * (unchanged, still 3 points) is what protects against flicker once a key IS held. */
    if (hist->n < 3) return;        /* no key yet; stay as *key already is (held or not) */
    for (j = 0; j < hist->n; j++) {
        pcset[j] = crh_pcset_at(hist->e[j].root_pc, hist->e[j].quality, hist->e[j].ext);
        n[j] = crh_popcount12(pcset[j]);
    }
    for (cand = 0; cand < 24; cand++) {
        uint8_t tonic = (uint8_t)(cand >> 1), scale = (uint8_t)(cand & 1);
        unsigned smask = crh_scale_mask_at(tonic, scale);
        uint16_t score = 0;
        int match_rank = 99, r;
        for (r = 0; r < hist->n && r < 4; r++) {
            int idx = hist->n - 1 - r;             /* newest first */
            int weight = 4 - r;
            int d = crh_popcount12(pcset[idx] & smask);
            int membership = (4 * d) / n[idx];     /* 0..4, integer floor */
            int bonus = (hist->e[idx].root_pc == tonic && crh_quality_matches_mode(hist->e[idx].quality, scale)) ? 1 : 0;
            score = (uint16_t)(score + weight * (membership + bonus));
            if (bonus && r < match_rank) match_rank = r;
        }
        /* better(cand, best)? score desc, then match_rank asc, then tonic asc, then scale asc (major first) */
        if (score > best_score ||
            (score == best_score && match_rank < best_match_rank) ||
            (score == best_score && match_rank == best_match_rank && (int)tonic < best_tonic) ||
            (score == best_score && match_rank == best_match_rank && (int)tonic == best_tonic && (int)scale < best_scale)) {
            best_score = score; best_match_rank = match_rank; best_tonic = tonic; best_scale = scale;
        }
    }
    if (!key->held) {
        key->held = 1; key->tonic_pc = (uint8_t)best_tonic; key->scale = (uint8_t)best_scale; key->score = best_score;
        return;
    }
    /* refresh the held key's own current score regardless of whether it switches */
    {
        unsigned smask = crh_scale_mask_at(key->tonic_pc, key->scale);
        uint16_t hs = 0; int r;
        for (r = 0; r < hist->n && r < 4; r++) {
            int idx = hist->n - 1 - r, weight = 4 - r;
            int d = crh_popcount12(pcset[idx] & smask);
            int membership = (4 * d) / n[idx];
            int bonus = (hist->e[idx].root_pc == key->tonic_pc && crh_quality_matches_mode(hist->e[idx].quality, key->scale)) ? 1 : 0;
            hs = (uint16_t)(hs + weight * (membership + bonus));
        }
        if (best_tonic == key->tonic_pc && best_scale == key->scale) { key->score = hs; return; }
        if (best_score >= hs + 3) { key->tonic_pc = (uint8_t)best_tonic; key->scale = (uint8_t)best_scale; key->score = best_score; }
        else { key->score = hs; }
    }
}

/* =============================================================== candidates === */
typedef struct { int8_t semi; uint8_t quality, ext, tension, rank, borrowed; } crh_cand_t;

/* Major: I, vi, bVI(borrowed) / ii7, IVmaj7, iv(borrowed), bVII(borrowed) / V7, vii-dim */
static const crh_cand_t CRH_MAJOR[] = {
    {  0, CR_Q_MAJ, 0,                         CR_HARM_REST, 0, 0 },
    {  9, CR_Q_MIN, 0,                         CR_HARM_REST, 1, 0 },
    {  8, CR_Q_MAJ, 0,                         CR_HARM_REST, 2, 1 },
    {  2, CR_Q_MIN, CR_EXT_M7,                 CR_HARM_MOVE, 0, 0 },
    {  5, CR_Q_MAJ, CR_EXT_MAJ7,                CR_HARM_MOVE, 1, 0 },
    {  5, CR_Q_MIN, 0,                         CR_HARM_MOVE, 2, 1 },
    { 10, CR_Q_MAJ, 0,                         CR_HARM_MOVE, 3, 1 },
    {  7, CR_Q_MAJ, CR_EXT_M7,                 CR_HARM_PULL, 0, 0 },
    { 11, CR_Q_DIM, 0,                         CR_HARM_PULL, 1, 0 },
};
/* Natural minor: bVImaj7, i / iv(m9), ii-half-dim7 / V7(borrowed, major), bVII, v */
static const crh_cand_t CRH_MINOR[] = {
    {  8, CR_Q_MAJ, CR_EXT_MAJ7,                CR_HARM_REST, 0, 0 },
    {  0, CR_Q_MIN, 0,                         CR_HARM_REST, 1, 0 },
    {  5, CR_Q_MIN, (uint8_t)(CR_EXT_M7 | CR_EXT_9), CR_HARM_MOVE, 0, 0 },
    {  2, CR_Q_DIM, CR_EXT_M7,                 CR_HARM_MOVE, 1, 0 },
    {  7, CR_Q_MAJ, CR_EXT_M7,                 CR_HARM_PULL, 0, 1 },
    { 10, CR_Q_MAJ, 0,                         CR_HARM_PULL, 1, 0 },
    {  7, CR_Q_MIN, 0,                         CR_HARM_PULL, 2, 0 },
};
#define CRH_NMAJOR (int)(sizeof CRH_MAJOR / sizeof CRH_MAJOR[0])
#define CRH_NMINOR (int)(sizeof CRH_MINOR / sizeof CRH_MINOR[0])

/* ========================================================== voice-leading cost === */
static int crh_move_cost(int a, int b)
{
    int d = a > b ? a - b : b - a;
    return d + (d > 7 ? 2 * (d - 7) : 0);
}
/* Order-preserving minimum-cost alignment of two sorted note arrays (a classic edit-distance DP, bounded by
 * CR_CHORD_MAX on each side): a matched pair costs crh_move_cost (0 when the notes are identical -- an exact
 * common tone); an unmatched note on either side costs 12. Reports the backtrace's exact-match count (the
 * search's own tie-break use, NOT the display "common pitch classes" figure -- see crh_pcset_of_notes for
 * that), added/removed voice counts, and the raw (undoubled) summed movement of matched pairs. */
static int crh_align(const uint8_t *s, int ns, const uint8_t *t, int nt, int *exact, int *added, int *removed, int *move)
{
    int16_t dp[CR_CHORD_MAX + 1][CR_CHORD_MAX + 1];
    uint8_t pd[CR_CHORD_MAX + 1][CR_CHORD_MAX + 1];
    int i, j;
    dp[0][0] = 0;
    for (i = 1; i <= ns; i++) { dp[i][0] = (int16_t)(dp[i - 1][0] + 12); pd[i][0] = 1; }
    for (j = 1; j <= nt; j++) { dp[0][j] = (int16_t)(dp[0][j - 1] + 12); pd[0][j] = 2; }
    for (i = 1; i <= ns; i++) {
        for (j = 1; j <= nt; j++) {
            int mc = crh_move_cost(s[i - 1], t[j - 1]);
            int diag = dp[i - 1][j - 1] + mc, up = dp[i - 1][j] + 12, left = dp[i][j - 1] + 12;
            if (diag <= up && diag <= left) { dp[i][j] = (int16_t)diag; pd[i][j] = 0; }
            else if (up <= left) { dp[i][j] = (int16_t)up; pd[i][j] = 1; }
            else { dp[i][j] = (int16_t)left; pd[i][j] = 2; }
        }
    }
    *exact = *added = *removed = *move = 0;
    i = ns; j = nt;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && pd[i][j] == 0) {
            int d = s[i - 1] > t[j - 1] ? s[i - 1] - t[j - 1] : t[j - 1] - s[i - 1];
            if (d == 0) (*exact)++; else *move += d;
            i--; j--;
        } else if (i > 0 && (j == 0 || pd[i][j] == 1)) { (*removed)++; i--; }
        else { (*added)++; j--; }
    }
    return dp[ns][nt];
}

/* lexicographic compare of two equal-length, ascending note arrays: <0 a smaller, >0 b smaller, 0 equal */
static int crh_notes_cmp(const uint8_t *a, const uint8_t *b, int n)
{
    int i;
    for (i = 0; i < n; i++) if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    return 0;
}

/* The lowest note-aligned MIDI placement of root_pc at or below anchor; oct steps of 12 from there give the
 * "sensible root octaves" the spec asks for -- one at/just-below the source chord, one an octave below, one
 * an octave above, bracketing the register the player is already in. */
static int crh_root_near(uint8_t root_pc, int anchor)
{
    int d = ((anchor - (int)root_pc) % 12 + 12) % 12;
    return anchor - d;
}

/* Builds (quality, ext) rooted near the source chord's register, over the supported rotation range -12..12 and
 * three bracketing octaves, and keeps the best against src_notes by: lower alignment cost; then fewer
 * added+removed; then more exact common tones; then smaller lowest-note movement; then lexicographic note
 * order (Edge Case Spec: Voice-leading contract). Rejects any placement cr_chord_base_ex had to clip at the
 * MIDI boundary (fewer notes than the unclipped shape) -- never accepts clipping as a valid voicing. Returns 0
 * if nothing unclipped exists anywhere in the search (out[] untouched); 1 otherwise. */
/* A lowest note outside [lo, hi] costs 2 a semitone (Harmony mode's map keeps a long walk near the middle of the
 * keyboard); cr_harm_suggest passes 0..127, so nothing changes for it. */
static int crh_window_pen(int low, int lo, int hi) { return low < lo ? 2 * (lo - low) : low > hi ? 2 * (low - hi) : 0; }

static int crh_search_voicing(uint8_t root_pc, uint8_t quality, uint8_t ext, int sixth_up,
                               const uint8_t *src_notes, int n_src, int win_lo, int win_hi, uint8_t out[CR_CHORD_MAX],
                               int *out_n, uint8_t *out_root_note, int8_t *out_rotation)
{
    uint8_t nominal_tmp[CR_CHORD_MAX];
    int nominal = cr_chord_base_ex(60, quality, ext, sixth_up, nominal_tmp);
    int anchor = n_src ? src_notes[0] : 60;
    int oct, k, found = 0;
    int best_cost = 0, best_ar = 0, best_exact = 0, best_lowmove = 0, best_root = 0, best_rot = 0;
    uint8_t best_notes[CR_CHORD_MAX]; int best_n = 0;
    for (oct = -1; oct <= 1; oct++) {
        int root_note = crh_root_near(root_pc, anchor) + 12 * oct;
        uint8_t base[CR_CHORD_MAX];
        int nbase;
        if (root_note < 0 || root_note > 127) continue;
        nbase = cr_chord_base_ex(root_note, quality, ext, sixth_up, base);
        if (nbase != nominal) continue;             /* clipped at this octave: not a valid realization */
        for (k = -12; k <= 12; k++) {
            uint8_t voiced[CR_CHORD_MAX];
            int nv = cr_voice_apply(base, nbase, k, voiced);
            int cost, exact, added, removed, move, ar, lowmove;
            if (nv != nbase) continue;               /* cr_voice_apply never drops notes; defensive only */
            cost = crh_align(src_notes, n_src, voiced, nv, &exact, &added, &removed, &move) +
                   crh_window_pen(voiced[0], win_lo, win_hi);
            ar = added + removed;
            lowmove = n_src ? (src_notes[0] > voiced[0] ? src_notes[0] - voiced[0] : voiced[0] - src_notes[0]) : 0;
            if (!found ||
                cost < best_cost ||
                (cost == best_cost && ar < best_ar) ||
                (cost == best_cost && ar == best_ar && exact > best_exact) ||
                (cost == best_cost && ar == best_ar && exact == best_exact && lowmove < best_lowmove) ||
                (cost == best_cost && ar == best_ar && exact == best_exact && lowmove == best_lowmove &&
                 found && nv == best_n && crh_notes_cmp(voiced, best_notes, nv) < 0)) {
                int i;
                best_cost = cost; best_ar = ar; best_exact = exact; best_lowmove = lowmove;
                best_n = nv; for (i = 0; i < nv; i++) best_notes[i] = voiced[i];
                best_root = root_note; best_rot = k;
                found = 1;
            }
        }
    }
    if (!found) return 0;
    { int i; for (i = 0; i < best_n; i++) out[i] = best_notes[i]; }
    *out_n = best_n;
    *out_root_note = (uint8_t)best_root;
    *out_rotation = (int8_t)best_rot;
    return 1;
}

static int crh_same_notes(const uint8_t *a, int na, const uint8_t *b, int nb)
{
    int i;
    if (na != nb) return 0;
    for (i = 0; i < na; i++) if (a[i] != b[i]) return 0;
    return 1;
}

int cr_harm_suggest(const cr_harm_key_t *key, const cr_chord_info_t *source, int sixth_up, cr_harm_result_t out[3])
{
    const crh_cand_t *table; int ntable;
    uint8_t tonic, scale; int provisional;
    uint8_t used[3][CR_CHORD_MAX]; uint8_t used_n[3]; int nused = 0;
    int slot;
    if (!source->valid) return 0;
    if (key->held) {
        tonic = key->tonic_pc; scale = key->scale; provisional = 0;
    } else {
        tonic = source->root_pc;
        scale = source->quality == CR_Q_MIN ? CR_SCALE_MINOR : CR_SCALE_MAJOR;   /* major is the deterministic
                                                                                    fallback for anything else */
        provisional = 1;
    }
    if (scale == CR_SCALE_MINOR) { table = CRH_MINOR; ntable = CRH_NMINOR; } else { table = CRH_MAJOR; ntable = CRH_NMAJOR; }

    for (slot = 0; slot < 3; slot++) {
        uint8_t want_tension = (uint8_t)slot;     /* CR_HARM_REST=0, MOVE=1, PULL=2 */
        int i, placed = 0;
        out[slot].kind = CR_HARM_UNAVAILABLE;
        out[slot].tension = want_tension;
        out[slot].nnotes = 0;
        /* REST, then MOVE, then PULL, each by rank -- never revisit or displace a lower-tension slot already
         * placed above this loop iteration */
        for (i = 0; i < ntable && !placed; i++) {
            /* candidates are tried lowest-rank-first within this tension; ranks are small, densely assigned
             * integers starting at 0, so "rank == i" visits them in order as i counts up */
            int chosen = -1, j;
            for (j = 0; j < ntable; j++) {
                if (table[j].tension == want_tension && (int)table[j].rank == i &&
                    !(provisional && table[j].borrowed)) { chosen = j; break; }
            }
            if (chosen < 0) continue;
            {
                const crh_cand_t *c = &table[chosen];
                uint8_t root_pc = (uint8_t)(((int)tonic + c->semi + 12) % 12);
                uint8_t notes[CR_CHORD_MAX]; int nn; uint8_t voiced_root; int8_t rotation;
                if (crh_search_voicing(root_pc, c->quality, c->ext, sixth_up, source->notes, source->nnotes, 0, 127, notes,
                                        &nn, &voiced_root, &rotation)) {
                    int dup = 0, u;
                    for (u = 0; u < nused; u++) if (crh_same_notes(notes, nn, used[u], used_n[u])) { dup = 1; break; }
                    if (!dup) {
                        unsigned pc_src = crh_pcset_of_notes(source->notes, source->nnotes);
                        unsigned pc_tgt = crh_pcset_of_notes(notes, nn);
                        int exact, added, removed, move;
                        crh_align(source->notes, source->nnotes, notes, nn, &exact, &added, &removed, &move);
                        out[slot].kind = CR_HARM_SUGGEST;
                        out[slot].root_pc = root_pc; out[slot].quality = c->quality; out[slot].ext = c->ext;
                        out[slot].nnotes = (uint8_t)nn;
                        { int z; for (z = 0; z < nn; z++) out[slot].notes[z] = notes[z]; }
                        out[slot].root_note = voiced_root; out[slot].rotation = rotation;
                        out[slot].common = (uint8_t)crh_popcount12(pc_src & pc_tgt);
                        out[slot].added = (uint8_t)added; out[slot].removed = (uint8_t)removed;
                        out[slot].move = (uint16_t)move;
                        used_n[nused] = (uint8_t)nn;
                        { int z; for (z = 0; z < nn; z++) used[nused][z] = notes[z]; }
                        nused++;
                        placed = 1;
                    }
                }
            }
        }
        if (!placed) {
            /* fallback: a different rotation/octave of the CURRENT chord itself */
            uint8_t notes[CR_CHORD_MAX]; int nn; uint8_t voiced_root; int8_t rotation;
            if (crh_search_voicing(source->root_pc, source->quality, source->ext, sixth_up,
                                    source->notes, source->nnotes, 0, 127, notes, &nn, &voiced_root, &rotation) &&
                !crh_same_notes(notes, nn, source->notes, source->nnotes)) {
                int dup = 0, u;
                for (u = 0; u < nused; u++) if (crh_same_notes(notes, nn, used[u], used_n[u])) { dup = 1; break; }
                if (!dup) {
                    int exact, added, removed, move;
                    unsigned pc_src = crh_pcset_of_notes(source->notes, source->nnotes);
                    unsigned pc_tgt = crh_pcset_of_notes(notes, nn);
                    crh_align(source->notes, source->nnotes, notes, nn, &exact, &added, &removed, &move);
                    out[slot].kind = CR_HARM_VOICING;
                    out[slot].root_pc = source->root_pc; out[slot].quality = source->quality; out[slot].ext = source->ext;
                    out[slot].nnotes = (uint8_t)nn;
                    { int z; for (z = 0; z < nn; z++) out[slot].notes[z] = notes[z]; }
                    out[slot].root_note = voiced_root; out[slot].rotation = rotation;
                    out[slot].common = (uint8_t)crh_popcount12(pc_src & pc_tgt);
                    out[slot].added = (uint8_t)added; out[slot].removed = (uint8_t)removed;
                    out[slot].move = (uint16_t)move;
                    used_n[nused] = (uint8_t)nn;
                    { int z; for (z = 0; z < nn; z++) used[nused][z] = notes[z]; }
                    nused++;
                }
            }
        }
    }
    return 1;
}

/* ===================================================================== the map === */
/* the black roots D#4 F#4 G#4 A#4 C#5 D#5 F#5: the white roots left of them, D4 F4 G4 A4 C5 D5 F5 */
const uint8_t CR_HMAP_BW[CR_HMAP_B] = { 0, 2, 3, 4, 6, 7, 9 };

typedef struct { uint8_t semi, quality, cat; } crh_pool_t;      /* a chord on the home key, semitones above its tonic */
static const crh_pool_t CRH_POOL_MAJOR[] = {
    { 0, CR_Q_MAJ, CR_HCAT_DIATONIC }, { 2, CR_Q_MIN, CR_HCAT_DIATONIC }, { 4, CR_Q_MIN, CR_HCAT_DIATONIC },
    { 5, CR_Q_MAJ, CR_HCAT_DIATONIC }, { 7, CR_Q_MAJ, CR_HCAT_DIATONIC }, { 9, CR_Q_MIN, CR_HCAT_DIATONIC },
    { 11, CR_Q_DIM, CR_HCAT_DIATONIC },
    { 5, CR_Q_MIN, CR_HCAT_BORROWED }, { 8, CR_Q_MAJ, CR_HCAT_BORROWED }, { 10, CR_Q_MAJ, CR_HCAT_BORROWED },
    { 3, CR_Q_MAJ, CR_HCAT_BORROWED }, { 0, CR_Q_MIN, CR_HCAT_BORROWED },
    { 2, CR_Q_MAJ, CR_HCAT_SECDOM }, { 4, CR_Q_MAJ, CR_HCAT_SECDOM }, { 9, CR_Q_MAJ, CR_HCAT_SECDOM },
    { 11, CR_Q_MAJ, CR_HCAT_SECDOM },
    { 1, CR_Q_MAJ, CR_HCAT_CHROMATIC }, { 7, CR_Q_SUS, CR_HCAT_CHROMATIC },
};
static const crh_pool_t CRH_POOL_MINOR[] = {
    { 0, CR_Q_MIN, CR_HCAT_DIATONIC }, { 2, CR_Q_DIM, CR_HCAT_DIATONIC }, { 3, CR_Q_MAJ, CR_HCAT_DIATONIC },
    { 5, CR_Q_MIN, CR_HCAT_DIATONIC }, { 7, CR_Q_MIN, CR_HCAT_DIATONIC }, { 8, CR_Q_MAJ, CR_HCAT_DIATONIC },
    { 10, CR_Q_MAJ, CR_HCAT_DIATONIC }, { 7, CR_Q_MAJ, CR_HCAT_DIATONIC },          /* (V: the harmonic minor's) */
    { 11, CR_Q_DIM, CR_HCAT_BORROWED }, { 0, CR_Q_MAJ, CR_HCAT_BORROWED }, { 5, CR_Q_MAJ, CR_HCAT_BORROWED },
    { 2, CR_Q_MAJ, CR_HCAT_SECDOM },
    { 1, CR_Q_MAJ, CR_HCAT_CHROMATIC }, { 7, CR_Q_SUS, CR_HCAT_CHROMATIC },
};
#define CRH_NPOOL_MAJOR (int)(sizeof CRH_POOL_MAJOR / sizeof CRH_POOL_MAJOR[0])
#define CRH_NPOOL_MINOR (int)(sizeof CRH_POOL_MINOR / sizeof CRH_POOL_MINOR[0])

static int crh_pcdist(int a, int b) { int d = ((a - b) % 12 + 12) % 12; return d > 6 ? 12 - d : d; }
/* semitones moved between two pitch-class sets: every note of each to the nearest note of the other, both ways (a
 * shared note moves 0). Voicing-free, so which chords are offered never depends on how the current one is voiced */
static int crh_vl(unsigned a, unsigned b)
{
    int s = 0, i, j;
    for (i = 0; i < 12; i++) {
        int ina = (int)((a >> i) & 1u), inb = (int)((b >> i) & 1u), best = 12;
        if (!ina && !inb) continue;
        for (j = 0; j < 12; j++)
            if ((((ina ? b : a) >> j) & 1u) && crh_pcdist(i, j) < best) best = crh_pcdist(i, j);
        s += (ina ? best : 0) + (inb ? best : 0);
    }
    return s;
}

/* a major chord with a chromatic third, a fifth above one of the home key's major or minor chords (not the tonic:
 * that is V itself) */
static int crh_is_secdom(uint8_t t, uint8_t scale, unsigned smask, uint8_t root, uint8_t q)
{
    static const uint8_t TG_MAJ[6] = { 0, 2, 4, 5, 7, 9 }, TG_MIN[6] = { 0, 3, 5, 7, 8, 10 };
    const uint8_t *tg = scale == CR_SCALE_MINOR ? TG_MIN : TG_MAJ;
    int i, target = (root + 5) % 12;
    if (q != CR_Q_MAJ || ((smask >> ((root + 4) % 12)) & 1u)) return 0;
    for (i = 1; i < 6; i++) if ((t + tg[i]) % 12 == target) return 1;
    return 0;
}

/* home-relative tension, doubled units (see cr_harmony.h) */
static int crh_tension(uint8_t t, uint8_t scale, unsigned smask, uint8_t root, uint8_t q)
{
    unsigned pcs = crh_pcset_at(root, q, 0), tri = crh_pcset_at(t, scale == CR_SCALE_MINOR ? CR_Q_MIN : CR_Q_MAJ, 0);
    unsigned lt = 1u << ((t + 11u) % 12u), f4 = 1u << ((t + 5u) % 12u);
    int x = 2 * (3 - crh_popcount12(pcs & tri));
    if (x < 0) x = 0;
    if (pcs & lt) x += 4;                                       /* the leading tone */
    if ((pcs & lt) && (pcs & f4)) x += 4;                       /* .. with the 4th: the dominant tritone */
    x += 3 * crh_popcount12(pcs & ~smask);                      /* notes outside the scale */
    if (q == CR_Q_MAJ && root == (t + 7) % 12) x += 4;          /* the home dominant */
    if (crh_is_secdom(t, scale, smask, root, q)) x += 4;        /* a secondary dominant */
    if (q == CR_Q_SUS) x += 4;                                  /* a suspension */
    return x;
}

/* how strongly destination d resolves the chord you are on: subtracted from d's tension */
static int crh_resolves(uint8_t t, uint8_t scale, unsigned smask, uint8_t cr, uint8_t cq, uint8_t cx, uint8_t dr,
                        uint8_t dq)
{
    int dominant = cq == CR_Q_MAJ && ((cx & CR_EXT_M7) || cr == (t + 7) % 12 || crh_is_secdom(t, scale, smask, cr, cq));
    if (cq == CR_Q_DIM && dr == (cr + 1) % 12 && dq != CR_Q_DIM) return 6;      /* vii0 -> its tonic */
    if (cq == CR_Q_SUS && dr == cr && dq == CR_Q_MAJ) return 6;                /* a sus -> its own major */
    if (dominant && dr == (cr + 5) % 12 && dq != CR_Q_DIM) return 6;           /* V -> I, V/x -> x */
    if (cq == CR_Q_MAJ && cr == (t + 1) % 12 && dr == t) return 4;             /* bII -> I */
    if (cr == (t + 5) % 12 && (cq == CR_Q_MAJ || cq == CR_Q_MIN) && dr == t) return 2;   /* IV / iv -> I (plagal) */
    return 0;
}

typedef struct { uint8_t root, rel, q, cat; int16_t ten, vl; } crh_cand_map_t;   /* rel: root above the home tonic */

/* a before b in the final left-to-right order */
static int crh_map_before(const crh_cand_map_t *a, const crh_cand_map_t *b)
{
    if (a->ten != b->ten) return a->ten < b->ten;
    if (a->vl != b->vl) return a->vl < b->vl;
    if (a->rel != b->rel) return a->rel < b->rel;   /* (relative to home: every key lays out alike) */
    return a->q < b->q;
}
/* a before b in the colour chords' selection: the nearest first */
static int crh_pick_before(const crh_cand_map_t *a, const crh_cand_map_t *b)
{
    if (a->vl != b->vl) return a->vl < b->vl;
    if (a->cat != b->cat) return a->cat < b->cat;
    return crh_map_before(a, b);
}

static void crh_enrich(uint8_t t, uint8_t scale, unsigned smask, const cr_hmap_chord_t *w, cr_hmap_chord_t *b)
{
    *b = *w;
    switch (w->quality) {
    case CR_Q_MAJ:                                  /* dominant function: a 9th chord; otherwise maj9 */
        b->ext = (uint8_t)((w->root_pc == (t + 7) % 12 || crh_is_secdom(t, scale, smask, w->root_pc, CR_Q_MAJ))
                           ? (CR_EXT_M7 | CR_EXT_9) : (CR_EXT_MAJ7 | CR_EXT_9));
        break;
    case CR_Q_MIN: b->ext = (uint8_t)(CR_EXT_M7 | CR_EXT_9); break;   /* m9 */
    case CR_Q_DIM: b->ext = CR_EXT_M7; break;                         /* half-diminished */
    case CR_Q_SUS: b->ext = (uint8_t)(CR_EXT_M7 | CR_EXT_9); break;   /* 9sus4 */
    default: b->ext = CR_EXT_9; break;
    }
}

void cr_harm_map(uint8_t home_pc, uint8_t home_scale, uint8_t cur_pc, uint8_t cur_quality, uint8_t cur_ext,
                 cr_hmap_t *map)
{
    crh_cand_map_t c[32], sel[CR_HMAP_W];
    int nc = 0, ns = 0, i, j, npool;
    const crh_pool_t *pool;
    uint8_t t = (uint8_t)(home_pc % 12u), s = home_scale == CR_SCALE_MINOR ? CR_SCALE_MINOR : CR_SCALE_MAJOR;
    uint8_t cr = (uint8_t)(cur_pc % 12u), cq = cur_quality == CR_Q_NONE ? CR_Q_MAJ : cur_quality;
    unsigned smask = crh_scale_mask_at(t, s), cur_pcs = crh_pcset_at(cr, cq, cur_quality == CR_Q_NONE ? 0 : cur_ext);
    uint8_t near_r[5], near_q[5];
    int nnear = 0;
    if (s == CR_SCALE_MINOR) { pool = CRH_POOL_MINOR; npool = CRH_NPOOL_MINOR; }
    else { pool = CRH_POOL_MAJOR; npool = CRH_NPOOL_MAJOR; }
    if (cq == CR_Q_MAJ) {               /* the chord's own neighbours: P, R, L, the major-third mediants */
        near_r[0] = cr; near_q[0] = CR_Q_MIN;
        near_r[1] = (uint8_t)((cr + 9) % 12); near_q[1] = CR_Q_MIN;
        near_r[2] = (uint8_t)((cr + 4) % 12); near_q[2] = CR_Q_MIN;
        near_r[3] = (uint8_t)((cr + 4) % 12); near_q[3] = CR_Q_MAJ;
        near_r[4] = (uint8_t)((cr + 8) % 12); near_q[4] = CR_Q_MAJ;
        nnear = 5;
    } else if (cq == CR_Q_MIN) {
        near_r[0] = cr; near_q[0] = CR_Q_MAJ;
        near_r[1] = (uint8_t)((cr + 3) % 12); near_q[1] = CR_Q_MAJ;
        near_r[2] = (uint8_t)((cr + 8) % 12); near_q[2] = CR_Q_MAJ;
        near_r[3] = (uint8_t)((cr + 4) % 12); near_q[3] = CR_Q_MIN;
        near_r[4] = (uint8_t)((cr + 8) % 12); near_q[4] = CR_Q_MIN;
        nnear = 5;
    }
    for (i = 0; i < npool + nnear; i++) {      /* the candidates: the pool, then the neighbours; a repeat keeps the
                                                  * first category it was seen under (the pool's: more specific) */
        uint8_t r = i < npool ? (uint8_t)((t + pool[i].semi) % 12) : near_r[i - npool];
        uint8_t q = i < npool ? pool[i].quality : near_q[i - npool];
        uint8_t cat = i < npool ? pool[i].cat : (uint8_t)CR_HCAT_NEAR;
        int dup = 0;
        if (r == cr && q == cq) continue;      /* never the chord you are on */
        for (j = 0; j < nc; j++) if (c[j].root == r && c[j].q == q) { dup = 1; break; }
        if (dup) continue;
        c[nc].root = r; c[nc].rel = (uint8_t)((r + 12 - t) % 12); c[nc].q = q; c[nc].cat = cat;
        c[nc].ten = (int16_t)(crh_tension(t, s, smask, r, q) - crh_resolves(t, s, smask, cr, cq, cur_ext, r, q));
        c[nc].vl = (int16_t)crh_vl(cur_pcs, crh_pcset_at(r, q, 0));
        nc++;
    }
    for (i = 0; i < nc && ns < CR_HMAP_W; i++)  /* the home key's own chords, all of them */
        if (c[i].cat == CR_HCAT_DIATONIC) sel[ns++] = c[i];
    while (ns < CR_HMAP_W) {                    /* the colour chords, nearest first */
        int best = -1;
        for (i = 0; i < nc; i++) {
            if (c[i].cat == CR_HCAT_DIATONIC || c[i].cat == 0xFF) continue;
            if (best < 0 || crh_pick_before(&c[i], &c[best])) best = i;
        }
        if (best < 0) break;                    /* (cannot happen: the pools hold more than eleven) */
        sel[ns++] = c[best];
        c[best].cat = 0xFF;                     /* (taken) */
    }
    for (i = 1; i < ns; i++)                    /* left to right: restful .. pulling */
        for (j = i; j > 0 && crh_map_before(&sel[j], &sel[j - 1]); j--) {
            crh_cand_map_t x = sel[j]; sel[j] = sel[j - 1]; sel[j - 1] = x;
        }
    map->home_pc = t;
    map->home_scale = s;
    for (i = 0; i < CR_HMAP_W; i++) {
        cr_hmap_chord_t *w = &map->w[i];
        w->root_pc = sel[i].root; w->quality = sel[i].q; w->ext = 0;
        w->cat = sel[i].cat == 0xFF ? (uint8_t)CR_HCAT_NEAR : sel[i].cat;
        w->tension = (int8_t)sel[i].ten;
        w->home = (uint8_t)(sel[i].root == t && sel[i].q == (s == CR_SCALE_MINOR ? CR_Q_MIN : CR_Q_MAJ));
    }
    for (i = 0; i < CR_HMAP_B; i++) crh_enrich(t, s, smask, &map->w[CR_HMAP_BW[i]], &map->b[i]);
}

int cr_harm_voice(const cr_hmap_chord_t *d, const uint8_t *src, int nsrc, int sixth_up, int shift,
                  uint8_t notes[CR_CHORD_MAX], uint8_t *root_note)
{
    int n = 0, j, best_cost = -1;
    int8_t rot;
    if (shift < -36) shift = -36;
    if (shift > 36) shift = 36;
    if (!crh_search_voicing(d->root_pc, d->quality, d->ext, sixth_up, src, nsrc, 48 + shift, 72 + shift, notes, &n,
                            root_note, &rot))
        return 0;
    /* the root the looper records: the octave whose plain chord is nearest the voicing (the search's own root can be
     * an octave or more away from it: a spread shape reached by a long rotation) */
    for (j = -1; j <= 1; j++) {
        uint8_t base[CR_CHORD_MAX];
        int r = crh_root_near(d->root_pc, notes[0]) + 12 * j, nb, cost, e, a, rm, mv;
        if (r < 0 || r > 127) continue;
        nb = cr_chord_base_ex(r, d->quality, d->ext, sixth_up, base);
        if (nb != n) continue;                      /* (clipped at this octave) */
        cost = crh_align(notes, n, base, nb, &e, &a, &rm, &mv);
        if (best_cost < 0 || cost < best_cost) {
            best_cost = cost;
            *root_note = (uint8_t)r;
        }
    }
    return n;
}
