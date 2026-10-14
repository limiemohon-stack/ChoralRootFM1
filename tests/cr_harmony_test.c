/* SPDX-License-Identifier: GPL-3.0-only
 * Host tests of Harmony's pure engine (firmware/src/cr_harmony.c: Harmony mode's map, the played-chord listener,
 * the retired Harmony Preview's suggestion engine), against the real chord
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

/* ------------------------------------------------- root_note/rotation replay --- */
static void t_replay(void)
{
    /* A confirm step must sound EXACTLY what's shown, not a re-derived approximation that could land on a
     * different octave or rotation: root_note + rotation through the engine's own cr_chord_base_ex /
     * cr_voice_apply must reproduce notes[] exactly, for every filled slot. */
    cr_chord_info_t src; cr_harm_key_t key; cr_harm_result_t out[3];
    int t, all_ok = 1;
    for (t = 0; t < 12; t++) {
        int s;
        make_source(&src, 60 + t, CR_Q_MIN, (uint8_t)(CR_EXT_M7 | CR_EXT_9));
        cr_harm_key_explicit(&key, (uint8_t)t, CR_SCALE_MINOR);
        cr_harm_suggest(&key, &src, 0, out);
        for (s = 0; s < 3; s++) {
            uint8_t base[CR_CHORD_MAX], voiced[CR_CHORD_MAX]; int nbase, nv;
            if (out[s].kind == CR_HARM_UNAVAILABLE) continue;
            nbase = cr_chord_base_ex(out[s].root_note, out[s].quality, out[s].ext, 0, base);
            nv = cr_voice_apply(base, nbase, out[s].rotation, voiced);
            if (nv != out[s].nnotes || memcmp(voiced, out[s].notes, (size_t)nv) != 0) all_ok = 0;
        }
    }
    ok(all_ok, "replay: root_note + rotation reproduce notes[] exactly, every filled slot, all 12 transpositions");
}

/* ------------------------------------------------- the played-chord listener --- */
/* history's newest entry is (root_pc, quality, ext) */
static int newest_is(const cr_harm_hist_t *h, uint8_t root_pc, uint8_t q, uint8_t x)
{
    return h->n > 0 && h->e[h->n - 1].root_pc == root_pc && h->e[h->n - 1].quality == q && h->e[h->n - 1].ext == x;
}
#define BEGIN 1
#define END 0

static void t_listen_settle(void)
{
    cr_harm_listen_t L; cr_harm_hist_t h; int n;
    cr_harm_listen_clear(&L); cr_harm_hist_clear(&h);
    ok(cr_harm_listen_event(&L, &h, 7, 60, CR_Q_MAJ, 0, BEGIN, 1000) == 0 && h.n == 0,
       "listen: a chord just struck is not history yet");
    n = cr_harm_listen_tick(&L, &h, 1000 + CR_HARM_SETTLE_MS - 1);
    ok(n == 0 && h.n == 0, "listen: one ms short of the settle time, still nothing");
    n = cr_harm_listen_tick(&L, &h, 1000 + CR_HARM_SETTLE_MS);
    ok(n == 1 && h.n == 1 && newest_is(&h, 0, CR_Q_MAJ, 0), "listen: held for the settle time, C major is committed");
    ok(cr_harm_listen_tick(&L, &h, 5000) == 0 && h.n == 1, "listen: a held chord commits once, not every tick");
    ok(cr_harm_listen_event(&L, &h, 7, 60, CR_Q_MAJ, 0, END, 5000) == 0 && h.n == 1,
       "listen: its release (unchanged) adds nothing");
}

static void t_listen_brush(void)
{
    cr_harm_listen_t L; cr_harm_hist_t h;
    cr_harm_listen_clear(&L); cr_harm_hist_clear(&h);
    cr_harm_listen_event(&L, &h, 1, 62, CR_Q_MIN, 0, BEGIN, 0);
    ok(cr_harm_listen_event(&L, &h, 1, 62, CR_Q_MIN, 0, END, 60) == 0, "listen: a 60 ms brush, released: not committed");
    ok(cr_harm_listen_tick(&L, &h, 10000) == 0 && h.n == 0, "listen: .. and never later either");
    /* a release drained in the same frame as its strike, but after a long one: still judged by the sound's length */
    cr_harm_listen_event(&L, &h, 2, 62, CR_Q_MIN, 0, BEGIN, 20000);
    ok(cr_harm_listen_event(&L, &h, 2, 62, CR_Q_MIN, 0, END, 20000 + 300) == 1 && newest_is(&h, 2, CR_Q_MIN, 0),
       "listen: struck and released before any tick, but it sounded 300 ms: committed at its end");
}

