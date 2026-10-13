/* SPDX-License-Identifier: GPL-3.0-only
 * Host tests of Harmony Preview's pure suggestion engine (firmware/src/cr_harmony.c), against the real chord
 * engine (cr_engine.c) for cr_chord_base_ex / cr_voice_apply / cr_chord_info_t -- no audio, no UI, no device:
 *   cc -std=c99 -Wall -Wextra -Werror -pedantic -o build/host/cr_harmony_test tests/cr_harmony_test.c \
 *      firmware/src/cr_harmony.c firmware/src/cr_engine.c
 * One line per check; exit 1 on any failure. */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/cr_harmony.h"

static int passed, failed;
static void ok(int c, const char *name) { if (c) passed++; else failed++; printf("%s %s\n", c ? "ok  " : "FAIL", name); }

/* a cr_chord_info_t for a chord built fresh at root_note (a real MIDI note), unrotated */
static void make_source(cr_chord_info_t *ci, int root_note, uint8_t quality, uint8_t ext)
{
    uint8_t base[CR_CHORD_MAX];
    int n = cr_chord_base_ex(root_note, quality, ext, 0, base);
    memset(ci, 0, sizeof *ci);
    ci->valid = 1; ci->sounding = 1;
    ci->root_pc = (uint8_t)(root_note % 12);
    ci->quality = quality; ci->ext = ext;
    ci->nnotes = (uint8_t)cr_voice_apply(base, n, 0, ci->notes);
}

/* ------------------------------------------------------- the worked example --- */
static void t_worked_example(void)
{
    cr_chord_info_t src; cr_harm_key_t key; cr_harm_result_t out[3];
    make_source(&src, 60, CR_Q_MIN, (uint8_t)(CR_EXT_M7 | CR_EXT_9));     /* Cm9 on C4 */
    cr_harm_key_explicit(&key, 0, CR_SCALE_MINOR);                       /* C minor */
    ok(cr_harm_suggest(&key, &src, 0, out) == 1, "worked example: suggest returns 1");
    ok(out[0].kind == CR_HARM_SUGGEST && out[0].root_pc == 8 && out[0].quality == CR_Q_MAJ && out[0].ext == CR_EXT_MAJ7,
       "worked example: REST is AbM7");
    ok(out[1].kind == CR_HARM_SUGGEST && out[1].root_pc == 5 && out[1].quality == CR_Q_MIN &&
       out[1].ext == (uint8_t)(CR_EXT_M7 | CR_EXT_9), "worked example: MOVE is Fm9");
    ok(out[2].kind == CR_HARM_SUGGEST && out[2].root_pc == 7 && out[2].quality == CR_Q_MAJ && out[2].ext == CR_EXT_M7,
       "worked example: PULL is G7");
    ok(out[0].notes[0] != 0 && out[1].notes[0] != 0 && out[2].notes[0] != 0, "worked example: every slot has real notes");
}

/* ------------------------------------------------------------- all 12 transpositions --- */
static void t_transpositions(void)
{
    int t, all_ok = 1;
    for (t = 0; t < 12; t++) {
        cr_chord_info_t src; cr_harm_key_t key; cr_harm_result_t out[3];
        make_source(&src, 60 + t, CR_Q_MIN, (uint8_t)(CR_EXT_M7 | CR_EXT_9));
        cr_harm_key_explicit(&key, (uint8_t)t, CR_SCALE_MINOR);
        cr_harm_suggest(&key, &src, 0, out);
        if (out[0].root_pc != (t + 8) % 12 || out[1].root_pc != (t + 5) % 12 || out[2].root_pc != (t + 7) % 12) all_ok = 0;
        if (out[0].kind != CR_HARM_SUGGEST || out[1].kind != CR_HARM_SUGGEST || out[2].kind != CR_HARM_SUGGEST) all_ok = 0;
    }
    ok(all_ok, "all 12 transpositions: same relative REST/MOVE/PULL roots");
}

