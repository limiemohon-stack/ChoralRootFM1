/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* GRAIN's sketches (docs/GRAIN.md): a source generator that renders its own material from four knobs (TYPE TONE
 * MOTION SEED) into a short buffer, and a grain player over that buffer (Felucca's eng_grain.c player without the
 * ADPCM decoder), rendered to WAVs for the owner to hear before the engine is built.
 *
 *   cc -O2 -w -Ibuild/gen -o build/host/grain_sketch tools/grain_sketch.c -lm
 *   build/host/grain_sketch build/grain-sketch              (the WAVs and INDEX.md there; ~130 files, ~30 MB)
 *   build/host/grain_sketch build/grain-sketch src          (the sources and the seed tours only, no granulation)
 *
 * The core is written as the firmware has it: int32 fixed point, no float, no libc, dsp.c's blocks (osc_saw with its
 * polyBLEP, osc_pulse, osc_tri, sine_i, the trapezoidal SVF, noise32, softclip) and the tables of
 * build/gen/felucca_tables.h (pitch_inc, SVF_G). The generator (gs_*) IS the firmware's: firmware/src/grain_src.c,
 * included here (step 1 of docs/GRAIN.md; tests/cr_grain_test.c holds its goldens, recorded from these WAVs); it
 * renders in slices (gs_begin, gs_step n samples at a time, gs_finish): the firmware chunks the render across
 * main-loop turns, the grains playing the old buffer meanwhile. The player (gp_*) is the sketch's own until step 2.
 * GS_UNIT 1 compiles the core alone for the device compiler (tools/grain_cost.py counts its flash and its loops). */
#include <stdint.h>
#ifndef GS_UNIT
#define GS_UNIT 0
#endif
#if !GS_UNIT
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#define __attribute__(x)
#define GS_API static
#define GS_FAM static
#else
#define GS_API
#define GS_FAM static __attribute__((noinline))   /* one listing a family (tools/grain_cost.py) */
#endif
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#define FELUCCA_VA 1
#define FELUCCA_CZ 1
#define FELUCCA_QUAD 1
#include "felucca_tables.h"
#include "../firmware/src/core.h"
#include "../firmware/src/dsp.c"

#include "../firmware/src/grain_src.c"

/* ============================================================================ the grain player (gp_) === */
/* eng_grain.c's player over a RAM buffer: no decoder, no seek index, a reverse grain reads backwards. The grain's
 * rate is the note against the buffer's root (GS_ROOT16), PITCH and the random detune, times GS_RATE / 44100 */