static void t_listen_latched(void)
{
    cr_harm_listen_t L; cr_harm_hist_t h;
    cr_harm_listen_clear(&L); cr_harm_hist_clear(&h);
    cr_harm_listen_event(&L, &h, 3, 64, CR_Q_MIN, 0, BEGIN, 0);
    ok(cr_harm_listen_event(&L, &h, 3, 64, CR_Q_MIN, 0, CR_G_KEYUP, 30) == 0, "listen: Sticky: a 30 ms tap's key-up commits nothing yet");
    ok(cr_harm_listen_tick(&L, &h, CR_HARM_SETTLE_MS) == 1 && newest_is(&h, 4, CR_Q_MIN, 0),
       "listen: .. but the latched chord sounds on: committed at the settle time");
    ok(cr_harm_listen_event(&L, &h, 3, 64, CR_Q_MIN, 0, END, 4000) == 0 && h.n == 1,
       "listen: the latch's end (the next press, a panic) adds nothing");
}

static void t_listen_morph(void)
{
    cr_harm_listen_t L; cr_harm_hist_t h;
    /* Advanced / Free: C major held, a M7 added while it sounds -- the end reports Cmaj7: both chords were heard */
    cr_harm_listen_clear(&L); cr_harm_hist_clear(&h);
    cr_harm_listen_event(&L, &h, 4, 60, CR_Q_MAJ, 0, BEGIN, 0);
    cr_harm_listen_tick(&L, &h, 200);
    ok(cr_harm_listen_event(&L, &h, 4, 60, CR_Q_MAJ, CR_EXT_MAJ7, END, 900) == 1 && h.n == 2 &&
       newest_is(&h, 0, CR_Q_MAJ, CR_EXT_MAJ7), "listen: a held chord changed after it committed: the final one is committed too");
    /* changed (and latched) before it settled: only the chord the player arrived at is committed */
    cr_harm_listen_clear(&L); cr_harm_hist_clear(&h);
    cr_harm_listen_event(&L, &h, 5, 60, CR_Q_MAJ, 0, BEGIN, 0);
    cr_harm_listen_event(&L, &h, 5, 60, CR_Q_MIN, CR_EXT_M7, CR_G_KEYUP, 50);
    ok(cr_harm_listen_tick(&L, &h, 200) == 1 && h.n == 1 && newest_is(&h, 0, CR_Q_MIN, CR_EXT_M7),
       "listen: changed before the settle time: only Cm7 is committed, not the C it began as");
    /* latched, committed, then its final state at key-up differs: committed at once */
    cr_harm_listen_clear(&L); cr_harm_hist_clear(&h);
    cr_harm_listen_event(&L, &h, 6, 60, CR_Q_MIN, 0, BEGIN, 0);
    cr_harm_listen_tick(&L, &h, 500);
    ok(cr_harm_listen_event(&L, &h, 6, 60, CR_Q_MIN, CR_EXT_9, CR_G_KEYUP, 800) == 1 && newest_is(&h, 0, CR_Q_MIN, CR_EXT_9),
       "listen: committed, then a 9 added before the key came up (latched): Cm add9 committed at the key-up");
}

static void t_listen_overlap(void)
{
    cr_harm_listen_t L; cr_harm_hist_t h;
    /* a chord held while a neighbouring root is brushed: the held one is not lost */
    cr_harm_listen_clear(&L); cr_harm_hist_clear(&h);
    cr_harm_listen_event(&L, &h, 10, 62, CR_Q_MAJ, 0, BEGIN, 0);
    cr_harm_listen_event(&L, &h, 11, 64, CR_Q_MAJ, 0, BEGIN, 10);
    cr_harm_listen_event(&L, &h, 11, 64, CR_Q_MAJ, 0, END, 50);
    ok(cr_harm_listen_tick(&L, &h, 200) == 1 && h.n == 1 && newest_is(&h, 2, CR_Q_MAJ, 0),
       "listen: D held, E brushed beside it: D is committed, the brush is not");
    /* two chords held together: both, in the order they began, even struck in the same millisecond */
    cr_harm_listen_clear(&L); cr_harm_hist_clear(&h);
    cr_harm_listen_event(&L, &h, 12, 65, CR_Q_MAJ, 0, BEGIN, 0);
    cr_harm_listen_event(&L, &h, 13, 69, CR_Q_MIN, 0, BEGIN, 0);
    ok(cr_harm_listen_tick(&L, &h, 200) == 2 && h.n == 2 && h.e[0].root_pc == 5 && h.e[1].root_pc == 9,
       "listen: F and Am struck in the same ms: both committed, F (first) then Am");
}