/* ----------------------------------------------------- key inference: stability --- */
static void t_key_stability(void)
{
    /* Acquisition was raised from 2 to 3 qualifying entries (2026-10-13): with only 2, the newest chord's
     * weight-4 tonic bonus could transiently outweigh the evidence for the musically correct key (Cm then Fm
     * briefly acquired F minor before self-correcting). With 3, no key is held after chord 1 or chord 2.
     *
     * Whichever of the two chords is played FIRST ends up the recognized tonic by the 3rd chord: it occurs
     * twice in the 3-chord window (positions 0 and 2) against the other's once, so its own tonic-quality
     * bonus fires twice. That means the two starting orders are NOT both "the tonic's own key wins" -- for
     * tonic-first (t, t+5, t) the held key is t; for iv-first (t+5, t, t+5) the held key is t+5 (confirmed
     * by instrumented run, not assumed). Either way: zero switches for the rest of an 8-chord alternating
     * run once that first chord's key is held. Checked across all 12 transpositions -- 24 runs, one
     * pass/fail summary. */
    int t, start_iv, all_ok = 1;
    for (t = 0; t < 12; t++) {
        for (start_iv = 0; start_iv < 2; start_iv++) {
            cr_harm_hist_t h; cr_harm_key_t key; int i;
            uint8_t expect_tonic = start_iv ? (uint8_t)((t + 5) % 12) : (uint8_t)t;
            cr_harm_hist_clear(&h); cr_harm_key_clear(&key);
            for (i = 0; i < 8; i++) {
                int on_tonic = (i % 2 == 0) == !start_iv;              /* alternates; which chord starts varies */
                uint8_t root = (uint8_t)(on_tonic ? t : (t + 5) % 12); /* tonic, or its iv (5 semitones up) */
                cr_harm_hist_push(&h, root, CR_Q_MIN, 0);
                cr_harm_key_infer(&key, &h);
                if (i < 2 && key.held) all_ok = 0;                     /* no key before the 3rd chord */
                if (i >= 2 && !(key.held && key.tonic_pc == expect_tonic && key.scale == CR_SCALE_MINOR)) all_ok = 0;
            }
        }
    }
    ok(all_ok, "key stability: both starting orders, all 12 transpositions -- held from the 3rd chord, zero switches");
}

/* --------------------------------------------------- key inference: a real modulation --- */
static void t_key_modulation(void)
{
    /* C minor firmly established (i-iv-i, 3 chords), then genuinely modulated to A minor by repeating ITS
     * own i-iv (A minor's iv is D minor -- unrelated to C minor's own chords, so nothing here keeps
     * reinforcing the old key the way a modulation to G minor would, since G minor's iv happens to BE C
     * minor). The held key must actually follow the modulation, not stay stuck on the first key it found. */
    static const uint8_t seq[] = {
        0, 5, 0,       /* C minor: i, iv, i */
        9, 2, 9, 2, 9, /* modulate to A minor: i, iv, i, iv, i */
    };
    cr_harm_hist_t h; cr_harm_key_t key; int i;
    int held_c_after_3 = 0, reached_a = 0, stable_on_a = 1;
    cr_harm_hist_clear(&h); cr_harm_key_clear(&key);
    for (i = 0; i < (int)(sizeof seq / sizeof seq[0]); i++) {
        cr_harm_hist_push(&h, seq[i], CR_Q_MIN, 0);
        cr_harm_key_infer(&key, &h);
        if (i == 2) held_c_after_3 = key.held && key.tonic_pc == 0 && key.scale == CR_SCALE_MINOR;
        if (i >= 3 && key.held && key.tonic_pc == 9 && key.scale == CR_SCALE_MINOR) reached_a = 1;
        if (reached_a && !(key.held && key.tonic_pc == 9 && key.scale == CR_SCALE_MINOR)) stable_on_a = 0;
    }
    ok(held_c_after_3, "modulation: C minor holds after its own i-iv-i");
    ok(reached_a, "modulation: a genuine shift to A minor's i-iv is actually followed, not stuck on C minor");
    ok(stable_on_a, "modulation: once it reaches A minor, it holds there (no bouncing back)");
}