/* 2^(i/192), Q16: pitch ratios in 1/16 semitones, d16 >= -3072 (eng_sample.c's) */
static uint32_t pow2_q16(int32_t d16)
{
    uint32_t u = (uint32_t)(d16 + 192 * 16), oct = u / 192u;
    uint32_t r = pitch_inc(1600 + u % 192u) / (pitch_inc(1600) >> 16);
    return oct >= 16u ? r << (oct - 16u) : r >> (16u - oct);
}
#define GP_NG 16                 /* grains a part, shared by its voices */
#define GP_POLY 6
#define GP_STEP_MAX (2u << 16)   /* the rate's cap: two source samples an output sample */
#define GP_RATE_Q16 ((GS_RATE << 16) / 44100u)   /* 0.5 */
#define GP_XFADE (!GS_UNIT)      /* the crossfade comparison (gp_par_t.xfade): the sketches only, not the design */
/* sin^2(pi i / 256), Q15: the grain window (eng_grain.c GR_HANN) */
static const int16_t GP_HANN[257] = {
    0, 5, 20, 44, 79, 123, 177, 241, 315, 398, 491, 593, 705, 827, 958, 1098,
    1247, 1406, 1573, 1749, 1935, 2128, 2331, 2542, 2761, 2989, 3224, 3468, 3719, 3978, 4244, 4518,
    4799, 5086, 5381, 5682, 5990, 6304, 6624, 6950, 7281, 7618, 7961, 8308, 8660, 9017, 9379, 9744,
    10114, 10487, 10864, 11244, 11628, 12014, 12403, 12794, 13187, 13583, 13980, 14378, 14778, 15178, 15580, 15981,
    16383, 16786, 17187, 17589, 17989, 18389, 18787, 19184, 19580, 19973, 20364, 20753, 21139, 21523, 21903, 22280,
    22653, 23023, 23388, 23750, 24107, 24459, 24806, 25149, 25486, 25817, 26143, 26463, 26777, 27085, 27386, 27681,
    27968, 28249, 28523, 28789, 29048, 29299, 29543, 29778, 30006, 30225, 30436, 30639, 30832, 31018, 31194, 31361,
    31520, 31669, 31809, 31940, 32062, 32174, 32276, 32369, 32452, 32526, 32590, 32644, 32688, 32723, 32747, 32762,
    32767, 32762, 32747, 32723, 32688, 32644, 32590, 32526, 32452, 32369, 32276, 32174, 32062, 31940, 31809, 31669,
    31520, 31361, 31194, 31018, 30832, 30639, 30436, 30225, 30006, 29778, 29543, 29299, 29048, 28789, 28523, 28249,
    27968, 27681, 27386, 27085, 26777, 26463, 26143, 25817, 25486, 25149, 24806, 24459, 24107, 23750, 23388, 23023,
    22653, 22280, 21903, 21523, 21139, 20753, 20364, 19973, 19580, 19184, 18787, 18389, 17989, 17589, 17187, 16786,
    16384, 15981, 15580, 15178, 14778, 14378, 13980, 13583, 13187, 12794, 12403, 12014, 11628, 11244, 10864, 10487,
    10114, 9744, 9379, 9017, 8660, 8308, 7961, 7618, 7281, 6950, 6624, 6304, 5990, 5682, 5381, 5086,
    4799, 4518, 4244, 3978, 3719, 3468, 3224, 2989, 2761, 2542, 2331, 2128, 1935, 1749, 1573, 1406,
    1247, 1098, 958, 827, 705, 593, 491, 398, 315, 241, 177, 123, 79, 44, 20, 5,
    0,
};
/* grain level by overlap: 1 / sqrt(n), Q15 */
static const int16_t GP_NORM[GP_NG + 1] = {32767, 32767, 23170, 18918, 16384, 14654, 13377, 12385, 11585, 10922,
                                           10362, 9880, 9459, 9088, 8757, 8460, 8192};
typedef struct {
    uint32_t pos;                /* forward: the next source sample to fetch; reverse: the index of a */
    int32_t frac;                /* Q16 between a and b */
    uint32_t step;               /* Q16 source samples an output sample */
    uint32_t wph, winc, left;    /* window phase (one turn = the grain), output samples to go */
    int32_t a, b, gain;
    const int16_t *src;          /* the buffer it reads: source A's or B's (MORPH) */
#if GP_XFADE
    const int16_t *src2;         /* xfade: the other one, mixed in per fetch (the comparison, not the design) */
    int32_t mix;                 /* xfade: B's share, Q15 */
#endif
    uint8_t owner, rev;
} gp_grain_t;
typedef struct {
    gp_grain_t g[GP_NG];
    const int16_t *src[2];       /* the part's two buffers, A and B (GS_N samples each) */
    int32_t root16[2];
    int32_t rng;
} gp_part_t;
/* the grain knobs; morph 0 .. 127: the share of grains that read B (0 all A, 127 all B); xfade 1: instead every
 * grain reads both buffers at its position and mixes them by morph (a plain crossfade, for the comparison) */
typedef struct { int32_t size, dens, pos_q8, sprd, rnd, pitch, cut, res, morph, xfade; } gp_par_t;
typedef struct {
    uint8_t active;
    int32_t note16, next;        /* the note; output samples to the next grain */
    int32_t macc;                /* MORPH's share spread evenly over the voice's grains (a Bresenham count) */
    tsvf_t f;
    int32_t ic1, ic2;
} gp_voice_t;

static inline uint32_t gp_scale(uint32_t n, uint32_t f16) { return (n >> 16) * f16 + (((n & 0xFFFFu) * f16) >> 16); }
static inline int32_t gp_win(uint32_t wph)
{
    uint32_t wi = wph >> 24;
    return GP_HANN[wi] + (((GP_HANN[wi + 1u] - GP_HANN[wi]) * (int32_t)((wph >> 9) & 0x7FFFu)) >> 15);
}

GS_API void gp_init(gp_part_t *P, const int16_t *srca, int32_t roota, const int16_t *srcb, int32_t rootb)
{
    uint32_t i;
    for (i = 0; i < GP_NG; i++)
        P->g[i].owner = 0;
    P->src[0] = srca;
    P->src[1] = srcb;
    P->root16[0] = roota;
    P->root16[1] = rootb;
    P->rng = 0x2545F491;
}

