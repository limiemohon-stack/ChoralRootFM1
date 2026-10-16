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

/* ----------------------------------------------------------- Harmony mode's lanes --- */

static unsigned pcs_of(const uint8_t *n, int c) { unsigned m = 0; int i; for (i = 0; i < c; i++) m |= 1u << (n[i] % 12u); return m; }

static void t_lanes_shape(void)
{
    /* every home key, both scales: the palette as written (C major: Cmaj9 Am9 Em7 Fmaj9 Dm9 Fm9 Abmaj9 Bb9 G9sus
     * G9 Db9; approach chords E7 C9 A9 Gbmaj7 F9 D9 Ab9), home marked once (D4), eleven distinct white chords */
    static const uint8_t WR[11] = { 0, 9, 4, 5, 2, 5, 8, 10, 7, 7, 1 }, BR[7] = { 4, 0, 9, 6, 5, 2, 8 };
    cr_hmap_t m;
    int t, sc, i, j, ok_written = 1, ok_home = 1, ok_distinct = 1, ok_minor = 1;
    cr_harm_lanes(0, CR_SCALE_MAJOR, &m);
    for (i = 0; i < 11; i++) if (m.w[i].root_pc != WR[i]) ok_written = 0;
    for (i = 0; i < 7; i++) if (m.b[i].root_pc != BR[i]) ok_written = 0;
    if (m.w[0].quality != CR_Q_MAJ || m.w[0].ext != (CR_EXT_MAJ7 | CR_EXT_9) || m.w[5].quality != CR_Q_MIN ||
        m.w[8].quality != CR_Q_SUS || m.w[9].quality != CR_Q_MAJ || m.b[3].ext != CR_EXT_MAJ7) ok_written = 0;
    ok(ok_written, "lanes: C major plays Cmaj9 Am9 Em7 Fmaj9 Dm9 Fm9 Abmaj9 Bb9 G9sus G9 Db9, black E7 C9 A9 Gbmaj7 F9 D9 Ab9");
    cr_harm_lanes(9, CR_SCALE_MINOR, &m);
    if (m.w[0].root_pc != 9 || m.w[0].quality != CR_Q_MIN || m.w[6].quality != CR_Q_DIM || m.w[9].root_pc != 4 ||
        m.w[9].ext != CR_EXT_M7 || m.b[6].root_pc != 6 || m.b[6].quality != CR_Q_DIM) ok_minor = 0;
    ok(ok_minor, "lanes: A minor starts on Am9, has Bm7b5, E7, and F#m7b5 on F#5");
    for (sc = 0; sc < 2; sc++)
        for (t = 0; t < 12; t++) {
            int homes = 0;
            cr_harm_lanes((uint8_t)t, (uint8_t)sc, &m);
            for (i = 0; i < 11; i++) homes += m.w[i].home;
            for (i = 0; i < 7; i++) homes += m.b[i].home * 10;
            if (homes != 1 || !m.w[0].home) ok_home = 0;
            for (i = 0; i < 11; i++)
                for (j = i + 1; j < 11; j++)
                    if (m.w[i].root_pc == m.w[j].root_pc && m.w[i].quality == m.w[j].quality && m.w[i].ext == m.w[j].ext)
                        ok_distinct = 0;
        }
    ok(ok_home, "lanes: the home chord is D4 in every key, and only D4");
    ok(ok_distinct, "lanes: eleven distinct white chords in every key, both scales");
}

static void t_lanes_transpose(void)
{
    cr_hmap_t a, b;
    int t, sc, i, all = 1;
    for (sc = 0; sc < 2; sc++) {
        cr_harm_lanes(0, (uint8_t)sc, &b);
        for (t = 1; t < 12; t++) {
            cr_harm_lanes((uint8_t)t, (uint8_t)sc, &a);
            for (i = 0; i < 11; i++)
                if (a.w[i].root_pc != (b.w[i].root_pc + t) % 12 || a.w[i].quality != b.w[i].quality ||
                    a.w[i].ext != b.w[i].ext || a.w[i].cat != b.w[i].cat) all = 0;
            for (i = 0; i < 7; i++)
                if (a.b[i].root_pc != (b.b[i].root_pc + t) % 12 || a.b[i].quality != b.b[i].quality) all = 0;
        }
    }
    ok(all, "lanes: every home key lays out alike, transposed (12 keys x 2 scales)");
}