/* ------------------------------------------------- key inference: relative-key tie --- */
static void t_relative_tie(void)
{
    /* Eb major and C minor share the same scale pitch classes (relative keys): any chord drawn from either
     * scores identical membership against both candidates, so only each candidate's own recency-weighted
     * tonic-quality bonus can ever decide between them. */
    cr_harm_hist_t h; cr_harm_key_t key;
    cr_harm_hist_clear(&h); cr_harm_key_clear(&key);
    cr_harm_hist_push(&h, 3, CR_Q_MAJ, 0);
    cr_harm_key_infer(&key, &h);
    ok(!key.held, "relative-key tie: one qualifying chord is not enough to acquire a key");
    cr_harm_hist_push(&h, 3, CR_Q_MAJ, 0);   /* repeat: spec says this must not consume a new slot */
    ok(h.n == 1, "history: a repeated identical chord does not consume a new slot");
    cr_harm_hist_push(&h, 0, CR_Q_MIN, 0);    /* Cm, a second distinct qualifying chord */
    cr_harm_key_infer(&key, &h);
    ok(!key.held, "relative-key tie: two qualifying chords is not enough either (raised to 3)");

    /* Cm, Eb major, Cm (3 distinct qualifying entries, none adjacent-identical): C minor is the NEWEST
     * chord's own tonic match (weight 4), Eb major only the middle one's (weight 3) -- C minor must win,
     * 42 to the runner-up, on recency alone, since membership can't distinguish them at all. */
    cr_harm_hist_clear(&h); cr_harm_key_clear(&key);
    cr_harm_hist_push(&h, 0, CR_Q_MIN, 0);
    cr_harm_hist_push(&h, 3, CR_Q_MAJ, 0);
    cr_harm_hist_push(&h, 0, CR_Q_MIN, 0);
    cr_harm_key_infer(&key, &h);
    ok(key.held && key.tonic_pc == 0 && key.scale == CR_SCALE_MINOR,
       "relative-key tie: recency-weighted tonic bonus decides it (C minor, not Eb major)");
}

/* --------------------------------------------------- history: qualifying filter --- */
static void t_history_qualify(void)
{
    cr_harm_hist_t h;
    cr_harm_hist_clear(&h);
    ok(cr_harm_hist_push(&h, 0, CR_Q_NONE, 0) == 0, "history: a single note (1 pitch class) does not qualify");
    ok(h.n == 0, "history: a rejected push leaves history untouched");
    ok(cr_harm_hist_push(&h, 0, CR_Q_POW, 0) == 0, "history: a power chord (2 pitch classes) does not qualify");
    ok(cr_harm_hist_push(&h, 0, CR_Q_MAJ, 0) == 1, "history: a plain triad (3 pitch classes) qualifies");
    ok(h.n == 1, "history: the qualifying triad was pushed");
}

/* --------------------------------------------------------- key mode on/off --- */
static void t_key_mode_transitions(void)
{
    cr_harm_hist_t h; cr_harm_key_t key;
    cr_harm_hist_clear(&h); cr_harm_key_clear(&key);
    cr_harm_key_explicit(&key, 2, CR_SCALE_MAJOR);     /* D major, immediately, no history needed */
    ok(key.held && key.tonic_pc == 2 && key.scale == CR_SCALE_MAJOR, "key mode: explicit key holds at once");
    /* turning Key Mode off: caller clears both hist and key (cr_harmony.h) */
    cr_harm_key_clear(&key); cr_harm_hist_clear(&h);
    ok(!key.held, "key mode off: key is cleared");
    cr_harm_hist_push(&h, 0, CR_Q_MIN, 0);
    cr_harm_key_infer(&key, &h);
    ok(!key.held, "key mode off: inference starts cold, needs 2 fresh qualifying chords");
}

