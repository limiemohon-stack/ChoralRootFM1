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

/* =================================================================== the lanes === */
const uint8_t CR_HMAP_BW[CR_HMAP_B] = { 0, 2, 3, 4, 6, 7, 9 };   /* D#4 F#4 G#4 A#4 C#5 D#5 F#5 */

typedef struct { uint8_t semi, quality, ext; } crh_lane_t;       /* a chord on the home key: semitones above it */
#define CRH_MAJ9 (CR_EXT_MAJ7 | CR_EXT_9)
#define CRH_DOM9 (CR_EXT_M7 | CR_EXT_9)
static const crh_lane_t CRH_LANES[2][CR_HMAP_W] = {
    { { 0, CR_Q_MAJ, CRH_MAJ9 }, { 9, CR_Q_MIN, CRH_DOM9 }, { 4, CR_Q_MIN, CR_EXT_M7 }, { 5, CR_Q_MAJ, CRH_MAJ9 },
      { 2, CR_Q_MIN, CRH_DOM9 }, { 5, CR_Q_MIN, CRH_DOM9 }, { 8, CR_Q_MAJ, CRH_MAJ9 }, { 10, CR_Q_MAJ, CRH_DOM9 },
      { 7, CR_Q_SUS, CRH_DOM9 }, { 7, CR_Q_MAJ, CRH_DOM9 }, { 1, CR_Q_MAJ, CRH_DOM9 } },
    { { 0, CR_Q_MIN, CRH_DOM9 }, { 8, CR_Q_MAJ, CRH_MAJ9 }, { 3, CR_Q_MAJ, CRH_MAJ9 }, { 5, CR_Q_MIN, CRH_DOM9 },
      { 7, CR_Q_MIN, CR_EXT_M7 }, { 10, CR_Q_MAJ, CRH_DOM9 }, { 2, CR_Q_DIM, CR_EXT_M7 }, { 5, CR_Q_MAJ, CRH_DOM9 },
      { 7, CR_Q_SUS, CRH_DOM9 }, { 7, CR_Q_MAJ, CR_EXT_M7 }, { 1, CR_Q_MAJ, CRH_DOM9 } },
};
static const crh_lane_t CRH_APPROACH[2][CR_HMAP_B] = {   /* each leans into the white root to its right */
    { { 4, CR_Q_MAJ, CR_EXT_M7 }, { 0, CR_Q_MAJ, CRH_DOM9 }, { 9, CR_Q_MAJ, CRH_DOM9 }, { 6, CR_Q_MAJ, CR_EXT_MAJ7 },
      { 5, CR_Q_MAJ, CRH_DOM9 }, { 2, CR_Q_MAJ, CRH_DOM9 }, { 8, CR_Q_MAJ, CRH_DOM9 } },
    { { 3, CR_Q_MAJ, CRH_DOM9 }, { 0, CR_Q_MAJ, CRH_DOM9 }, { 2, CR_Q_MAJ, CR_EXT_M7 }, { 11, CR_Q_MAJ, CR_EXT_MAJ7 },
      { 6, CR_Q_MAJ, CRH_DOM9 }, { 8, CR_Q_MAJ, CRH_DOM9 }, { 9, CR_Q_DIM, CR_EXT_M7 } },
};

/* the chord's intervals above its root by role */
typedef struct { int third, fifth, seventh, ninth; } crh_shape_t;   /* seventh: 7th, or 6th, or -1; ninth: or -1 */
static crh_shape_t crh_shape(uint8_t q, uint8_t ext)
{
    crh_shape_t s;
    s.third = q == CR_Q_MIN || q == CR_Q_DIM ? 3 : q == CR_Q_SUS ? 5 : 4;
    s.fifth = q == CR_Q_DIM ? 6 : q == CR_Q_AUG ? 8 : 7;
    s.seventh = (ext & CR_EXT_MAJ7) ? 11 : (ext & CR_EXT_M7) ? 10 : (ext & CR_EXT_6) ? 9 : -1;
    s.ninth = (ext & CR_EXT_9) ? 14 : -1;
    return s;
}