/* start a grain of voice v (vi) with the interval iv between grains (for the overlap's level). MORPH: the voice's
 * grains read A or B in the share morph / 127, spread evenly (0.5: A B A B ..), so the ends are pure and the
 * middle is a granular blend; the grain's rate is against its own source's root */
GS_API void gp_spawn(gp_part_t *P, const gp_par_t *p, uint32_t vi, gp_voice_t *v, uint32_t iv)
{
    gp_grain_t *g = 0;
    uint32_t i, n = GS_N, len, step, span, c, sp, r1 = noise32(&P->rng), r2 = noise32(&P->rng), ov, rev, which;
    int32_t d16, amt, pos, note16 = v->note16;
    for (i = 0; i < GP_NG && !g; i++)
        if (!P->g[i].owner)
            g = &P->g[i];
    if (!g)
        return;
    v->macc += p->morph;
    which = v->macc >= 127 ? 1u : 0u;
    if (which)
        v->macc -= 127;
#if GP_XFADE
    if (p->xfade)
        which = 0;
#endif
    len = (441u * pow2_q16(p->size * 8)) >> 16;              /* 10 .. 393 ms */
    amt = p->rnd * p->rnd * 192 / (127 * 127);
    d16 = note16 - P->root16[which] + p->pitch * 16 + (((int32_t)(r1 & 0xFFFFu) - 32768) * amt >> 15);
    step = (pow2_q16(clamp(d16, -1536, 576)) >> 8) * (GP_RATE_Q16 >> 8);
    step = step > GP_STEP_MAX ? GP_STEP_MAX : step < 256u ? 256u : step;
    rev = ((r1 >> 16) & 255u) < (uint32_t)p->rnd;            /* RAND 127: half of them */
    span = (len * (step >> 4)) >> 12;
    if (span + 3u > n) {
        span = n - 3u;
        len = (span << 8) / (step >> 8);
    }
    if (len < 16u)
        return;
    c = gp_scale(n - 1u, (uint32_t)clamp(p->pos_q8, 0, 127 << 8) * 65535u / (127u << 8));
    sp = gp_scale(n, (uint32_t)p->sprd * 516u);
    pos = (int32_t)c + (int32_t)gp_scale(sp, r2 & 0xFFFFu) - (int32_t)(sp >> 1);
    pos = rev ? clamp(pos, (int32_t)span, (int32_t)n - 2) : clamp(pos, 0, (int32_t)(n - 3u - span));
    g->step = step;
    g->frac = 0;
    g->wph = 0;
    g->winc = 0xFFFFFFFFu / len;
    g->left = len;
    g->rev = (uint8_t)rev;
    ov = len / (iv ? iv : 1u);
    g->gain = GP_NORM[ov < 1u ? 1u : ov > GP_NG ? GP_NG : ov];
    g->owner = (uint8_t)(vi + 1u);
    g->src = P->src[which];
#if GP_XFADE
    g->src2 = p->xfade ? P->src[1] : 0;
    g->mix = p->morph * 32767 / 127;
#endif
    if (rev) {
        g->pos = (uint32_t)pos;
        g->a = g->src[pos];
        g->b = g->src[pos + 1];
    } else {
        g->a = g->src[pos];
        g->b = g->src[pos + 1];
        g->pos = (uint32_t)pos + 2u;
    }
#if GP_XFADE
    if (g->src2) {
        g->a += ((g->src2[pos] - g->a) * g->mix) >> 15;
        g->b += ((g->src2[pos + 1] - g->b) * g->mix) >> 15;
    }
#endif
}

/* one grain into acc over n samples; returns 0 when it ended. The window (times the grain's level) ramps linearly
 * over the block between its exact values at the block's ends (eng_grain.c gr_run) */