static void t_lanes_voicing(void)
{
    /* every chord of every key, both scales, three styles: its own notes (root at the bottom, every pitch class the
     * chord's, the shell's three the root + 3rd + 7th), ascending, the same notes every call, in range, and the
     * top notes in a narrow band per style (the line on top moves by small steps) */
    cr_hmap_t m;
    int t, sc, st, i, own = 1, asc = 1, same = 1, range = 1, shell = 1, lo[3] = { 127, 127, 127 }, hi[3] = { 0, 0, 0 };
    for (sc = 0; sc < 2; sc++)
        for (t = 0; t < 12; t++) {
            cr_harm_lanes((uint8_t)t, (uint8_t)sc, &m);
            for (i = 0; i < 18; i++) {
                const cr_hmap_chord_t *d = i < 11 ? &m.w[i] : &m.b[i - 11];
                uint8_t base[CR_CHORD_MAX];
                int nb = cr_chord_base_ex(60 + d->root_pc, d->quality, d->ext, 0, base);
                unsigned want = pcs_of(base, nb);
                for (st = 0; st < CR_HSTYLE_N; st++) {
                    uint8_t n1[CR_CHORD_MAX], n2[CR_CHORD_MAX], r1, r2;
                    int c1 = cr_harm_voice_style(d, st, 0, 0, n1, &r1), c2 = cr_harm_voice_style(d, st, 0, 0, n2, &r2), k;
                    if (c1 != c2 || memcmp(n1, n2, (size_t)c1) || r1 != r2) same = 0;
                    if (n1[0] % 12u != d->root_pc || r1 % 12u != d->root_pc || (pcs_of(n1, c1) & ~want)) own = 0;
                    for (k = 1; k < c1; k++) if (n1[k] <= n1[k - 1]) asc = 0;
                    if (n1[0] < 40 || n1[0] > 51 || n1[c1 - 1] > 76 || c1 < 3 || c1 > 5) range = 0;
                    if (st == CR_HSTYLE_SHELL && c1 != 3) shell = 0;
                    if (n1[c1 - 1] < lo[st]) lo[st] = n1[c1 - 1];
                    if (n1[c1 - 1] > hi[st]) hi[st] = n1[c1 - 1];
                }
            }
        }
    ok(own, "lane voicing: every chord's notes are its own, root at the bottom (and the loop root its root)");
    ok(asc, "lane voicing: notes ascending, no doubled pitch");
    ok(same, "lane voicing: the same chord gives the same notes every time (nothing depends on the last chord)");
    ok(range && shell, "lane voicing: root in E2..D#3, top at most E5, shell three notes, the others up to five");
    ok(hi[CR_HSTYLE_SHELL] - lo[CR_HSTYLE_SHELL] <= 12 && hi[CR_HSTYLE_COLOUR] - lo[CR_HSTYLE_COLOUR] <= 12,
       "lane voicing: shell and colour keep their top notes within an octave band, every key");
    {
        uint8_t n[CR_CHORD_MAX], r;
        int c, k, sh = 1;
        cr_harm_lanes(0, CR_SCALE_MAJOR, &m);
        c = cr_harm_voice_style(&m.w[0], CR_HSTYLE_COLOUR, 12, 0, n, &r);
        {
            uint8_t z[CR_CHORD_MAX], rz;
            int cz = cr_harm_voice_style(&m.w[0], CR_HSTYLE_COLOUR, 0, 0, z, &rz);
            if (cz != c || rz + 12 != r) sh = 0;
            for (k = 0; k < c && sh; k++) if (z[k] + 12 != n[k]) sh = 0;
        }
        ok(sh, "lane voicing: an octave up (shift 12) is the same voicing, 12 semitones higher");
    }
    {   /* rootless (the Bass part on): no low root, the upper notes as before, Shell keeps three */
        int t, sc, i, st, rl = 1;
        for (sc = 0; sc < 2; sc++)
            for (t = 0; t < 12; t++) {
                cr_harm_lanes((uint8_t)t, (uint8_t)sc, &m);
                for (i = 0; i < 18; i++) {
                    const cr_hmap_chord_t *d = i < 11 ? &m.w[i] : &m.b[i - 11];
                    for (st = 0; st < CR_HSTYLE_N; st++) {
                        uint8_t a[CR_CHORD_MAX], b[CR_CHORD_MAX], ra, rb;
                        int ca = cr_harm_voice_style(d, st, 0, 0, a, &ra), cb = cr_harm_voice_style(d, st, 0, 1, b, &rb), k;
                        if (b[0] < 50 || cb < 3 || ra != rb) rl = 0;
                        if (st != CR_HSTYLE_SHELL) {
                            if (cb != ca - 1) rl = 0;
                            for (k = 0; k < cb; k++) if (b[k] != a[k + 1]) rl = 0;
                        } else if (cb != 3 || b[0] != a[1] || b[1] != a[2]) rl = 0;
                    }
                }
            }
        ok(rl, "lane voicing: rootless drops the low root (C3 B3 D4 E4 G4 -> B3 D4 E4 G4), Shell adds the 9th on top");
    }
}