/* the modes other than Ionian and Aeolian (CRH_LANES): Lydian, Mixolydian, Dorian, Phrygian (cr_harmony.h) */
static const crh_lane_t CRH_MODE_LANES[4][CR_HMAP_W] = {
    { { 0, CR_Q_MAJ, CRH_MAJ9 }, { 2, CR_Q_MAJ, CRH_DOM9 }, { 11, CR_Q_MIN, CR_EXT_M7 }, { 4, CR_Q_MIN, CRH_DOM9 },
      { 7, CR_Q_MAJ, CRH_MAJ9 }, { 9, CR_Q_MIN, CRH_DOM9 }, { 6, CR_Q_DIM, CR_EXT_M7 }, { 5, CR_Q_MAJ, CRH_MAJ9 },
      { 7, CR_Q_SUS, CRH_DOM9 }, { 7, CR_Q_MAJ, CRH_DOM9 }, { 1, CR_Q_MAJ, CRH_DOM9 } },
    { { 0, CR_Q_MAJ, CRH_DOM9 }, { 10, CR_Q_MAJ, CRH_MAJ9 }, { 7, CR_Q_MIN, CRH_DOM9 }, { 5, CR_Q_MAJ, CRH_MAJ9 },
      { 2, CR_Q_MIN, CRH_DOM9 }, { 9, CR_Q_MIN, CR_EXT_M7 }, { 4, CR_Q_DIM, CR_EXT_M7 }, { 8, CR_Q_MAJ, CRH_MAJ9 },
      { 7, CR_Q_SUS, CRH_DOM9 }, { 7, CR_Q_MAJ, CRH_DOM9 }, { 1, CR_Q_MAJ, CRH_DOM9 } },
    { { 0, CR_Q_MIN, CRH_DOM9 }, { 5, CR_Q_MAJ, CRH_DOM9 }, { 3, CR_Q_MAJ, CRH_MAJ9 }, { 2, CR_Q_MIN, CR_EXT_M7 },
      { 7, CR_Q_MIN, CRH_DOM9 }, { 10, CR_Q_MAJ, CRH_MAJ9 }, { 9, CR_Q_DIM, CR_EXT_M7 }, { 8, CR_Q_MAJ, CRH_MAJ9 },
      { 7, CR_Q_SUS, CRH_DOM9 }, { 7, CR_Q_MAJ, CR_EXT_M7 }, { 1, CR_Q_MAJ, CRH_DOM9 } },
    { { 0, CR_Q_MIN, CR_EXT_M7 }, { 1, CR_Q_MAJ, CRH_MAJ9 }, { 10, CR_Q_MIN, CRH_DOM9 }, { 8, CR_Q_MAJ, CRH_MAJ9 },
      { 5, CR_Q_MIN, CRH_DOM9 }, { 3, CR_Q_MAJ, CRH_DOM9 }, { 7, CR_Q_DIM, CR_EXT_M7 }, { 10, CR_Q_MAJ, CRH_DOM9 },
      { 7, CR_Q_SUS, CRH_DOM9 }, { 7, CR_Q_MAJ, CR_EXT_M7 }, { 1, CR_Q_MAJ, CRH_DOM9 } },
};
static const uint16_t CRH_MODE_MASK[CR_HMODE_N] = {   /* the mode's notes above home, bit semitone */
    0xAD5, 0xAB5, 0x6B5, 0x6AD, 0x5AD, 0x5AB };

static void crh_lane_fill(uint8_t t, unsigned scale, const crh_lane_t *l, int place, int home, cr_hmap_chord_t *d)
{
    unsigned pcs;
    d->root_pc = (uint8_t)((t + l->semi) % 12);
    d->quality = l->quality;
    d->ext = l->ext;
    pcs = crh_pcset_at(d->root_pc, d->quality, d->ext);
    d->cat = (uint8_t)((pcs & ~scale) ? CR_HCAT_CHROMATIC : CR_HCAT_DIATONIC);
    d->tension = (int8_t)place;
    d->home = (uint8_t)home;
    d->approach = 0;
    d->flav = 0;
}