static void t_listen_retrigger_and_filters(void)
{
    cr_harm_listen_t L; cr_harm_hist_t h;
    cr_harm_listen_clear(&L); cr_harm_hist_clear(&h);
    cr_harm_listen_event(&L, &h, 20, 60, CR_Q_MAJ, 0, BEGIN, 0);
    cr_harm_listen_tick(&L, &h, 150);
    cr_harm_listen_event(&L, &h, 20, 60, CR_Q_MAJ, 0, END, 300);   /* the same root again: the engine ends .. */
    cr_harm_listen_event(&L, &h, 21, 60, CR_Q_MAJ, 0, BEGIN, 300); /* .. one gesture and begins another */
    cr_harm_listen_tick(&L, &h, 450);
    ok(h.n == 1, "listen: the same chord struck again (a retrigger) is one history entry, not two");

    cr_harm_listen_clear(&L); cr_harm_hist_clear(&h);
    cr_harm_listen_event(&L, &h, 22, 40, CR_Q_NONE, CR_GX_BASS, BEGIN, 0);
    ok(cr_harm_listen_tick(&L, &h, 1000) == 0 && h.n == 0, "listen: a Split Bass bass-side note is ignored");
    cr_harm_listen_event(&L, &h, 23, 62, CR_Q_NONE, 0, BEGIN, 0);
    ok(cr_harm_listen_tick(&L, &h, 1000) == 0 && h.n == 0, "listen: a single note sounds but is no chord: history rejects it");
    cr_harm_listen_event(&L, &h, 24, 62, CR_Q_POW, 0, BEGIN, 0);
    ok(cr_harm_listen_tick(&L, &h, 1000) == 0 && h.n == 0, "listen: a power chord (2 pitch classes) is rejected too");
    ok(cr_harm_listen_event(&L, &h, 99, 62, CR_Q_MAJ, 0, END, 1000) == 0 && h.n == 0,
       "listen: the end of a gesture never followed changes nothing");
}

static void t_listen_clock(void)
{
    cr_harm_listen_t L; cr_harm_hist_t h;
    /* the UI reads its clock (1000), then drains a strike the ISR stamped 1001: it must not look 2^32 ms old */
    cr_harm_listen_clear(&L); cr_harm_hist_clear(&h);
    cr_harm_listen_event(&L, &h, 30, 67, CR_Q_MAJ, 0, BEGIN, 1001);
    ok(cr_harm_listen_tick(&L, &h, 1000) == 0 && h.n == 0, "listen: a report stamped after the clock read is not ancient");
    ok(cr_harm_listen_tick(&L, &h, 1001 + CR_HARM_SETTLE_MS) == 1, "listen: .. it settles on time");
    /* fm1_ms wraps after 49.7 days of power-on: a chord across the wrap still settles at 120 ms, not before */
    cr_harm_listen_clear(&L); cr_harm_hist_clear(&h);
    cr_harm_listen_event(&L, &h, 31, 67, CR_Q_MAJ, 0, BEGIN, 0xFFFFFFC0u);
    ok(cr_harm_listen_tick(&L, &h, 0x00000030u) == 0, "listen: across the clock's wrap, 112 ms is not yet settled");
    ok(cr_harm_listen_tick(&L, &h, 0x00000038u) == 1, "listen: .. 120 ms is");
}

static void t_listen_capacity(void)
{
    cr_harm_listen_t L; cr_harm_hist_t h; int i;
    /* more gestures at once than slots, none settled: the oldest is forgotten, the newest four commit in order */
    cr_harm_listen_clear(&L); cr_harm_hist_clear(&h);
    for (i = 0; i < CR_HARM_LISTEN_N + 1; i++)
        cr_harm_listen_event(&L, &h, (uint32_t)(40 + i), (int16_t)(60 + 2 * i), CR_Q_MAJ, 0, BEGIN, (uint32_t)i);
    ok(cr_harm_listen_tick(&L, &h, 1000) == CR_HARM_LISTEN_N && h.n == CR_HARM_HIST && h.e[0].root_pc == 2 &&
       h.e[CR_HARM_HIST - 1].root_pc == 8, "listen: five at once, four slots: the first is forgotten, D E F# G# commit in order");
    /* slots full of committed gestures: a new one takes the oldest committed slot, and still settles */
    cr_harm_listen_event(&L, &h, 50, 70, CR_Q_MIN, 0, BEGIN, 2000);
    ok(cr_harm_listen_tick(&L, &h, 2200) == 1 && newest_is(&h, 10, CR_Q_MIN, 0),
       "listen: with every slot following a committed chord, a new one still gets a slot and commits");
}

