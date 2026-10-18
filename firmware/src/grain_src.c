/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* GRAIN's source generator (docs/GRAIN.md section 1): four knobs render the material a GRAIN sound's grains read
 * into a buffer of GS_N samples at GS_RATE, deterministic (the same knobs render the same bytes on the host and the
 * unit: tests/cr_grain_test.c's goldens), in slices (gs_begin, gs_step n samples at a time from the state it keeps,
 * gs_finish once the buffer is whole; or gs_step_to into a slice's scratch and the finish's two passes a slice at a
 * time, gs_measure then gs_apply: the engine's render in place, the same bytes), from dsp.c's blocks and
 * felucca_tables.h. Included by the engine (eng_grain.c,
 * step 2), by tools/grain_sketch.c (the sketches, the same code heard) and by the test. GS_API / GS_FAM: static here;
 * tools/grain_cost.py's device compile makes them extern and the families noinline to read each one's listing. */
#ifndef GS_API
#define GS_API static
#endif
#ifndef GS_FAM
#define GS_FAM static
#endif
/* ======================================================================= the source generator (gs_) === */
#define GS_RATE 22050u           /* the buffer's rate: half the output rate (the delay and the reverb run there too) */
#ifndef GS_N
#define GS_N 16384u              /* samples a buffer: 743 ms, 32 KB (docs/GRAIN.md "The buffer") */
#endif
#define GS_ROOT16 960            /* C4: the pitch the material is rendered at; the player repitches against it */
#define GS_PEAK 28000            /* gs_finish normalizes the buffer to this peak */
#define GS_NTYPE 8
enum { GT_PLUCK, GT_DRONE, GT_BELL, GT_BREATH, GT_CHIRP, GT_SWEEP, GT_VOWEL, GT_METAL };
static const char *const GS_TYPE_NAMES[GS_NTYPE] = {"PLUCK", "DRONE", "BELL", "BREATH", "CHIRP", "SWEEP", "VOWEL", "METAL"};

/* a phase increment at the buffer's rate (pitch_inc is 44.1 kHz's: twice it) */
static inline uint32_t gs_inc(int32_t p16) { return pitch_inc((uint32_t)clamp(p16, 0, 2047)) << 1; }
/* the SVF's band-pass output (dsp.c has the low-pass; eng_va.c takes v1 the same way) */
static inline int32_t gs_svf_bp(const tsvf_t *c, int32_t in, int32_t *ic1, int32_t *ic2)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    return v1;
}
/* exp decay: x -= x * k >> 16 once a control tick (32 samples) gives the time constant tau_ms */
static inline int32_t gs_kctl(uint32_t tau_ms) { return (int32_t)(65536u * 32u * 1000u / (GS_RATE * (tau_ms ? tau_ms : 1u)) / 1u); }
/* a 32-bit phase increment for a rate in hundredths of a hertz */
static inline uint32_t gs_lfo_inc(uint32_t hz_x100) { return hz_x100 * 1948u; }   /* 2^32 / 22050 / 100 = 1947.8 */
static inline int16_t gs_clip(int32_t y) { return (int16_t)clamp(y, -32767, 32767); }

typedef struct {
    uint8_t type, tone, motion, seed;
    uint32_t pos;                /* samples rendered so far */
    int32_t root16;              /* the pitch the buffer holds (PLUCK's integer period moves it off GS_ROOT16) */
    int32_t rng;
    uint32_t ph[8], inc[8];      /* oscillators */
    int32_t a[8];                /* their levels / indices (decaying) */
    int32_t s[12];               /* the family's other state */
    tsvf_t f[3];
    int32_t ic[6];
} gs_t;

static inline uint32_t gs_rnd(gs_t *g) { return noise32(&g->rng); }
static inline int32_t gs_r15(gs_t *g) { return (int32_t)(gs_rnd(g) >> 17); }          /* 0..32767 */
static inline int32_t gs_white(gs_t *g) { return gs_r15(g) * 2 - 32767; }             /* -32767..32767 */

/* ---- PLUCK: a Karplus-Strong string, the buffer its own delay line. TONE: the burst's and the loop's brightness.
 * MOTION: the decay (long ring .. damped), above 64 re-plucks (a strum of up to five) */