const char *cr_harm_mode_name(int mode)
{
    static const char *const N[CR_HMODE_N] = { "lydian", "major", "mixolydian", "dorian", "minor", "phrygian" };
    return mode >= 0 && mode < CR_HMODE_N ? N[mode] : N[CR_HMODE_IONIAN];
}
int cr_harm_mode_family(int mode) { return mode >= CR_HMODE_DORIAN && mode < CR_HMODE_N ? CR_SCALE_MINOR : CR_SCALE_MAJOR; }

static int crh_lane_same(const crh_lane_t *a, const crh_lane_t *b)   /* the same chord, give or take its 9th */
{
    return a->semi % 12 == b->semi % 12 && a->quality == b->quality &&
           (a->ext & (CR_EXT_M7 | CR_EXT_MAJ7)) == (b->ext & (CR_EXT_M7 | CR_EXT_MAJ7));
}
void cr_harm_lanes_mode(uint8_t home_pc, int mode, cr_hmap_t *map)
{
    uint8_t t = (uint8_t)(home_pc % 12u);
    int i, j, k;
    if (mode < 0 || mode >= CR_HMODE_N) mode = CR_HMODE_IONIAN;
    map->home_pc = t;
    map->home_scale = (uint8_t)cr_harm_mode_family(mode);
    map->mode = (uint8_t)mode;
    if (mode == CR_HMODE_IONIAN || mode == CR_HMODE_AEOLIAN) {   /* the palettes as they always were */
        uint8_t s = mode == CR_HMODE_AEOLIAN ? CR_SCALE_MINOR : CR_SCALE_MAJOR;
        unsigned scale = crh_scale_mask_at(t, s);
        for (i = 0; i < CR_HMAP_W; i++) crh_lane_fill(t, scale, &CRH_LANES[s][i], i, i == 0, &map->w[i]);
        for (i = 0; i < CR_HMAP_B; i++) {
            crh_lane_fill(t, scale, &CRH_APPROACH[s][i], CR_HMAP_BW[i] + 1, 0, &map->b[i]);
            map->b[i].approach = 1;
        }
        return;
    }
    {
        const crh_lane_t *W = CRH_MODE_LANES[mode == CR_HMODE_LYDIAN ? 0 : mode == CR_HMODE_MIXO ? 1 :
                                             mode == CR_HMODE_DORIAN ? 2 : 3];
        unsigned m = CRH_MODE_MASK[mode], scale = ((m << t) | (m >> (12u - t))) & 0x0FFFu;
        crh_lane_t B[CR_HMAP_B];
        for (i = 0; i < CR_HMAP_W; i++) crh_lane_fill(t, scale, &W[i], i, i == 0, &map->w[i]);
        for (i = 0; i < CR_HMAP_B; i++) {            /* the approach into the white key to the right */
            const crh_lane_t *to = &W[CR_HMAP_BW[i] + 1];
            int minor = to->quality == CR_Q_MIN || to->quality == CR_Q_DIM;
            crh_lane_t c[3];
            c[0].semi = (uint8_t)((to->semi + 7) % 12); c[0].quality = CR_Q_MAJ; c[0].ext = minor ? CR_EXT_M7 : CRH_DOM9;
            c[1].semi = (uint8_t)((to->semi + 1) % 12); c[1].quality = CR_Q_MAJ; c[1].ext = CRH_DOM9;
            c[2].semi = (uint8_t)((to->semi + 1) % 12); c[2].quality = CR_Q_MAJ; c[2].ext = CR_EXT_MAJ7;
            B[i] = i == 3 ? c[2] : c[0];
            for (k = 0; k < 3; k++) {                /* the first not already on the keys (A#4: the major 7th first) */
                const crh_lane_t *x = &c[i == 3 ? (k + 2) % 3 : k];
                int dup = 0;
                for (j = 0; j < CR_HMAP_W && !dup; j++) dup = crh_lane_same(x, &W[j]);
                for (j = 0; j < i && !dup; j++) dup = crh_lane_same(x, &B[j]);
                if (!dup) { B[i] = *x; break; }
            }
            crh_lane_fill(t, scale, &B[i], CR_HMAP_BW[i] + 1, 0, &map->b[i]);
            map->b[i].approach = 1;
        }
    }
}