/* ------------------------------------------------- no source / provisional tonic --- */
static void t_no_source_and_provisional(void)
{
    cr_chord_info_t src; cr_harm_key_t key; cr_harm_result_t out[3];
    memset(&src, 0, sizeof src); src.valid = 0;
    cr_harm_key_clear(&key);
    ok(cr_harm_suggest(&key, &src, 0, out) == 0, "no source: suggest returns 0, caller shows \"Play a chord\"");

    make_source(&src, 60, CR_Q_MIN, 0);     /* a plain Cm triad, no key held: provisional tonic = C minor */
    ok(cr_harm_suggest(&key, &src, 0, out) == 1, "provisional tonic: suggest still returns 1 with no key held");
    {
        /* bVI (AbM7, REST) and plain i are both diatonic to natural minor -- not borrowed, fine to show.
         * The one candidate flagged borrowed for minor is major V7 (the PULL slot landing on root 7, MAJ,
         * m7): that one must be suppressed while provisional, falling back to bVII instead. */
        int pull_is_borrowed_v7 = out[2].kind == CR_HARM_SUGGEST && out[2].root_pc == 7 &&
                                   out[2].quality == CR_Q_MAJ && out[2].ext == CR_EXT_M7;
        ok(!pull_is_borrowed_v7, "provisional tonic: the borrowed major-V7 candidate is suppressed");
    }
}

/* ------------------------------------------------------ no duplicate slots --- */
static void t_no_duplicate_slots(void)
{
    int t, s, all_distinct = 1;
    static const struct { uint8_t q, x; } sources[] = {
        { CR_Q_MIN, 0 }, { CR_Q_MAJ, 0 }, { CR_Q_MIN, (uint8_t)(CR_EXT_M7 | CR_EXT_9) }, { CR_Q_DIM, 0 },
    };
    for (t = 0; t < 12; t++) {
        for (s = 0; s < (int)(sizeof sources / sizeof sources[0]); s++) {
            cr_chord_info_t src; cr_harm_key_t key; cr_harm_result_t out[3]; int a, b;
            make_source(&src, 60 + t, sources[s].q, sources[s].x);
            cr_harm_key_explicit(&key, (uint8_t)t, s % 2 ? CR_SCALE_MINOR : CR_SCALE_MAJOR);
            cr_harm_suggest(&key, &src, 0, out);
            for (a = 0; a < 3; a++) for (b = a + 1; b < 3; b++) {
                if (out[a].kind == CR_HARM_UNAVAILABLE || out[b].kind == CR_HARM_UNAVAILABLE) continue;
                if (out[a].nnotes != out[b].nnotes) continue;
                if (memcmp(out[a].notes, out[b].notes, out[a].nnotes) == 0) all_distinct = 0;
            }
        }
    }
    ok(all_distinct, "no duplicate slots: across 12 keys x 4 source chords x 2 scales, every pair of filled slots differs");
}

/* ------------------------------------------------------------- determinism --- */
static void t_determinism(void)
{
    cr_chord_info_t src; cr_harm_key_t key; cr_harm_result_t out1[3], out2[3];
    memset(out1, 0, sizeof out1); memset(out2, 0, sizeof out2);   /* notes[]'s unused tail is otherwise
                                                                      * uninitialized stack garbage, not a
                                                                      * real nondeterminism */
    make_source(&src, 67, CR_Q_MAJ, CR_EXT_M7);
    cr_harm_key_explicit(&key, 9, CR_SCALE_MINOR);
    cr_harm_suggest(&key, &src, 0, out1);
    cr_harm_suggest(&key, &src, 0, out2);
    ok(memcmp(out1, out2, sizeof out1) == 0, "determinism: same input, same output, called twice");
}

/* ------------------------------------------------------- unequal chord sizes --- */
static void t_unequal_sizes(void)
{
    cr_chord_info_t src; cr_harm_key_t key; cr_harm_result_t out[3];
    make_source(&src, 60, CR_Q_NONE, 0);        /* a single note: ns = 1 against targets of 3-5 notes */
    cr_harm_key_explicit(&key, 0, CR_SCALE_MAJOR);
    ok(cr_harm_suggest(&key, &src, 0, out) == 1, "unequal sizes: a 1-note source still produces results");
    ok(out[0].nnotes >= 1, "unequal sizes: REST has at least one voice");
}

int main(void)
{
    t_worked_example();
    t_transpositions();
    t_key_stability();
    t_key_modulation();
    t_relative_tie();
    t_history_qualify();
    t_key_mode_transitions();
    t_no_source_and_provisional();
    t_no_duplicate_slots();
    t_determinism();
    t_unequal_sizes();
    printf("\ncr_harmony: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