#define GS_PLUCK_N 84u           /* the period: 22050 / 84 = 262.5 Hz */
#define GS_PLUCK_ROOT16 961      /* .. is C4 + 6 ct */
static void gs_pluck_begin(gs_t *g)
{
    g->root16 = GS_PLUCK_ROOT16;
    g->s[0] = 1500 + g->tone * 240;                         /* the burst's low-pass, Q15 */
    g->s[1] = 65536 - 30 - g->motion * 7;                   /* the loop's decay, Q16 */
    g->s[2] = 16384 - g->tone * 100;                        /* the loop's second tap: 1/2 (dull) .. 1/9 (bright) */
    g->s[3] = g->motion > 64 ? (int32_t)(GS_N / (1u + ((uint32_t)g->motion - 64u) / 14u)) : 0;   /* re-pluck period */
    g->s[4] = GS_PLUCK_N;                                    /* burst samples to go */
    g->s[5] = 0;                                             /* the burst's low-pass */
    g->s[6] = g->s[3];                                       /* the next re-pluck */
    g->s[7] = g->s[8] = 0;                                   /* the burst's DC blocker (a low-passed noise burst carries an
                                                              * offset, and the string's loop would hold it) */
}
GS_FAM void gs_pluck(gs_t *g, int16_t *out, uint32_t i0, uint32_t i1)
{
    int32_t k = g->s[0], dec = g->s[1], b = g->s[2], a = 32768 - b, lp = g->s[5], x1 = g->s[7], hp = g->s[8];
    uint32_t i;
    for (i = i0; i < i1; i++) {
        int32_t y = 0;
        if (i >= GS_PLUCK_N + 1u)                           /* (rounded to nearest: a floored shift's bias would
                                                              * integrate round the loop into an offset) */
            y = (((out[(int32_t)(i - i0) - (int32_t)GS_PLUCK_N] * a + out[(int32_t)(i - i0) - (int32_t)GS_PLUCK_N - 1] * b
                   + 16384) >> 15) * dec + 32768) >> 16;
        if (g->s[4] > 0) {
            lp += ((gs_white(g) - lp) * k) >> 15;
            hp = lp - x1 + ((hp * 32112) >> 15);             /* one pole at ~70 Hz */
            x1 = lp;
            y += hp >> 1;
            g->s[4]--;
        }
        if (g->s[3] && (int32_t)i == g->s[6]) {
            g->s[4] = GS_PLUCK_N;
            g->s[6] += g->s[3];
        }
        out[i - i0] = gs_clip(y);
    }
    g->s[5] = lp;
    g->s[7] = x1;
    g->s[8] = hp;
}

/* ---- DRONE: three detuned saws (the third a pulse on odd seeds) and a sub sine through the SVF, its cutoff swept
 * by a triangle LFO. TONE: the cutoff. MOTION: the detune and the sweep's rate */
static void gs_drone_begin(gs_t *g)
{
    uint32_t inc0 = gs_inc(GS_ROOT16), d = 80u + (uint32_t)g->motion * 12u;    /* Q16: 0.12 .. 2.4 % */
    uint32_t i;
    g->inc[0] = inc0;
    g->inc[1] = inc0 + (uint32_t)mulq16((int32_t)inc0, d);
    g->inc[2] = inc0 - (uint32_t)mulq16((int32_t)inc0, d);
    g->inc[3] = inc0 >> 1;
    for (i = 0; i < 4; i++)
        g->ph[i] = gs_rnd(g);
    g->s[0] = (g->seed & 1u) ? (int32_t)(0x20000000u + ((uint32_t)gs_r15(g) << 13)) : 0;   /* the pulse's width */
    g->ph[4] = gs_rnd(g);                                    /* the LFO */
    g->inc[4] = gs_lfo_inc(25u + (uint32_t)g->motion * 3u);  /* 0.25 .. 4.06 Hz */
    g->s[1] = (40 << 8) + (g->tone * 70 << 8) / 127;        /* the cutoff's centre, Q8 index 40 .. 110 */
    g->s[2] = ((16 + g->motion / 4) << 8);                   /* the sweep's depth */
    g->s[3] = 40 + ((g->seed >> 1) & 3u) * 20;               /* resonance */
}
GS_FAM void gs_drone(gs_t *g, int16_t *out, uint32_t i0, uint32_t i1)
{
    uint32_t i, pw = (uint32_t)g->s[0];
    for (i = i0; i < i1; i++) {
        int32_t x, y;
        if (!(i & 31u)) {
            int32_t cut = g->s[1] + ((osc_tri(g->ph[4]) * g->s[2]) >> 15);
            tsvf_coef(&g->f[0], cut, g->s[3]);
            g->ph[4] += g->inc[4] * 32u;
        }
        x = osc_saw(g->ph[0], g->inc[0]) + osc_saw(g->ph[1], g->inc[1]);
        x += pw ? osc_pulse(g->ph[2], g->inc[2], pw) : osc_saw(g->ph[2], g->inc[2]);
        x += (sine_i(g->ph[3]) * 13107) >> 15;
        g->ph[0] += g->inc[0];
        g->ph[1] += g->inc[1];
        g->ph[2] += g->inc[2];
        g->ph[3] += g->inc[3];
        y = tsvf_lp(&g->f[0], x >> 2, &g->ic[0], &g->ic[1]);
        out[i - i0] = gs_clip(soft_knee(y, 24000));
    }
}