void cr_harm_lanes(uint8_t home_pc, uint8_t home_scale, cr_hmap_t *map)
{
    cr_harm_lanes_mode(home_pc, home_scale == CR_SCALE_MINOR ? CR_HMODE_AEOLIAN : CR_HMODE_IONIAN, map);
}

static uint8_t crh_flav_ext(unsigned flav, uint8_t ext)     /* the chord keys' 7th / 6th / 9th over ext */
{
    if (flav & (1u << CR_MOD_M7)) ext = (uint8_t)((ext & ~(CR_EXT_MAJ7 | CR_EXT_6)) | CR_EXT_M7);
    if (flav & (1u << CR_MOD_MAJ7)) ext = (uint8_t)((ext & ~(CR_EXT_M7 | CR_EXT_6)) | CR_EXT_MAJ7);
    if (flav & (1u << CR_MOD_6)) ext = (uint8_t)((ext & ~(CR_EXT_M7 | CR_EXT_MAJ7)) | CR_EXT_6);
    if (flav & (1u << CR_MOD_9)) ext = (uint8_t)(ext | CR_EXT_9);
    return ext;
}
void cr_harm_flavour(const cr_hmap_chord_t *d, unsigned flav, cr_hmap_chord_t *out)
{
    *out = *d;
    if (flav & (1u << CR_MOD_SUS))
        out->quality = CR_Q_SUS;
    out->ext = crh_flav_ext(flav, d->ext);
    out->flav = (uint8_t)flav;
}

/* the lowest note at or above lo with the pitch class of root + iv */
static int crh_place(int root, int iv, int lo)
{
    int pc = (root + iv) % 12, n = lo + ((pc - lo % 12) % 12 + 12) % 12;
    return n;
}
static int crh_abs(int x) { return x < 0 ? -x : x; }

int cr_harm_voice_style(const cr_hmap_chord_t *d, int style, int shift, int rootless, uint8_t notes[CR_CHORD_MAX],
                        uint8_t *root_note)
{
    crh_shape_t sh = crh_shape(d->quality, d->ext);
    int r = d->root_pc, n = 0, i, bass, up[4], nu = 0;
    int guide2 = sh.seventh >= 0 ? sh.seventh : sh.fifth;
    if (shift < -24) shift = -24;
    if (shift > 24) shift = 24;
    bass = crh_place(r, 0, 40 + shift);                      /* E2 .. D#3 */
    if (!rootless)
        notes[n++] = (uint8_t)bass;
    if (style == CR_HSTYLE_SHELL) {                          /* the 3rd and 7th, the order that tops nearest E4 */
        int best = -1, ba = 0, bb = 0, o;
        for (o = 0; o < 2; o++) {
            int a = crh_place(r, o ? sh.third : guide2, 52 + shift);
            int b = crh_place(r, o ? guide2 : sh.third, a + 1);
            int cost = crh_abs(b - (64 + shift));
            if (best < 0 || cost < best) { best = cost; ba = a; bb = b; }
        }
        notes[n++] = (uint8_t)ba;
        notes[n++] = (uint8_t)bb;
        if (rootless)                                        /* (no root: the 9th, or the 5th, on top keeps 3) */
            notes[n++] = (uint8_t)crh_place(r, sh.ninth >= 0 ? sh.ninth : sh.fifth, bb + 1);
    } else if (style == CR_HSTYLE_SPREAD) {                  /* 7th (or 5th), 3rd, then the 9th (or 5th) on top */
        int top = sh.ninth >= 0 ? sh.ninth : (guide2 == sh.fifth ? 0 : sh.fifth);
        int a = crh_place(r, guide2, (50 + shift > bass ? 50 + shift : bass + 1));
        int b = crh_place(r, sh.third, a + 1);
        int c = crh_place(r, top, b + 1 > 64 + shift ? b + 1 : 64 + shift);   /* (the top at least E4: open, not low) */
        if (c > 74 + shift) {                                /* (too high: the colour tone tucks under the 3rd) */
            int c2 = crh_place(r, top, a + 1);
            if (c2 < b) { c = b; b = c2; }
        }
        notes[n++] = (uint8_t)a;
        notes[n++] = (uint8_t)b;
        notes[n++] = (uint8_t)c;
    } else {                                                 /* close position, the top nearest A4 */
        int best = -1, bl = 0, k, ivs[4];
        ivs[nu++] = sh.third;
        ivs[nu++] = sh.fifth;
        if (sh.seventh >= 0) ivs[nu++] = sh.seventh;
        ivs[nu++] = sh.ninth >= 0 ? sh.ninth : 12;           /* (no 9th: the root again, on top) */
        for (k = 0; k < nu; k++) {                           /* each member as the lowest of the stack */
            int lo = crh_place(r, ivs[k], 55 + shift), top = lo, m;
            for (m = 1; m < nu; m++) top = crh_place(r, ivs[(k + m) % nu], top + 1);
            if (best < 0 || crh_abs(top - (69 + shift)) < best) { best = crh_abs(top - (69 + shift)); bl = k; }
        }
        up[0] = crh_place(r, ivs[bl], 55 + shift);
        for (i = 1; i < nu; i++) up[i] = crh_place(r, ivs[(bl + i) % nu], up[i - 1] + 1);
        for (i = 0; i < nu; i++) notes[n++] = (uint8_t)up[i];
    }
    *root_note = (uint8_t)crh_place(r, 0, 52 + shift);       /* (the root an octave up: a loop's chord sits there) */
    return n;
}

