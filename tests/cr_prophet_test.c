/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of ChoralRoot's P5 (Melodee's Prophet-5 engine, docs/PROPHET.md; the code's names: PROPHET, p5_) on the
 * firmware as the emulator builds it (tools/emu/emu_firmware.h: hostsim's sound side with voice.c, the ChoralRoot UI,
 * storage.c on the RAM NOR):
 *   sh tests/run_cr_tests.sh
 *   build/host/cr_prophet_test               the checks
 *   build/host/cr_prophet_test --pcm FILE    a digest of every sample of a fixed render of every shipped program
 *                                            (a line names the program by its Sequential number, not its place) and
 *                                            of stress programs (sync, Poly-Mod, the wheel, both filters):
 *                                            tests/run_prophet_test.sh builds this file with P5_STAGE36 0, 1, 2, 4
 *                                            and 7 and compares the five outputs (the remix's optimisations are
 *                                            bit-exact), and with two shipped lists (a program renders the same
 *                                            samples wherever it stands)
 *   build/host/cr_prophet_test --idle FILE   a digest of every shipped program played, silent, and played again (the
 *                                            wheel up): run_prophet_test.sh compares it with P5_IDLE_SKIP=0's (a part
 *                                            with no voice skips the wheel's block and leaves the same state, 3y45)
 *   build/host/cr_prophet_test --gesture [ID [v]]   the owner's gesture of 3y44 (minor + 6 + 9, then minor + 9 again)
 *                                            on Sequential's program ID (16) through the chord engine, the allocator
 *                                            and the mix: 600 runs, exit 1 when one breaks a rule (g_sweep, below; v:
 *                                            every run's line); --gesture-sweep: every option and timing (minutes; G_ROOT=0..3: one root)
 *   build/host/cr_prophet_test --id-out FILE the session's record of the chord part on every shipped program, as
 *                                            cr_settings.c css_part_get writes it
 *   build/host/cr_prophet_test --fav-out FILE / --fav-in FILE   the favourites across lists (0.22): programs marked in
 *                                            one build, the object read by another: the star follows the NUMBER
 *   build/host/cr_prophet_test --id-in FILE  .. read by THIS build (css_part_put), which may ship another list: every
 *                                            record brings its own program back, at the program's place here or, when
 *                                            it is not shipped here, at no place (run_prophet_test.sh)
 * Checks: the engine's place (16, after CZ-1 and before WHEEL, six engines shown, named P5); Sequential's bank (the
 * generated packed form of the shipped list = the .syx's frames of those numbers decoded; decode / encode round trip;
 * bad frames refused); the PRESETS names; the blob (138 bytes) round trip, a bad blob = INIT; every program rendered
 * through voice.c (a four-note chord held and released): sounds, no sample beyond the voices' sum, no DC after the
 * blocker, every voice free after its release, no step at the last voice's end (3y44: the blocker discharges, it is
 * not cut to 0), exact silence after that; note-off frees the voices; a struck note again (no
 * click beyond the wave's own steps); the four-voice cap, the budget's units and which keys of a six-key chord sound;
 * UNISON (the editor's switch; A LOAD TAKES THE PROGRAM'S: one note of a chord on the stacked voices, which note, the
 * units; poly again on the next program; another engine; the bass part; the session's record; no hanging note) and
 * GLIDE; the pitch bend through the part's MIDI (cr_out.c midi_event): +2 semitones measured; the deep page table (19
 * pages: every column sets the byte it names and only that one, clamps, reads back; every one of the 56 bytes on
 * exactly one column); the pool (INIT + the shipped list) and a load through it; the SAVE dialog (Favorite alone, OCT+ refused, no
 * record written. With -DFELUCCA_P5_FACTORY=0: the engine with INIT alone builds and plays. */
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../tools/emu/emu_hooks.h"

emu_hal_t emu_hal;
const int8_t emu_keymap[6][EMU_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
#include "../tools/emu/emu_firmware.h"

static int fails, checks;
#define CHECK(c, ...)                                                   \
    do {                                                                \
        checks++;                                                       \
        if (!(c)) {                                                     \
            fails++;                                                    \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);               \
            printf(__VA_ARGS__);                                        \
            printf("\n");                                               \
        }                                                               \
    } while (0)
#define OK(c, ...)                                                      \
    do {                                                                \
        int ok_ = (c);                                                  \
        CHECK(ok_, __VA_ARGS__);                                        \
        if (ok_) {                                                      \
            printf("ok   ");                                            \
            printf(__VA_ARGS__);                                        \
            printf("\n");                                               \
        }                                                               \
    } while (0)

static uint32_t rs = 0x1234567u;
static uint32_t rnd(uint32_t n)
{
    rs = rs * 1664525u + 1013904223u;
    return (rs >> 8) % n;
}

/* part 0 on PROPHET with program p (as a load leaves it), its engine state as at an engine switch */
static void part_set(track_t *t, const p5_patch_t *p)
{
    uint32_t i;
    memset(t->v, 0, sizeof t->v);
    for (i = 0; i < P_COUNT; i++)
        t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = 0;
    t->p[P_VOICE] = V_POLY;
    t->p[P_SUS] = 127;
    t->engine = t->eng_req = (uint8_t)ENGI_PROPHET;
    t->preset = 0;
    t->user = 0;
    t->nmono = t->mono_note = 0;
    t->mw = t->at = 0;
    t->xf_on = t->xf = 0;
    eng_state_clear((uint32_t)(t - trk));
    midi_bend_q8[t - trk] = midi_bend_target[t - trk] = 0;
    p5_track_loaded(t);                               /* (preset 0: INIT, the part's voice mode noted) */
    if (p) {
        p5_patch_put(t, p);
        p5_track_accept(t);
    }
}
static uint32_t busy(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active;
    return n;
}
static uint64_t fnv(uint64_t h, const int32_t *x, uint32_t n)
{
    uint32_t i, k;
    for (i = 0; i < n; i++)
        for (k = 0; k < 4u; k++)
            h = (h ^ (((uint32_t)x[i] >> (8u * k)) & 255u)) * 1099511628211ull;
    return h;
}

/* a program's fixed render: a chord, held hb blocks, released, rb blocks more. *pk the largest |sample|, *sum2 / *sum
 * of the held part after its first quarter (the level, the DC); returns the digest of every sample */
typedef struct { int32_t pk; double rms, dc; uint32_t left; int32_t tail; uint64_t h; } rr_t;
static rr_t render(track_t *t, const p5_patch_t *p, const uint8_t *notes, uint32_t nn, uint32_t vel, uint32_t hb, uint32_t rb)
{
    rr_t r;
    int32_t out[CTL];
    double s2 = 0, s1 = 0;
    uint32_t b, i, n = 0;
    memset(&r, 0, sizeof r);
    r.h = 1469598103934665603ull;
    part_set(t, p);
    for (i = 0; i < nn; i++)
        trk_note_on(t, notes[i], vel);
    for (b = 0; b < hb + rb; b++) {
        if (b == hb)
            for (i = 0; i < nn; i++)
                trk_note_off(t, notes[i]);
        track_render(t, out, CTL);
        r.h = fnv(r.h, out, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t a = out[i] < 0 ? -out[i] : out[i];
            if (a > r.pk)
                r.pk = a;
            if (b >= hb / 4u && b < hb) {
                s2 += (double)out[i] * out[i];
                s1 += out[i];
                n++;
            }
            if (b + 1u == hb + rb && a > r.tail)
                r.tail = a;
        }
    }
    r.rms = n ? sqrt(s2 / n) : 0;
    r.dc = n ? s1 / n : 0;
    r.left = busy(t);
    return r;
}

/* the stress programs of --pcm (and of the overflow check): every path of the sample loop the factory bank may not
 * reach at its extremes */
static void stress(uint32_t k, p5_patch_t *p)
{
    p5_patch_init(p);
    p->raw[P5_LEVEL_B] = 127;
    p->raw[P5_SAW_B] = 1;
    p->raw[P5_PULSE_A] = p->raw[P5_PULSE_B] = 1;
    p->raw[P5_TRI_B] = (uint8_t)(k & 1u);
    p->raw[P5_SYNC] = (uint8_t)((k >> 1) & 1u);
    p->raw[P5_POLY_FREQ] = (uint8_t)((k >> 2) & 1u);
    p->raw[P5_POLY_PW] = p->raw[P5_POLY_FILTER] = (uint8_t)((k >> 3) & 1u);
    p->raw[P5_FILTER_REV] = (uint8_t)((k >> 4) & 1u);
    p->raw[P5_POLY_ENV] = 127;
    p->raw[P5_POLY_B] = 127;
    p->raw[P5_FREQ_B] = (uint8_t)(k & 32u ? 96 : 31);
    p->raw[P5_FINE_B] = 77;
    p->raw[P5_RESONANCE] = (uint8_t)(k & 64u ? 127 : 60);
    p->raw[P5_CUTOFF] = (uint8_t)(k & 64u ? 70 : 100);
    p->raw[P5_NOISE] = (uint8_t)(k & 128u ? 127 : 0);
    p->raw[P5_ENV_FILTER] = 127;
    p->raw[P5_LFO_RATE] = 127;
    p->raw[P5_LFO_INITIAL] = 127;
    p->raw[P5_LFO_SAW] = p->raw[P5_LFO_TRI] = p->raw[P5_LFO_PULSE] = 1;
    p->raw[P5_WHEEL_MIX] = 64;
    p->raw[P5_WHEEL_FREQ_A] = p->raw[P5_WHEEL_FREQ_B] = p->raw[P5_WHEEL_PW_A] = p->raw[P5_WHEEL_PW_B] =
        p->raw[P5_WHEEL_FILTER] = (uint8_t)((k >> 8) & 1u);
    p->raw[P5_LOW_B] = (uint8_t)((k >> 9) & 1u);
    p->raw[P5_KEY_B] = (uint8_t)!((k >> 10) & 1u);
    p->raw[P5_VINTAGE] = 127;
    p->raw[P5_DECAY_FILTER] = 40;
    p->raw[P5_SUSTAIN_FILTER] = 40;
}
#define NSTRESS 2048u

static int pcm_file(const char *path)
{
    static const uint8_t CH[4] = {36, 55, 64, 96};
    track_t *t = &trk[0];
    FILE *f = fopen(path, "w");
    p5_patch_t p;
    uint32_t k;
    if (!f)
        return 1;
    for (k = 0; k < NELEM(P5_TEST_PRESETS); k++) {       /* (named by the program's number: 000 INIT) */
        p5_preset_patch(k, &p);
        vage = 0;                                      /* (a render of its own: the voices' ages seed their noise, and
                                                        * the same samples whatever was rendered before: the lists) */
        rng_state = 0x1234567u;
        fprintf(f, "program %03u %016llx\n", (unsigned)p5_preset_id(k), (unsigned long long)render(t, &p, CH, 4, 100, 300, 150).h);
    }
    for (k = 0; k < NSTRESS; k += 3u) {
        stress(k, &p);
        fprintf(f, "stress %u %016llx\n", (unsigned)k, (unsigned long long)render(t, &p, CH, 4, 127, 60, 30).h);
    }
    fclose(f);
    return 0;
}


/* --stack [v] (3y46, docs/PROPHET.md "Level"): A UNISON KEY IS AS LOUD WITH ONE CORE AS WITH TWO. Every shipped program
 * with the UNISON switch on the BASS part, one key struck four times (the first with the voices' phases as a cleared
 * part has them, the others as the stack leaves them): with two cores (the program's whole stack sounds) and with
 * one (ONE voice, raised to the stack's level: eng_prophet_test.c P5_STACK_G). The RMS of the held note, strikes 2
 * to 4. Returns the largest difference in dB (0: no such program); v: a line a program */
typedef struct { uint32_t n, n1, n2, nx; double mean, worst, wx; } p5stk_t;   /* programs, within 1 / 2 dB, the ones that
                                                         * are one voice N times (DETUNE 0, Vintage 0, no noise, no
                                                         * Lo Freq B) and their largest difference */
static double stack_levels(int verbose, p5stk_t *st)
{
    track_t *t = &trk[1];
    p5_patch_t p;
    int32_t out[CTL];
    double worst = 0, sum = 0;
    uint32_t k, m, s, b, i, n = 0;
    memset(st, 0, sizeof *st);
    for (k = 1; k < NELEM(P5_TEST_PRESETS); k++) {
        double r[2] = {0, 0};
        uint32_t nv[2] = {0, 0};
        int32_t pk[2] = {0, 0};
        p5_preset_patch(k, &p);
        if (!p.raw[P5_UNISON])
            continue;
        for (m = 0; m < 2u; m++) {
            double s2 = 0;
            uint32_t ns = 0;
            cr_dbg.c1 = m ? C1_OFF : C1_OK;
            vage = 0;
            rng_state = 0x1234567u;
            part_set(t, &p);
            t->p[P_VOICE] = V_UNISON;
            for (s = 0; s < 4u; s++) {
                trk_note_on(t, 40, 100);
                for (b = 0; b < 600u; b++) {
                    if (b == 400u)
                        trk_note_off(t, 40);
                    track_render(t, out, CTL);
                    if (b == 100u && busy(t) > nv[m])
                        nv[m] = busy(t);
                    if (s && b >= 20u && b < 400u)
                        for (i = 0; i < CTL; i++, ns++)
                            s2 += (double)out[i] * out[i];
                    for (i = 0; i < CTL; i++)
                        if (abs(out[i]) > pk[m])
                            pk[m] = abs(out[i]);
                }
                trk_all_off(t);
                for (b = 0; b < 4u * FS / CTL && busy(t); b++)
                    track_render(t, out, CTL);
            }
            r[m] = sqrt(s2 / (ns ? ns : 1u));
        }
        cr_dbg.c1 = C1_OFF;
        if (r[0] > 1.0 && r[1] > 1.0) {
            double d = 20.0 * log10(r[1] / r[0]);
            if (verbose)
                printf("stack: program %03u %-12s the stack %u: two cores %u voices rms %.0f, one core %u voice rms %.0f: %+.2f dB; the peaks %d, %d: %+.2f dB\n",
                       (unsigned)p5_preset_id(k), P5_TEST_PRESETS[k].name, (unsigned)p5_stack(t), (unsigned)nv[0], r[0],
                       (unsigned)nv[1], r[1], d, (int)pk[0], (int)pk[1], 20.0 * log10((pk[1] + 1.0) / (pk[0] + 1.0)));
            sum += d;
            n++;
            st->n1 += fabs(d) <= 1.0;
            st->n2 += fabs(d) <= 2.0;
            if (!p.raw[P5_UNISON_DETUNE] && !p.raw[P5_VINTAGE] && !p.raw[P5_NOISE] && !p.raw[P5_LOW_B]) {
                st->nx++;
                if (fabs(d) > st->wx)
                    st->wx = fabs(d);
            }
            if (fabs(d) > fabs(worst))
                worst = d;
            if (nv[1] != 1u || nv[0] != p5_stack(t))
                worst = 99.0;                           /* (not the voices the rules give: a failure) */
        }
    }
    st->n = n;
    st->mean = n ? sum / n : 0;
    st->worst = worst;
    return worst;
}

/* --unichord (3y46, docs/PROPHET.md "Level": the stack's trim, decided by this): every shipped program, velocity 127:
 * ONE KEY ON A UNISON STACK of four (C3; the editor's switch on every program) against A FOUR-NOTE CHORD of the same
 * program (C4 E4 G4 B4, POLY), two cores, the part's largest sample. Prints the distribution of the difference */
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static int unichord(void)
{
    static const uint8_t CH[4] = {60, 64, 67, 71}, ONE[1] = {48};
    static double d[256], pu[256], pc[256];
    track_t *t = &trk[0];
    p5_patch_t p;
    uint32_t k, n = 0, over3 = 0, over6 = 0;
    cr_dbg.c1 = C1_OK;
    for (k = 0; k < NELEM(P5_TEST_PRESETS) && n < 256u; k++) {
        rr_t c, u;
        p5_preset_patch(k, &p);
        p.raw[P5_UNISON] = 0;
        vage = 0;
        rng_state = 0x1234567u;
        c = render(t, &p, CH, 4, 127, 1400, 100);
        p.raw[P5_UNISON] = 1;
        p.raw[P5_UNISON_COUNT] = 4;
        vage = 0;
        rng_state = 0x1234567u;
        part_set(t, &p);
        t->p[P_VOICE] = V_UNISON;
        {   /* (render() loads the part again: the stack by hand) */
            int32_t out[CTL], pk = 0;
            uint32_t b, i;
            trk_note_on(t, ONE[0], 127);
            for (b = 0; b < 1500u; b++) {
                if (b == 1400u)
                    trk_note_off(t, ONE[0]);
                track_render(t, out, CTL);
                for (i = 0; i < CTL; i++)
                    if (abs(out[i]) > pk)
                        pk = abs(out[i]);
            }
            u.pk = pk;
        }
        if (c.pk < 64 || u.pk < 64)
            continue;
        pu[n] = 20.0 * log10(u.pk / 32768.0);
        pc[n] = 20.0 * log10(c.pk / 32768.0);
        d[n] = pu[n] - pc[n];
        over3 += d[n] > 3.0;
        over6 += d[n] > 6.0;
        n++;
    }
    cr_dbg.c1 = C1_OFF;
    if (!n)
        return 1;
    qsort(d, n, sizeof d[0], cmp_d);
    qsort(pu, n, sizeof pu[0], cmp_d);
    qsort(pc, n, sizeof pc[0], cmp_d);
    printf("unichord: %u programs (P5_UNISON_TRIM %d): a UNISON key's peak re 32768: median %+.1f dB, max %+.1f; a four-note "
           "chord's: median %+.1f, max %+.1f; the key over the chord of the same program: median %+.1f dB, quartiles %+.1f .. "
           "%+.1f, max %+.1f; more than 3 dB over on %u, more than 6 dB on %u\n", (unsigned)n, P5_UNISON_TRIM, pu[n / 2u],
           pu[n - 1u], pc[n / 2u], pc[n - 1u], d[n / 2u], d[n / 4u], d[3u * n / 4u], d[n - 1u], (unsigned)over3, (unsigned)over6);
    return 0;
}

/* --half FILE (3y46, the level): every shipped program's fixed render with the samples as 3y45's level has them: this
 * build's shifted up by 1 - P5_OUT_SHIFT. tests/run_prophet_test.sh builds the file with -DP5_DC_BLOCK=0 twice,
 * P5_OUT_SHIFT 0 (this tree) and 1 (3y45's "x 2" after the VCA), and wants the two outputs to be the same bytes: every
 * sample of every voice is exactly half of what it was, the filter's and the ladder's values what they were */
static int half_file(const char *path)
{
    static const uint8_t CH[4] = {36, 55, 64, 96};
    track_t *t = &trk[0];
    FILE *f = fopen(path, "w");
    p5_patch_t p;
    int32_t out[CTL];
    uint32_t k, b, i;
    if (!f)
        return 1;
    for (k = 0; k < NELEM(P5_TEST_PRESETS); k++) {
        uint64_t h = 1469598103934665603ull;
        uint32_t odd = 0;
        p5_preset_patch(k, &p);
        vage = 0;
        rng_state = 0x1234567u;
        part_set(t, &p);
        for (i = 0; i < 4u; i++)
            trk_note_on(t, CH[i], 100);
        for (b = 0; b < 450u; b++) {
            if (b == 300u)
                for (i = 0; i < 4u; i++)
                    trk_note_off(t, CH[i]);
            track_render(t, out, CTL);
            for (i = 0; i < CTL; i++) {
                odd += (uint32_t)(P5_OUT_SHIFT && (out[i] & 1));
                out[i] = (int32_t)((uint32_t)out[i] << (1 - P5_OUT_SHIFT));
            }
            h = fnv(h, out, CTL);
        }
        fprintf(f, "program %03u %016llx odd %u\n", (unsigned)p5_preset_id(k), (unsigned long long)h, (unsigned)odd);
    }
    fclose(f);
    return 0;
}

/* --idle FILE (3y45): a part with no voice skips the wheel's block (eng_prophet_test.c P5_IDLE_SKIP) and must leave
 * the same state behind: every shipped program, the wheel up and (every other program) on oscillator A's pitch with
 * the triangle and half noise, plays a chord, rings out, stays silent for a number of blocks of its own, and plays
 * again; a digest of every sample. tests/run_prophet_test.sh compares this build's file with P5_IDLE_SKIP=0's */
static int idle_file(const char *path)
{
    static const uint8_t CH[4] = {36, 55, 64, 96};
    track_t *t = &trk[0];
    FILE *f = fopen(path, "w");
    p5_patch_t p;
    int32_t out[CTL];
    uint32_t k, i, b, ph;
    if (!f)
        return 1;
    for (k = 0; k < NELEM(P5_TEST_PRESETS); k++) {
        uint64_t h = 1469598103934665603ull;
        uint32_t idle = 0;
        p5_preset_patch(k, &p);
        if (k & 1u) {
            p.raw[P5_WHEEL_FREQ_A] = 1;
            p.raw[P5_LFO_TRI] = 1;
            p.raw[P5_WHEEL_MIX] = 64;
        }
        vage = 0;
        rng_state = 0x1234567u;
        part_set(t, &p);
        t->mw = 90;
        for (b = 0; b < 37u + k % 11u; b++) {           /* (silent before the first note too) */
            track_render(t, out, CTL);
            h = fnv(h, out, CTL);
        }
        for (ph = 0; ph < 3u; ph++) {
            for (i = 0; i < 4u; i++)
                trk_note_on(t, CH[i], 100);
            for (b = 0; b < 40u; b++) {
                track_render(t, out, CTL);
                h = fnv(h, out, CTL);
            }
            trk_all_off(t);
            for (i = 0; i < NVOICE; i++)                /* (cut: the silence starts now, whatever the release) */
                if (t->v[i].active)
                    voice_kill(&t->v[i]);
            for (b = 0; b < 100u + (k * 7u + ph * 13u) % 50u; b++) {
                idle += !busy(t);
                track_render(t, out, CTL);
                h = fnv(h, out, CTL);
            }
        }
        fprintf(f, "program %03u %016llx idle %u\n", (unsigned)p5_preset_id(k), (unsigned long long)h, (unsigned)idle);
    }
    fclose(f);
    return 0;
}

/* --id-out: the session's record of the chord part on every shipped program (cr_settings.c css_part_get) */
static int id_out(const char *path)
{
    FILE *f = fopen(path, "wb");
    css_part_t r;
    uint32_t k;
    if (!f)
        return 1;
    for (k = 1; k < NELEM(P5_TEST_PRESETS); k++) {
        cu_pool_load(0, ENGI_PROPHET, k);
        memset(&r, 0, sizeof r);
        css_part_get(0, &r);
        fwrite(&r, sizeof r, 1, f);
    }
    fclose(f);
    printf("id-out: %u records (INIT + %u shipped programs in this build)\n", (unsigned)k - 1u, (unsigned)k - 1u);
    return 0;
}
/* --fav-out: every third program of THIS build's pool marked through the UI's turn (cu_fav_turn: what KNOB 4 does),
 * then the favourites' object as it goes to flash (cr_favs.c crf_pack), after a line of the Sequential numbers marked */
static int fav_out(const char *path)
{
    FILE *f = fopen(path, "wb");
    uint8_t b[CRF_PACK_MAX];
    uint32_t k, n, m = 0, ids[256];
    if (!f)
        return 1;
    (void)crf_unpack(0, 0);
    for (k = 1; k < NELEM(P5_TEST_PRESETS); k += 3u) {
        cu_pool_load(0, ENGI_PROPHET, k);
        if (!cu_fav_turn(0, +1))
            return 1;
        ids[m++] = p5_preset_id(k);
    }
    n = crf_pack(b);
    fwrite(&m, sizeof m, 1, f);
    fwrite(ids, sizeof ids[0], m, f);
    fwrite(&n, sizeof n, 1, f);
    fwrite(b, 1, n, f);
    fclose(f);
    printf("fav-out: %u programs marked (every third place of this build's pool), %u bytes\n", (unsigned)m, (unsigned)n);
    return 0;
}
/* --fav-in: the object another build wrote, read by THIS one, whose pool is another list. Every place of this pool
 * must show the star exactly when its program's NUMBER was marked by the writer, wherever the writer had it; a marked
 * program this build does not ship keeps its mark in the store (it comes back with a list that ships it) */
static int fav_in(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint8_t b[CRF_PACK_MAX], c[CRF_PACK_MAX];
    uint32_t k, n = 0, m = 0, ids[256], bad = 0, here = 0, away = 0, i;
    if (!f || fread(&m, sizeof m, 1, f) != 1 || m > 256u || fread(ids, sizeof ids[0], m, f) != m ||
        fread(&n, sizeof n, 1, f) != 1 || n > sizeof b || fread(b, 1, n, f) != n)
        return 1;
    fclose(f);
    if (crf_unpack(b, n) != 0 || crf_count() != m)
        return 1;
    for (k = 1; k < NELEM(P5_TEST_PRESETS); k++) {
        uint32_t id = p5_preset_id(k), want = 0;
        for (i = 0; i < m; i++)
            want |= ids[i] == id;
        cu_pool_load(0, ENGI_PROPHET, k);
        cu_pool_popup(0, k);
        bad += cu.pop.star != want || (uint32_t)cu_fav_part(0) != want || (uint32_t)cu_fav_ent(ENGI_PROPHET, pool_entry(ENGI_PROPHET, k)) != want;
        here += want;
    }
    for (i = 0; i < m; i++) {
        away += !p5_preset_of(ids[i]);
        bad += !crf_has(ENGI_PROPHET, ids[i]);
    }
    bad += here + away != m || crf_pack(c) != n || memcmp(b, c, n) != 0;   /* nothing lost by reading and writing it here */
    printf("fav-in: %u marks, %u wrong; %u of the marked programs are shipped here and starred at their own place, %u are "
           "not shipped here and keep their mark in the store\n", (unsigned)m, (unsigned)bad, (unsigned)here, (unsigned)away);
    return bad != 0;
}
/* --id-in: records another build wrote, put into the chord part by this one (css_part_put). Every record must bring
 * its own program (the blob's 138 bytes, its name, its voice mode), and its place must be the place of the program
 * of that NUMBER in this build's pool (not the place the writer had it at); a program this build does not ship plays
 * all the same, at no place (the pool's INIT position, edited) */
static int id_in(const char *path)
{
    FILE *f = fopen(path, "rb");
    css_part_t r;
    track_t *t = &trk[CR_PART_CHORD];
    uint32_t n = 0, here = 0, moved = 0, away = 0, bad = 0;
    if (!f)
        return 1;
    while (fread(&r, sizeof r, 1, f) == 1) {
        p5_patch_t want, got, fac;
        uint32_t id, pos;
        memcpy(&want, r.blob, sizeof want);
        id = p5_patch_id(&want);
        pos = p5_preset_of(id);
        cu_pool_load(0, ENGI_FM6, 1);                  /* (something else first) */
        n++;
        if (!css_part_put(0, &r)) {
            bad++;
            printf("  record %u (program %u): refused\n", (unsigned)n, (unsigned)id);
            continue;
        }
        got = *p5_patch_of(t);
        if (memcmp(&got, &want, sizeof want) || strcmp(psnd[0].name, r.name) || t->p[P_VOICE] != r.p[P_VOICE] ||
            t->eng_req != ENGI_PROPHET || !id) {
            bad++;
            printf("  record %u (program %u): not the record's program\n", (unsigned)n, (unsigned)id);
        }
        if (pos) {
            p5_preset_patch(pos, &fac);
            here++;
            moved += pos != r.fk;
            if (memcmp(&fac, &want, sizeof want) || t->preset != pos || psnd[0].kind != PK_FACTORY || psnd[0].fk != pos ||
                cu_part_pos(0) != pos || psnd[0].edited != r.edited) {
                bad++;
                printf("  record %u (program %u): place %u, want %u\n", (unsigned)n, (unsigned)id, (unsigned)cu_part_pos(0), (unsigned)pos);
            }
        } else {
            away++;
            if (t->preset != 0 || psnd[0].kind != PK_INIT || !psnd[0].edited || cu_part_pos(0) != 0u) {
                bad++;
                printf("  record %u (program %u): not shipped here, place %u\n", (unsigned)n, (unsigned)id, (unsigned)cu_part_pos(0));
            }
        }
    }
    fclose(f);
    printf("id-in: %u records, %u wrong; %u of the programs are shipped here and stand at their own place (%u of them at "
           "another place than the writer's), %u are not shipped here and play from the record at no place\n",
           (unsigned)n, (unsigned)bad, (unsigned)here, (unsigned)moved, (unsigned)away);
    return bad != 0 || !n;
}

/* the part's frequency: the rising zero crossings of nb blocks (a plain wave) */
static double freq(track_t *t, uint32_t nb)
{
    int32_t out[CTL], prev = 0;
    uint32_t b, i, n = 0, first = 0, last = 0, s = 0;
    for (b = 0; b < nb; b++) {
        track_render(t, out, CTL);
        for (i = 0; i < CTL; i++, s++) {
            if (prev < 0 && out[i] >= 0) {
                if (!n)
                    first = s;
                last = s;
                n++;
            }
            prev = out[i];
        }
    }
    return n > 1u ? (double)(n - 1u) * FS / (double)(last - first) : 0;
}

/* ------------------------------------------------- the owner's gesture (3y44) --- */
/* "On P5 though, preset 16 I was able to create some kind of glitch by using minor + 6 + 9 then playing chord again as
 * minor + 9 which had a long crackling effect like the voice got stuck in a washing machine or something. All dry, no
 * bass on." (the owner, on the unit, 2026-10-13: all 200 programs, so place 16 = Sequential's program 16, Canned
 * Happiness). Here: that gesture through the WHOLE sound side as the unit runs it (cr_out.c's queue -> cr_engine.c ->
 * voice.c's allocator with the fade-and-defer steal -> P5 -> fx.c's mix; mix_block is the audio ISR's block), a
 * five-note chord on four voices and then a four-note one over its release, over the engine's options and the timing
 * of the second strike in blocks (a note is applied at a block's start: 32 samples is the finest timing there is).
 * WHAT IS CHECKED, per run: never more than P5_POLY voices; the voices faded for another note stay bounded by the
 * notes struck (a voice given up and taken again in a circle would count without end); the second chord's sustain is
 * no rougher than the same chord struck from silence (the mean square of the output's third difference, the measure
 * of tools/emu/wavclicks.py: a crackle is all high frequency); no sample-to-sample step beyond the reference's; after
 * the release every voice free and the output exactly 0. over: a model of a unit that does not keep up (audio.c: two
 * halves in a row above 85 % shed a voice): a half that ends with `over` or more voices sounding counts as above.
 * IT DOES NOT REPRODUCE THE CRACKLE (docs/PROPHET.md "The crackle"): this is the sweep that says so, kept as a test */
typedef struct { uint8_t root, style, extadd, key, perf, over, variant; uint16_t hold, gap; } gcase_t;
typedef struct { uint32_t vmax, kills, ons, sheds, busy_end, loud_end, nblk; int32_t pk, step; double hf, hfmax; } gstat_t;
static struct { double d3[3]; int32_t last; uint32_t over_was; } gx;
static void g_run(uint32_t nb, const gcase_t *c, gstat_t *g, int measure)
{
    int32_t out[2u * CTL];
    uint32_t b, i;
    for (b = 0; b < nb; b++) {
        uint32_t n = 0, snd = 0;
        double e = 0;
        if (c->over && !(g->nblk & 3u)) {              /* a half's start: audio.c's shed, on the model's word */
            for (i = 0; i < NVOICE; i++)
                snd += trk[0].v[i].active && trk[0].v[i].stage != 4u;
            if (snd >= c->over && gx.over_was) {
                shed_voice();
                g->sheds++;
            }
            gx.over_was = snd >= c->over;
        }
        g->nblk++;
        mix_block(out, CTL);
        for (i = 0; i < NVOICE; i++)
            n += trk[0].v[i].active;
        if (n > g->vmax)
            g->vmax = n;
        g->loud_end = 0;                               /* the largest |sample| of the run's last block */
        for (i = 0; i < CTL; i++) {
            int32_t x = out[2u * i], d = x - gx.last;
            double d1 = d, d2 = d1 - gx.d3[0], d3 = d2 - gx.d3[1];
            gx.d3[0] = d1;
            gx.d3[1] = d2;
            gx.last = x;
            e += d3 * d3;
            if ((uint32_t)abs(x) > g->loud_end)
                g->loud_end = (uint32_t)abs(x);
            if ((uint32_t)abs(out[2u * i + 1u]) > g->loud_end)
                g->loud_end = (uint32_t)abs(out[2u * i + 1u]);
            if (measure) {
                if (abs(x) > g->pk)
                    g->pk = abs(x);
                if (abs(d) > g->step)
                    g->step = abs(d);
            }
        }
        if (measure) {
            g->hf += e / CTL;
            if (e / CTL > g->hfmax)
                g->hfmax = e / CTL;
        }
    }
}
static void g_mods(uint32_t mask, int down)            /* bit cr_mod_t */
{
    uint32_t m;
    for (m = 0; m < 8u; m++)
        if (mask >> m & 1u)
            cr_post(CRE_MOD, m, 0, down);
}
#define G_M69 (1u << CR_MOD_MIN | 1u << CR_MOD_6 | 1u << CR_MOD_9)
#define G_M9 (1u << CR_MOD_MIN | 1u << CR_MOD_9)
#define G_SUS 500u                                     /* blocks of the second chord measured (0.36 s), after .. */
#define G_ATT 80u                                      /* .. its attack */
static gstat_t g_case(const gcase_t *c, int ref)
{
    gstat_t g;
    uint32_t k0, o0, b;
    memset(&g, 0, sizeof g);
    cr_post(CRE_PLAYSTYLE, 0, 0, c->style);
    cr_post(CRE_EXTADD, 0, 0, c->extadd);
    cr_post(CRE_KEYMODE, c->key, 2, CR_SCALE_MINOR);   /* (D minor) */
    cr_post(CRE_PERFORM_MODE, 0, 0, c->perf ? c->perf - 1 : 0);
    cr_post(CRE_PERFORM, 0, 0, c->perf != 0);
    g_run(4, c, &g, 0);
    k0 = voice_kills;
    o0 = cr_out_notes;
    gx.over_was = 0;
    if (ref) {
        g_mods(G_M9, 1);
        g_run(2, c, &g, 0);
        cr_post(CRE_KEY, c->root, 100, 1);
    } else {
        g_mods(c->variant == 4u ? 1u << CR_MOD_MIN : G_M69, 1);
        g_run(2, c, &g, 0);
        cr_post(CRE_KEY, c->root, 100, 1);
        g_run(c->hold, c, &g, 0);
        switch (c->variant) {
        case 0:                                        /* the 6 let go, the root struck again after gap blocks */
            g_mods(1u << CR_MOD_6, 0);
            cr_post(CRE_KEY, c->root, 0, 0);
            g_run(c->gap, c, &g, 0);
            cr_post(CRE_KEY, c->root, 100, 1);
            break;
        case 1:                                        /* everything let go; minor + 9 and the root again */
            cr_post(CRE_KEY, c->root, 0, 0);
            g_mods(G_M69, 0);
            g_run(c->gap, c, &g, 0);
            g_mods(G_M9, 1);
            g_run(1, c, &g, 0);
            cr_post(CRE_KEY, c->root, 100, 1);
            break;
        case 2:                                        /* the root held, the 6 let go under it; then the root again */
            g_mods(1u << CR_MOD_6, 0);
            g_run(c->gap, c, &g, 0);
            cr_post(CRE_KEY, c->root, 0, 0);
            g_run(1, c, &g, 0);
            cr_post(CRE_KEY, c->root, 100, 1);
            break;
        case 3:                                        /* the second chord on another root key before the first is up */
            g_mods(1u << CR_MOD_6, 0);
            cr_post(CRE_KEY, c->root + 12u, 100, 1);
            g_run(c->gap, c, &g, 0);
            cr_post(CRE_KEY, c->root, 0, 0);
            cr_post(CRE_KEY, c->root + 12u, 0, 0);
            g_run(1, c, &g, 0);
            cr_post(CRE_KEY, c->root, 100, 1);
            break;
        default:                                       /* minor alone held, the 6 and the 9 pressed into it, the 6 up */
            g_mods(1u << CR_MOD_6 | 1u << CR_MOD_9, 1);
            g_run(c->gap, c, &g, 0);
            g_mods(1u << CR_MOD_6, 0);
            g_run(c->gap, c, &g, 0);
            cr_post(CRE_KEY, c->root, 0, 0);
            g_run(1, c, &g, 0);
            cr_post(CRE_KEY, c->root, 100, 1);
            break;
        }
    }
    g_run(G_ATT, c, &g, 0);
    g_run(G_SUS, c, &g, 1);
    g.hf /= G_SUS;
    cr_post(CRE_KEY, c->root, 0, 0);
    g_mods(0xFFu, 0);
    g_run(8, c, &g, 0);
    cr_post(CRE_PERFORM, 0, 0, 0);                     /* (an arp's Hold keeps playing: off, and everything released) */
    cr_post(CRE_PANIC, 0, 0, 0);
    for (b = 0; b < 3u * FS / CTL && busy(&trk[0]); b += 16u)   /* the release: 3 s at most */
        g_run(16, c, &g, 0);
    g_run(64, c, &g, 0);                               /* (the limiter's look-ahead, the DC blocker's last block) */
    g.busy_end = busy(&trk[0]) + vsq[0].n;
    g.kills = voice_kills - k0;
    g.ons = cr_out_notes - o0;
    return g;
}
/* the chord part on Sequential's program id (place 16 of all 200), dry */
static int g_load(uint32_t id)
{
    int32_t out[2u * CTL];
    uint32_t pos = p5_preset_of(id), b;
    if (!pos)
        return 0;
    cu_pool_load(0, ENGI_PROPHET, pos);
    trk[0].p[P_REV] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = 0;   /* "All dry" */
    cr_post(CRE_BASS, 0, 0, 0);                        /* "no bass on" */
    for (b = 0; b < 64u; b++)
        mix_block(out, CTL);
    return trk[0].engine == ENGI_PROPHET && p5_patch_id(p5_patch_of(&trk[0])) == id;
}
/* the sweep. full: every option and timing (minutes; --gesture-sweep [ID]); else the part the test keeps (a second).
 * Returns the runs that broke a rule; *runs, *worst (the roughest sustain against its reference) */
static uint32_t g_sweep(uint32_t id, int full, int verbose, uint32_t *runs, double *worst)
{
    static const uint8_t ROOT[] = {62, 57, 67, 74};    /* D4 first: Dm6/9 = 62 65 69 71 76 */
    static const uint16_t GAPF[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16, 20, 24, 32, 48, 64, 96, 137, 200, 411, 1378};
    static const uint16_t GAPT[] = {0, 1, 2, 3, 4, 5, 8, 16, 137, 1378};
    static const uint16_t HOLDF[] = {1, 2, 3, 8, 41, 411, 2067}, HOLDT[] = {3, 411};
    gcase_t c;
    uint32_t bad = 0, nr = full ? 4u : 1u, ns = full ? 3u : 1u, np = full ? 6u : 1u, r, h, gp, v, o;
    *runs = 0;
    *worst = 0;
    if (!g_load(id))
        return 1;
    memset(&c, 0, sizeof c);
    for (r = getenv("G_ROOT") ? (uint32_t)atoi(getenv("G_ROOT")) : 0u; r < (getenv("G_ROOT") ? (uint32_t)atoi(getenv("G_ROOT")) + 1u : nr); r++)
    for (c.style = 0; c.style < ns; c.style++)
    for (c.extadd = 0; c.extadd < (full ? 2u : 1u); c.extadd++)
    for (c.key = 0; c.key < 2u; c.key++)
    for (c.perf = 0; c.perf < np; c.perf++)
    for (o = 0; o < 3u; o++) {
        gstat_t ref;
        c.root = ROOT[r];
        c.over = (uint8_t)(o == 1u ? 4u : o == 2u ? 3u : 0u);
        c.variant = 0;
        ref = g_case(&c, 1);
        for (v = 0; v < 5u; v++)
        for (h = 0; h < (full ? NELEM(HOLDF) : NELEM(HOLDT)); h++)
        for (gp = 0; gp < (full ? NELEM(GAPF) : NELEM(GAPT)); gp++) {
            gstat_t g;
            double rough;
            int b;
            c.variant = (uint8_t)v;
            c.hold = full ? HOLDF[h] : HOLDT[h];
            c.gap = full ? GAPF[gp] : GAPT[gp];
            g = g_case(&c, 0);
            rough = g.hf / (ref.hf + 1.0);
            /* an arp or a pattern is another phrase each time, and a chord the model thinned is another chord: the
             * roughness is compared for the plain chord on a unit that keeps up; the other rules hold for all */
            b = g.vmax > P5_POLY || g.busy_end || g.loud_end > 4u || g.kills > 3u * g.ons + g.sheds + 4u ||
                (!c.perf && !c.over && (rough > 4.0 || g.step > 2 * ref.step + 2000));
            if (!c.perf && !c.over && rough > *worst)
                *worst = rough;
            ++*runs;
            bad += (uint32_t)b;
            if (b || verbose)
                printf("%s root %u style %u extadd %u key %u perf %u over %u variant %u hold %u gap %u: voices %u, faded %u "
                       "(shed %u) for %u notes, rough x%.2f (%.0f / %.0f), step %d (ref %d), pk %d, left %u, loud %u\n",
                       b ? "  GESTURE" : "  gesture", c.root, c.style, c.extadd, c.key, c.perf, c.over, c.variant, c.hold, c.gap,
                       (unsigned)g.vmax, (unsigned)g.kills, (unsigned)g.sheds, (unsigned)g.ons, rough, g.hf, ref.hf,
                       (int)g.step, (int)ref.step, (int)g.pk, (unsigned)g.busy_end, (unsigned)g.loud_end);
        }
    }
    return bad;
}


/* ---------------------------------------------------------------------------------------------------------------
 * 3y46: P5's VOICES OVER THE TWO PARTS AND THE TWO CORES (eng_prophet_test.c "THE VOICES", voice.c p5_pool_*, fx.c
 * p5x_run; docs/PROPHET.md "Voices"). Through mix_block, as the unit plays: the events, the pool, the split (the
 * host's core 1 runs its job inline), the mix. cr_dbg.c1 says whether there are two cores */
static uint32_t heldv(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += P5_HELD(&t->v[i]);
    return n;
}
static uint32_t c_pool, c_core[2];                     /* the fullest pool and the most whole voices a core rendered */
static void blocks(uint32_t n)
{
    int32_t o[2u * CTL];
    while (n--) {
        mix_block(o, CTL);
        if (p5_pool_used() > c_pool)
            c_pool = p5_pool_used();
    }
    if (cr_dbg.p5_c0 > c_core[0])
        c_core[0] = cr_dbg.p5_c0;
    if (cr_dbg.p5_c1 > c_core[1])
        c_core[1] = cr_dbg.p5_c1;
}
static uint32_t wait_free(void)                         /* every voice of both parts ended; 0: one hangs (10 s) */
{
    uint32_t b;
    for (b = 0; b < 10u * FS / CTL && (busy(&trk[0]) || busy(&trk[1])); b += 8u)
        blocks(8);
    return !busy(&trk[0]) && !busy(&trk[1]);
}
/* both parts on P5 (the chord part on pool place cpos, the bass part on bpos), a short release, nothing sounding */
static void p5_pair(uint32_t cpos, uint32_t bpos, int two)
{
    uint32_t k;
    cr_c1_test_hang = cr_c1_test_nostart = 0;
    cr_c1_test_rejected = cr_c1_test_faults = cr_c1_seen = 0;
    cr_dbg.c1 = two ? C1_OK : C1_OFF;
    cr_dbg.c1_fallback = cr_dbg.c1_misreads = 0;
    trk_all_off(&trk[0]);
    trk_all_off(&trk[1]);
    cu_pool_load(0, ENGI_PROPHET, cpos);
    cu_pool_load(1, ENGI_PROPHET, bpos);
    for (k = 0; k < 2u; k++) {
        trk[k].engine = trk[k].eng_req;
        trk[k].xf_on = trk[k].xf = 0;
        memset(trk[k].v, 0, sizeof trk[k].v);
        eng_state_clear(k);
        p5_patch_of(&trk[k])->raw[P5_RELEASE_ON] = 1;
        p5_patch_of(&trk[k])->raw[P5_RELEASE_AMP] = 12;
        p5_patch_of(&trk[k])->raw[P5_GLIDE] = 0;
    }
    blocks(6);
    cr_dbg.p5_c0 = cr_dbg.p5_c1 = cr_dbg.p5_bass = 0;
    c_pool = c_core[0] = c_core[1] = 0;
}
static void t_cores(void)
{
    static const uint8_t K[10] = {48, 50, 52, 54, 56, 58, 60, 62, 64, 66};
    track_t *ct = &trk[0], *bt = &trk[1];
    p5_patch_t q;
    uint32_t k, i, uni = 0, poly = 0, bad, m, g0;
    for (k = 1; k < pool_count(ENGI_PROPHET) && !(uni && poly); k++) {   /* a UNISON program with a stack of four that
                                                         * holds its note, and a POLY one */
        p5_preset_patch(pool_entry(ENGI_PROPHET, k).fk, &q);
        if (!uni && q.raw[P5_UNISON] && (!q.raw[P5_UNISON_COUNT] || q.raw[P5_UNISON_COUNT] >= 4u) && q.raw[P5_SUSTAIN_AMP] > 60u)
            uni = k;
        if (!poly && !q.raw[P5_UNISON] && q.raw[P5_SUSTAIN_AMP] > 60u && q.raw[P5_ATTACK_AMP] < 40u)
            poly = k;
    }
    if (!uni || !poly) {
        printf("     (two cores: this list has no UNISON program with a stack of four, or no POLY one: the pool's checks are not run)\n");
        return;
    }
    /* ---- TWO CORES: the chord part up to eight, four on each core ---- */
    p5_pair(poly, uni, 1);
    for (k = 0; k < 10u; k++)
        trk_note_on(ct, K[k], 100);
    blocks(24);
    for (i = 0, m = 0; i < NVOICE; i++)
        if (P5_HELD(&ct->v[i]))
            for (k = 0; k < 10u; k++)
                if (ct->v[i].note == K[k])
                    m |= 1u << k;
    OK(trk_nvoice(ct) == 8u && voice_units(ct) == 2u && heldv(ct) == 8u && p5_sounding(ct) == 8u && voices_busy_of(ct) == 16u &&
       m == 0x3F9u && c_pool == 8u && c_core[0] == 4u && c_core[1] == 4u,
       "two cores: ten keys sound EIGHT voices on the chord part (two budget units each: its 16), four rendered by each "
       "core (core 0 %u, core 1 %u at the most), the pool never above eight (%u); the bottom note and the top seven sound "
       "(mask %03X): the oldest held voice goes, never the lowest note", (unsigned)c_core[0], (unsigned)c_core[1],
       (unsigned)c_pool, (unsigned)m);
    {   /* an eleventh key: the voice it takes fades as a whole voice over P5_STEAL_BLOCKS blocks, on whichever core
         * the list gives it to (the fade's state is the voice's own: p5_voice_t.steal), and the note comes after it */
        uint32_t st = NVOICE, nf = 0, on1 = 0, on0 = 0, started = 0, b;
        trk_note_on(ct, 70, 100);
        for (i = 0; i < NVOICE; i++)
            if (ct->v[i].active && ct->v[i].stage == 4u)
                st = i;
        for (b = 0; b < 8u && st < NVOICE; b++) {
            uint32_t was = ct->v[st].active && ct->v[st].stage == 4u;
            blocks(1);
            if (was) {
                nf++;
                for (i = 0; i < p5x.n; i++)
                    if (p5x.it[i].v == &ct->v[st]) {
                        on1 += p5x.it[i].c1;
                        on0 += !p5x.it[i].c1;
                    }
            }
            for (i = 0; i < NVOICE && !started; i++)
                if (P5_HELD(&ct->v[i]) && ct->v[i].note == 70u)
                    started = b + 1u;
        }
        OK(st < NVOICE && ct->v[0].note == 48u && nf == (P5_STEAL_BLOCKS ? P5_STEAL_BLOCKS : 1u) && on1 + on0 == nf && (on1 || !P5_STEAL_BLOCKS) &&
           started && heldv(ct) == 8u && c_pool == 8u && c_core[0] <= 4u && c_core[1] <= 4u,
           "two cores: an eleventh key takes the oldest held voice that is not the lowest note; it fades over %u block(s), "
           "%u of them rendered by core 1, and the note starts after it (block %u): never more than eight voices, never more "
           "than four on a core", (unsigned)nf, (unsigned)on1, (unsigned)started);
    }
    trk_all_off(ct);
    OK(wait_free(), "two cores: every voice free after the release");
    /* ---- the bass part's stack and the chord: THE CHORD FIRST ---- */
    {
        uint32_t cv[8], bv[8], refused, back;
        static const uint8_t WANT_B[8] = {4, 4, 4, 4, 3, 2, 1, 1}, WANT_C[8] = {1, 2, 3, 4, 5, 6, 7, 7};
        p5_pair(poly, uni, 1);
        g0 = p5_bass_gave;
        trk_note_on(bt, 36, 100);
        blocks(6);
        bad = p5_sounding(bt) != 4u || p5_part(1)->sg1 != 4096u;
        for (k = 0; k < 8u; k++) {
            trk_note_on(ct, K[k], 100);
            blocks(6);
            cv[k] = heldv(ct);
            bv[k] = heldv(bt);
            bad += cv[k] != WANT_C[k] || bv[k] != WANT_B[k];
            if (k == 5u)                               /* (two of four: each at twice the level, the stack's own sum) */
                bad += p5_part(1)->sg1 != P5_STACK_G[1][3][1] && p5_part(1)->sg1 != P5_STACK_G[0][3][1];
        }
        OK(!bad && p5_bass_gave - g0 == 3u && c_pool == 8u && c_core[0] <= 4u && c_core[1] <= 4u,
           "two cores, a UNISON bass (%s, four voices) and then a chord of 1 .. 8 keys: the chord has %u %u %u %u %u %u %u %u "
           "voices, the bass %u %u %u %u %u %u %u %u: its stack thins to one and keeps that one (3y53): the eighth chord key "
           "takes a voice of its own chord (%u held bass voices given up; the pool at most %u; the stack's voices that stay are raised to "
           "its level)", P5_TEST_PRESETS[pool_entry(ENGI_PROPHET, uni).fk].name, (unsigned)cv[0], (unsigned)cv[1],
           (unsigned)cv[2], (unsigned)cv[3], (unsigned)cv[4], (unsigned)cv[5], (unsigned)cv[6], (unsigned)cv[7], (unsigned)bv[0],
           (unsigned)bv[1], (unsigned)bv[2], (unsigned)bv[3], (unsigned)bv[4], (unsigned)bv[5], (unsigned)bv[6], (unsigned)bv[7],
           (unsigned)(p5_bass_gave - g0), (unsigned)c_pool);
        trk_note_off(bt, 36);
        trk_note_on(bt, 38, 100);                      /* a bass key under a chord of eight: one voice (3y53) */
        blocks(6);
        refused = p5_sounding(bt) == 1u && heldv(bt) == 1u && heldv(ct) == 7u;
        trk_note_off(bt, 38);
        for (k = 4; k < 8u; k++)
            trk_note_off(ct, K[k]);
        for (i = 0; i < 4u * FS / CTL && p5_sounding(ct) > 4u; i += 4u)
            blocks(4);
        trk_note_on(bt, 40, 100);
        blocks(6);
        back = p5_sounding(bt);
        OK(refused && back == 4u && heldv(ct) == 3u && c_pool == 8u,
           "two cores: a bass key under a chord of eight held keys sounds one voice, the chord seven (3y53: the bass keeps "
           "one voice; the second key, 50, gave way); with the top four chord keys let go and ended (three sound) the next "
           "bass key has its four voices again");
        trk_all_off(ct);
        trk_all_off(bt);
        bad = !wait_free();
        /* the chord first, then the bass: the stack's extra voices start only into free room */
        p5_pair(poly, uni, 1);
        for (k = 0; k < 6u; k++)
            trk_note_on(ct, K[k], 100);
        blocks(8);
        trk_note_on(bt, 36, 100);
        blocks(6);
        cv[0] = heldv(ct);
        bv[0] = heldv(bt);
        trk_note_on(ct, K[6], 100);
        blocks(6);
        cv[1] = heldv(ct);
        bv[1] = heldv(bt);
        trk_note_on(ct, K[7], 100);
        blocks(6);
        OK(!bad && cv[0] == 6u && bv[0] == 2u && cv[1] == 7u && bv[1] == 1u && heldv(ct) == 7u && heldv(bt) == 1u && c_pool == 8u &&
           c_core[0] <= 4u && c_core[1] <= 4u,
           "two cores, a chord of six and then the UNISON bass key (the owner's case): the chord keeps its six, the bass has "
           "two of its four; a seventh chord key: 7 and 1; an eighth: 7 and 1 (3y53)");
        trk_all_off(ct);
        trk_all_off(bt);
        OK(wait_free(), "two cores: every voice of both parts free after the release");
    }
    /* ---- a chord struck over its own release, with common notes, a P5 bass key in the same block (as a chord
     * progression plays): every chord note sounds; the bass has what the chord leaves. (Found by the listening
     * files: with every slot of the part still sounding or fading, the eighth note took one of its own chord's new
     * notes while the bass kept a voice: voice.c voice_alloc now waits for a slot that is fading) ---- */
    {
        static const uint8_t CH[4][8] = {{50, 57, 62, 66, 69, 73, 76, 81}, {47, 54, 59, 62, 66, 69, 74, 78},
                                         {43, 50, 55, 59, 62, 67, 71, 74}, {45, 52, 57, 61, 64, 69, 73, 76}};
        static const uint8_t BASS[4] = {38, 35, 31, 33};
        uint32_t nk, c, rep, lost[3] = {0, 0, 0}, bmin[3] = {99, 99, 99}, bmax[3] = {0, 0, 0};
        for (nk = 6; nk <= 8u; nk++) {
            p5_pair(poly, uni, 1);
            p5_patch_of(ct)->raw[P5_RELEASE_AMP] = 90;  /* (the release rings into the next chord) */
            for (rep = 0; rep < 2u; rep++)
                for (c = 0; c < 4u; c++) {
                    uint32_t x = nk - 6u, b;
                    trk_note_on(bt, BASS[c], 100);
                    for (k = 0; k < nk; k++)
                        trk_note_on(ct, CH[c][k], 96);
                    blocks(40);
                    lost[x] += nk - heldv(ct);
                    b = heldv(bt);
                    if (b < bmin[x])
                        bmin[x] = b;
                    if (b > bmax[x])
                        bmax[x] = b;
                    blocks(400);
                    for (k = 0; k < nk; k++)
                        trk_note_off(ct, CH[c][k]);
                    trk_note_off(bt, BASS[c]);
                    blocks(270);
                }
            trk_all_off(ct);
            trk_all_off(bt);
            bad = !wait_free();
        }
        OK(!bad && !lost[0] && !lost[1] && lost[2] == 8u && bmin[0] == 2u && bmax[0] == 2u && bmin[1] == 1u && bmax[1] == 1u &&
           bmin[2] == 1u && bmax[2] == 1u && c_pool <= 8u && c_core[0] <= 4u && c_core[1] <= 4u,
           "two cores, D Bm G A twice over the ringing release with the UNISON bass's key in the chord's block: six-note "
           "chords: %u chord notes lost, the bass %u .. %u voices; seven-note: %u lost, the bass %u .. %u; eight-note chords: %u "
           "lost, the bass %u .. %u (the chord first; 3y53: an eight-note chord sounds seven, the bass keeps one)",
           (unsigned)lost[0], (unsigned)bmin[0], (unsigned)bmax[0], (unsigned)lost[1], (unsigned)bmin[1], (unsigned)bmax[1],
           (unsigned)lost[2], (unsigned)bmin[2], (unsigned)bmax[2]);
    }
    /* ---- 3y53: THE BASS ALWAYS HAS ONE VOICE. Eight chord keys (48 .. 62, in that order) and a bass key, both orders,
     * both modes, a P5 (UNISON) bass and a VA one: the bass sounds one voice; the chord note that goes is the oldest
     * held one that is not the lowest (50) ---- */
    {
        uint32_t two, order, va, ok3 = 1, cvn[2][2][2], bvn[2][2][2], msk[2][2][2];
        for (two = 0; two < 2u; two++)
            for (va = 0; va < 2u; va++)
                for (order = 0; order < 2u; order++) {
                    uint32_t want_c, want_m;
                    p5_pair(poly, uni, (int)two);
                    if (va) {
                        cu_pool_load(1, ENGI_VA, 1);
                        blocks(8);
                    }
                    if (order) {
                        trk_note_on(bt, 36, 100);
                        blocks(4);
                    }
                    for (k = 0; k < 8u; k++) {
                        trk_note_on(ct, K[k], 100);
                        blocks(4);
                    }
                    if (!order)
                        trk_note_on(bt, 36, 100);
                    blocks(12);
                    for (k = m = 0; k < 8u; k++)
                        for (i = 0; i < NVOICE; i++)
                            if (P5_HELD(&ct->v[i]) && ct->v[i].note == K[k])
                                m |= 1u << k;
                    cvn[two][va][order] = heldv(ct);
                    bvn[two][va][order] = heldv(bt);
                    msk[two][va][order] = m;
                    /* two cores: 7 + 1 (a P5 bass), 8 + its own (VA); one core: 3 + 1 (P5), 4 + 1 (VA: the budget) */
                    want_c = two ? (va ? 8u : 7u) : (va ? 4u : 3u);
                    want_m = two ? (va ? 0xFFu : 0xFDu) : 0u;
                    ok3 &= cvn[two][va][order] == want_c && bvn[two][va][order] == 1u && (!want_m || m == want_m) &&
                           (m & 1u) && c_pool <= p5_pool_size();
                    trk_all_off(ct);
                    trk_all_off(bt);
                    ok3 &= wait_free();
                }
        OK(ok3, "3y53: eight chord keys and a bass key (chord first / bass first): two cores, P5 bass: chord %u %u + bass "
           "%u %u (sounding %02X %02X: the second key, 50, the oldest that is not the lowest, gives way); VA bass: %u %u + "
           "%u %u (%02X); one core, P5 bass: %u %u + %u %u; VA bass: %u %u + %u %u: the bass always sounds one voice",
           (unsigned)cvn[1][0][0], (unsigned)cvn[1][0][1], (unsigned)bvn[1][0][0], (unsigned)bvn[1][0][1],
           (unsigned)msk[1][0][0], (unsigned)msk[1][0][1], (unsigned)cvn[1][1][0], (unsigned)cvn[1][1][1],
           (unsigned)bvn[1][1][0], (unsigned)bvn[1][1][1], (unsigned)msk[1][1][0], (unsigned)cvn[0][0][0],
           (unsigned)cvn[0][0][1], (unsigned)bvn[0][0][0], (unsigned)bvn[0][0][1], (unsigned)cvn[0][1][0],
           (unsigned)cvn[0][1][1], (unsigned)bvn[0][1][0], (unsigned)bvn[0][1][1]);
    }
    /* ---- ONE CORE: the chord part four, a P5 bass ONE voice, four P5 voices in all ---- */
    {
        uint32_t c1v, b1v, c2v, b2v, alone, galone;
        p5_pair(poly, uni, 0);
        trk_note_on(bt, 36, 100);
        blocks(6);
        alone = p5_sounding(bt);
        galone = p5_part(1)->sg1;
        for (k = 0; k < 4u; k++)
            trk_note_on(ct, K[k], 100);
        blocks(8);
        c1v = heldv(ct);
        b1v = heldv(bt);
        trk_all_off(ct);
        trk_all_off(bt);
        bad = !wait_free();
        for (k = 0; k < 6u; k++)                       /* six keys alone: four voices */
            trk_note_on(ct, K[k], 100);
        blocks(16);
        m = heldv(ct);
        trk_note_on(bt, 36, 100);                      /* the chord first, then the bass */
        blocks(8);
        c2v = heldv(ct);
        b2v = heldv(bt);
        OK(!bad && !cr_dualcore_active() && trk_nvoice(bt) == 1u && trk_nvoice(ct) == 4u && voice_units(ct) == 3u && alone == 1u &&
           (galone == P5_STACK_G[1][3][0] || galone == P5_STACK_G[0][3][0]) && c1v == 3u && b1v == 1u && m == 4u && c2v == 3u &&
           b2v == 1u && ct->v[lowest_held(ct)].note == 48u && c_pool <= P5_ONE_POOL && !c_core[1],
           "one core: a UNISON program on the bass part plays ONE voice (raised to its stack's level: gain %u / 4096); the chord "
           "part has four voices (six keys: %u), and with the P5 bass sounding three (the bass first: %u + %u; the chord first: "
           "%u + %u, its lowest note kept): %u P5 voices on the one core at most", (unsigned)galone, (unsigned)m, (unsigned)c1v,
           (unsigned)b1v, (unsigned)c2v, (unsigned)b2v, (unsigned)c_pool);
        trk_all_off(ct);
        trk_all_off(bt);
        OK(wait_free(), "one core: every voice of both parts free after the release");
    }
    /* ---- CORE 1 GIVEN UP UNDER HELD NOTES: the one core's rules in the next block, no voice stuck or lost ---- */
    {
        static const char *const HOW[3] = {"a job that never answers (the 8 ms wait)", "a fault about core 1", "a second misread"};
        uint32_t h;
        for (h = 0; h < 3u; h++) {
            int32_t o[2u * CTL], pk = 0;
            uint32_t before, after_c, after_b, same, low;
            p5_pair(poly, uni, 1);
            if (h == 1u) {                             /* a chord of four and the bass's stack of four */
                trk_note_on(bt, 36, 100);
                for (k = 0; k < 4u; k++)
                    trk_note_on(ct, K[k], 100);
            } else {
                for (k = 0; k < 8u; k++)
                    trk_note_on(ct, K[k], 100);
            }
            blocks(40);
            before = p5_sounding(ct) + p5_sounding(bt);
            if (h == 0u)
                cr_c1_test_hang = 1;
            else if (h == 1u)
                cr_c1_test_faults = 1;
            else {
                cr_c1_test_rejected = 1;               /* (the first is forgiven: core 1 kept) */
                blocks(1);
                bad = cr_dbg.c1 != C1_OK;
                cr_c1_test_rejected = 2;
            }
            mix_block(o, CTL);                         /* the block in which it is given up: core 0 renders all of it */
            for (i = 0; i < 2u * CTL; i++)
                if (abs(o[i]) > pk)
                    pk = abs(o[i]);
            same = p5_sounding(ct) + p5_sounding(bt);
            blocks(2);
            after_c = p5_sounding(ct);
            after_b = p5_sounding(bt);
            low = lowest_held(ct);
            OK((h != 2u || !bad) && before == 8u && cr_dbg.c1 == C1_GAVE_UP && cr_dbg.c1_fallback == 1u && !cr_dualcore_active() &&
               same == 8u && pk > 16 && (h == 1u ? after_c == 3u && after_b == 1u : after_c == 4u && !after_b) && low < NVOICE &&
               ct->v[low].note == 48u,
               "core 1 given up under %s (%s): that block is core 0's, whole (%u voices, the output's peak %d); two blocks "
               "later the one core's rules hold: the chord %u, the bass %u, the chord's lowest note kept",
               h == 1u ? "a chord of four and a bass stack of four" : "a chord of eight", HOW[h], (unsigned)same, (int)pk,
               (unsigned)after_c, (unsigned)after_b);
            trk_note_on(ct, 70, 100);                  /* a key after it: one core's steal, nothing more */
            blocks(8);
            bad = p5_sounding(ct) + p5_sounding(bt) > P5_ONE_POOL;
            trk_all_off(ct);
            trk_all_off(bt);
            OK(!bad && wait_free(), "core 1 given up: a new key steals as one core does (four P5 voices), and every voice is free after the release");
            cr_c1_test_hang = 0;
            cr_c1_test_faults = cr_c1_test_rejected = cr_c1_seen = 0;
        }
    }
    /* ---- Options > Dual Core turned while notes sound: nothing changes until the next power-on ---- */
    {
        uint32_t was = (uint32_t)opt_get(O_DUALCORE), nv, nb;
        p5_pair(poly, uni, 1);
        for (k = 0; k < 6u; k++)
            trk_note_on(ct, K[k], 100);
        trk_note_on(bt, 36, 100);
        blocks(8);
        nv = heldv(ct);
        nb = heldv(bt);
        opt_set(O_DUALCORE, 0);
        blocks(8);
        bad = !cr_dualcore_active() || heldv(ct) != nv || heldv(bt) != nb;
        opt_set(O_DUALCORE, 1);
        blocks(8);
        OK(!bad && cr_dualcore_active() && nv == 6u && nb == 2u && heldv(ct) == 6u && heldv(bt) == 2u,
           "Options > Dual Core turned Off and On again under held notes: the cores, the chord's six voices and the bass's two "
           "stay as they are (the option is read at power-on)");
        opt_set(O_DUALCORE, (int32_t)was);
        /* ---- the bass part leaves P5 and comes back under held chord keys ---- */
        cu_pool_load(1, ENGI_VA, 1);
        blocks(16);
        bad = bt->engine != ENGI_VA || p5_sounding(bt);
        trk_note_on(ct, K[6], 100);
        trk_note_on(ct, K[7], 100);
        trk_note_on(bt, 36, 100);
        blocks(8);
        bad += heldv(ct) != 8u || heldv(bt) != 1u || c_core[0] > 4u || c_core[1] > 4u;
        trk_note_off(bt, 36);
        cu_pool_load(1, ENGI_PROPHET, uni);
        blocks(16);
        p5_patch_of(bt)->raw[P5_RELEASE_ON] = 1;
        p5_patch_of(bt)->raw[P5_RELEASE_AMP] = 12;
        bad += bt->engine != ENGI_PROPHET || heldv(ct) != 8u;
        trk_note_on(bt, 36, 100);
        blocks(6);
        bad += p5_sounding(bt) != 1u || heldv(ct) != 7u;
        trk_note_off(bt, 36);
        for (k = 5; k < 8u; k++)
            trk_note_off(ct, K[k]);
        for (i = 0; i < 4u * FS / CTL && p5_sounding(ct) > 5u; i += 4u)
            blocks(4);
        trk_note_on(bt, 38, 100);
        blocks(6);
        OK(!bad && heldv(ct) <= 5u && heldv(ct) >= 4u && p5_sounding(bt) == 8u - heldv(ct) && c_pool == 8u,
           "the bass part to the VA and back to P5 under held chord keys: with a VA bass the chord has all eight (a MONO bass "
           "of another engine takes no voice of the pool); back on P5 the bass key has one voice under eight chord keys (3y53), "
           "and the rest of the pool when three of them have ended");
        trk_all_off(ct);
        trk_all_off(bt);
        OK(wait_free(), "the bass part's engine changed under held keys: no voice stuck");
    }
    /* ---- a UNISON key is as loud with one core as with two ---- */
    {
        p5stk_t st;
        (void)stack_levels(0, &st);
        OK(st.n && st.worst < 90.0 && st.n1 * 2u >= st.n && (!st.nx || st.wx < 0.05),
           "level: a UNISON program on the bass part, one voice (one core) against its whole stack (two cores), the held "
           "note's RMS: within 1 dB on %u of %u programs, within 2 dB on %u (mean %+.2f dB, the largest %+.2f dB: where the "
           "program takes its voices apart); %u are one voice N times: the same to %.3f dB", (unsigned)st.n1, (unsigned)st.n,
           (unsigned)st.n2, st.mean, st.worst, (unsigned)st.nx, st.wx);
    }
    p5_pair(poly, uni, 0);
    cu_pool_load(1, ENGI_VA, 1);
    cu_pool_load(0, ENGI_FM6, 1);
    trk[0].engine = trk[0].eng_req;
    trk[1].engine = trk[1].eng_req;
    trk[0].xf_on = trk[0].xf = trk[1].xf_on = trk[1].xf = 0;
    blocks(4);
}

int main(int argc, char **argv)
{
    static const uint8_t CHORD[4] = {48, 60, 64, 67};
    track_t *t = &trk[0];
    p5_patch_t p, q;
    uint32_t k, i, bad;
    rr_t r;

    emu_flash_open();
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();
    if (argc == 3 && !strcmp(argv[1], "--pcm"))
        return pcm_file(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "--idle"))
        return idle_file(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "--half"))
        return half_file(argv[2]);
    if (argc == 2 && !strcmp(argv[1], "--unichord"))
        return unichord();
    if (argc >= 2 && !strcmp(argv[1], "--stack")) {
        p5stk_t st;
        (void)stack_levels(1, &st);
        printf("stack: %u UNISON programs, one voice with one core against the whole stack with two (the held note's RMS): "
               "within 1 dB on %u, within 2 dB on %u, mean %+.2f dB, the largest difference %+.2f dB; %u of them are one "
               "voice N times (DETUNE 0, Vintage 0, no noise): the largest %.3f dB\n", (unsigned)st.n, (unsigned)st.n1,
               (unsigned)st.n2, st.mean, st.worst, (unsigned)st.nx, st.wx);
        return 0;
    }
    if (argc >= 2 && (!strcmp(argv[1], "--gesture") || !strcmp(argv[1], "--gesture-sweep"))) {
        uint32_t id = argc >= 3 ? (uint32_t)atoi(argv[2]) : 16u, runs, n;
        int full = !strcmp(argv[1], "--gesture-sweep");
        double worst;
        if (!p5_preset_of(id)) {
            printf("gesture: program %u is not in this build's shipped list\n", (unsigned)id);
            return 2;
        }
        n = g_sweep(id, full, argc >= 4, &runs, &worst);
        printf("gesture: program %u, minor + 6 + 9 then minor + 9: %u runs, %u broke a rule; the roughest second chord "
               "x%.2f of the same chord struck from silence\n", (unsigned)id, (unsigned)runs, (unsigned)n, worst);
        return n != 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--id-out"))
        return id_out(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "--id-in"))
        return id_in(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "--fav-out"))
        return fav_out(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "--fav-in"))
        return fav_in(argv[2]);

    /* ---- the engine's place ---- */
    for (k = 0, bad = 1; k + 2u < NENG_SHOWN + 1u; k++)
        if (ENGINE_ORDER[k] == ENGI_CZ)
            bad = !(k + 2u < NENG_SHOWN && ENGINE_ORDER[k + 1u] == ENGI_PROPHET && ENGINE_ORDER[k + 2u] == 7u);
    OK(ENGI_PROPHET == 16u && NENGINES == 17u && ENGINES[ENGI_PROPHET] == &ENG_P5_TEST && !strcmp(ENG_P5_TEST.name, "P5") &&
       eng_ok(ENGI_PROPHET) && !bad && NENG_SHOWN == 7 && cu_engine_melodic(ENGI_PROPHET) && cu_engine_rank(ENGI_PROPHET) == 4,
       "engine: P5 (the code's PROPHET) is 16 (appended after FM TONE's 15), shown after CZ-1 and before WHEEL, seven engines to pick (GRAIN after WHEEL since 3y109)");
    OK(sizeof(p5_patch_t) == 138u && sizeof(p5_part_t) <= sizeof eng_state[0] && sizeof eng_state[0] <= sizeof(fm6_part_t) + 8u &&   /* (+8: the host's
                                                  * alignment padding for GRAIN's pool, which holds pointers; none on the device) */
       ENG_P5_TEST.poly == 4u && P5_CORE == 4u && P5_POLY == 8u && P5_STACK == 4u && !cr_dualcore_active() && p5_units(t) == 3u &&
       ENG_P5_TEST.ownenv && ENG_P5_TEST.deep == &P5_DEEP,
       "state: p5_part_t %u B in the parts' engine state (%u B, FM6's size: no growth): eight voices' state; one core: four voices (the cap) of three units",
       (unsigned)sizeof(p5_part_t), (unsigned)sizeof eng_state[0]);

    /* ---- INIT PROPHET, the wire format ---- */
    {
        uint8_t f[P5_FRAME_MAX], f2[P5_FRAME_MAX];
        uint32_t n, n2;
        p5_patch_init(&p);
        n = p5_patch_encode(&p, f, sizeof f);
        OK(p5_patch_valid(&p) && n == 157u && p5_patch_decode(&q, f, n) && !memcmp(&p, &q, sizeof p) &&
           !memcmp(p.raw + P5_NAME, "INIT PROPHET        ", 20) && !strcmp(P5_TEST_PRESETS[0].name, "INIT P5") && !p5_patch_id(&p),
           "INIT: valid, an edit-buffer dump of 157 bytes that decodes to the same 138; no program number");
        memcpy(f2, f, n);
        f2[4] |= 0x80u;                                /* a mask byte over 127 */
        bad = (uint32_t)p5_patch_decode(&q, f2, n);
        memcpy(f2, f, n);
        f2[2] = 0x33;
        bad += (uint32_t)p5_patch_decode(&q, f2, n);
        bad += (uint32_t)p5_patch_decode(&q, f, n - 1u);
        memcpy(f2, f, n);
        f2[21] = 0x80;                                 /* a data byte over 127 */
        bad += (uint32_t)p5_patch_decode(&q, f2, n);
        p.command = 2;
        p.group = 4;
        p.program = 39;
        n2 = p5_patch_encode(&p, f2, sizeof f2);
        p.size = 100;
        OK(!bad && n2 == 159u && p5_patch_decode(&q, f2, n2) && q.group == 4 && q.program == 39 && !p5_patch_valid(&p),
           "wire: a bad mask, another model, a short frame and a data byte over 127 are refused; a program dump is 159");
    }