static void t_listen_infers_key(void)
{
    /* end to end on the pure side: a progression played (strike, hold 400 ms, release) through the listener gives
     * the history key inference reads -- C minor's i-iv-i holds C minor, exactly as the history pushes did */
    static const uint8_t roots[] = { 0, 5, 0 };
    cr_harm_listen_t L; cr_harm_hist_t h; cr_harm_key_t key; int i; uint32_t t = 0;
    cr_harm_listen_clear(&L); cr_harm_hist_clear(&h); cr_harm_key_clear(&key);
    for (i = 0; i < 3; i++) {
        cr_harm_listen_event(&L, &h, (uint32_t)(60 + i), (int16_t)(60 + roots[i]), CR_Q_MIN, 0, BEGIN, t);
        if (cr_harm_listen_tick(&L, &h, t + 200)) cr_harm_key_infer(&key, &h);
        cr_harm_listen_event(&L, &h, (uint32_t)(60 + i), (int16_t)(60 + roots[i]), CR_Q_MIN, 0, END, t + 400);
        t += 500;
    }
    ok(h.n == 3 && key.held && key.tonic_pc == 0 && key.scale == CR_SCALE_MINOR,
       "listen: Cm Fm Cm played on the keys -> history of 3 -> C minor held");
}

/* ------------------------------------------------------------- Harmony mode's map --- */
static int map_has(const cr_hmap_t *m, uint8_t r, uint8_t q)
{
    int i;
    for (i = 0; i < CR_HMAP_W; i++) if (m->w[i].root_pc == r && m->w[i].quality == q) return 1;
    return 0;
}

static void t_map_shape(void)
{
    /* every home key, both scales, every chord you could be on: eleven distinct white chords, never the current
     * one, left to right by tension, each black key the richer version of its white key, the way home marked once */
    static const uint8_t Q[5] = { CR_Q_MAJ, CR_Q_MIN, CR_Q_DIM, CR_Q_SUS, CR_Q_NONE };
    int t, s, r, qi, distinct = 1, notself = 1, ordered = 1, black = 1, home1 = 1;
    for (t = 0; t < 12; t++) for (s = 0; s < 2; s++) for (r = 0; r < 12; r++) for (qi = 0; qi < 5; qi++) {
        cr_hmap_t m; int i, j, homes = 0;
        uint8_t cq = Q[qi] == CR_Q_NONE ? CR_Q_MAJ : Q[qi];
        int on_home = r == t && cq == (s ? CR_Q_MIN : CR_Q_MAJ);
        cr_harm_map((uint8_t)t, (uint8_t)s, (uint8_t)r, Q[qi], 0, &m);
        for (i = 0; i < CR_HMAP_W; i++) {
            for (j = i + 1; j < CR_HMAP_W; j++)
                if (m.w[i].root_pc == m.w[j].root_pc && m.w[i].quality == m.w[j].quality) distinct = 0;
            if (m.w[i].root_pc == r && m.w[i].quality == cq) notself = 0;
            if (i && m.w[i].tension < m.w[i - 1].tension) ordered = 0;
            homes += m.w[i].home;
        }
        for (i = 0; i < CR_HMAP_B; i++) {
            const cr_hmap_chord_t *w = &m.w[CR_HMAP_BW[i]], *b = &m.b[i];
            if (b->root_pc != w->root_pc || b->quality != w->quality || !b->ext) black = 0;
        }
        if (homes != (on_home ? 0 : 1)) home1 = 0;
    }
    ok(distinct, "map: 12 keys x 2 scales x 60 current chords: eleven distinct white chords every time");
    ok(notself, "map: the chord you are on is never offered (it is where you are)");
    ok(ordered, "map: white keys run from restful to pulling (tension never falls left to right)");
    ok(black, "map: each black key plays the richer version of the white key just left of it");
    ok(home1, "map: the home chord is on the keys exactly once (none while you are on it)");
}