/* ============================================================== colour worlds === */
static void crh_strcpy(char *d, const char *s, int max);
enum { CRH_CL_MAJ, CRH_CL_DOM, CRH_CL_MIN, CRH_CL_SUS, CRH_CL_DIM, CRH_NCL };   /* a chord's job */
static int crh_class(uint8_t q, uint8_t ext)
{
    if (q == CR_Q_MIN) return CRH_CL_MIN;
    if (q == CR_Q_SUS) return CRH_CL_SUS;
    if (q == CR_Q_DIM) return CRH_CL_DIM;
    return (ext & CR_EXT_M7) ? CRH_CL_DOM : CRH_CL_MAJ;
}
/* per world (LUSH .. UPPER) and job: the notes above the root (semitones; for UPPER the first `under` notes go
 * below, the rest is the triad on top; for QUARTAL the order is the stack, low to high), its name (qual, sup; for
 * UPPER the triad's root semitones above the chord's in `slash`, and the bass's qual) */
typedef struct { int8_t iv[4]; uint8_t n, under; int8_t slash; const char *qual, *sup; } crh_world_t;
static const crh_world_t CRH_WORLDS[4][CRH_NCL] = {
    {   /* LUSH */
        { { 4, 11, 14, 18 }, 4, 0, -1, "", "M9 #11" }, { { 4, 10, 14, 21 }, 4, 0, -1, "", "13" },
        { { 3, 10, 14, 17 }, 4, 0, -1, "m", "11" },     { { 5, 10, 14, 21 }, 4, 0, -1, "sus", "13" },
        { { 3, 6, 10, 17 }, 4, 0, -1, "m", "11 b5" } },
    {   /* QUARTAL: a stack of 4ths (one tritone in the dominant's) */
        { { 4, 9, 14, 19 }, 4, 0, -1, "", "6 9" },      { { 10, 16, 21, 26 }, 4, 0, -1, "", "13" },
        { { 5, 10, 15, 19 }, 4, 0, -1, "m", "11" },     { { 7, 12, 17, 22 }, 4, 0, -1, "sus", "7" },
        { { 6, 10, 15, 20 }, 4, 0, -1, "m", "7 b5" } },
    {   /* ALTERED */
        { { 4, 8, 11, 14 }, 4, 0, -1, "+", "M7" },      { { 4, 10, 13, 20 }, 4, 0, -1, "", "7 b9b13" },
        { { 3, 7, 11, 14 }, 4, 0, -1, "m", "M9" },      { { 5, 7, 10, 13 }, 4, 0, -1, "sus", "7 b9" },
        { { 3, 6, 9, 15 }, 4, 0, -1, "\260", "7" } },
    {   /* UPPER: (under) + a major triad on top, named triad / bass */
        { { 4, 14, 18, 21 }, 4, 1, 2, "", "" },         { { 10, 14, 18, 21 }, 4, 1, 2, "", "7" },
        { { 3, 10, 14, 17 }, 4, 1, 10, "", "m" },       { { 10, 14, 17, 7 }, 4, 0, 10, "", "sus" },
        { { 3, 6, 10, 17 }, 4, 0, -1, "m", "11 b5" } },
};