/* ---- BELL: two-operator FM, the modulator at an inharmonic ratio (by SEED), the index decaying. TONE: the index.
 * MOTION: the index's decay (slow .. a tick), above 64 a second detuned pair beats against the first */
static const uint16_t GS_BELL_RATIO[12] = {361, 512, 707, 888, 1075, 1329, 1792, 768, 443, 597, 128, 384};   /* Q8 */
static void gs_bell_begin(gs_t *g)
{
    uint32_t inc0 = gs_inc(GS_ROOT16), r = GS_BELL_RATIO[g->seed % 12u], tau;
    g->inc[0] = inc0;
    g->inc[1] = (inc0 >> 8) * r;
    g->ph[1] = gs_rnd(g) & 0x3FFFFFFFu;
    g->a[0] = ((100 + g->tone * 16) * 20861) >> 8;           /* the index: 0.4 .. 8.3 rad, as (sine >> 3) x this */
    tau = 600u - (uint32_t)g->motion * 560u / 127u;          /* 600 .. 40 ms */
    g->s[0] = gs_kctl(tau);
    g->a[1] = 32767;
    g->s[1] = gs_kctl(450);
    g->s[2] = g->motion > 64;
    if (g->s[2]) {
        g->inc[2] = inc0 + (inc0 >> 12) * ((uint32_t)g->motion - 64u) / 2u;   /* up to 0.8 %: a 2 .. 4 Hz beat */
        g->inc[3] = (g->inc[2] >> 8) * r;
        g->ph[3] = gs_rnd(g) & 0x3FFFFFFFu;
    }
}
GS_FAM void gs_bell(gs_t *g, int16_t *out, uint32_t i0, uint32_t i1)
{
    uint32_t i;
    for (i = i0; i < i1; i++) {
        int32_t y, amp;
        if (!(i & 31u)) {
            g->a[0] -= (g->a[0] * g->s[0]) >> 16;
            if (i >= 64u)
                g->a[1] -= (g->a[1] * g->s[1]) >> 16;
        }
        amp = i < 64u ? (int32_t)i * 512 : g->a[1];
        y = sine_i(g->ph[0] + (uint32_t)((sine_i(g->ph[1]) >> 3) * g->a[0]));
        if (g->s[2]) {
            y += sine_i(g->ph[2] + (uint32_t)((sine_i(g->ph[3]) >> 3) * g->a[0])) >> 1;
            g->ph[2] += g->inc[2];
            g->ph[3] += g->inc[3];
            y = (y * 21845) >> 15;
        }
        g->ph[0] += g->inc[0];
        g->ph[1] += g->inc[1];
        out[i - i0] = gs_clip((y * amp) >> 15);
    }
}

/* ---- BREATH: white noise through a resonant band-pass whose centre wanders (a random walk) under a tremolo.
 * TONE: the centre. MOTION: the walk's depth and the tremolo's rate and depth */