static void t_map_context(void)
{
    cr_hmap_t m;
    int i, j, sets = 0;
    cr_hmap_t all[7];
    static const struct { uint8_t r, q; } DIA[7] = { {0, CR_Q_MAJ}, {2, CR_Q_MIN}, {4, CR_Q_MIN}, {5, CR_Q_MAJ},
                                                      {7, CR_Q_MAJ}, {9, CR_Q_MIN}, {11, CR_Q_DIM} };
    cr_harm_map(0, CR_SCALE_MAJOR, 7, CR_Q_MAJ, 0, &m);
    ok(m.w[0].root_pc == 0 && m.w[0].quality == CR_Q_MAJ, "map, C major, on G: the leftmost key resolves home to C");
    cr_harm_map(0, CR_SCALE_MAJOR, 7, CR_Q_MAJ, CR_EXT_M7, &m);
    ok(m.w[0].root_pc == 0 && m.w[0].quality == CR_Q_MAJ, "map, on G7: the same resolution");
    cr_harm_map(0, CR_SCALE_MAJOR, 4, CR_Q_MAJ, 0, &m);
    ok(m.w[0].root_pc == 9 && m.w[0].quality == CR_Q_MIN, "map, on E (V of vi): the leftmost key resolves to Am");
    cr_harm_map(0, CR_SCALE_MAJOR, 11, CR_Q_DIM, 0, &m);
    ok(m.w[0].root_pc == 0 && m.w[0].quality == CR_Q_MAJ, "map, on Bdim: the leading-tone chord resolves to C");
    {   /* a local resolution moves its chord toward the restful end, home staying the most restful of all: G sits
         * further left from Gsus4 (its suspension) and from D (its secondary dominant) than from C */
        int from_c = -1, from_sus = -1, from_d = -1, i;
        cr_harm_map(0, CR_SCALE_MAJOR, 0, CR_Q_MAJ, 0, &m);
        for (i = 0; i < CR_HMAP_W; i++) if (m.w[i].root_pc == 7 && m.w[i].quality == CR_Q_MAJ) from_c = i;
        cr_harm_map(0, CR_SCALE_MAJOR, 7, CR_Q_SUS, 0, &m);
        for (i = 0; i < CR_HMAP_W; i++) if (m.w[i].root_pc == 7 && m.w[i].quality == CR_Q_MAJ) from_sus = i;
        cr_harm_map(0, CR_SCALE_MAJOR, 2, CR_Q_MAJ, 0, &m);
        for (i = 0; i < CR_HMAP_W; i++) if (m.w[i].root_pc == 7 && m.w[i].quality == CR_Q_MAJ) from_d = i;
        ok(from_c >= 0 && from_sus >= 0 && from_d >= 0 && from_sus < from_c && from_d < from_c,
           "map: G moves toward the restful end from Gsus4 and from D (its resolutions) compared with from C");
    }
    cr_harm_map(9, CR_SCALE_MINOR, 4, CR_Q_MAJ, 0, &m);
    ok(m.w[0].root_pc == 9 && m.w[0].quality == CR_Q_MIN, "map, A minor, on E: the leftmost key resolves to Am");
    cr_harm_map(9, CR_SCALE_MINOR, 9, CR_Q_MIN, 0, &m);
    ok(m.w[CR_HMAP_W - 1].root_pc == 4 && m.w[CR_HMAP_W - 1].quality == CR_Q_MAJ,
       "map, A minor, on Am: the rightmost key is E, the dominant");
    ok(map_has(&m, 4, CR_Q_MAJ) && map_has(&m, 5, CR_Q_MAJ) && map_has(&m, 0, CR_Q_MAJ),
       "map, A minor: its own chords are there (E, F, C ..)");
    /* the function-blindness the benchmark found in cr_harm_suggest: here every chord of the key lays the keys out
     * differently */
    for (i = 0; i < 7; i++) {
        int same = 0;
        cr_harm_map(0, CR_SCALE_MAJOR, DIA[i].r, DIA[i].q, 0, &all[i]);
        for (j = 0; j < i; j++) {
            int k, eq = 1;
            for (k = 0; k < CR_HMAP_W; k++)
                if (all[i].w[k].root_pc != all[j].w[k].root_pc || all[i].w[k].quality != all[j].w[k].quality) eq = 0;
            if (eq) same = 1;
        }
        sets += !same;
    }
    ok(sets == 7, "map, C major: each of the 7 diatonic chords lays the keys out differently");
}