/* which chords a world recolours (the owner, 2026-10-16: only Lush, maybe Jazz, felt musical at once on every key):
 * each world keeps the job of the chords it does not suit, so the palette still rests at home and pulls at the right.
 *   LUSH      every chord
 *   QUARTAL   the stable ones: major, minor, sus (on white keys); dominants and approach chords keep their pull
 *   UPPER     the same: a triad over the root is colour on a resting chord, blur on a pulling one
 *   ALTERED   the pulling ones: dominants, sus, diminished, every approach chord; home and the resting chords stay
 *             as written -- jazz that leans harder, with somewhere to land
 * And a diminished 7th (the lead-in approach, cr_harm_approach) is a diminished 7th in every world but ALTERED's own
 * (Lush would make it m11b5: another chord). */
static int crh_world_suits(int world, int cl, const cr_hmap_chord_t *d)
{
    if (cl == CRH_CL_DIM && (d->ext & CR_EXT_6) && world != CR_HCOL_ALTERED) return 0;
    switch (world) {
    case CR_HCOL_QUARTAL:
    case CR_HCOL_UPPER: return !d->approach && (cl == CRH_CL_MAJ || cl == CRH_CL_MIN || cl == CRH_CL_SUS);
    case CR_HCOL_ALTERED: return d->approach || cl == CRH_CL_DOM || cl == CRH_CL_SUS || cl == CRH_CL_DIM;
    default: return 1;
    }
}

/* an approach into the chord to (Harmony mode's lower chord keys held with a root, cr_harmony.h) */
void cr_harm_approach(const cr_hmap_chord_t *to, int kind, cr_hmap_chord_t *out)
{
    int minor = to->quality == CR_Q_MIN || to->quality == CR_Q_DIM;
    *out = *to;
    out->home = 0;
    out->flav = 0;
    out->cat = CR_HCAT_CHROMATIC;
    switch (kind) {
    case CR_HAPP_SLIP:                                      /* the same chord a semitone up (its world, its shape) */
        out->root_pc = (uint8_t)((to->root_pc + 1) % 12);
        break;
    case CR_HAPP_DIM:                                       /* the diminished 7th a semitone under it */
        out->root_pc = (uint8_t)((to->root_pc + 11) % 12);
        out->quality = CR_Q_DIM;
        out->ext = CR_EXT_6;
        out->approach = 1;
        break;
    case CR_HAPP_TRITONE:                                   /* the dominant a semitone over it (bII7: V's tritone) */
        out->root_pc = (uint8_t)((to->root_pc + 1) % 12);
        out->quality = CR_Q_MAJ;
        out->ext = CRH_DOM9;
        out->approach = 1;
        break;
    default:                                                /* CR_HAPP_DOM: its V (a 7th into a minor one) */
        out->root_pc = (uint8_t)((to->root_pc + 7) % 12);
        out->quality = CR_Q_MAJ;
        out->ext = minor ? CR_EXT_M7 : CRH_DOM9;
        out->approach = 1;
        break;
    }
}

static void crh_strcpy(char *d, const char *s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }

int cr_harm_voice_world(const cr_hmap_chord_t *d, int world, int style, int shift, int rootless,
                        uint8_t notes[CR_CHORD_MAX], uint8_t *root_note, char root[3], char qual[5],
                        char sup[CR_SUP_MAX], uint8_t *loop_q, uint8_t *loop_x)
{
    cr_hmap_chord_t e = *d;
    const crh_world_t *w;
    static const char *const NN[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int r = d->root_pc, n = 0, i, k, cl, up[5], nu, lo, best, bk;
    if (shift < -24) shift = -24;
    if (shift > 24) shift = 24;
    *loop_q = d->quality;
    *loop_x = d->ext;
    if (world == CR_HCOL_BARE) e.ext = 0;
    if (world == CR_HCOL_TRIAD) e.ext = crh_flav_ext(d->flav, 0);   /* (the palette's colour off, the keys' kept) */
    if (world == CR_HCOL_BARE) {                             /* root, 5th, root */
        int b = crh_place(r, 0, 40 + shift), f = crh_place(r, 7, rootless ? 50 + shift : b + 1);
        if (!rootless) notes[n++] = (uint8_t)b;
        notes[n++] = (uint8_t)f;
        notes[n++] = (uint8_t)crh_place(r, 0, f + 1);
        notes[n] = (uint8_t)crh_place(r, 7, notes[n - 1] + 1);
        n++;
        *root_note = (uint8_t)crh_place(r, 0, 52 + shift);
        cr_chord_name((uint8_t)r, CR_Q_POW, 0, root, qual, sup);
        *loop_q = CR_Q_POW; *loop_x = 0;
        return n;
    }
    cl = crh_class(d->quality, d->ext);
    if (world > CR_HCOL_JAZZ && world < CR_HCOL_N && !crh_world_suits(world, cl, d))
        world = CR_HCOL_JAZZ;                               /* (a chord the world does not suit keeps its job) */
    if (world <= CR_HCOL_JAZZ || world >= CR_HCOL_N) {      /* the palette's own chords (6ths, flavours) */
        n = cr_harm_voice_style(&e, style, shift, rootless, notes, root_note);
        cr_chord_name(e.root_pc, e.quality, e.ext, root, qual, sup);
        if (e.quality == CR_Q_DIM && e.ext == CR_EXT_6) {   /* (the engine's "dim6": a diminished 7th, said so) */
            crh_strcpy(qual, "\260", 5);
            crh_strcpy(sup, "7", CR_SUP_MAX);
        }
        *loop_q = e.quality; *loop_x = e.ext;
        return n;
    }
    w = &CRH_WORLDS[world - CR_HCOL_LUSH][cl];
    if (world == CR_HCOL_ALTERED && cl == CRH_CL_DOM) {      /* an approach: 7b9 (the 5th kept); the 9 key: #9 */
        static const crh_world_t ALT[2][2] = {
            { { { 4, 10, 13, 20 }, 4, 0, -1, "", "7 b9b13" }, { { 4, 10, 15, 20 }, 4, 0, -1, "", "7 #9b13" } },
            { { { 4, 7, 10, 13 }, 4, 0, -1, "", "7 b9" },     { { 4, 7, 10, 15 }, 4, 0, -1, "", "7 #9" } } };
        w = &ALT[d->approach ? 1 : 0][(d->flav >> CR_MOD_9) & 1u];
    }
    *root_note = (uint8_t)crh_place(r, 0, 52 + shift);
    if (!rootless) notes[n++] = (uint8_t)crh_place(r, 0, 40 + shift);
    nu = w->n;
    if (world == CR_HCOL_QUARTAL) {                          /* the stack in its order, the top nearest A4 */
        up[0] = crh_place(r, w->iv[0], 50 + shift);
        for (i = 1; i < nu; i++) up[i] = up[0] + (w->iv[i] - w->iv[0]);
        while (up[nu - 1] > 74 + shift) for (i = 0; i < nu; i++) up[i] -= 12;
        while (up[0] < 50 + shift) for (i = 0; i < nu; i++) up[i] += 12;
    } else if (world == CR_HCOL_UPPER && w->under) {         /* the guide tone low, the triad close on top */
        int g = crh_place(r, w->iv[0], 50 + shift), t0;
        up[0] = g;
        best = -1; bk = 0;
        for (k = 0; k < 3; k++) {                            /* the triad's inversion whose top is nearest A4 */
            int a = crh_place(r, w->iv[1 + k], g + 1), b = crh_place(r, w->iv[1 + (k + 1) % 3], a + 1);
            int c = crh_place(r, w->iv[1 + (k + 2) % 3], b + 1);
            if (best < 0 || crh_abs(c - (69 + shift)) < best) { best = crh_abs(c - (69 + shift)); bk = k; }
        }
        t0 = crh_place(r, w->iv[1 + bk], g + 1);
        up[1] = t0;
        up[2] = crh_place(r, w->iv[1 + (bk + 1) % 3], up[1] + 1);
        up[3] = crh_place(r, w->iv[1 + (bk + 2) % 3], up[2] + 1);
    } else {                                                 /* close position, the top nearest A4 */
        int iv[4] = { 0, 0, 0, 0 }, ex[4] = { 0, 0, 0, 0 }, m, rub;
        for (i = 0; i < nu; i++) { iv[i] = w->iv[i] % 12; ex[i] = w->iv[i] == 15; }  /* (in pitch order: a close
                                                                                       * stack is a rotation) */
        for (i = 1; i < nu; i++)
            for (m = i; m > 0 && iv[m] < iv[m - 1]; m--) {
                int x = iv[m]; iv[m] = iv[m - 1]; iv[m - 1] = x;
                x = ex[m]; ex[m] = ex[m - 1]; ex[m - 1] = x;
            }
        best = -1; bk = 0;
        for (rub = 0; rub < 2 && best < 0; rub++)            /* (first without the #9 just under the 3rd: under
                                                              * it is mud, over it the sound) */
            for (k = 0; k < nu; k++) {
                int top = crh_place(r, iv[k], 55 + shift), bad = 0;
                for (m = 1; m < nu; m++) {
                    int nx = crh_place(r, iv[(k + m) % nu], top + 1);
                    if (nx - top == 1 && ex[(k + m - 1) % nu] && !ex[(k + m) % nu]) bad = 1;
                    top = nx;
                }
                if (bad && !rub) continue;
                if (best < 0 || crh_abs(top - (69 + shift)) < best) { best = crh_abs(top - (69 + shift)); bk = k; }
            }
        up[0] = crh_place(r, iv[bk], 55 + shift);
        for (i = 1; i < nu; i++) up[i] = crh_place(r, iv[(bk + i) % nu], up[i - 1] + 1);
    }
    if (style == CR_HSTYLE_SPREAD && nu == 4 && world != CR_HCOL_QUARTAL) {   /* drop 2: the second from the top
                                                                               * an octave down */
        int x = up[2] - 12;
        if (x >= (n ? notes[0] + 1 : 50 + shift)) { up[2] = up[1]; up[1] = up[0]; up[0] = x; }
    }
    lo = (style == CR_HSTYLE_SHELL && nu > 3) ? nu - 3 : 0;  /* Shell: the top three */
    for (i = lo; i < nu; i++) notes[n++] = (uint8_t)up[i];
    for (i = 1; i < n; i++)                                  /* (ascending, no doubled pitch: lift what collides) */
        while (notes[i] <= notes[i - 1]) notes[i] = (uint8_t)(notes[i] + 12);
    if (w->slash >= 0) {                                     /* "D/C": the triad's root, "/" and the bass */
        crh_strcpy(root, NN[(r + w->slash) % 12], 3);
        qual[0] = '/';
        crh_strcpy(qual + 1, NN[r], 4);
        crh_strcpy(sup, w->sup, CR_SUP_MAX);
    } else {
        crh_strcpy(root, NN[r], 3);
        crh_strcpy(qual, w->qual, 5);
        crh_strcpy(sup, w->sup, CR_SUP_MAX);
    }
    return n;
}