GS_API int gp_run(gp_part_t *P, gp_grain_t *g, int32_t *acc, uint32_t n)
{
    uint32_t i, m = n < g->left ? n : g->left, step = g->step, pos = g->pos;
    int32_t a = g->a, b = g->b, frac = g->frac, w0, w1, wq, dq;
    const int16_t *src = g->src;
#if GP_XFADE
    if (g->src2) {                                           /* the crossfade comparison: both buffers a fetch */
        const int16_t *s2 = g->src2;
        int32_t mix = g->mix;
        w0 = mulq15(gp_win(g->wph), g->gain);
        g->wph += g->winc * m;
        g->left -= m;
        w1 = g->left ? mulq15(gp_win(g->wph), g->gain) : 0;
        wq = w0 * 1024;
        dq = m == CTL ? ((w1 - w0) * 1024) >> CTL_LOG2 : m ? (w1 - w0) * 1024 / (int32_t)m : 0;
        for (i = 0; i < m; i++) {
            int32_t s = a + (((b - a) * (frac >> 1)) >> 15);
            acc[i] += (s * (wq >> 10)) >> 15;
            wq += dq;
            if (!g->rev) {
                frac += (int32_t)step;
                while (frac >= 65536) {
                    frac -= 65536;
                    a = b;
                    b = pos < GS_N ? src[pos] + (((s2[pos] - src[pos]) * mix) >> 15) : a;
                    pos++;
                }
            } else {
                frac -= (int32_t)step;
                while (frac < 0) {
                    frac += 65536;
                    b = a;
                    if (pos) {
                        pos--;
                        a = src[pos] + (((s2[pos] - src[pos]) * mix) >> 15);
                    }
                }
            }
        }
        g->pos = pos;
        g->a = a;
        g->b = b;
        g->frac = frac;
        if (!g->left)
            g->owner = 0;
        return g->left != 0;
    }
#endif
    w0 = mulq15(gp_win(g->wph), g->gain);
    g->wph += g->winc * m;
    g->left -= m;
    w1 = g->left ? mulq15(gp_win(g->wph), g->gain) : 0;
    wq = w0 * 1024;
    dq = m == CTL ? ((w1 - w0) * 1024) >> CTL_LOG2 : m ? (w1 - w0) * 1024 / (int32_t)m : 0;
    if (!g->rev) {
        for (i = 0; i < m; i++) {
            int32_t s = a + (((b - a) * (frac >> 1)) >> 15);
            acc[i] += (s * (wq >> 10)) >> 15;
            wq += dq;
            frac += (int32_t)step;
            while (frac >= 65536) {
                frac -= 65536;
                a = b;
                b = pos < GS_N ? src[pos++] : a;
            }
        }
    } else {
        for (i = 0; i < m; i++) {
            int32_t s = a + (((b - a) * (frac >> 1)) >> 15);
            acc[i] += (s * (wq >> 10)) >> 15;
            wq += dq;
            frac -= (int32_t)step;
            while (frac < 0) {
                frac += 65536;
                b = a;
                if (pos)
                    a = src[--pos];
            }
        }
    }
    g->pos = pos;
    g->a = a;
    g->b = b;
    g->frac = frac;
    if (!g->left)
        g->owner = 0;
    return g->left != 0;
}

/* a voice's block: its grains, the next grain (DENS a second, SPRD jitters the interval), its filter. nact: the
 * part's sounding voices (the pool's share). out: CTL samples, Q15-ish (the caller applies the envelope) */
GS_API void gp_voice_block(gp_part_t *P, const gp_par_t *p, gp_voice_t *v, uint32_t vi, uint32_t nact, int32_t *out)
{
    int32_t acc[CTL];
    uint32_t i, mine = 0, iv, share;
    for (i = 0; i < CTL; i++)
        acc[i] = 0;
    for (i = 0; i < GP_NG; i++)
        if (P->g[i].owner == vi + 1u)
            mine += (uint32_t)gp_run(P, &P->g[i], acc, CTL);
    iv = (44100u * pow2_q16(-p->dens * 10)) >> 16;           /* 1 .. 98 a second */
    v->next -= CTL;
    if (v->next <= 0) {
        uint32_t r = noise32(&P->rng);
        int32_t j = (((int32_t)(r & 0xFFFFu) - 32768) * p->sprd) / 254;
        v->next += (int32_t)iv + mulq15((int32_t)iv, j);
        if (v->next < 0)
            v->next = 0;
        share = nact > 1u ? (GP_NG / nact > 2u ? GP_NG / nact : 2u) : GP_NG;
        if (mine < share)
            gp_spawn(P, p, vi, v, iv);
    }
    tsvf_coef(&v->f, p->cut << 8, p->res);
    for (i = 0; i < CTL; i++)
        out[i] = tsvf_lp(&v->f, clamp(acc[i], -65535, 65535), &v->ic1, &v->ic2);
}