static void gs_breath_begin(gs_t *g)
{
    g->s[0] = (45 << 8) + (g->tone * 63 << 8) / 127;        /* the centre, Q8 index 45 .. 108 */
    g->s[1] = g->s[0];                                       /* the cutoff now */
    g->s[2] = g->s[0];                                       /* the walk's target */
    g->s[3] = (g->motion * 24 / 127) << 8;                   /* the walk's depth */
    g->s[4] = 60 + ((g->seed >> 2) & 3u) * 15;               /* resonance */
    g->inc[0] = gs_lfo_inc(30u + (uint32_t)g->motion * 5u);  /* the tremolo: 0.3 .. 6.6 Hz */
    g->ph[0] = gs_rnd(g);
    g->a[0] = g->motion * 32767 / 254;                       /* its depth: up to a half */
    g->a[1] = 32767;
}
GS_FAM void gs_breath(gs_t *g, int16_t *out, uint32_t i0, uint32_t i1)
{
    uint32_t i;
    for (i = i0; i < i1; i++) {
        if (!(i & 31u)) {
            if (!(i & 1023u))
                g->s[2] = g->s[0] + (((gs_r15(g) - 16384) * g->s[3]) >> 14);
            g->s[1] += (g->s[2] - g->s[1]) >> 4;
            tsvf_coef(&g->f[0], g->s[1], g->s[4]);
            g->a[1] = 32767 - ((g->a[0] * (32767 - osc_tri(g->ph[0]))) >> 16);
            g->ph[0] += g->inc[0] * 32u;
        }
        out[i - i0] = gs_clip((gs_svf_bp(&g->f[0], gs_white(g) >> 1, &g->ic[0], &g->ic[1]) * g->a[1]) >> 15);
    }
}

/* ---- CHIRP: a sine sweeping over a span of semitones, repeated; a second one a fifth up on some seeds. TONE: the
 * span (half an octave .. nearly five). MOTION: how many chirps fill the buffer (1 .. 16) */
static void gs_chirp_begin(gs_t *g)
{
    int32_t span = 16 * (6 + g->tone * 51 / 127), c = 1 + g->motion / 8;
    g->s[0] = (int32_t)(GS_N / (uint32_t)c);                 /* a chirp's length */
    g->s[1] = (int32_t)((uint32_t)span * 65536u / (uint32_t)g->s[0]);   /* the pitch's step a sample, Q16 */
    g->s[2] = (GS_ROOT16 - span / 2) << 16;                  /* the start */
    g->s[3] = span << 16;
    g->s[4] = (g->seed & 1u) ? -1 : 1;                       /* down / up */
    g->s[5] = (g->seed >> 1) & 1u;                           /* alternate */
    g->s[6] = (g->seed >> 2) & 1u;                           /* the second voice */
    g->s[7] = 0;                                             /* the sample within the chirp */
    g->s[8] = g->s[4] > 0 ? g->s[2] : g->s[2] + g->s[3];     /* the pitch now, Q16 */
}
GS_FAM void gs_chirp(gs_t *g, int16_t *out, uint32_t i0, uint32_t i1)
{
    uint32_t i;
    for (i = i0; i < i1; i++) {
        int32_t k = g->s[7], gain, y, p;
        uint32_t inc;
        if (k >= g->s[0]) {
            k = g->s[7] = 0;
            if (g->s[5])
                g->s[4] = -g->s[4];
            g->s[8] = g->s[4] > 0 ? g->s[2] : g->s[2] + g->s[3];
        }
        gain = k < 64 ? k * 512 : g->s[0] - 1 - k < 64 ? (g->s[0] - 1 - k) * 512 : 32767;
        p = g->s[8] >> 16;
        inc = gs_inc(p);
        g->ph[0] += inc;
        y = sine_i(g->ph[0]);
        if (g->s[6]) {
            g->ph[1] += gs_inc(p + 112);
            y = ((y + (sine_i(g->ph[1]) >> 1)) * 21845) >> 15;
        }
        g->s[8] += g->s[4] * g->s[1];
        g->s[7] = k + 1;
        out[i - i0] = gs_clip((y * gain) >> 15);
    }
}

/* ---- SWEEP: a saw and a pulse (an octave down on some seeds), driven, through the SVF with its cutoff sweeping
 * down, up or both. TONE: the resonance and the sweep's top. MOTION: how many sweeps fill the buffer (1 .. 8) */