static void t_map_transpose(void)
{
    /* the map is the same shape in every key: home t, chord r == home 0, chord r - t, moved up t */
    int t, s, r, all_ok = 1;
    for (t = 1; t < 12; t++) for (s = 0; s < 2; s++) for (r = 0; r < 12; r++) {
        cr_hmap_t a, b; int i;
        cr_harm_map((uint8_t)t, (uint8_t)s, (uint8_t)r, CR_Q_MIN, 0, &a);
        cr_harm_map(0, (uint8_t)s, (uint8_t)((r - t + 12) % 12), CR_Q_MIN, 0, &b);
        for (i = 0; i < CR_HMAP_W; i++)
            if (a.w[i].root_pc != (b.w[i].root_pc + t) % 12 || a.w[i].quality != b.w[i].quality ||
                a.w[i].tension != b.w[i].tension) all_ok = 0;
    }
    ok(all_ok, "map: every home key gives the same layout, transposed (11 keys x 2 scales x 12 chords)");
}

static void t_map_voice(void)
{
    /* a long walk over the keys (a fixed pseudo-random path): every chord voiced near the last, exactly its chord's
     * notes (pitch classes and count), its root_note the chord's root near the voicing (what a looper records), and
     * the lowest note never far from the middle */
    uint8_t cur[CR_CHORD_MAX], base[CR_CHORD_MAX];
    int n = cr_chord_base_ex(60, CR_Q_MAJ, 0, 0, cur), step, shape = 1, sizes = 1, near = 1, lo = 127, hi = 0, moved = 0;
    uint8_t cr = 0, cq = CR_Q_MAJ, cx = 0;
    uint32_t rng = 12345u;
    for (step = 0; step < 400; step++) {
        cr_hmap_t m; const cr_hmap_chord_t *d; uint8_t notes[CR_CHORD_MAX], root_note; int nv, nb, k, i;
        unsigned want = 0, got = 0;
        rng = rng * 1103515245u + 12345u;
        k = (int)((rng >> 16) % (CR_HMAP_W + CR_HMAP_B));
        cr_harm_map(0, CR_SCALE_MAJOR, cr, cq, cx, &m);
        d = k < CR_HMAP_W ? &m.w[k] : &m.b[k - CR_HMAP_W];
        nv = cr_harm_voice(d, cur, n, 0, 0, notes, &root_note);
        nb = cr_chord_base_ex(root_note, d->quality, d->ext, 0, base);
        for (i = 0; i < nb; i++) want |= 1u << (base[i] % 12u);
        for (i = 0; i < nv; i++) got |= 1u << (notes[i] % 12u);
        if (root_note % 12u != d->root_pc || want != got) shape = 0;
        for (i = 1; i < nv; i++) if (notes[i] <= notes[i - 1]) shape = 0;
        if (nv != cr_chord_base_ex(60, d->quality, d->ext, 0, base)) sizes = 0;
        nb = cr_chord_base_ex(root_note, d->quality, d->ext, 0, base);
        if (base[0] + 12 <= notes[0] || base[0] >= notes[0] + 12) near = 0;
        if (notes[0] < lo) lo = notes[0];
        if (notes[0] > hi) hi = notes[0];
        moved += notes[0] > cur[0] ? notes[0] - cur[0] : cur[0] - notes[0];
        memcpy(cur, notes, (size_t)nv); n = nv;
        cr = d->root_pc; cq = d->quality; cx = d->ext;
    }
    ok(shape, "map voicing: every chord of a 400-chord walk is its own notes (pitch classes, root, ascending)");
    ok(sizes, "map voicing: never a note clipped or dropped (triads 3 notes, the black keys' 4 or 5)");
    ok(near, "map voicing: the root_note's plain chord starts within an octave of the voicing (a loop keeps the register)");
    ok(lo >= 40 && hi <= 80, "map voicing: a 400-chord walk keeps its lowest note near the middle (40..80)");
    ok(moved <= 400 * 4, "map voicing: the bass voice moves 4 semitones a chord or less on average (voice leading)");
}

int main(void)
{
    t_map_shape();
    t_map_context();
    t_map_transpose();
    t_map_voice();
    t_listen_settle();
    t_listen_brush();
    t_listen_latched();
    t_listen_morph();
    t_listen_overlap();
    t_listen_retrigger_and_filters();
    t_listen_clock();
    t_listen_capacity();
    t_listen_infers_key();
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
    t_replay();
    printf("\ncr_harmony: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