/* a voice's grains are let go (a fresh voice) */
GS_API void gp_voice_on(gp_part_t *P, gp_voice_t *v, uint32_t vi, int32_t note16)
{
    uint32_t i;
    for (i = 0; i < GP_NG; i++)
        if (P->g[i].owner == vi + 1u)
            P->g[i].owner = 0;
    v->active = 1;
    v->note16 = note16;
    v->next = 0;
    v->macc = 63;                                            /* the first grain of a half-way morph is A's, the second B's */
    v->ic1 = v->ic2 = 0;
}

#if !GS_UNIT
/* ================================================================================== the sketches === */
static void wav_write(const char *path, const int16_t *s, uint32_t n, uint32_t rate)
{
    FILE *f = fopen(path, "wb");
    uint32_t v;
    uint16_t a = 1, ch = 1, ba = 2, bits = 16;
    if (!f) {
        perror(path);
        exit(1);
    }
    fwrite("RIFF", 1, 4, f); v = 36 + n * 2; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    fwrite(&a, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); v = rate * 2; fwrite(&v, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); v = n * 2; fwrite(&v, 4, 1, f);
    fwrite(s, 2, n, f);
    fclose(f);
}

static const struct { const char *name; uint8_t tone, motion, seed; } SRC_SET[3] = {
    {"mid", 64, 64, 0}, {"dark-slow", 28, 20, 3}, {"bright-fast", 104, 104, 11}};
static const struct { const char *name; gp_par_t p; int scan; const char *what; } GRAN_SET[3] = {
    {"pad", {92, 88, 50 << 8, 40, 14, 0, 100, 20}, 1,
     "SIZE 92 (160 ms) DENS 88 POS 50 % wandering +-20 % SPRD 40 RAND 14 CUT 100 RES 20: a cloud (Felucca's CLOUD PAD)"},
    {"glitch", {24, 112, 64 << 8, 100, 90, 0, 127, 0}, 0,
     "SIZE 24 (20 ms) DENS 112 POS 64 % SPRD 100 RAND 90 (detune +-oct, half reversed) CUT open: Felucca's GLITCH"},
    {"frozen", {108, 72, 10 << 8, 0, 10, 0, 92, 10}, 2,
     "SIZE 108 (250 ms) DENS 72 SPRD 0 RAND 10 CUT 92: POS scans 10 -> 90 % over the three seconds (a frozen read)"}};
static const uint8_t CHORD[4] = {48, 55, 60, 64};             /* C3 G3 C4 E4 */
#define SCENE_S 3.0
#define OFF_S 2.2
#define MSCENE_S 4.0             /* the morph scenes: longer */
#define MOFF_S 3.4
enum { M_STATIC, M_SWEEP, M_ENV };   /* MORPH over the scene: the knob as set; 0 -> 127 over the hold; 127 -> 0 in 0.6 s */

/* the granulated scene: a 4-note chord held off_s seconds on the buffers A and B, len_s seconds rendered at
 * 44.1 kHz, normalized to -2 dBFS; returns the peak before normalizing (Q15 of one voice's scale) */