static void gs_sweep_begin(gs_t *g)
{
    uint32_t inc0 = gs_inc(GS_ROOT16);
    g->inc[0] = inc0;
    g->inc[1] = (g->seed & 4u) ? inc0 >> 1 : inc0;
    g->ph[0] = gs_rnd(g);
    g->ph[1] = gs_rnd(g);
    g->s[0] = (int32_t)(0x80000000u - (g->seed & 3u) * 0x10000000u);   /* the pulse's width: 50 .. 12.5 % */
    g->s[1] = 30 + g->tone * 89 / 127;                       /* resonance 30 .. 119 */
    g->s[2] = 20 << 8;                                       /* the cutoff's floor */
    g->s[3] = ((70 + g->tone * 42 / 127) << 8) - g->s[2];    /* .. and its span to the top (70 .. 112) */
    g->s[4] = (int32_t)(GS_N / (1u + (uint32_t)g->motion * 7u / 127u));   /* a sweep's length */
    g->s[5] = (g->seed >> 3) & 3u;                           /* 0 down, 1 up, 2 up then down, 3 down then up */
    g->s[6] = 0;
}
GS_FAM void gs_sweep(gs_t *g, int16_t *out, uint32_t i0, uint32_t i1)
{
    uint32_t i, pw = (uint32_t)g->s[0];
    for (i = i0; i < i1; i++) {
        int32_t x, y;
        if (!(i & 31u)) {
            int32_t k = g->s[6], L = g->s[4], f;             /* the sweep's position 0 .. 65535 */
            f = (int32_t)(((uint32_t)k << 16) / (uint32_t)L);
            if (g->s[5] >= 2)
                f = f < 32768 ? f * 2 : (65535 - f) * 2;
            if (g->s[5] == 0 || g->s[5] == 3)
                f = 65535 - f;
            tsvf_coef(&g->f[0], g->s[2] + ((g->s[3] * f) >> 16), g->s[1]);
            g->s[6] = k + 32 >= L ? 0 : k + 32;
        }
        x = (osc_saw(g->ph[0], g->inc[0]) + osc_pulse(g->ph[1], g->inc[1], pw)) >> 1;
        g->ph[0] += g->inc[0];
        g->ph[1] += g->inc[1];
        x = softclip(x + (x >> 1));
        y = tsvf_lp(&g->f[0], x, &g->ic[0], &g->ic[1]);
        out[i - i0] = gs_clip(soft_knee(y, 20000));
    }
}

/* ---- VOWEL: a narrow pulse at the root with vibrato through three band-passes at a vowel's formants, the vowel
 * travelling round A E I O U. TONE: where on the cycle it starts. MOTION: how far it travels (none .. two
 * rounds) and the vibrato's depth */
static const uint16_t GS_VOWEL[5][3] = {                     /* F1 F2 F3 as SVF cutoff indices, Q8 */
    {16512, 18432, 22528},                                   /* A  730 1090 2440 Hz */
    {14848, 21248, 22630},                                   /* E  530 1840 2480 */
    {11264, 22272, 23680},                                   /* I  270 2290 3010 */
    {15104, 17152, 22528},                                   /* O  570  840 2410 */
    {11776, 17280, 22170},                                   /* U  300  870 2240 */
};
static void gs_vowel_begin(gs_t *g)
{
    g->s[0] = (int32_t)(((uint32_t)g->tone * 5u * 65536u) / 128u + ((uint32_t)g->seed % 5u) * 65536u);   /* position */
    g->s[1] = (int32_t)(((uint32_t)g->motion * 65536u / 127u) * 320u / GS_N);   /* .. its step a control tick */
    g->s[2] = 2 + g->motion / 8;                             /* vibrato depth, 1/16 st */
    g->inc[1] = gs_lfo_inc(450u + ((uint32_t)g->seed & 7u) * 30u);   /* 4.5 .. 6.6 Hz */
    g->ph[1] = gs_rnd(g);
    g->inc[0] = gs_inc(GS_ROOT16);
}
GS_FAM void gs_vowel(gs_t *g, int16_t *out, uint32_t i0, uint32_t i1)
{
    uint32_t i;
    for (i = i0; i < i1; i++) {
        int32_t x, y;
        if (!(i & 31u)) {
            uint32_t vp = (uint32_t)g->s[0] % (5u << 16), vi = vp >> 16, vj = (vi + 1u) % 5u, k;
            int32_t fr = (int32_t)(vp & 0xFFFFu) >> 8;
            for (k = 0; k < 3; k++)
                tsvf_coef(&g->f[k], GS_VOWEL[vi][k] + (((int32_t)GS_VOWEL[vj][k] - GS_VOWEL[vi][k]) * fr >> 8), 100);
            g->s[0] = (int32_t)((vp + (uint32_t)g->s[1]) % (5u << 16));
            g->inc[0] = gs_inc(GS_ROOT16 + ((osc_tri(g->ph[1]) * g->s[2]) >> 15));
            g->ph[1] += g->inc[1] * 32u;
        }
        x = osc_pulse(g->ph[0], g->inc[0], 0x1999999Au) >> 1;
        g->ph[0] += g->inc[0];
        y = gs_svf_bp(&g->f[0], x, &g->ic[0], &g->ic[1]);
        y += (gs_svf_bp(&g->f[1], x, &g->ic[2], &g->ic[3]) * 19661) >> 15;
        y += (gs_svf_bp(&g->f[2], x, &g->ic[4], &g->ic[5]) * 9830) >> 15;
        out[i - i0] = gs_clip(soft_knee(y, 24000));
    }
}