#if FELUCCA_P5_FACTORY
    /* ---- Sequential's bank ---- */
    {
        static uint8_t syx[200 * 159 + 1];
        uint8_t f[P5_FRAME_MAX];
        char nm[21];
        FILE *fh = fopen("assets/prophet5-factory/prophet5-v1.03.syx", "rb");
        size_t got = fh ? fread(syx, 1, sizeof syx, fh) : 0;
        uint32_t uni = 0, ssi = 0, sync = 0, over = 0, suni = 0, seen[201] = {0};
        if (fh)
            fclose(fh);
        for (k = 0, bad = 0; k < 200u && got == 200u * 159u; k++) {     /* the whole bank, from the file */
            bad += !p5_patch_decode(&q, syx + k * 159u, 159u) || p5_patch_id(&q) != k + 1u;
            uni += q.raw[P5_UNISON] != 0;
            ssi += !q.raw[P5_FILTER_REV];
            sync += q.raw[P5_SYNC] != 0;
            for (i = 0; i < 55u; i++)
                over += q.raw[i] > 127u;
        }
        OK(got == 200u * 159u && !bad && uni == 40u && ssi == 80u && sync == 25u && !over && P5_FACTORY_ALL == 200u,
           "bank: Sequential's 200 programs: 40 with UNISON, 80 on the SSI filter, 25 with sync; no knob over 127");
        for (k = 0, bad = 0; k < P5_FACTORY_N && got == 200u * 159u; k++) {
            uint32_t id = P5_FACTORY_ID[k];
            bad += id < 1u || id > 200u || seen[id <= 200u ? id : 0]++;
            if (id < 1u || id > 200u)
                continue;
            p5_factory(k, &p);
            bad += !p5_patch_decode(&q, syx + (id - 1u) * 159u, 159u) || memcmp(&p, &q, sizeof p) != 0;
            bad += p5_patch_encode(&p, f, sizeof f) != 159u || memcmp(f, syx + (id - 1u) * 159u, 159u) != 0;
            bad += !p5_patch_valid(&p) || p5_patch_id(&p) != id || p5_preset_of(id) != k + 1u || p5_preset_id(k + 1u) != id;
            suni += p.raw[P5_UNISON] != 0;
        }
        OK(P5_FACTORY_N >= 1u && P5_FACTORY_N <= 200u && !bad && NELEM(P5_TEST_PRESETS) == P5_FACTORY_N + 1u &&
           ENG_P5_TEST.npresets == P5_FACTORY_N + 1u && !p5_preset_of(0) && !p5_preset_of(201) && !p5_preset_id(0),
           "shipped: %u of the 200 (%u with UNISON, %u B a program packed): each unpacks to the .syx's frame of its own "
           "number, encodes back to its bytes, and is found by that number", (unsigned)P5_FACTORY_N, (unsigned)suni,
           (unsigned)P5_FACTORY_BYTES);
        for (k = 1, bad = 0; k <= P5_FACTORY_N; k++) {
            uint32_t l = (uint32_t)strlen(P5_TEST_PRESETS[k].name);
            bad += !l || l > 12u;
            for (i = 1; i < k; i++)
                bad += !strcmp(P5_TEST_PRESETS[k].name, P5_TEST_PRESETS[i].name);
        }
        if (p5_preset_of(1)) {
            p5_factory(p5_preset_of(1) - 1u, &p);
            p5_patch_name(nm, &p);
            bad += strcmp(nm, "It's a Prophet 5") || strcmp(P5_TEST_PRESETS[p5_preset_of(1)].name, "ITS PROPHET5");
        }
        OK(!bad, "names: PRESETS shows 1..12 characters, all different; the program keeps its 20 (program 1: ITS PROPHET5)");
    }