static int32_t scene(const int16_t *A, int32_t rootA, const int16_t *B, int32_t rootB, const gp_par_t *par, int scan,
                     int mmode, double len_s, double off_s, const char *path)
{
    static gp_part_t P;
    gp_voice_t v[4];
    gp_par_t p = *par;
    uint32_t nb = (uint32_t)(len_s * FS / CTL), b, i, k, offb = (uint32_t)(off_s * FS / CTL), nact = 4;
    double *mix = calloc(nb * CTL, sizeof *mix), pk = 1;
    int16_t *out = malloc(nb * CTL * sizeof *out);
    int32_t env[4], tmp[CTL];
    gp_init(&P, A, rootA, B, rootB);
    memset(v, 0, sizeof v);
    for (k = 0; k < 4; k++) {
        gp_voice_on(&P, &v[k], k, CHORD[k] * 16);
        env[k] = 0;
    }
    for (b = 0; b < nb; b++) {
        double t = (double)b * CTL / FS;
        if (scan == 1)
            p.pos_q8 = par->pos_q8 + (int32_t)(25 * 256 * sin(2 * M_PI * 0.3 * t));
        else if (scan == 2)
            p.pos_q8 = (10 << 8) + (int32_t)((80 << 8) * t / len_s);
        if (mmode == M_SWEEP)
            p.morph = t < off_s ? (int32_t)(127 * t / off_s) : 127;
        else if (mmode == M_ENV)
            p.morph = t < 0.6 ? 127 - (int32_t)(127 * t / 0.6) : 0;
        if (b == offb)
            nact = 0;
        for (k = 0; k < 4; k++) {
            int32_t target = b < offb ? 32767 : 0, rate = b < offb ? 32767 * CTL / (FS * 30 / 1000) : 32767 * CTL / (FS * 400 / 1000);
            if (!v[k].active)
                continue;
            env[k] += env[k] < target ? (target - env[k] < rate ? target - env[k] : rate) : -(env[k] - target < rate ? env[k] - target : rate);
            gp_voice_block(&P, &p, &v[k], k, b < offb ? 4 : 4, tmp);
            for (i = 0; i < CTL; i++)
                mix[b * CTL + i] += (double)tmp[i] * env[k] / 32768.0;
            if (b >= offb && env[k] == 0) {              /* released: its grains fade out on their own */
                uint32_t left = 0;
                for (i = 0; i < GP_NG; i++)
                    left |= P.g[i].owner == k + 1u;
                if (!left)
                    v[k].active = 0;
            }
        }
    }
    for (i = 0; i < nb * CTL; i++)
        pk = fabs(mix[i]) > pk ? fabs(mix[i]) : pk;
    for (i = 0; i < nb * CTL; i++)
        out[i] = (int16_t)(mix[i] / pk * 32767 * 0.79);
    wav_write(path, out, nb * CTL, FS);
    free(mix);
    free(out);
    return (int32_t)pk;
}