/* ---- METAL: six sine partials at inharmonic ratios (by SEED), each decaying at its own rate, a noise strike.
 * TONE: how far the partials spread (clustered gong .. bright spray) and the strike. MOTION: the decay */
static void gs_metal_begin(gs_t *g)
{
    uint32_t inc0 = gs_inc(GS_ROOT16), k, spread = 384u + (uint32_t)g->tone * 13u;   /* Q8: 1.5 .. 8 */
    uint32_t tau0 = 900u - (uint32_t)g->motion * 820u / 127u;                        /* 900 .. 80 ms */
    for (k = 0; k < 6; k++) {
        uint32_t r = k ? 256u + (((uint32_t)gs_r15(g) * spread) >> 15) : 256u;
        g->inc[k] = (inc0 >> 8) * r;
        g->ph[k] = gs_rnd(g);
        g->a[k] = (int32_t)((12000u + ((uint32_t)gs_r15(g) >> 3)) / (k + 1u));
        g->s[k] = gs_kctl(tau0 * 10u / (10u + 4u * k));
    }
    g->s[6] = 90;                                            /* the strike's samples */
    g->s[7] = g->tone * 64;                                  /* .. and level */
}
GS_FAM void gs_metal(gs_t *g, int16_t *out, uint32_t i0, uint32_t i1)
{
    uint32_t i, k;
    for (i = i0; i < i1; i++) {
        int32_t y = 0;
        if (!(i & 31u))
            for (k = 0; k < 6; k++)
                g->a[k] -= (g->a[k] * g->s[k]) >> 16;
        for (k = 0; k < 6; k++) {
            y += (sine_i(g->ph[k]) * g->a[k]) >> 15;
            g->ph[k] += g->inc[k];
        }
        if (g->s[6] > 0) {
            y += (gs_white(g) * g->s[7]) >> 14;
            g->s[6]--;
        }
        out[i - i0] = gs_clip(y);
    }
}

/* ---- the API the firmware would have */
GS_API void gs_begin(gs_t *g, uint32_t type, uint32_t tone, uint32_t motion, uint32_t seed)
{
    uint32_t i;
    for (i = 0; i < sizeof *g / 4u; i++)
        ((uint32_t *)g)[i] = 0;
    g->type = (uint8_t)(type % GS_NTYPE);
    g->tone = (uint8_t)(tone & 127u);
    g->motion = (uint8_t)(motion & 127u);
    g->seed = (uint8_t)(seed & 127u);
    g->root16 = GS_ROOT16;
    g->rng = (int32_t)(0x9E3779B9u ^ ((seed & 127u) * 0x01000193u + g->type * 0x61C88647u + 1u));
    switch (g->type) {
    case GT_PLUCK: gs_pluck_begin(g); break;
    case GT_DRONE: gs_drone_begin(g); break;
    case GT_BELL: gs_bell_begin(g); break;
    case GT_BREATH: gs_breath_begin(g); break;
    case GT_CHIRP: gs_chirp_begin(g); break;
    case GT_SWEEP: gs_sweep_begin(g); break;
    case GT_VOWEL: gs_vowel_begin(g); break;
    default: gs_metal_begin(g); break;
    }
}
/* the next n samples (at most up to GS_N) into out[0 ..], the family's own samples raw (before the finish); out[-1 ..
 * -GS_HIST] must hold the GS_HIST raw samples before them (PLUCK's string reads its own last 85: a slice renders into
 * a scratch with the last slice's tail ahead of it, eng_grain.c grain_slice); returns the samples rendered so far */