static void t_worlds(void)
{
    /* every chord of every key, both scales, every world, style, with and without the root: 3 .. 5 notes,
     * ascending, in range, the root at the bottom unless rootless, a name; and the worlds as written in C */
    static const char *const NN[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    cr_hmap_t m;
    int t, sc, i, w, st, rl, ok_all = 1, ok_c = 1;
    for (sc = 0; sc < 2; sc++)
        for (t = 0; t < 12; t++) {
            cr_harm_lanes((uint8_t)t, (uint8_t)sc, &m);
            for (i = 0; i < 18; i++)
                for (w = 0; w < CR_HCOL_N; w++)
                    for (st = 0; st < CR_HSTYLE_N; st++)
                        for (rl = 0; rl < 2; rl++) {
                            const cr_hmap_chord_t *d = i < 11 ? &m.w[i] : &m.b[i - 11];
                            uint8_t n[CR_CHORD_MAX], r, lq, lx;
                            char a[3], q[5], su[CR_SUP_MAX];
                            int c = cr_harm_voice_world(d, w, st, 0, rl, n, &r, a, q, su, &lq, &lx), k;
                            if (c < 3 || c > 5 || !a[0] || r % 12u != d->root_pc || lq >= CR_Q_COUNT) ok_all = 0;
                            for (k = 1; k < c; k++) if (n[k] <= n[k - 1]) ok_all = 0;
                            if (n[0] < 38 || n[c - 1] > 88) ok_all = 0;
                            if (!rl && n[0] % 12u != d->root_pc) ok_all = 0;
                            if (rl && n[0] < 50) ok_all = 0;
                        }
        }
    ok(ok_all, "worlds: every chord, key, world, style, rooted and rootless: 3..5 notes, ascending, in range, named");
    {
        /* key: 0..10 white, 11..17 black (D#4 F#4 G#4 ..); fl: the chord keys held (bit cr_mod_t) */
#define F_(m) (1u << CR_MOD_##m)
        static const struct { int key, world; unsigned fl; const char *name, *notes; } X[] = {
            { 0, CR_HCOL_BARE, 0, "C5", "48 55 60 67" },          { 0, CR_HCOL_TRIAD, 0, "CM", "48 60 64 67" },
            { 0, CR_HCOL_LUSH, 0, "CM9 #11", "48 62 64 66 71" },  { 1, CR_HCOL_LUSH, 0, "Am11", "45 59 60 62 67" },
            { 9, CR_HCOL_ALTERED, 0, "G7 b9b13", "43 59 63 65 68" }, { 1, CR_HCOL_QUARTAL, 0, "Am11", "45 50 55 60 64" },
            { 0, CR_HCOL_UPPER, 0, "D/C", "48 52 62 66 69" },     { 9, CR_HCOL_UPPER, 0, "G7 9", "43 59 62 65 69" },
            { 0, CR_HCOL_TRIAD, F_(6), "C6", "48 60 64 67 69" },  { 0, CR_HCOL_TRIAD, F_(MAJ7), "CM7", "48 59 60 64 67" },
            { 0, CR_HCOL_TRIAD, F_(M7), "C7", "48 60 64 67 70" }, { 1, CR_HCOL_TRIAD, F_(M7), "Am7", "45 60 64 67 69" },
            { 0, CR_HCOL_TRIAD, F_(6) | F_(9), "C6 9", "48 62 64 67 69" }, { 0, CR_HCOL_JAZZ, F_(6), "C6 9", "48 62 64 67 69" },
            { 13, CR_HCOL_ALTERED, 0, "A7 b9", "45 61 64 67 70" }, { 13, CR_HCOL_ALTERED, F_(9), "A7 #9", "45 61 64 67 72" },
            { 9, CR_HCOL_ALTERED, F_(9), "G7 #9b13", "43 59 63 65 70" }, { 11, CR_HCOL_ALTERED, 0, "E7 b9", "40 59 62 65 68" } };
#undef F_
        cr_harm_lanes(0, CR_SCALE_MAJOR, &m);
        for (i = 0; i < (int)(sizeof X / sizeof X[0]); i++) {
            uint8_t n[CR_CHORD_MAX], r, lq, lx;
            char a[3], q[5], su[CR_SUP_MAX], nm[24], ns[32];
            cr_hmap_chord_t e;
            int c, k;
            cr_harm_flavour(X[i].key < 11 ? &m.w[X[i].key] : &m.b[X[i].key - 11], X[i].fl, &e);
            c = cr_harm_voice_world(&e, X[i].world, CR_HSTYLE_COLOUR, 0, 0, n, &r, a, q, su, &lq, &lx);
            size_t o = 0;
            snprintf(nm, sizeof nm, "%s%s%s", a, q, su);
            ns[0] = 0;
            for (k = 0; k < c; k++) o += (size_t)snprintf(ns + o, sizeof ns - o, "%s%d", k ? " " : "", n[k]);
            if (strcmp(nm, X[i].name) || strcmp(ns, X[i].notes)) {
                printf("     world %d key %d: %s [%s], want %s [%s]\n", X[i].world, X[i].key, nm, ns, X[i].name, X[i].notes);
                ok_c = 0;
            }
        }
        (void)NN;
    }
    ok(ok_c, "worlds in C: C5, C, Cmaj9#11, Am11, G7b9b13, quartal Am11 (A D G C E), D/C, upper leaves G9 as it is; triads with chord "
             "keys: C6 Cmaj7 C7 Am7 C6/9; altered approach keys: A7b9 (A7#9 with 9, the #9 over the 3rd), E7b9");
}

static int chord_eq(const cr_hmap_chord_t *a, const cr_hmap_chord_t *b)
{
    return a->root_pc == b->root_pc && a->quality == b->quality && a->ext == b->ext && a->cat == b->cat &&
           a->tension == b->tension && a->home == b->home && a->approach == b->approach;
}
static void t_modes(void)
{
    /* the six modes on every home: Ionian / Aeolian are the palettes as they were; every mode's home on D4, its
     * first seven white keys in the mode, the pull home at the right end the same in all; no chord twice; the
     * signature chords in C; transposing moves it all */
    cr_hmap_t a, b;
    int t, m, i, j, same = 1, shape = 1, pull = 1, uniq = 1, tr = 1, fam = 1;
    for (t = 0; t < 12; t++) {
        cr_harm_lanes((uint8_t)t, CR_SCALE_MAJOR, &a); cr_harm_lanes_mode((uint8_t)t, CR_HMODE_IONIAN, &b);
        for (i = 0; i < 18; i++) if (!chord_eq(i < 11 ? &a.w[i] : &a.b[i - 11], i < 11 ? &b.w[i] : &b.b[i - 11])) same = 0;
        cr_harm_lanes((uint8_t)t, CR_SCALE_MINOR, &a); cr_harm_lanes_mode((uint8_t)t, CR_HMODE_AEOLIAN, &b);
        for (i = 0; i < 18; i++) if (!chord_eq(i < 11 ? &a.w[i] : &a.b[i - 11], i < 11 ? &b.w[i] : &b.b[i - 11])) same = 0;
        if (a.mode != CR_HMODE_AEOLIAN || a.home_scale != CR_SCALE_MINOR) same = 0;
    }
    ok(same, "modes: Ionian and Aeolian are the major and minor palettes exactly, every home");
    for (m = 0; m < CR_HMODE_N; m++) {
        cr_hmap_t c0;
        cr_harm_lanes_mode(0, m, &c0);
        if (c0.home_scale != cr_harm_mode_family(m) || c0.mode != m) fam = 0;
        if (!c0.w[0].home || c0.w[0].root_pc != 0) shape = 0;
        for (i = 1; i < 11; i++) if (c0.w[i].home) shape = 0;
        for (i = 0; i < (m == CR_HMODE_IONIAN || m == CR_HMODE_AEOLIAN ? 5 : 7); i++)   /* (major: Fm9 borrowed on B4) */
            if (c0.w[i].cat != CR_HCAT_DIATONIC) shape = 0;
        for (i = 0; i < 7; i++) if (!c0.b[i].approach || c0.w[i].approach) shape = 0;
        if (c0.w[8].root_pc != 7 || c0.w[8].quality != CR_Q_SUS || c0.w[9].root_pc != 7 || c0.w[9].quality != CR_Q_MAJ ||
            c0.w[10].root_pc != 1 || c0.w[10].quality != CR_Q_MAJ) pull = 0;
        for (i = 0; i < 18; i++)
            for (j = i + 1; j < 18; j++) {
                const cr_hmap_chord_t *x = i < 11 ? &c0.w[i] : &c0.b[i - 11], *y = j < 11 ? &c0.w[j] : &c0.b[j - 11];
                if (x->root_pc == y->root_pc && x->quality == y->quality &&
                    (x->ext & (CR_EXT_M7 | CR_EXT_MAJ7)) == (y->ext & (CR_EXT_M7 | CR_EXT_MAJ7))) uniq = 0;
            }
        for (t = 1; t < 12; t++) {
            cr_harm_lanes_mode((uint8_t)t, m, &a);
            for (i = 0; i < 18; i++) {
                const cr_hmap_chord_t *x = i < 11 ? &c0.w[i] : &c0.b[i - 11], *y = i < 11 ? &a.w[i] : &a.b[i - 11];
                if (y->root_pc != (x->root_pc + t) % 12 || y->quality != x->quality || y->ext != x->ext ||
                    y->cat != x->cat) tr = 0;
            }
        }
    }
    ok(fam, "modes: the family (major: Lydian Ionian Mixolydian; minor: Dorian Aeolian Phrygian) and the mode recorded");
    ok(shape, "modes: home on D4 alone, the first white keys in the mode (seven; major and minor five), the black keys approach chords");
    ok(pull, "modes: the right end pulls home the same way in every mode (V9sus, V, bII9)");
    ok(uniq, "modes: no chord on two keys (give or take a 9th: an approach is never a white key again)");
    ok(tr, "modes: every home is the C palette transposed");
    {   /* the signatures in C */
        static const struct { int mode, key; const char *name; } S[] = {
            { CR_HMODE_LYDIAN, 1, "D7 9" }, { CR_HMODE_LYDIAN, 6, "F#dim7" }, { CR_HMODE_MIXO, 0, "C7 9" },
            { CR_HMODE_MIXO, 1, "A#M7 9" }, { CR_HMODE_MIXO, 2, "Gm7 9" }, { CR_HMODE_DORIAN, 0, "Cm7 9" },
            { CR_HMODE_DORIAN, 1, "F7 9" }, { CR_HMODE_PHRYGIAN, 0, "Cm7" }, { CR_HMODE_PHRYGIAN, 1, "C#M7 9" },
            { CR_HMODE_PHRYGIAN, 2, "A#m7 9" } };
        int ok_s = 1;
        for (i = 0; i < (int)(sizeof S / sizeof S[0]); i++) {
            char r[3], q[5], su[CR_SUP_MAX], nm[24];
            cr_harm_lanes_mode(0, S[i].mode, &a);
            cr_chord_name(a.w[S[i].key].root_pc, a.w[S[i].key].quality, a.w[S[i].key].ext, r, q, su);
            snprintf(nm, sizeof nm, "%s%s%s", r, q, su);
            if (strcmp(nm, S[i].name)) { printf("     mode %d key %d: %s, want %s\n", S[i].mode, S[i].key, nm, S[i].name); ok_s = 0; }
        }
        ok(ok_s, "modes in C: Lydian D9 and F#m7b5, Mixolydian C9 Bbmaj9 Gm9, Dorian Cm9 F9, Phrygian Cm7 Dbmaj9 Bbm9");
    }
    ok(!strcmp(cr_harm_mode_name(CR_HMODE_MIXO), "mixolydian") && !strcmp(cr_harm_mode_name(99), "major"),
       "modes: names (an unknown mode is major)");
}

static void t_roles_approach(void)
{
    /* the worlds keep the job of the chords they do not suit; the four approaches into Dm9, in their worlds */
    static const struct { int key, world, ap; const char *name, *notes; } X[] = {
        { 0, CR_HCOL_ALTERED, -1, "CM7 9", "48 59 62 64 67" },     /* home stays as written in Altered */
        { 1, CR_HCOL_ALTERED, -1, "Am7 9", "45 60 64 67 71" },
        { 9, CR_HCOL_QUARTAL, -1, "G7 9", "43 59 62 65 69" },      /* the V keeps its pull in Quartal .. */
        { 8, CR_HCOL_QUARTAL, -1, "Gsus7", "43 50 55 60 65" },     /* .. its sus is a stack of 4ths */
        { 7, CR_HCOL_UPPER, -1, "A#7 9", "46 60 62 65 68" },       /* a dominant in Upper: as written */
        { 4, CR_HCOL_JAZZ, CR_HAPP_SLIP, "D#m7 9", "51 61 65 66 70" },
        { 4, CR_HCOL_QUARTAL, CR_HAPP_SLIP, "D#m11", "51 56 61 66 70" },   /* the quartal shape slides as itself */
        { 4, CR_HCOL_JAZZ, CR_HAPP_DOM, "A7", "45 61 64 67 69" },
        { 4, CR_HCOL_ALTERED, CR_HAPP_DOM, "A7 b9", "45 61 64 67 70" },
        { 4, CR_HCOL_LUSH, CR_HAPP_DOM, "A13", "45 59 61 66 67" },
        { 4, CR_HCOL_JAZZ, CR_HAPP_DIM, "C#\2607", "49 61 64 67 70" },
        { 4, CR_HCOL_LUSH, CR_HAPP_DIM, "C#\2607", "49 61 64 67 70" },     /* a dim7 in Lush too */
        { 4, CR_HCOL_JAZZ, CR_HAPP_TRITONE, "D#7 9", "51 61 65 67 70" },
        { 0, CR_HCOL_JAZZ, CR_HAPP_DOM, "G7 9", "43 59 62 65 69" },        /* into a major chord: its V9 */
    };
    cr_hmap_t m;
    int i, ok_x = 1, all = 1, t, sc, k, w, ap;
    cr_harm_lanes(0, CR_SCALE_MAJOR, &m);
    for (i = 0; i < (int)(sizeof X / sizeof X[0]); i++) {
        uint8_t n[CR_CHORD_MAX], r, lq, lx;
        char a[3], q[5], su[CR_SUP_MAX], nm[24], ns[32];
        cr_hmap_chord_t e = m.w[X[i].key];
        int c, j;
        size_t o = 0;
        if (X[i].ap >= 0) cr_harm_approach(&m.w[X[i].key], X[i].ap, &e);
        c = cr_harm_voice_world(&e, X[i].world, CR_HSTYLE_COLOUR, 0, 0, n, &r, a, q, su, &lq, &lx);
        snprintf(nm, sizeof nm, "%s%s%s", a, q, su);
        ns[0] = 0;
        for (j = 0; j < c; j++) o += (size_t)snprintf(ns + o, sizeof ns - o, "%s%d", j ? " " : "", n[j]);
        if (strcmp(nm, X[i].name) || strcmp(ns, X[i].notes)) {
            printf("     case %d: %s [%s], want %s [%s]\n", i, nm, ns, X[i].name, X[i].notes);
            ok_x = 0;
        }
    }
    ok(ok_x, "roles: Altered leaves home and the resting chords as written, Quartal and Upper leave the dominants; "
             "approaches into Dm9: Ebm9 (quartal: its shape), A7 (A7b9 altered, A13 lush), C#dim7 everywhere, Eb9; into C: G9");
    for (sc = 0; sc < 2; sc++)
        for (t = 0; t < 12; t++) {
            cr_harm_lanes((uint8_t)t, (uint8_t)sc, &m);
            for (k = 0; k < 18; k++)
                for (ap = 0; ap < CR_HAPP_N; ap++)
                    for (w = 0; w < CR_HCOL_N; w++) {
                        const cr_hmap_chord_t *d = k < 11 ? &m.w[k] : &m.b[k - 11];
                        cr_hmap_chord_t e;
                        uint8_t n[CR_CHORD_MAX], r, lq, lx;
                        char a[3], q[5], su[CR_SUP_MAX];
                        int c, j, want;
                        cr_harm_approach(d, ap, &e);
                        want = ap == CR_HAPP_DOM ? (d->root_pc + 7) % 12 : ap == CR_HAPP_DIM ? (d->root_pc + 11) % 12
                                                                                              : (d->root_pc + 1) % 12;
                        c = cr_harm_voice_world(&e, w, CR_HSTYLE_SPREAD, 0, 0, n, &r, a, q, su, &lq, &lx);
                        if (e.root_pc != want || c < 3 || c > 5 || !a[0] || r % 12u != e.root_pc || n[0] % 12u != e.root_pc)
                            all = 0;
                        for (j = 1; j < c; j++) if (n[j] <= n[j - 1]) all = 0;
                    }
        }
    ok(all, "approaches: into every key of every home, in every world: the right root, 3..5 notes ascending, named");
}

int main(void)
{
    t_lanes_shape();
    t_roles_approach();
    t_modes();
    t_lanes_transpose();
    t_lanes_voicing();
    t_worlds();
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