static double rms_of(const int16_t *s, uint32_t n)
{
    double q = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        q += (double)s[i] * s[i];
    return sqrt(q / n) / 32768.0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/grain-sketch";
    int src_only = argc > 2 && !strcmp(argv[2], "src");
    static int16_t buf[GS_N], tour[8 * (GS_N + 2205)];
    char path[512], cmd[600];
    FILE *idx;
    uint32_t ty, k, s, i;
    gs_t g;
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", dir);
    if (system(cmd))
        return 1;
    snprintf(path, sizeof path, "%s/INDEX.md", dir);
    idx = fopen(path, "w");
    if (!idx) {
        perror(path);
        return 1;
    }
    fprintf(idx, "# GRAIN sketches\n\nRendered by `tools/grain_sketch.c` (docs/GRAIN.md). The source buffers are %u samples at %u Hz "
                 "(%.0f ms), what the engine would hold in POOL; the granulated scenes are 44.1 kHz, a C3 G3 C4 E4 chord held "
                 "%.1f s, %.0f s in all.\n\n", GS_N, GS_RATE, 1000.0 * GS_N / GS_RATE, OFF_S, SCENE_S);
    fprintf(idx, "## Files\n\n- `src-<family>-<setting>.wav`: the buffer as rendered (the material the grains read).\n"
                 "- `seeds-<family>.wav`: the same family at TONE 64 MOTION 64, SEED 0 .. 7 in a row, 100 ms apart: what SEED gives.\n"
                 "- `gran-<family>-<setting>-<grains>.wav`: the buffer granulated.\n"
                 "- `morph-<A>-<B>-<how>.wav`: two buffers under one voice, MORPH between them (below).\n\n");
    fprintf(idx, "## The source settings\n\n| setting | TONE | MOTION | SEED |\n| --- | --- | --- | --- |\n");
    for (s = 0; s < 3; s++)
        fprintf(idx, "| %s | %u | %u | %u |\n", SRC_SET[s].name, SRC_SET[s].tone, SRC_SET[s].motion, SRC_SET[s].seed);
    fprintf(idx, "\n## The grain settings\n\n| grains | what |\n| --- | --- |\n");
    for (s = 0; s < 3; s++)
        fprintf(idx, "| %s | %s |\n", GRAN_SET[s].name, GRAN_SET[s].what);
    fprintf(idx, "\n## The families\n\n| family | TONE | MOTION | setting | peak before normalizing | RMS after | render ms (host) |\n"
                 "| --- | --- | --- | --- | --- | --- | --- |\n");
    for (ty = 0; ty < GS_NTYPE; ty++) {
        static const char *const WHAT[GS_NTYPE][2] = {
            {"the burst's and the string's brightness", "the decay; above 64 a strum of re-plucks"},
            {"the filter's cutoff", "the detune and the sweep's rate"},
            {"the FM index (soft .. clangy)", "the index's decay; above 64 a beating second pair"},
            {"the band-pass's centre", "the wander's depth, the tremolo's rate and depth"},
            {"the sweep's span", "chirps a buffer (1 .. 16)"},
            {"the resonance and the sweep's top", "sweeps a buffer (1 .. 8)"},
            {"the starting vowel (A E I O U round)", "how far it travels, the vibrato"},
            {"the partials' spread and the strike", "the decay (shimmer .. clang)"}};
        char fam[16];
        for (k = 0; GS_TYPE_NAMES[ty][k]; k++)
            fam[k] = (char)(GS_TYPE_NAMES[ty][k] | 32);
        fam[k] = 0;
        for (s = 0; s < 3; s++) {
            clock_t t0 = clock();
            int32_t gain, pk = 1;
            gs_begin(&g, ty, SRC_SET[s].tone, SRC_SET[s].motion, SRC_SET[s].seed);
            while (gs_step(&g, buf, 512u) < GS_N)         /* in slices, as the firmware would */
                ;
            for (i = 0; i < GS_N; i++)
                pk = abs(buf[i]) > pk ? abs(buf[i]) : pk;
            gain = gs_finish(buf);
            snprintf(path, sizeof path, "%s/src-%s-%s.wav", dir, fam, SRC_SET[s].name);
            wav_write(path, buf, GS_N, GS_RATE);
            fprintf(idx, "| %s | %s | %s | %s | %.2f (gain x%.2f) | %.3f | %.1f |\n", s ? "" : GS_TYPE_NAMES[ty],
                    s ? "" : WHAT[ty][0], s ? "" : WHAT[ty][1], SRC_SET[s].name, pk / 32767.0, gain / 4096.0,
                    rms_of(buf, GS_N), 1000.0 * (clock() - t0) / CLOCKS_PER_SEC);
            printf("%-7s %-12s peak %.2f gain x%.2f rms %.3f root16 %d\n", GS_TYPE_NAMES[ty], SRC_SET[s].name,
                   pk / 32767.0, gain / 4096.0, rms_of(buf, GS_N), g.root16);
            fflush(stdout);
            if (src_only)
                continue;
            for (k = 0; k < 3; k++) {
                snprintf(path, sizeof path, "%s/gran-%s-%s-%s.wav", dir, fam, SRC_SET[s].name, GRAN_SET[k].name);
                scene(buf, g.root16, buf, g.root16, &GRAN_SET[k].p, GRAN_SET[k].scan, M_STATIC, SCENE_S, OFF_S, path);
            }
        }
        for (s = 0; s < 8; s++) {                         /* the seed tour */
            gs_begin(&g, ty, 64, 64, s);
            while (gs_step(&g, buf, GS_N) < GS_N)
                ;
            gs_finish(buf);
            memcpy(tour + s * (GS_N + 2205), buf, GS_N * sizeof *buf);
            memset(tour + s * (GS_N + 2205) + GS_N, 0, 2205 * sizeof *buf);
        }
        snprintf(path, sizeof path, "%s/seeds-%s.wav", dir, fam);
        wav_write(path, tour, 8 * (GS_N + 2205), GS_RATE);
    }
    if (!src_only) {
        /* two sources and MORPH (the owner, 2026-10-15: "generate two waves, each with their own settings, and then
         * there's a parameter to morph between them"): eight pairs, the pad grains, MORPH swept A -> B over the
         * hold, held half way, and falling B -> A in 0.6 s (what an envelope on MORPH does); for two pairs the
         * crossfade comparison (every grain mixes both buffers) beside the granular morph */
        static const struct { uint8_t ta, sa, tb, sb; } PAIRS[8] = {
            {GT_PLUCK, 0, GT_VOWEL, 0}, {GT_DRONE, 0, GT_BELL, 0}, {GT_BREATH, 0, GT_METAL, 0},
            {GT_CHIRP, 1, GT_SWEEP, 0}, {GT_BELL, 1, GT_BELL, 2}, {GT_VOWEL, 1, GT_BREATH, 1},
            {GT_METAL, 1, GT_DRONE, 1}, {GT_SWEEP, 2, GT_PLUCK, 2}};
        static const char *const SHORT[3] = {"", ".dark", ".bright"};
        static const char *const MNAME[3] = {"half", "sweep", "env"};
        static int16_t bufb[GS_N];
        gs_t gb;
        fprintf(idx, "\n## Two sources, MORPH\n\n`morph-<A>-<B>-<how>.wav`: the pad grains over two buffers; each grain reads A or "
                     "B, the share set by MORPH and spread evenly over a voice's grains (0.5: A B A B ..). `half`: MORPH 64 held; "
                     "`sweep`: 0 -> 127 over the %.1f s hold; `env`: 127 -> 0 in the first 0.6 s (an envelope on MORPH: B's attack "
                     "into A's body). `-xfade`: the comparison, every grain mixing both buffers at its position by MORPH (a plain "
                     "crossfade; costs a second fetch a sample).\n\n| A | B | files |\n| --- | --- | --- |\n", MOFF_S);
        for (k = 0; k < 8; k++) {
            char fa[24], fb[24];
            uint32_t m;
            gs_begin(&g, PAIRS[k].ta, SRC_SET[PAIRS[k].sa].tone, SRC_SET[PAIRS[k].sa].motion, SRC_SET[PAIRS[k].sa].seed);
            while (gs_step(&g, buf, GS_N) < GS_N)
                ;
            gs_finish(buf);
            gs_begin(&gb, PAIRS[k].tb, SRC_SET[PAIRS[k].sb].tone, SRC_SET[PAIRS[k].sb].motion, SRC_SET[PAIRS[k].sb].seed);
            while (gs_step(&gb, bufb, GS_N) < GS_N)
                ;
            gs_finish(bufb);
            for (i = 0; GS_TYPE_NAMES[PAIRS[k].ta][i]; i++)
                fa[i] = (char)(GS_TYPE_NAMES[PAIRS[k].ta][i] | 32);
            fa[i] = 0;
            strcat(fa, SHORT[PAIRS[k].sa]);
            for (i = 0; GS_TYPE_NAMES[PAIRS[k].tb][i]; i++)
                fb[i] = (char)(GS_TYPE_NAMES[PAIRS[k].tb][i] | 32);
            fb[i] = 0;
            strcat(fb, SHORT[PAIRS[k].sb]);
            for (m = 0; m < 3; m++) {
                gp_par_t pp = GRAN_SET[0].p;
                pp.morph = 64;
                snprintf(path, sizeof path, "%s/morph-%s-%s-%s.wav", dir, fa, fb, MNAME[m]);
                scene(buf, g.root16, bufb, gb.root16, &pp, GRAN_SET[0].scan, (int)m, MSCENE_S, MOFF_S, path);
            }
            if (k < 2) {
                gp_par_t pp = GRAN_SET[0].p;
                pp.xfade = 1;
                snprintf(path, sizeof path, "%s/morph-%s-%s-sweep-xfade.wav", dir, fa, fb);
                scene(buf, g.root16, bufb, gb.root16, &pp, GRAN_SET[0].scan, M_SWEEP, MSCENE_S, MOFF_S, path);
            }
            fprintf(idx, "| %s %s | %s %s | `morph-%s-%s-{half,sweep,env}.wav`%s |\n", GS_TYPE_NAMES[PAIRS[k].ta],
                    SRC_SET[PAIRS[k].sa].name, GS_TYPE_NAMES[PAIRS[k].tb], SRC_SET[PAIRS[k].sb].name, fa, fb,
                    k < 2 ? ", `-sweep-xfade.wav`" : "");
            printf("morph %s -> %s\n", fa, fb);
        }
    }
    fprintf(idx, "\n## Listening order\n\n1. `seeds-*.wav`, one a family: is the family worth having, does SEED give variety.\n"
                 "2. `src-*-mid.wav` against `-dark-slow` and `-bright-fast`: do TONE and MOTION move it where expected.\n"
                 "3. `gran-*-pad.wav`: the sound of the engine as a pad; then `-frozen` (the scan through the material) and "
                 "`-glitch`.\n4. `morph-*-sweep.wav`: one material becoming another; `-env` for what an envelope on MORPH gives; "
                 "`-sweep-xfade` against `-sweep` for the two ways to blend.\n\nThe granulated scenes have no reverb, no delay and a plain 30 ms / 400 ms envelope: the unit adds "
                 "its effects and the sound's ADSR.\n");
    fclose(idx);
    printf("wrote %s/INDEX.md\n", dir);
    return 0;
}
#endif