#define GS_HIST 128u
GS_API uint32_t gs_step_to(gs_t *g, int16_t *out, uint32_t n)
{
    uint32_t i0 = g->pos, i1 = i0 + n;
    if (i1 > GS_N)
        i1 = GS_N;
    if (i1 <= i0)
        return g->pos;
    switch (g->type) {
    case GT_PLUCK: gs_pluck(g, out, i0, i1); break;
    case GT_DRONE: gs_drone(g, out, i0, i1); break;
    case GT_BELL: gs_bell(g, out, i0, i1); break;
    case GT_BREATH: gs_breath(g, out, i0, i1); break;
    case GT_CHIRP: gs_chirp(g, out, i0, i1); break;
    case GT_SWEEP: gs_sweep(g, out, i0, i1); break;
    case GT_VOWEL: gs_vowel(g, out, i0, i1); break;
    default: gs_metal(g, out, i0, i1); break;
    }
    g->pos = i1;
    return i1;
}
/* render the next n samples into buf (at most up to GS_N): buf[pos ..], the history is the buffer's own; returns the
 * samples rendered so far */
GS_API uint32_t gs_step(gs_t *g, int16_t *buf, uint32_t n)
{
    return gs_step_to(g, buf + (g->pos < GS_N ? g->pos : GS_N), n);
}
/* the finish, in two passes over the raw samples so that it can run a slice at a time (eng_grain.c's render in
 * place: a measuring pass that writes nothing, then the writing pass): a one-pole high-pass at ~20 Hz takes the DC
 * out (PLUCK's loop and SWEEP's pulse carry some), and the buffer is normalized to GS_PEAK (a gain of at most 8) */
typedef struct { int32_t x1, y, pk; } gs_hp_t;
static inline void gs_hp_begin(gs_hp_t *h) { h->x1 = 0; h->y = 0; h->pk = 1; }
static inline int32_t gs_hp(gs_hp_t *h, int32_t x)
{
    int32_t y = x - h->x1 + ((h->y * 32581 + 16384) >> 15);   /* (rounded: a floored shift settles at -88) */
    h->x1 = x;
    h->y = y;
    return y;
}
/* the measuring pass over n raw samples: the high-passed peak */
GS_API void gs_measure(gs_hp_t *h, const int16_t *x, uint32_t n)
{
    uint32_t i;
    int32_t pk = h->pk;
    for (i = 0; i < n; i++) {
        int32_t y = gs_hp(h, x[i]), a = y < 0 ? -y : y;
        pk = a > pk ? a : pk;
    }
    h->pk = pk;
}
/* the gain the measured peak gives, Q12 */
GS_API int32_t gs_gain(const gs_hp_t *h)
{
    int32_t pk = h->pk > 32767 ? 32767 : h->pk, gain = GS_PEAK * 4096 / pk;
    return gain > 8 * 4096 ? 8 * 4096 : gain;
}
/* the writing pass: n raw samples x, high-passed and at the gain, into dst (dst may be x) */
GS_API void gs_apply(gs_hp_t *h, const int16_t *x, int16_t *dst, uint32_t n, int32_t gain)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        dst[i] = gs_clip((gs_clip(gs_hp(h, x[i])) * gain) >> 12);
}
/* the whole buffer rendered: both passes over it in place; returns the gain, Q12 */
GS_API int32_t gs_finish(int16_t *buf)
{
    gs_hp_t h;
    int32_t gain;
    gs_hp_begin(&h);
    gs_measure(&h, buf, GS_N);
    gain = gs_gain(&h);
    gs_hp_begin(&h);
    gs_apply(&h, buf, buf, GS_N, gain);
    return gain;
}
