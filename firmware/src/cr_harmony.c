/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: Harmony Preview's pure suggestion engine (cr_harmony.h). See the Harmony Preview Edge Case
 * Spec for the decisions this ports to code: the key-inference score and hysteresis, the candidate tables and
 * their allocation order, the two-tier duplicate-collapse fallback, and the voice-leading cost function. */
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
static int crh_search_voicing(uint8_t root_pc, uint8_t quality, uint8_t ext, int sixth_up,
                               const uint8_t *src_notes, int n_src, uint8_t out[CR_CHORD_MAX], int *out_n,
                               uint8_t *out_root_note, int8_t *out_rotation)
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
            cost = crh_align(src_notes, n_src, voiced, nv, &exact, &added, &removed, &move);
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
                if (crh_search_voicing(root_pc, c->quality, c->ext, sixth_up, source->notes, source->nnotes, notes, &nn,
                                        &voiced_root, &rotation)) {
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
                                    source->notes, source->nnotes, notes, &nn, &voiced_root, &rotation) &&
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