#else
    OK(NELEM(P5_TEST_PRESETS) == 1u && ENG_P5_TEST.npresets == 1u && pool_count(ENGI_PROPHET) == 1u,
       "FELUCCA_P5_FACTORY 0: INIT alone (no factory data in the build), the pool is INIT");
#endif

    /* ---- the blob ---- */
    {
        uint8_t b[ENG_BLOB_MAX], b2[ENG_BLOB_MAX];
        t->eng_req = t->engine = (uint8_t)ENGI_PROPHET;
        for (k = 0, bad = 0; k < NELEM(P5_TEST_PRESETS); k++) {
            p5_blob_preset(t, k);
            p5_blob_get(t, b);
            p5_preset_patch(k, &p);
            bad += memcmp(b, &p, sizeof p) != 0;
            p5_blob_preset(t, (k + 7u) % NELEM(P5_TEST_PRESETS));
            p5_blob_set(t, b);
            bad += memcmp(p5_patch_of(t), &p, sizeof p) != 0;
        }
        memcpy(b2, b, sizeof b2);
        b2[133] = 99;                                  /* a size that is neither 128 nor 133 */
        p5_blob_set(t, b2);
        p5_patch_init(&p);
        OK(!bad && P5_DEEP.blob_size == 138u && !memcmp(p5_patch_of(t), &p, sizeof p),
           "blob: the 138-byte program, every preset round trip; a bad blob -> INIT");
        p5_blob_set(t, 0);
        OK(!memcmp(p5_patch_of(t), &p, sizeof p), "blob: none -> INIT");
    }

    /* ---- every program through voice.c: a chord held 1.5 s, released, until its voices are free ---- */
    {
        uint32_t silent = 0, loud = 0, dc = 0, stuck = 0, nz = 0, worst = 0, quiet = 0, wk = 0, cut = 0, tails = 0, tailmax = 0;
        int32_t maxpk = 0, maxcut = 0;
        double maxdc = 0;
        for (k = 0; k < NELEM(P5_TEST_PRESETS); k++) {
            int32_t out[CTL];
            uint32_t b, cap;
            p5_preset_patch(k, &p);
            r = render(t, &p, CHORD, 4, 100, 3u * FS / (2u * CTL), 0);
            /* four voices of at most 2 x VOICE_FS each after the filter's knee (p5_samples: the amplifier x 2) */
            if (r.pk > 4 * 2 * VOICE_FS + 4 * VOICE_FS)
                loud++, printf("  program %u: peak %d\n", (unsigned)k, (int)r.pk);
            if (r.pk > maxpk)
                maxpk = r.pk, wk = k;
            if (r.rms < 20.0)
                quiet++;
            if (r.pk == 0)
                silent++;
            /* the DC after the blocker, against the part's level (a slow LFO is not DC: the mean of 1.1 s) */
            if (fabs(r.dc) > maxdc)
                maxdc = fabs(r.dc);
            if (fabs(r.dc) > 0.25 * r.rms + 300.0)
                dc++, printf("  program %u: dc %.0f rms %.0f\n", (unsigned)k, r.dc, r.rms);
            /* the release: 3 time constants past -60 dB, 100 s at most (P5_ENV_EXP: tau = 10000^(raw/127) ms) */
            cap = (uint32_t)(FS / CTL * (0.5 + 9.0 * pow(10000.0, p.raw[P5_RELEASE_AMP] / 127.0) / 1000.0));
            for (i = 0; i < 4u; i++)
                trk_note_off(t, CHORD[i]);
            {   /* the last voice's end (3y44): the part's capacitor discharges into the blocks after it (p5_post,
                 * p5_tail); cut to 0 there, as it was, the output steps by all it held. The step from the last
                 * sample with a voice to the next, against the largest step inside that last block (the voice's own
                 * end: its amplifier's last ramp) */
                int32_t last = 0, in = 0, was = 0, st;
                for (b = 0; b < cap && busy(t); b++) {
                    track_render(t, out, CTL);
                    st = abs(out[0] - last);
                    if (!busy(t)) {
                        if (st > 2 * was + 32)
                            cut++, printf("  program %u: a step of %d at its last voice's end (%d inside the block before)\n",
                                          (unsigned)k, (int)st, (int)was);
                        if (st > maxcut)
                            maxcut = st;
                    }
                    for (i = 1, in = st; i < CTL; i++)
                        if (abs(out[i] - out[i - 1u]) > in)
                            in = abs(out[i] - out[i - 1u]);
                    was = in;
                    last = out[CTL - 1u];
                }
            }
            if (b > worst)
                worst = b;
            if (busy(t))
                stuck++, printf("  program %u: %u voices after %u blocks\n", (unsigned)k, (unsigned)busy(t), (unsigned)b);
            for (b = 0; b < FS / CTL && p5_tail(t); b++)   /* the capacitor: 32 ms a time constant, at rest under 16 */
                track_render(t, out, CTL);
            if (b > tailmax)
                tailmax = b;
            tails += b != 0;
            if (p5_tail(t))
                stuck++, printf("  program %u: the capacitor not at rest after %u blocks\n", (unsigned)k, (unsigned)b);
            for (b = 0; b < 4u; b++) {
                track_render(t, out, CTL);
                for (i = 0; i < CTL; i++)
                    nz += out[i] != 0;
            }
        }
        OK(!loud && !silent, "render: %u programs, a four-note chord at velocity 100: none silent, the largest sample %d "
           "(program %u; four voices' sum is at most %d)", (unsigned)NELEM(P5_TEST_PRESETS), (int)maxpk, (unsigned)wk,
           8 * VOICE_FS);
        OK(!dc, "render: no DC in a held chord (the part's blocker; the largest mean %.0f)", maxdc);
        OK(!stuck && !nz, "render: every voice free after its release (the longest %.1f s), exact silence after the "
           "capacitor's discharge (%u programs have one, the longest %.0f ms)", worst * (double)CTL / FS, (unsigned)tails,
           tailmax * 1000.0 * CTL / FS);
        OK(!cut, "render: no step at the last voice's end beyond the voice's own last ramp (the largest %d: the part's "
           "DC blocker is not cut to 0 there, 3y44)", (int)maxcut);
        printf("     (%u programs under 20 rms in the held chord: percussive or self-ending)\n", (unsigned)quiet);
    }

    /* ---- the stress programs: no sample beyond the voices' sum ---- */
    {
        static const uint8_t HI[5] = {24, 43, 60, 79, 108};
        int32_t maxpk = 0, maxcut = 0;
        for (k = 0, bad = 0; k < NSTRESS; k += 5u) {
            stress(k, &p);
            r = render(t, &p, HI, 5, 127, 40, 20);
            bad += r.pk > 5 * 2 * VOICE_FS + 5 * VOICE_FS;
            if (r.pk > maxpk)
                maxpk = r.pk;
        }
        OK(!bad, "stress: sync, Poly-Mod at 127, the wheel at 127 on every destination, resonance 127, both filters, five "
           "keys at velocity 127: the largest sample %d (the voices' sum is at most %d)", (int)maxpk, 10 * VOICE_FS);
    }

    /* ---- voices: the cap, the units, note-off, a note struck again ---- */
    {
        int32_t out[CTL], last = 0, in = 0, edge = 0, d;
        uint32_t b;
        p5_patch_init(&p);
        p.raw[P5_RELEASE_AMP] = 10;
        part_set(t, &p);
        for (k = 0; k < 7u; k++)
            trk_note_on(t, 48 + 3 * k, 100);
        for (b = 0; b < 8u; b++)
            track_render(t, out, CTL);
        OK(busy(t) == 4u && trk_nvoice(t) == 4u && voice_units(t) == 3u && voices_busy_of(&trk[1]) == 12u,
           "voices: seven keys sound four voices (the cap, P5_POLY), three budget units each: 12 of the 16 of one core");
        for (k = 0; k < 7u; k++)
            trk_note_off(t, 48 + 3 * k);
        for (b = 0; b < FS / CTL && busy(t); b++)
            track_render(t, out, CTL);
        /* which keys of a five- and a six-key chord sound: the chord's notes as cr_out.c brings them (ascending), a
         * block rendered and events_block run between and after (a note that takes a sounding voice waits for that
         * voice's fade: voice.c voice_fade_steal, vsq) */
        {
            static const uint8_t SIX[6] = {50, 54, 57, 61, 64, 66};   /* D3 F#3 A3 C#4 E4 F#4 */
            uint32_t nk, m5 = 0, m6 = 0;
            events_block(CTL);                         /* (a release the loads above asked for) */
            for (nk = 5; nk <= 6u; nk++) {
                uint32_t m = 0;
                for (k = 0; k < nk; k++)
                    trk_note_on(t, SIX[k], 100);
                for (b = 0; b < 12u; b++) {
                    track_render(t, out, CTL);
                    events_block(CTL);
                }
                for (i = 0; i < NVOICE; i++)
                    if (t->v[i].active && t->v[i].gate && t->v[i].stage != 4u)
                        for (k = 0; k < nk; k++)
                            if (t->v[i].note == SIX[k])
                                m |= 1u << k;
                if (nk == 5u)
                    m5 = m;
                else
                    m6 = m;
                for (k = 0; k < nk; k++)
                    trk_note_off(t, SIX[k]);
                for (b = 0; b < FS / CTL && busy(t); b++) {
                    track_render(t, out, CTL);
                    events_block(CTL);
                }
            }
            /* bit k: the chord's k-th note from the bottom sounds */
            OK(m5 == 0x1Du && m6 == 0x39u,
               "voices: of five keys the bottom note and the top three sound (the second from the bottom gives way; "
               "mask %02X), of six the bottom and the top three (the second and third give way; mask %02X): the oldest "
               "held voice goes, never the lowest note", (unsigned)m5, (unsigned)m6);
        }
        /* A STOLEN VOICE (3y45, docs/PROPHET.md "A stolen voice"): the fifth key takes the oldest held voice that is
         * not the lowest note; that voice fades as a WHOLE VOICE over P5_STEAL_BLOCKS blocks (its oscillators run on:
         * to a773d30 its last sample went to 0 on a straight line, one block), the new note starts when the fade has
         * ended, and there are never more than four voices. A voice given up for the budget or by the overload shed
         * (voice_kill alone) keeps the one-block ramp: no whole voice more in a block that cannot pay for one */
        {
            static const uint8_t FOUR[4] = {50, 54, 57, 61};
            uint32_t st = NVOICE, nf = 0, vm = 0, started = 0, moved = 1, nfw = P5_STEAL_BLOCKS ? P5_STEAL_BLOCKS : 1u, sn = 0;
            int32_t first = -1, last = -1;
            p5_voice_t *sv;
            events_block(CTL);
            for (k = 0; k < 4u; k++)
                trk_note_on(t, FOUR[k], 100);
            for (b = 0; b < 40u; b++) {
                track_render(t, out, CTL);
                events_block(CTL);
            }
            trk_note_on(t, 66, 100);
            for (i = 0; i < NVOICE; i++)
                if (t->v[i].active && t->v[i].stage == 4u) {
                    st = i;
                    sn = t->v[i].note;
                }
            sv = st < NVOICE ? p5_voice(t, &t->v[st]) : 0;
            for (b = 0; b < 12u && sv; b++) {
                uint32_t ph = sv->phase[0], was = t->v[st].active && t->v[st].stage == 4u, on = 0;
                track_render(t, out, CTL);
                if (was) {
                    nf++;
                    if (first < 0)
                        first = t->v[st].env_out;
                    last = t->v[st].env_out;
                    moved &= sv->phase[0] != ph;
                }
                if (busy(t) > vm)
                    vm = busy(t);
                events_block(CTL);
                for (i = 0; i < NVOICE; i++)
                    on |= t->v[i].active && t->v[i].gate && t->v[i].note == 66u;
                if (!on)
                    started = b + 2u;
            }
            OK(sv && sn == 54u && nf == nfw && last == 0 && first == (int32_t)(32767u * (nfw - 1u) / nfw) &&
               vm <= P5_POLY && started == (nfw > 2u ? nfw : 2u) && (moved || !P5_STEAL_BLOCKS),
               "steal: the fifth key takes the second note's voice, which fades over %u block(s) as a whole voice (the VCA "
               "at %d after the first, 0 after the last; its oscillators run on), never more than %u voices, the new note "
               "after %u blocks", (unsigned)nf, (int)first, (unsigned)vm, (unsigned)started);
            /* the budget's or the shed's victim: one block, the last sample's ramp (the oscillators stand) */
            for (st = 0; st < NVOICE && !(t->v[st].active && t->v[st].gate); st++)
                ;
            sv = st < NVOICE ? p5_voice(t, &t->v[st]) : 0;
            if (sv) {
                uint32_t ph = sv->phase[0];
                voice_kill(&t->v[st]);
                track_render(t, out, CTL);
                OK(!t->v[st].active && sv->phase[0] == ph && !sv->steal,
                   "steal: a voice given up for the budget or by the overload shed still ends in one block, on its last sample's ramp");
            }
            trk_all_off(t);
            for (b = 0; b < FS / CTL && busy(t); b++) {
                track_render(t, out, CTL);
                events_block(CTL);
            }
        }
        for (k = 0; k < 7u; k++)
            trk_note_on(t, 48 + 3 * k, 100);
        for (b = 0; b < 8u; b++)
            track_render(t, out, CTL);
        /* one core's shared budget (16 units; Dual Core Off, the default): a bass note takes two (the VA's) of them,
         * the chord part keeps its four voices (4 x 3 + 2 = 14; with four units a voice it kept three) */
        cu_pool_load(1, ENGI_VA, 1);
        trk[1].engine = trk[1].eng_req;
        trk[1].xf_on = trk[1].xf = 0;
        trk_note_on(&trk[1], 36, 100);
        for (b = 0; b < 8u; b++) {
            track_render(t, out, CTL);
            track_render(&trk[1], out, CTL);
        }
        OK(!cr_dualcore_active() && busy(t) == 4u && busy(&trk[1]) == 1u && voices_busy_of(&trk[2]) == 14u,
           "voices: with the bass sounding on one core the chord part keeps its four (4 x 3 + the bass's 2 = 14 of 16 units)");
        trk_note_off(&trk[1], 36);
        for (b = 0; b < 4u * FS / CTL && busy(&trk[1]); b++)
            track_render(&trk[1], out, CTL);
        for (k = 0; k < 7u; k++)
            trk_note_off(t, 48 + 3 * k);
        for (b = 0; b < FS / CTL && busy(t); b++)
            track_render(t, out, CTL);
        OK(!busy(t) && b < FS / CTL / 2u, "voices: note-off frees them (release 10: %u ms)", (unsigned)(b * CTL * 1000u / FS));
        /* a struck note again: a low-passed saw, the largest step at the first sample of the restrike's block against
         * the largest inside the blocks */
        p5_patch_init(&p);
        p.raw[P5_CUTOFF] = 40;
        p.raw[P5_ATTACK_AMP] = 30;
        p.raw[P5_SUSTAIN_AMP] = 100;
        part_set(t, &p);
        trk_note_on(t, 57, 100);
        for (b = 0; b < 200u; b++) {
            if (b == 120u)
                trk_note_on(t, 57, 100);
            track_render(t, out, CTL);
            for (i = 0; i < CTL; i++) {
                d = out[i] - (i ? out[i - 1u] : last);
                d = d < 0 ? -d : d;
                if (b >= 100u && i && d > in)
                    in = d;
                if (b == 120u && !i)
                    edge = d;
            }
            last = out[CTL - 1u];
        }
        OK(busy(t) == 1u && edge <= in, "restrike: the same key again takes its voice; the step at the restrike %d, inside "
           "the blocks at most %d", (int)edge, (int)in);
    }

    /* ---- UNISON and GLIDE (the editor's columns) ---- */
    {
        int32_t out[CTL];
        uint32_t b, mid = 0;
        p5_patch_init(&p);
        part_set(t, &p);
        P5_DEEP.set(t, 17, 1, 3);                      /* VOICE: VOICES 3 */
        P5_DEEP.set(t, 17, 2, 7);                      /* DETUNE 7 */
        bad = t->p[P_VOICE] != V_POLY;
        P5_DEEP.set(t, 17, 0, 1);                      /* UNISON on */
        bad += t->p[P_VOICE] != V_UNISON || t->p[P_DETUNE] != 127 || p5_cap(t) != 3u;
        trk_note_on(t, 60, 100);
        for (b = 0; b < 4u; b++)
            track_render(t, out, CTL);
        bad += busy(t) != 3u;
        trk_note_off(t, 60);
        P5_DEEP.set(t, 17, 0, 0);
        OK(!bad && t->p[P_VOICE] == V_POLY, "unison: the editor's switch makes the part UNISON (three voices of one key, "
           "DETUNE 7), off: POLY again");
        t->p[P_VOICE] = V_MONO;                        /* a bass part's load of a program without UNISON */
        p5_track_loaded(t);
        bad = t->p[P_VOICE] != V_MONO || p5_own[0] != V_MONO;
        P5_DEEP.set(t, 17, 0, 1);
        bad += t->p[P_VOICE] != V_UNISON;
        P5_DEEP.set(t, 17, 0, 0);
        OK(!bad && t->p[P_VOICE] == V_MONO, "unison: on a MONO part the switch goes to UNISON and back to MONO");
#if FELUCCA_P5_FACTORY
        /* ---- A LOAD TAKES THE PROGRAM'S UNISON (the owner, 2026-10-13) ---- */
        {
            static const uint8_t CH4[4] = {62, 66, 69, 73};          /* D4 F#4 A4 C#5, as the chord engine sends them */
            uint32_t un = 0, po = 0, lowp = 0, lastp = 0, nuni = 0, wrong = 0, hang = 0, g;
            css_part_t rec;
            for (k = 1; k <= P5_FACTORY_N; k++) {
                p5_preset_patch(k, &p);
                if (p.raw[P5_UNISON]) {
                    uint32_t cnt = (uint32_t)clamp(p.raw[P5_UNISON_COUNT] ? p.raw[P5_UNISON_COUNT] : 4, 1, 4);
                    nuni++;
                    if (!un)
                        un = k;
                    if (cnt == 4u && p.raw[P5_RETRIGGER] < 2u && !lowp)
                        lowp = k;
                    if (cnt == 4u && p.raw[P5_RETRIGGER] >= 2u && !lastp)
                        lastp = k;
                } else if (!po)
                    po = k;
                /* every program through the pool, on both parts: UNISON exactly when the program says so */
                cu_pool_load(0, ENGI_PROPHET, k);
                cu_pool_load(1, ENGI_PROPHET, k);
                wrong += trk[0].p[P_VOICE] != (p.raw[P5_UNISON] ? V_UNISON : V_POLY) || p5_own[0] != V_POLY;
                wrong += trk[1].p[P_VOICE] != (p.raw[P5_UNISON] ? V_UNISON : V_MONO) || p5_own[1] != V_MONO;
            }
            cu_pool_load(1, ENGI_VA, 1);
            cu_pool_load(0, ENGI_PROPHET, 0);
            OK(!wrong && trk[0].p[P_VOICE] == V_POLY,
               "unison on load: each of the %u shipped programs makes the part UNISON exactly when its switch is on (%u "
               "of them), on the chord part (else POLY) and on the bass part (else MONO); INIT is POLY",
               (unsigned)P5_FACTORY_N, (unsigned)nuni);
            if (un && po) {
                /* a four-note chord on a UNISON program: ONE note on the stacked voices */
                for (g = 0; g < 2u; g++) {
                    uint32_t pr = g ? lastp : lowp, nv = 0, note = 0, mixed = 0;
                    if (!pr)
                        continue;
                    cu_pool_load(0, ENGI_PROPHET, pr);
                    t->engine = t->eng_req;
                    t->xf_on = t->xf = 0;
                    events_block(CTL);
                    for (i = 0; i < 4u; i++)
                        trk_note_on(t, CH4[i], 100);
                    for (b = 0; b < 8u; b++) {
                        track_render(t, out, CTL);
                        events_block(CTL);
                    }
                    for (i = 0; i < NVOICE; i++)
                        if (t->v[i].active && t->v[i].gate) {
                            nv++;
                            mixed += note && t->v[i].note != note;
                            note = t->v[i].note;
                        }
                    OK(nv == 4u && !mixed && note == (g ? 73u : 62u) && voices_busy_of(&trk[1]) == 12u && p5_cap(t) == 4u,
                       "unison on load: a 4-note chord (D4 F#4 A4 C#5) on %s: %u voices, all on %s (the program's key "
                       "priority: %s), %u of the 16 budget units", P5_TEST_PRESETS[pr].name, (unsigned)nv,
                       note == 62u ? "D4, the chord's lowest note" : note == 73u ? "C#5, the last note sent" : "another note",
                       g ? "LAST" : "LOW", (unsigned)voices_busy_of(&trk[1]));
                    if (!g) {                          /* with a bass note on one core: all four still stack */
                        trk[1].engine = trk[1].eng_req;
                        trk[1].xf_on = trk[1].xf = 0;
                        for (i = 0; i < 4u; i++)
                            trk_note_off(t, CH4[i]);
                        for (b = 0; b < 2u * FS / CTL && busy(t); b++)
                            track_render(t, out, CTL);
                        trk_note_on(&trk[1], 38, 100);
                        for (i = 0; i < 4u; i++)
                            trk_note_on(t, CH4[i], 100);
                        for (b = 0; b < 8u; b++) {
                            track_render(t, out, CTL);
                            track_render(&trk[1], out, CTL);
                            events_block(CTL);
                        }
                        for (i = 0, nv = 0; i < NVOICE; i++)
                            nv += t->v[i].active && t->v[i].gate && t->v[i].stage != 4u;
                        OK(nv == 4u && busy(&trk[1]) == 1u,
                           "unison on load: with a VA bass note sounding (2 units) the chord's one note keeps its four voices");
                        trk_note_off(&trk[1], 38);
                        for (b = 0; b < 4u * FS / CTL && busy(&trk[1]); b++)
                            track_render(&trk[1], out, CTL);
                    }
                    /* held keys, then the next program (poly): the load releases them, nothing hangs */
                    cu_pool_load(0, ENGI_PROPHET, po);
                    events_block(CTL);
                    for (i = 0; i < NVOICE; i++)
                        hang += t->v[i].gate;
                    hang += t->p[P_VOICE] != V_POLY || t->nmono || t->mono_note;
                    for (i = 0; i < 4u; i++)
                        trk_note_off(t, CH4[i]);      /* (the keys come up after the load) */
                    for (b = 0; b < 100u * FS / CTL && busy(t); b++) {
                        track_render(t, out, CTL);
                        events_block(CTL);
                    }
                    hang += busy(t);
                }
                /* .. and the other way: a poly chord held, a UNISON program loaded */
                cu_pool_load(0, ENGI_PROPHET, po);
                events_block(CTL);
                for (i = 0; i < 4u; i++)
                    trk_note_on(t, CH4[i], 100);
                for (b = 0; b < 8u; b++) {
                    track_render(t, out, CTL);
                    events_block(CTL);
                }
                bad = busy(t) != 4u;
                cu_pool_load(0, ENGI_PROPHET, un);
                events_block(CTL);
                for (i = 0; i < NVOICE; i++)
                    hang += t->v[i].gate;
                for (i = 0; i < 4u; i++)
                    trk_note_off(t, CH4[i]);
                for (b = 0; b < 100u * FS / CTL && busy(t); b++) {
                    track_render(t, out, CTL);
                    events_block(CTL);
                }
                hang += busy(t);
                OK(!bad && !hang && t->p[P_VOICE] == V_UNISON,
                   "unison on load: a load over held keys releases them both ways (UNISON -> POLY: the part is POLY, no "
                   "mono stack left; POLY -> UNISON): no voice hangs");
                /* the editor's cell on a loaded UNISON program, and back */
                P5_DEEP.set(t, 17, 0, 0);
                bad = t->p[P_VOICE] != V_POLY;
                P5_DEEP.set(t, 17, 0, 1);
                OK(!bad && t->p[P_VOICE] == V_UNISON, "unison on load: the editor's UNISON cell turns it off (POLY) and on again");
                /* the engine changes: a load of another engine's sound, and the editor's engine switch (the mix kept) */
                cu_pool_load(0, ENGI_VA, 1);
                bad = trk[0].p[P_VOICE] != V_POLY;
                cu_pool_load(0, ENGI_PROPHET, un);
                cu_engine_switch(0, ENGI_VA);
                bad += trk[0].p[P_VOICE] != V_POLY || trk[0].eng_req != ENGI_VA;
                cu_pool_load(1, ENGI_PROPHET, un);
                cu_engine_switch(1, ENGI_VA);
                bad += trk[1].p[P_VOICE] != V_MONO;
                cu_pool_load(0, ENGI_PROPHET, po);
                trk[0].p[P_VOICE] = V_UNISON;          /* (the MIX page's VOICE on a program without the switch) */
                cu_engine_switch(0, ENGI_VA);
                OK(!bad && trk[0].p[P_VOICE] == V_UNISON,
                   "unison on load: another engine gets the part as it expects it (a pool load: POLY; the editor's engine "
                   "switch from a UNISON program: POLY, the bass MONO); a UNISON set on the MIX page stays, as between any two engines");
                trk[0].p[P_VOICE] = V_POLY;
                /* a UNISON program on the BASS part, one core: its note's stack and a chord share the 16 units. The bass
                 * note first, then the chord's four notes; then the chord first, then the bass note */
                if (lowp) {
                    uint32_t cb[2], cc[2];
                    for (g = 0; g < 2u; g++) {
                        cu_pool_load(0, ENGI_PROPHET, po);
                        cu_pool_load(1, ENGI_PROPHET, lowp);
                        trk[0].engine = trk[0].eng_req;
                        trk[1].engine = trk[1].eng_req;
                        trk[0].xf_on = trk[0].xf = trk[1].xf_on = trk[1].xf = 0;
                        events_block(CTL);
                        if (!g)
                            trk_note_on(&trk[1], 38, 100);
                        for (b = 0; b < 4u; b++) {
                            track_render(&trk[0], out, CTL);
                            track_render(&trk[1], out, CTL);
                            events_block(CTL);
                        }
                        for (i = 0; i < 4u; i++)
                            trk_note_on(&trk[0], CH4[i], 100);
                        for (b = 0; b < 4u; b++) {
                            track_render(&trk[0], out, CTL);
                            track_render(&trk[1], out, CTL);
                            events_block(CTL);
                        }
                        if (g)
                            trk_note_on(&trk[1], 38, 100);
                        for (b = 0; b < 12u; b++) {
                            track_render(&trk[0], out, CTL);
                            track_render(&trk[1], out, CTL);
                            events_block(CTL);
                        }
                        for (i = 0, cc[g] = cb[g] = 0; i < NVOICE; i++) {
                            cc[g] += trk[0].v[i].active && trk[0].v[i].gate && trk[0].v[i].stage != 4u;
                            cb[g] += trk[1].v[i].active && trk[1].v[i].gate && trk[1].v[i].stage != 4u;
                        }
                        trk_note_off(&trk[1], 38);
                        for (i = 0; i < 4u; i++)
                            trk_note_off(&trk[0], CH4[i]);
                        for (b = 0; b < 100u * FS / CTL && (busy(&trk[0]) || busy(&trk[1])); b++) {
                            track_render(&trk[0], out, CTL);
                            track_render(&trk[1], out, CTL);
                            events_block(CTL);
                        }
                    }
                    OK(cc[0] + cb[0] == P5_ONE_POOL && cc[1] + cb[1] == P5_ONE_POOL && cb[0] == 1u && cb[1] == 1u,
                       "unison on load: a UNISON bass (%s) with a 4-note chord on one core: FOUR P5 voices between the two "
                       "parts (3y46: the pool of one core; five ran late on the unit). The bass note first: the bass has %u voice, the chord %u of its notes; the chord first: "
                       "the chord %u, the bass %u", P5_TEST_PRESETS[lowp].name, (unsigned)cb[0], (unsigned)cc[0],
                       (unsigned)cc[1], (unsigned)cb[1]);
                    cu_pool_load(1, ENGI_VA, 1);
                    trk[1].engine = trk[1].eng_req;
                }
                /* the session's record: the mode as it was left */
                cu_pool_load(0, ENGI_PROPHET, un);
                memset(&rec, 0, sizeof rec);
                css_part_get(0, &rec);
                cu_pool_load(0, ENGI_FM6, 1);
                bad = !css_part_put(0, &rec) || trk[0].p[P_VOICE] != V_UNISON || p5_own[0] != V_POLY || cu_part_pos(0) != un;
                P5_DEEP.set(&trk[0], 17, 0, 0);        /* UNISON turned off in the editor, then the power cycle */
                memset(&rec, 0, sizeof rec);
                css_part_get(0, &rec);
                cu_pool_load(0, ENGI_FM6, 1);
                bad += !css_part_put(0, &rec) || trk[0].p[P_VOICE] != V_POLY || p5_patch_of(&trk[0])->raw[P5_UNISON] != 0;
                cu_pool_load(1, ENGI_PROPHET, un);
                memset(&rec, 0, sizeof rec);
                css_part_get(1, &rec);
                cu_pool_load(1, ENGI_VA, 1);
                bad += !css_part_put(1, &rec) || trk[1].p[P_VOICE] != V_UNISON || p5_own[1] != V_MONO;
                P5_DEEP.set(&trk[1], 17, 0, 0);
                bad += trk[1].p[P_VOICE] != V_MONO;
                cu_pool_load(1, ENGI_VA, 1);
                trk[1].engine = trk[1].eng_req;
                OK(!bad, "unison on load: the session's record brings the part back as it was left (a UNISON program: "
                   "UNISON; one with UNISON turned off in the editor: POLY; the bass part: UNISON over MONO)");
            } else
                printf("     (the shipped list has no UNISON program or no poly one: the chord checks are skipped)\n");
            part_set(t, 0);
        }
#endif
        /* glide */
        p5_patch_init(&p);
        p.raw[P5_GLIDE] = 90;
        part_set(t, &p);
        trk_note_on(t, 48, 100);
        for (b = 0; b < 400u; b++)
            track_render(t, out, CTL);
        trk_note_off(t, 48);
        trk_note_on(t, 72, 100);
        for (b = 0; b < 4000u; b++) {
            track_render(t, out, CTL);
            for (i = 0; i < P5_POLY; i++)
                if (t->v[i].gate && b == 20u)
                    mid = (uint32_t)p5_part(0)->voice[i].glide_pitch;
        }
        for (i = 0, k = 0; i < P5_POLY; i++)
            if (t->v[i].gate)
                k = (uint32_t)p5_part(0)->voice[i].glide_pitch;
        OK(mid > 48u * 16u && mid < 72u * 16u && k == 72u * 16u,
           "glide: GLIDE 90, C2 then C4: the new voice starts at the last key's pitch (%.1f after 15 ms) and arrives", mid / 16.0);
    }

    /* ---- the pitch bend, as the part's MIDI brings it (cr_out.c midi_event -> voice.c -> vmod_t) ---- */
    {
        double f0, f1, f2;
        p5_patch_init(&p);
        p.raw[P5_CUTOFF] = 30;                         /* (a saw's fundamental: one rising crossing a cycle) */
        part_set(t, &p);
        trk_note_on(t, 69, 100);
        freq(t, 200);
        f0 = freq(t, 1400);
        midi_event(0xE0, 0, 0x7F, 0x7F);               /* bend full up: the channel's range, +2 semitones */
        freq(t, 200);
        f1 = freq(t, 1400);
        midi_event(0xE0, 0, 0x00, 0x00);               /* full down */
        freq(t, 200);
        f2 = freq(t, 1400);
        midi_event(0xE0, 0, 0x00, 0x40);
        OK(fabs(f0 - 440.0) < 1.0 && fabs(f1 - 440.0 * pow(2, 2.0 / 12)) < 1.2 && fabs(f2 - 440.0 * pow(2, -2.0 / 12)) < 1.2,
           "bend: A4 %.2f Hz, bent full up %.2f (+2 semitones: 493.88), full down %.2f (391.99)", f0, f1, f2);
        freq(t, 200);
        f0 = freq(t, 700);
        OK(fabs(f0 - 440.0) < 1.0, "bend: back at the centre %.2f Hz", f0);
    }

    /* ---- the deep pages ---- */
    {
        uint8_t seen[P5_RAW_MAX] = {0};
        uint32_t pg, c, ncol = 0, only = 0, clampb = 0, rd = 0;
        const eng_deep_t *d = &P5_DEEP;
        p5_patch_init(&p);
        part_set(t, &p);
        for (pg = 0, bad = 0; pg < d->npages; pg++)
            for (c = 0; c < 4u; c++) {
                const param_desc_t *cd = &d->pages[pg].col[c];
                int32_t id = p5_dref(pg, c), v[5], j;
                if (!cd->label) {
                    bad += id >= 0 || d->get(t, pg, c) != 0;
                    p5_patch_put(t, &p);
                    d->set(t, pg, c, 1);
                    bad += memcmp(p5_patch_of(t), &p, sizeof p) != 0;
                    continue;
                }
                ncol++;
                bad += id < 0 || id >= (int32_t)P5_RAW_MAX || strlen(cd->label) > 7u || cd->min > cd->max ||
                       cd->def < cd->min || cd->def > cd->max;
                if (id < 0)
                    continue;
                seen[id]++;
                v[0] = cd->min;
                v[1] = cd->max;
                v[2] = (cd->min + cd->max) / 2;
                v[3] = cd->min - 5;
                v[4] = cd->max + 300;
                for (j = 0; j < 5; j++) {
                    int32_t want = clamp(v[j], cd->min, cd->max);
                    p5_patch_put(t, &p);
                    d->set(t, pg, c, v[j]);
                    q = *p5_patch_of(t);
                    clampb += q.raw[id] != (uint8_t)want;
                    rd += d->get(t, pg, c) != want;
                    q.raw[id] = p.raw[id];
                    only += memcmp(&q, &p, sizeof p) != 0;
                }
            }
        for (k = 0, i = 0; k < P5_RAW_MAX; k++) {
            i += seen[k] > 1u;
            bad += (k < 55u || k == P5_RETRIGGER) ? seen[k] != 1u : seen[k] != 0u;
        }
        OK(d->npages == 19u && ncol == 56u && !bad && !i && d->section[0] == 0 && d->section[1] == 5 && d->section[2] == 7 &&
           d->section[3] == 11 && d->section[4] == 15 && d->section[5] == 0xFF,
           "pages: 19 pages in five sections, 56 columns: each of the 55 panel bytes and PRIORITY on exactly one (BEND on none)");
        OK(!only && !clampb && !rd, "pages: a column sets its byte and no other, clamped to its range, and reads it back");
        /* the names the editor's rules read (cr_edit.c): the filter's curve, the two ADSRs, the + pages */
        OK(!strcmp(d->pages[5].title, "FILTER") && cp_dcol(&d->pages[5], "CUT") == 0 && cp_dcol(&d->pages[5], "RES") == 1 &&
           !strcmp(d->pages[7].title, "ENV 1") && cp_dcol(&d->pages[7], "ATK") == 0 && cp_dcol(&d->pages[7], "REL") == 3 &&
           P5_DKEY[7][0] == P5_ATTACK_AMP && !strcmp(d->pages[9].title, "ENV 2") && P5_DKEY[9][3] == P5_RELEASE_FILTER &&
           ce_plus_of(d, 0, 1) && ce_plus_of(d, 2, 3) && ce_plus_of(d, 11, 12) && ce_plus_of(d, 15, 16) &&
           ce_deep(t) == cp_deep(t) && cp_deep(t) != d && cp_deep(t)->blob_get == d->blob_get,
           "pages: FILTER has CUT and RES (the curve), ENV 1 is the amplifier's ADSR and ENV 2 the filter's, the + pages pair; "
           "the editor's pages are the compiled rows' over them (cr_pages.c cp_deep, tests/cr_p5rows_test.c), the blob the engine's");
        /* a factory program's switch bytes and VOICES 0 read as what plays */
        p5_patch_init(&p);
        p.raw[P5_SAW_A] = 7;
        p.raw[P5_UNISON_COUNT] = 0;
        p.raw[P5_KEY_FILTER] = 9;
        p5_patch_put(t, &p);
        OK(d->get(t, 0, 1) == 1 && d->get(t, 17, 1) == 4 && d->get(t, 5, 3) == 2 && p5_patch_of(t)->raw[P5_SAW_A] == 7,
           "pages: a switch byte other than 0 reads on, VOICES 0 reads 4, a value past its names reads the last; the bytes stay");
        OK(P5_DEEP.mod_dst(t, 5, 0) == -1, "pages: no matrix destination of its own");
    }

    /* ---- the pool, a load through it, SAVE ---- */
    {
        char nm[13];
        uint32_t n = pool_count(ENGI_PROPHET), used = 0;
        cu_pool_load(0, ENGI_PROPHET, 0);
        p5_patch_init(&p);
        bad = trk[CR_PART_CHORD].eng_req != ENGI_PROPHET || memcmp(p5_patch_of(&trk[CR_PART_CHORD]), &p, sizeof p) != 0 ||
              psnd[0].kind != PK_INIT;
        pool_name(ENGI_PROPHET, 0, nm);
        OK(!bad && pool_f0(ENGI_PROPHET) == 1u && n == NELEM(P5_TEST_PRESETS) && !strcmp(nm, "INIT"),
           "pool: INIT and the %u shipped factory programs", (unsigned)n - 1u);
#if FELUCCA_P5_FACTORY
        {
            uint32_t pos = P5_FACTORY_N >= 3u ? 3u : 1u, last = P5_FACTORY_N;
            cu_pool_load(0, ENGI_PROPHET, pos);
            p5_factory(pos - 1u, &p);
            pool_name(ENGI_PROPHET, pos, nm);
            OK(!memcmp(p5_patch_of(&trk[CR_PART_CHORD]), &p, sizeof p) && !strcmp(nm, P5_TEST_PRESETS[pos].name) &&
               psnd[0].kind == PK_FACTORY && trk[CR_PART_CHORD].preset == pos && cu_part_pos(0) == pos &&
               p5_patch_id(p5_patch_of(&trk[CR_PART_CHORD])) == P5_FACTORY_ID[pos - 1u],
               "pool: position %u loads the list's program there (%s, Sequential's %u) into the chord part", (unsigned)pos,
               nm, (unsigned)P5_FACTORY_ID[pos - 1u]);
            cu_pool_load(1, ENGI_PROPHET, last);
            p5_factory(last - 1u, &p);
            OK(!memcmp(p5_patch_of(&trk[CR_PART_BASS]), &p, sizeof p) && trk[CR_PART_BASS].preset == last &&
               cu_part_pos(1) == last && pool_count(ENGI_PROPHET) == last + 1u,
               "pool: the last position, %u, loads the list's last program (%s, Sequential's %u) into the bass part",
               (unsigned)last, P5_TEST_PRESETS[last].name, (unsigned)P5_FACTORY_ID[last - 1u]);
        }
        cu_pool_load(1, ENGI_VA, 1);
#endif
        /* SAVE on a P5 part (0.22, the sound pool: upool.c): the dialog's five entries as every engine's (Overwrite, Save
         * as new, Favorite, Favorites filter, Favorite type); Save as new keeps the whole 138-byte program with the
         * record; the slot loads it back after a power cycle, byte for byte and sounding the same (the render's
         * digest); KNOB 4 on Favorite marks a factory program by its Sequential number */
        {
            cr_screen_t *sc = &cu_scr;
            uint32_t pos = pool_count(ENGI_PROPHET) > 3u ? 3u : pool_count(ENGI_PROPHET) - 1u, id = p5_preset_id(pos);
            int has_prog = pos > 0u;
            uint32_t used, slot = UP_SLOTS, n0 = pool_count(ENGI_PROPHET);
            static const uint8_t NOTES[3] = {57, 60, 64};
            p5_patch_t saved, back;
            rr_t r0, r1;
            cu_pool_load(0, ENGI_PROPHET, pos);
            (void)crf_unpack(0, 0);
            cu.page = PG_NONE;
            cu.lock = L_NONE;
            cu.opt_open = 0;
            cu.msg.text[0] = 0;
            p5_patch_of(&trk[CR_PART_CHORD])->raw[P5_CUTOFF] ^= 3u;   /* (edited: the program as it is now is saved) */
            psnd[0].edited = 1;
            saved = *p5_patch_of(&trk[CR_PART_CHORD]);
            cu_save_open(0);
            cr_screen_clear(sc);
            cu_save_screen(sc, 0);
            OK(cu.page == PG_SAVE && ce.save_choice == 0u && sc->n_items == 5u && !cu.msg.text[0],
               "save: a P5 part's dialog has the five entries, Overwrite first for an edited sound (0.22; before: Favorite alone)");
            cu_knob(EN_SELECT, +1);
            OK(ce.save_choice == 1u, "save: P5: SELECT +1: Save as new");
            cu_oct_tap(B_OCTUP);
            OK(ce.save_step == 1u, "save: P5: OCT+ on Save as new: the naming page");
            cu_oct_tap(B_OCTUP);
            for (k = 0, used = 0; k < UP_SLOTS; k++)
                if (up_used(k)) {
                    used++;
                    slot = k;
                }
            OK(used == 1u && slot < UP_SLOTS && up_rec(slot)->engine == ENGI_PROPHET &&
               up_patch_get(slot, ENGI_PROPHET, (uint8_t *)&back, sizeof back) == 0 && !memcmp(&back, &saved, sizeof back) &&
               pool_count(ENGI_PROPHET) == n0 + 1u,
               "save: P5: Save as new wrote one slot (U%03u): engine P5, its 138 bytes as the part's program, the pool one longer",
               (unsigned)slot + 1u);
            {   /* the part's sound parameters as they were saved (the part's own, param_kept, aside) */
                int16_t before[P_COUNT];
                memcpy(before, trk[CR_PART_CHORD].p, sizeof before);
                memset(upn, 0, sizeof upn);          /* a power cycle: the table from flash */
                up_boot();
                cu_pool_load(0, ENGI_PROPHET, 1);
                cu_pool_load(0, ENGI_PROPHET, pool_pos_slot(ENGI_PROPHET, slot));
                back = *p5_patch_of(&trk[CR_PART_CHORD]);
                for (k = 0, used = 0; k < P_COUNT; k++)
                    used += !param_kept(k) && before[k] != trk[CR_PART_CHORD].p[k];
            }
            {   /* the two programs rendered from one state (render keeps state from call to call: a fork renders each
                 * from the same memory) */
                int fd[2];
                pid_t pid;
                r0.h = r1.h = 0;
                if (!pipe(fd) && (pid = fork()) >= 0) {
                    if (!pid) {
                        rr_t c = render(&trk[CR_PART_CHORD], &saved, NOTES, 3, 100, 40, 20);
                        (void)!write(fd[1], &c.h, sizeof c.h);
                        _exit(0);
                    }
                    r1 = render(&trk[CR_PART_CHORD], &back, NOTES, 3, 100, 40, 20);
                    (void)!read(fd[0], &r0.h, sizeof r0.h);
                    waitpid(pid, 0, 0);
                    close(fd[0]);
                    close(fd[1]);
                }
            }
            OK(up_used(slot) && psnd[0].kind == PK_USER && psnd[0].slot == slot && !memcmp(&back, &saved, sizeof back) &&
               !used && r0.h && r0.h == r1.h, "save: P5: after a power cycle the slot loads its program byte for byte, the "
               "part's sound parameters as saved, and renders the same (digest %016llx)", (unsigned long long)r1.h);
            OK(cu_fav_part_key(0, &k, &used) && k == CRF_T_USER && used == slot,
               "save: P5: a P5 user sound's favourite key is its slot, as any user sound's");
            OK(up_put(slot, 0) == 0 && !up_used(slot) && pool_count(ENGI_PROPHET) == n0, "save: P5: the slot erased (the pool as before)");
            /* the mark on a factory program: KNOB 4 on Favorite, by its Sequential number */
            cu_pool_load(0, ENGI_PROPHET, pos);
            cu.page = PG_NONE;
            cu.msg.text[0] = 0;
            cu_save_open(0);
            cu_knob(EN_SELECT, +2);
            OK(ce.save_choice == CU_SAVE_FAV, "save: P5: SELECT +2 from Save as new: Favorite");
            cu_knob(EN_K4, +1);
            cr_screen_clear(sc);
            cu_save_screen(sc, 0);
            OK(has_prog ? crf_has(ENGI_PROPHET, id) && crf_count() == 1u && sc->star == 1
                        : crf_count() == 0u && !strcmp(cu.msg.text, "INIT: cannot be marked"),
               has_prog ? "save: P5: KNOB 4 marks the program by its Sequential number (%u), not its place (%u)"
                        : "save: P5 INIT cannot be marked%.0u%.0u", (unsigned)id, (unsigned)pos);
            cu.msg.text[0] = 0;
            cu_tap(BT_SAVE);
            for (k = 0, used = 0; k < UP_SLOTS; k++)
                used += (uint32_t)up_used(k);
            OK(cu.page == PG_NONE && !used && (!has_prog || crf_has(ENGI_PROPHET, id)),
               "save: P5: SAVE again closes the dialog; nothing saved, the mark kept");
            /* the mark is read wherever the name is: the pool read-out, the editor's browser */
            if (has_prog) {
                cu_pool_popup(0, pos);
                OK(cu.pop.star == 1, "favourite: the pool read-out of that place carries the star");
                cu_pool_popup(0, pos == 1u ? 2u : 1u);
                OK(cu.pop.star == 0 || pool_count(ENGI_PROPHET) < 3u, "favourite: another place's does not");
                /* edited: still its program's mark (the number is in the program's bytes) */
                psnd[0].edited = 1;
                p5_patch_of(&trk[CR_PART_CHORD])->raw[P5_CUTOFF] ^= 1u;
                OK(cu_fav_part(0), "favourite: the program edited: still marked (its factory origin)");
                p5_patch_of(&trk[CR_PART_CHORD])->raw[P5_CUTOFF] ^= 1u;
                psnd[0].edited = 0;
            }
            /* the editor's browser (PRESETS over the editor): KNOB 4 marks the previewed sound, the browser stays */
            if (pool_count(ENGI_PROPHET) > 2u) {
                uint32_t p2 = pos == 1u ? 2u : 1u, id2 = p5_preset_id(p2), crc;
                cu_pool_load(0, ENGI_PROPHET, pos);
                cu.page = PG_EDIT;
                ce.part = 0;
                cu_pick_open();
                cu_pool_load(0, ENGI_PROPHET, p2);    /* (the browser's step to another place: KNOB 1 / PRESETS) */
                cpk.changed = 1;
                crc = cu_snd_crc(&trk[CR_PART_CHORD]);
                cu_knob(EN_K4, +1);
                cr_screen_clear(sc);
                cu_layer_screen(sc, L_EDIT);
                OK(cu.lock == L_EDIT && cpk.on && crf_has(ENGI_PROPHET, id2) && crf_count() == 2u && sc->star == 1 &&
                   cu.pop.star == 1 && crc == cu_snd_crc(&trk[CR_PART_CHORD]),
                   "browser: KNOB 4 right marks the previewed program (Sequential's %u); the browser stays open, the sound "
                   "untouched, the star beside the name and on the read-out", (unsigned)id2);
                cu_knob(EN_K4, -1);
                cr_screen_clear(sc);
                cu_layer_screen(sc, L_EDIT);
                OK(!crf_has(ENGI_PROPHET, id2) && crf_count() == 1u && !sc->star && cu.lock == L_EDIT,
                   "browser: KNOB 4 left takes it off");
                cu_knob(EN_K4, +1);
                cu_knob(EN_K3, +1);
                OK(crf_count() == 2u && cu.lock == L_EDIT && crc == cu_snd_crc(&trk[CR_PART_CHORD]), "browser: KNOB 3 still does nothing");
                cu_pick_end(0);                       /* OCT-: the preview cancelled: the marks are not part of it */
                cu.lock = L_NONE;
                cu.page = PG_NONE;
                OK(crf_has(ENGI_PROPHET, id2) && crf_has(ENGI_PROPHET, id) && cu_part_pos(0) == pos,
                   "browser: OCT- gives the sound back (place %u); the marks made while browsing stay", (unsigned)pos);
            }
            (void)crf_unpack(0, 0);
            crf_dirty = 0;
        }
        /* another engine: the five entries, in the Options' grammar (SELECT the entry, KNOB 1 its value) */
        cu_pool_load(0, ENGI_FM6, 1);
        cu.msg.text[0] = 0;
        cu_save_open(0);
        OK(cu.page == PG_SAVE && ce.save_choice == 1u, "save: another engine's part opens the dialog as before (TINE EP, a factory sound: Save as new)");
        cu_knob(EN_SELECT, -1);
        bad = ce.save_choice != 0u;
        cu_knob(EN_SELECT, -1);
        bad |= ce.save_choice != 0u;
        cu_knob(EN_K1, -1);
        cu_knob(EN_K1, +1);
        bad |= ce.save_choice != 0u || crf_count() != 0u || crf_filter;
        cu_knob(EN_SELECT, +1);
        bad |= ce.save_choice != 1u;
        cu_knob(EN_K1, +1);
        bad |= ce.save_choice != 1u || crf_count() != 0u || crf_filter;
        cu_knob(EN_SELECT, +5);
        bad |= ce.save_choice != CU_SAVE_FAV;
        cu_knob(EN_SELECT, +1);
        bad |= ce.save_choice != CU_SAVE_FILT;
        cu_knob(EN_SELECT, +1);
        bad |= ce.save_choice != CU_SAVE_TYPE;
        cu_knob(EN_SELECT, +1);
        bad |= ce.save_choice != CU_SAVE_TYPE;
        OK(!bad, "save: SELECT steps through five entries, a turn a step, and stops at the ends: Overwrite, Save as new, Favorite, "
           "Favorites filter, Favorite type; KNOB 1 no longer moves the entry, and on the two saves (no value) does nothing");
        {
            cr_screen_t *sc = &cu_scr;
            int32_t send = cu_send(0, CU_FX[cs.fx_sel].send);
#define SCR() (cr_screen_clear(sc), cu_save_screen(sc, 0))
            /* Favorite type */
            SCR();
            OK(sc->n_items == 5u && sc->sel == 4u && !strcmp(sc->item[4], "Both") && !sc->star && !strcmp(sc->label, "type \267 not a favorite") &&
               !strcmp(sc->footer, CU_SAVE_FOOT),
               "type: the screen of a sound that is not a favourite: its type (Both), no star, \"type . not a favorite\"");
            cu_knob(EN_K1, +1);
            SCR();
            OK(crf_has(ENGI_FM6, 0) && crf_type(ENGI_FM6, 0) == CRF_CHORD && sc->star == 1 && !strcmp(sc->item[4], "Chord") &&
               !strcmp(sc->label, "favorite type"), "type: KNOB 1 +1 on a sound that is not a favourite: Chord, and it marks the sound");
            cu_knob(EN_K4, -1);
            SCR();
            OK(!crf_has(ENGI_FM6, 0) && crf_type(ENGI_FM6, 0) == CRF_CHORD && !sc->star && !strcmp(sc->item[4], "Chord"),
               "type: KNOB 4 left takes the mark off and leaves the type: Chord still shown, no star");
            cu_knob(EN_K4, +1);
            OK(crf_has(ENGI_FM6, 0) && crf_type(ENGI_FM6, 0) == CRF_CHORD, "type: KNOB 4 right marks it again: Chord as it was");
            cu_knob(EN_K1, +3);
            bad = crf_type(ENGI_FM6, 0) != CRF_BASS;
            cu_knob(EN_K1, +1);
            bad |= crf_type(ENGI_FM6, 0) != CRF_BASS;
            SCR();
            bad |= strcmp(sc->item[4], "Bass");
            cu_knob(EN_K4, -1);
            cu_knob(EN_K1, +1);                       /* (against the end: no change of type, the turn still marks) */
            bad |= !crf_has(ENGI_FM6, 0) || crf_type(ENGI_FM6, 0) != CRF_BASS;
            cu_knob(EN_K1, -1);
            cu_knob(EN_K1, -1);
            cu_knob(EN_K1, -1);
            OK(!bad && crf_type(ENGI_FM6, 0) == CRF_BOTH && crf_count() == 1u,
               "type: KNOB 1 a step a turn through Both, Chord, Bass, stopping at the ends; a turn on an unmarked sound marks it");
            cu_oct_tap(B_OCTUP);
            /* Favorites filter */
            cu_knob(EN_SELECT, -1);
            SCR();
            bad = ce.save_step || sc->sel != 3u || strcmp(sc->item[3], "Filter off") || strcmp(sc->item[4], "Type: Both") ||
                  strcmp(sc->label, "favorites filter");
            cu_knob(EN_K1, +1);
            SCR();
            bad |= !crf_filter || strcmp(sc->item[3], "Filter on");
            cu_knob(EN_K1, +1);
            bad |= !crf_filter;
            cu_knob(EN_K4, -1);
            SCR();
            bad |= crf_count() != 0u || !crf_filter || strcmp(sc->label, "no favorites here: all");
            cu_knob(EN_K4, +1);
            cu_oct_tap(B_OCTUP);
            cu_knob(EN_K1, -1);
            OK(!bad && !crf_filter && crf_count() == 1u && !ce.save_step,
               "filter: KNOB 1 right On, left Off; KNOB 4 there is the sound's mark; with no favourite here the line says the "
               "whole pool is turned; OCT+ does nothing");
            /* Favorite */
            cu_knob(EN_SELECT, -1);
            cu_knob(EN_K1, -1);
            bad = ce.save_choice != CU_SAVE_FAV || crf_count() != 0u;
            cu_knob(EN_K1, +1);
            bad |= !crf_has(ENGI_FM6, 0);
            cu_knob(EN_K4, -1);
            bad |= crf_count() != 0u;
            cu_knob(EN_K4, +1);
            cu_oct_tap(B_OCTUP);
            for (k = 0, used = 0; k < UP_SLOTS; k++)
                used += (uint32_t)up_used(k);
            OK(!bad && crf_has(ENGI_FM6, 0) && cu.page == PG_SAVE && !ce.save_step && !used && send == cu_send(0, CU_FX[cs.fx_sel].send),
               "save: on Favorite KNOB 1 and KNOB 4 both set the mark (not the send KNOB 4 turns elsewhere); OCT+ saves nothing and names nothing");
            /* the two saves: KNOB 4 is the mark there too */
            cu_knob(EN_SELECT, -1);
            cu_knob(EN_K4, -1);
            bad = crf_count() != 0u;
            cu_knob(EN_K4, +1);
            SCR();
            OK(!bad && ce.save_choice == 1u && send == cu_send(0, CU_FX[cs.fx_sel].send) && crf_count() == 1u && sc->star == 1 &&
               !strcmp(sc->footer, "OCT+: save \267 OCT-: cancel"),
               "save: on the two saves KNOB 4 is the mark too (until this menu: the chord part's send), the send untouched");
            cu_knob(EN_SELECT, -1);
            cu_knob(EN_K4, -1);
            OK(ce.save_choice == 0u && crf_count() == 0u, "save: .. on Overwrite as on Save as new");
            cu_knob(EN_SELECT, +1);
            cu_oct_tap(B_OCTUP);
            OK(ce.save_step == 1u, "save: OCT+ on Save as new: the naming page, as before");
            cu_knob(EN_K4, +1);
            OK(send == cu_send(0, CU_FX[cs.fx_sel].send) && crf_count() == 0u,
               "save: on the naming page KNOB 4 right is nothing (not the mark, not the send it was until the name's cursor)");
#undef SCR
        }
        /* KNOB 3 on every entry: the sound's type, as KNOB 1 on the Favorite type entry */
        {
            int32_t pv = trk[CR_PART_CHORD].p[0], e;
            uint32_t crc = cu_snd_crc(&trk[CR_PART_CHORD]);
            (void)pv;
            cu.page = PG_NONE;
            (void)crf_unpack(0, 0);
            cu_save_open(0);
            for (e = 0, bad = 0; e < (int32_t)CU_SAVE_N; e++) {
                while ((int32_t)ce.save_choice > e)
                    cu_knob(EN_SELECT, -1);
                while ((int32_t)ce.save_choice < e)
                    cu_knob(EN_SELECT, +1);
                (void)crf_unpack(0, 0);
                cu.msg.text[0] = 0;
                cu_knob(EN_K3, +1);                   /* not a favourite: Chord, and marked by it */
                bad |= !crf_has(ENGI_FM6, 0) || crf_type(ENGI_FM6, 0) != CRF_CHORD || (int32_t)ce.save_choice != e;
                bad |= e == (int32_t)CU_SAVE_TYPE ? cu.msg.text[0] != 0 : strcmp(cu.msg.text, "Type: Chord");
                cu_knob(EN_K3, +1);
                cu_knob(EN_K3, +1);                   /* (the end) */
                bad |= crf_type(ENGI_FM6, 0) != CRF_BASS || crf_count() != 1u;
                cu_knob(EN_K4, -1);
                cu_knob(EN_K4, +1);                   /* the mark off and on: Bass still */
                bad |= crf_type(ENGI_FM6, 0) != CRF_BASS || !crf_has(ENGI_FM6, 0);
                cu_knob(EN_K3, -1);                   /* a marked sound: the type alone */
                bad |= crf_type(ENGI_FM6, 0) != CRF_CHORD || !crf_has(ENGI_FM6, 0) || crf_count() != 1u;
                cu_knob(EN_K3, -1);
                cu_knob(EN_K3, -1);
                bad |= crf_type(ENGI_FM6, 0) != CRF_BOTH || !crf_has(ENGI_FM6, 0) || ce.save_step;
                if (e != (int32_t)CU_SAVE_TYPE)
                    bad |= strcmp(cu.msg.text, "Type: Both");
            }
            OK(!bad && crc == cu_snd_crc(&trk[CR_PART_CHORD]),
               "save: KNOB 3 on each of the five entries is the sound's type (a step a turn, the ends): it marks a sound that is "
               "not a favourite, leaves the mark of one that is, and says the type on the entries that do not show it; the "
               "sound itself untouched (until this menu: the Perform mode's knob)");
            cu.msg.text[0] = 0;
            cu.msg.until = 0;
            cu.page = PG_NONE;
            (void)crf_unpack(0, 0);
        }
        /* KNOB 2 on every entry: the favourites filter, as KNOB 1 on the Favorites filter entry; on a P5 part's menu
         * too; not on the naming page (the last letter there) */
        {
            int32_t e, bv = cr_snap.bass_voicing;
            uint32_t pass;
            for (pass = 0, bad = 0; pass < 2u; pass++) {
                cu.page = PG_NONE;
                (void)crf_unpack(0, 0);
                cu_pool_load(0, pass ? ENGI_PROPHET : ENGI_FM6, 1);
                cu_save_open(0);
                for (e = pass ? (int32_t)CU_SAVE_FAV : 0; e < (int32_t)CU_SAVE_N; e++) {
                    while ((int32_t)ce.save_choice > e)
                        cu_knob(EN_SELECT, -1);
                    while ((int32_t)ce.save_choice < e)
                        cu_knob(EN_SELECT, +1);
                    cu.msg.text[0] = 0;
                    cu_knob(EN_K2, +1);
                    bad |= !crf_filter || (int32_t)ce.save_choice != e || crf_count() != 0u;
                    bad |= e == (int32_t)CU_SAVE_FILT ? cu.msg.text[0] != 0 : strcmp(cu.msg.text, "Filter on");
                    cu_knob(EN_K2, +2);
                    bad |= !crf_filter;
                    cu_knob(EN_K2, -1);
                    bad |= crf_filter || ce.save_step;
                    if (e != (int32_t)CU_SAVE_FILT)
                        bad |= strcmp(cu.msg.text, "Filter off");
                    cu_knob(EN_K2, -1);
                    bad |= crf_filter;
                }
                if (pass && pool_count(ENGI_PROPHET) > 1u)   /* (a build with INIT alone: nothing to mark) */
                    cu_knob(EN_K3, +1), bad |= crf_count() != 1u || crf_type(ENGI_PROPHET, p5_preset_id(1)) != CRF_CHORD;
            }
            OK(!bad && bv == cr_snap.bass_voicing,
               "save: KNOB 2 on each entry (the five, and a P5 part's three) is the favourites filter, left Off, right On, with a "
               "message of the state on the entries that do not show it; no mark, no type changes (until this menu: the bass "
               "register); KNOB 3 on a P5 part's menu types and marks the program");
            cu.page = PG_NONE;
            (void)crf_unpack(0, 0);
            cu_pool_load(0, ENGI_FM6, 1);
            cu_save_open(0);
            cu_oct_tap(B_OCTUP);                      /* Save as new: the naming page */
            {   /* the naming page: SELECT the cursor, KNOB 1 the character at it, KNOB 4 left the backspace; the rest nothing */
                uint32_t pos0 = cu_part_pos(0), bs0 = cs.bass_sound, bpm0 = cs.bpm, e0 = trk[CR_PART_CHORD].eng_req, sp0 = cs.split;
                int32_t send0 = cu_send(0, CU_FX[cs.fx_sel].send);
                uint32_t crc0 = cu_snd_crc(&trk[CR_PART_CHORD]);
                bad = strcmp(cn.s, "TINE EP") || cn.cur != 6u || !cn.pristine;   /* the cursor ON the last character */
                cu_knob(EN_K2, +1);
                cu_knob(EN_K3, +1);
                cu_knob(EN_K4, +1);
                cu_knob(EN_PRESET, +1);
                cu_knob(EN_ALGO, +1);
                cu_knob(EN_PRESET, -3);
                cu.bheld |= 1u << BT_OPT;                 /* GLO held: no engine picker, no split point, no click level */
                cu_knob(EN_PRESET, +1);
                cu_knob(EN_ALGO, +1);
                cu_knob(EN_K2, +1);
                cu.bheld &= ~(1u << BT_OPT);
                OK(!bad && ce.save_step == 1u && !crf_filter && !strcmp(cn.s, "TINE EP") && cn.cur == 6u && cn.pristine &&
                   cu_part_pos(0) == pos0 && cs.bass_sound == bs0 && cs.bpm == bpm0 && trk[CR_PART_CHORD].eng_req == e0 &&
                   !cu.epk_on && cs.split == sp0 && send0 == cu_send(0, CU_FX[cs.fx_sel].send) && crc0 == cu_snd_crc(&trk[CR_PART_CHORD]) &&
                   (int32_t)(cu.pop.until - cu_now()) <= 0,
                   "naming: the page opens with the cursor on the last character; KNOB 2, KNOB 3, KNOB 4 right, PRESETS and "
                   "ALGORITHM (GLO held too) do nothing: the name, the sound, the engine, the bass, the send, the tempo and the "
                   "filter as they were, no read-out");
                cu_knob(EN_K1, +1);                       /* at once the last letter, as KNOB 2 was: P -> Q */
                bad = strcmp(cn.s, "TINE EQ") || cn.pristine || cs.bpm != bpm0;
                cu_knob(EN_K1, -1);
                cu_knob(EN_SELECT, -9);                   /* (a step a turn) */
                bad |= cn.cur != 5u;
                cu_knob(EN_K1, +1);                       /* the cursor on 'E' (5): F */
                bad |= strcmp(cn.s, "TINE FP");
                cu_knob(EN_K1, -1);
                cu_knob(EN_SELECT, -1);
                cu_knob(EN_K4, -1);                       /* backspace in the middle: the space UNDER the cursor goes, the
                                                           * cursor to the character before it */
                bad |= strcmp(cn.s, "TINEEP") || cn.cur != 3u;
                cu_knob(EN_SELECT, +1);
                cu_knob(EN_SELECT, +1);
                bad |= cn.cur != 5u || cn.len != 6u;      /* on the last character */
                cu_knob(EN_K4, -1);                       /* backspace on the last character */
                bad |= strcmp(cn.s, "TINEE") || cn.cur != 4u;
                cu_knob(EN_SELECT, +1);                   /* right from the last: a new character, the cursor on it */
                bad |= strcmp(cn.s, "TINEEA") || cn.cur != 5u;
                cu_knob(EN_K1, +1);
                bad |= strcmp(cn.s, "TINEEB");
                for (k = 0; k < 20u; k++)                 /* to the maximum: 12 characters, then SELECT right stops */
                    cu_knob(EN_SELECT, +1);
                bad |= cn.len != 12u || strcmp(cn.s, "TINEEBAAAAAA") || cn.cur != 11u;
                for (k = 0; k < 20u; k++)
                    cu_knob(EN_SELECT, -1);
                bad |= cn.cur != 0u || cn.len != 12u;
                cn_white(0, 1000);                        /* a key with the name full: nothing */
                bad |= cn.len != 12u || strcmp(cn.s, "TINEEBAAAAAA") || cn.cur != 0u;
                cu_knob(EN_K4, -1);                       /* backspace on the first character: the cursor on the new first */
                bad |= strcmp(cn.s, "INEEBAAAAAA") || cn.cur != 0u;
                for (k = 0; k < 12u; k++)
                    cu_knob(EN_SELECT, +1);               /* (to the end, and one more character: 12 again) */
                for (k = 0; k < 9u; k++)
                    cu_knob(EN_K4, -1);
                bad |= strcmp(cn.s, "INE") || cn.cur != 2u;
                cu_knob(EN_SELECT, -1);
                cu_knob(EN_SELECT, -1);                   /* the cursor on 'I' */
                cn_white(0, 5000);                        /* D4: 'A' in after it, the cursor on the A */
                cn_white(0, 5100);                        /* .. again within 0.8 s: 'B' */
                bad |= strcmp(cn.s, "IBNE") || cn.cur != 1u;
                cn_space();
                bad |= strcmp(cn.s, "IB NE") || cn.cur != 2u;
                cn_delete();                              /* F#4: the backspace */
                cn_delete();
                bad |= strcmp(cn.s, "INE") || cn.cur != 0u;
                {
                    cr_screen_t *sc = &cu_scr;
                    cr_screen_clear(sc);
                    cu_save_screen(sc, 0);
                    bad |= sc->kind != CR_K_TEXT || strcmp(sc->lines[1].t, "INE") || sc->lines[1].cur != 1u;
                }
                OK(!bad, "naming with the knobs: the cursor is always on a character; KNOB 1 changes it (at once the last one, as "
                   "KNOB 2 did); SELECT moves a step a turn, and right from the last character adds one, to 12; KNOB 4 left "
                   "deletes the character under the cursor (the middle, the last, the first); a key goes in after the "
                   "cursor, which moves onto it; F#4 is the backspace");
                for (k = 0; k < 3u; k++)
                    cu_knob(EN_K4, -1);
                {
                    cr_screen_t *sc = &cu_scr;
                    cr_screen_clear(sc);
                    cu_save_screen(sc, 0);
                    bad = cn.len != 0u || cn.cur != 0u || sc->lines[1].cur != 0u || sc->lines[1].t[0];
                }
                cu_knob(EN_K4, -1);
                bad |= cn.len != 0u;
                cu_knob(EN_SELECT, +1);
                bad |= strcmp(cn.s, "A") || cn.cur != 0u;
                cu_knob(EN_K4, -1);
                cu_knob(EN_K1, -1);
                OK(!bad && !strcmp(cn.s, "A") && cn.cur == 0u, "naming: every character deleted: an empty name, allowed as before "
                   "(saved under the slot's automatic name), no cursor drawn; SELECT right or KNOB 1 gives it a first character");
            }
            cu.msg.text[0] = 0;
            cu.msg.until = 0;
            cu.page = PG_NONE;
            (void)crf_unpack(0, 0);
            crf_dirty = 0;
        }
        /* the filter's turns (cuf_*): FM6 01 Chord, 03 Bass, 04 Both; the chord part FM6, the bass part FM6 */
        {
            uint32_t keep_bs = cs.bass_sound;
            cu.page = PG_NONE;
            (void)crf_unpack(0, 0);
            (void)crf_set(ENGI_FM6, 0, 1), (void)crf_set_type(ENGI_FM6, 0, CRF_CHORD);
            (void)crf_set(ENGI_FM6, 2, 1), (void)crf_set_type(ENGI_FM6, 2, CRF_BASS);
            (void)crf_set(ENGI_FM6, 3, 1);
            (void)crf_set_type(ENGI_FM6, 5, CRF_CHORD);   /* (typed, not marked: not a favourite) */
            cu_pool_load(0, ENGI_FM6, 6);
            cu_pool_load(1, ENGI_FM6, 6);
            OK(cuf_n(0, ENGI_FM6) == 0u && cuf_n(1, ENGI_FM6) == 0u, "filter off: no list (the whole pool is turned)");
            cu_knob(EN_PRESET, +1);
            OK(cu_part_pos(0) == 7u && !cu.pop.filt, "filter off: PRESETS +1 from 06: 07, the read-out without the sign");
            (void)crf_set_filter(1);
            OK(cuf_n(0, ENGI_FM6) == 2u && cuf_n(1, ENGI_FM6) == 2u && cuf_at(0, 1) && !cuf_at(0, 3) && cuf_at(0, 4) && !cuf_at(1, 1) &&
               cuf_at(1, 3) && cuf_at(1, 4) && !cuf_at(0, 6) && !cuf_at(0, 0),
               "filter on: the chord part's favourites of FM6 are 01 and 04, the bass part's 03 and 04; a typed sound without a mark is none");
            cu_knob(EN_PRESET, -1);
            bad = cu_part_pos(0) != 4u || !cu.pop.filt || strcmp(cu.pop.label, "favorites 02/02");
            cu_knob(EN_PRESET, -1);
            bad |= cu_part_pos(0) != 1u;
            cu_knob(EN_PRESET, -1);
            bad |= cu_part_pos(0) != 4u;
            cu_knob(EN_PRESET, +2);
            bad |= cu_part_pos(0) != 4u;
            cu_knob(EN_PRESET, +1);
            OK(!bad && cu_part_pos(0) == 1u && cu.pop.star, "filter on: PRESETS from 07 (no favourite): -1: 04, the nearest that way; "
               "then 01, 04 (round), +2: 04, +1: 01; the read-out's line is its place among them, the sign on");
            cs.bass_sound = 7;
            cs.bass_on = 1;
            cu_knob(EN_ALGO, +1);
            bad = cs.bass_sound != 0u;                /* (past the last favourite: OFF, as past the pool's end) */
            cu_knob(EN_ALGO, +1);
            bad |= cu_part_pos(1) != 3u || cs.bass_sound != 4u;
            cu_knob(EN_ALGO, +1);
            bad |= cu_part_pos(1) != 4u;
            cu_knob(EN_ALGO, -2);
            bad |= cs.bass_sound != 0u;
            cu_knob(EN_ALGO, -1);
            OK(!bad && cu_part_pos(1) == 4u && cs.bass_sound == 5u,
               "filter on: ALGORITHM from 06 +1: OFF (none after it), +1: 03, +1: 04, -2: OFF, -1: 04 (round)");
            (void)crf_set(ENGI_FM6, 0, 0);
            (void)crf_set(ENGI_FM6, 3, 0);
            OK(cuf_n(0, ENGI_FM6) == 0u && cuf_n(1, ENGI_FM6) == 1u, "the marks of 01 and 04 off: the chord part has none here, the bass part 03");
            cu_knob(EN_PRESET, +1);
            OK(cu_part_pos(0) == 2u && cu.pop.filt && !strcmp(cu.pop.label, "no favorites: all"),
               "filter on, no favourite of the part in the engine: PRESETS turns the whole pool (01 -> 02), the read-out says \"no favorites: all\"");
            cu_knob(EN_ALGO, -1);
            bad = cu_part_pos(1) != 3u;
            cu_knob(EN_ALGO, +1);
            OK(!bad && cs.bass_sound == 0u, "the sound playing lost its mark (04): it stays; ALGORITHM -1: 03, the nearest favourite; +1: OFF");
            (void)crf_unpack(0, 0);
            crf_dirty = 0;
            cs.bass_sound = (uint16_t)keep_bs;
            cs.bass_on = keep_bs != 0u;
            cu_pool_load(1, ENGI_VA, 1);
            cu_pool_load(0, ENGI_FM6, 1);
            cu.pop.until = 0;
        }
#if FELUCCA_P5_FACTORY
        /* P5's pool, the filter's cache: a turn does not walk the pool (the list is built once per change of a mark) */
        if (pool_count(ENGI_PROPHET) > 3u) {
            uint32_t last = pool_count(ENGI_PROPHET) - 1u, g;
            cu_pool_load(0, ENGI_PROPHET, 1);
            (void)crf_set(ENGI_PROPHET, p5_preset_id(2), 1);
            (void)crf_set(ENGI_PROPHET, p5_preset_id(last), 1);
            (void)crf_set_filter(1);
            cu_knob(EN_PRESET, +1);
            bad = cu_part_pos(0) != 2u;
            g = cuf[0].gen;
            cu_knob(EN_PRESET, +1);
            bad |= cu_part_pos(0) != last || cuf[0].gen != g || cuf[0].e != ENGI_PROPHET + 1u;
            cu_knob(EN_PRESET, +1);
            OK(!bad && cu_part_pos(0) == 2u && cuf[0].n == 2u,
               "filter on, P5: PRESETS turns between its two favourites (place 02 and the last, %u), round; the list is the cached one", (unsigned)last);
            (void)crf_unpack(0, 0);
            crf_dirty = 0;
            cu_pool_load(0, ENGI_FM6, 1);
            cu.pop.until = 0;
        }
#endif
        cu.page = PG_NONE;
        (void)crf_unpack(0, 0);
        crf_dirty = 0;
    }

    t_cores();
    printf("%s: %d checks, %d failed\n", fails ? "cr_prophet_test FAILED" : "cr_prophet_test ok", checks, fails);
    return fails != 0;
}
