/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* What the VA (eng_va.c) and GRAIN (eng_grain.c) share, in every build whatever the engine flags (engines.c includes it
 * after dsp.c): the filter's type weights and its SVF step (FTYPE's LP BP HP NOTCH cycle), the LFO sync list and its
 * phase increments, the matrix amounts' fine rule (tenths below 6: va_amt_q, VA_AMX), the names of the filter
 * positions and the LFO waves, a xorshift. Moved out of eng_va.c as they were (3y137: GRAIN's second phase); nothing
 * here is an engine. */
#ifndef VA_COMMON_C
#define VA_COMMON_C
#define VA_NSYNC 42u             /* LFO SYNC since version 4: a synced RATE is an index into N_VA_SYNC (va_lfo_inc) */

#define VA_R3(x) x, x, x
#define VA_R4(x) x, x, x, x
#define VA_R7(x) VA_R4(x), VA_R3(x)
#define VA_R16(x) VA_R4(x), VA_R4(x), VA_R4(x), VA_R4(x)
#define VA_R17(x) VA_R16(x), x
#define VA_R24(x) VA_R16(x), VA_R4(x), VA_R4(x)
#define VA_R31(x) VA_R24(x), VA_R7(x)

/* FTYPE: the four types at 0 32 64 96, "A>B" between them (127 runs back toward LP) */
static const char *const N_VA_FTYPE128[129] = {"LP", VA_R31("LP>BP"), "BP", VA_R31("BP>HP"), "HP", VA_R31("HP>NT"),
                                               "NOTCH", VA_R31("NT>LP"), 0};

static const char *const N_VA_LWAVE[] = {"SIN", "TRI", "SAW", "SQR", "S&H"};

/* a synced LFO's RATE (version 4, the owner 2026-10-14): slowest first, so turning the knob up is faster, as the free
 * rate: 8 4 3 2 1 bars, 1/2 .. 1/256, each dotted (D, 3/2), straight, triplet (T, 2/3); past 256T free periods in
 * ms. At most 5 characters (params.c F_ENUM): 1/128's and 1/256's T and D drop the "1/" */
static const char *const N_VA_SYNC[VA_NSYNC + 1u] = {"8BARD", "8BAR", "8BART", "4BARD", "4BAR", "4BART", "3BARD",
    "3BAR", "3BART", "2BARD", "2BAR", "2BART", "1BARD", "1BAR", "1BART", "1/2D", "1/2", "1/2T", "1/4D", "1/4", "1/4T",
    "1/8D", "1/8", "1/8T", "1/16D", "1/16", "1/16T", "1/32D", "1/32", "1/32T", "1/64D", "1/64", "1/64T", "128D",
    "1/128", "128T", "256D", "1/256", "256T", "4ms", "3ms", "2ms", 0};

/* the AMT column's values (hundredths) at or below v, at or above v: tenths between -6 and 6, whole numbers outside */
static int32_t va_amt_floor(int32_t v)
{
    int32_t g = v > -600 && v < 600 ? 10 : 100;
    return v >= 0 ? v / g * g : -((-v + g - 1) / g * g);
}
static int32_t va_amt_ceil(int32_t v) { return -va_amt_floor(-v); }
/* a turn of the AMT column from old to v: a value between two moves on from old to the next one its way (a slow
 * detent's 0.01: a tenth below 6, a whole above), a larger one stops at the last it passed (a quick detent's 6.35: 6,
 * the whole amounts' step of before) */
static int32_t va_amt_q(int32_t v, int32_t old)
{
    int32_t r = v = clamp(v, -6400, 6300);
    if (v > old && (r = va_amt_floor(v)) <= old)
        r = va_amt_ceil(old + 1);
    else if (v < old && (r = va_amt_ceil(v)) >= old)
        r = va_amt_floor(old - 1);
    return clamp(r, -6400, 6300);
}

/* LFO n's RATE with its SYNC on: the list (one item a detent: cr_pages.c kn_list; OPT snaps to the straight ones) */
static const param_desc_t VA_D_SRATE = {"RATE", F_ENUM, 0, VA_NSYNC - 1, 19, N_VA_SYNC, 0};

/* LFO SYNC: N_VA_SYNC's phase increments per control tick (CTL = 32 samples at 44.1 kHz). A division of n / d beats:
 * per BPM, Q8 (2^32 x 32 / (44100 x 60) / beats x 256 = 2^45 d / (2646000 n); the largest, 256T at 1/96 beat, 1.28e9;
 * the smallest, 8BARD at 48 beats, 277k: 1/277k at 1 BPM, exact to 4e-6); x BPM >> 8 in 64 bits (256T at 300 BPM:
 * 1.5e9, 480 Hz). Past 256T a free period of m ms (VA_MS: the increment itself, 2^32 x 32000 / (44100 m); 2 ms is
 * 1.56e9, 500 Hz: under the control rate's half, 689 Hz) */
#define VA_SQ8(n, d) (uint32_t)((35184372088832ull * (d) + 1323000ull * (n)) / (2646000ull * (n)))
#define VA_MS(m) (uint32_t)((137438953472000ull + 22050ull * (m)) / (44100ull * (m)))
#define VA_SB(b) VA_SQ8(6 * (b), 1), VA_SQ8(4 * (b), 1), VA_SQ8(8 * (b), 3)   /* b bars: dotted, straight, triplet */
#define VA_SN(n, d) VA_SQ8(3 * (n), 2 * (d)), VA_SQ8(n, d), VA_SQ8(2 * (n), 3 * (d))   /* a note of n / d beats */
static const uint32_t VA_SYNC_Q8[VA_NSYNC] = {VA_SB(8), VA_SB(4), VA_SB(3), VA_SB(2), VA_SB(1), VA_SN(2, 1),
    VA_SN(1, 1), VA_SN(1, 2), VA_SN(1, 4), VA_SN(1, 8), VA_SN(1, 16), VA_SN(1, 32), VA_SN(1, 64), VA_MS(4), VA_MS(3),
    VA_MS(2)};
#define VA_SYNC_MS0 39u          /* N_VA_SYNC's first free period */

static uint32_t va_xs(uint32_t s)                /* xorshift32 (S&H; its own: the shared rng is not touched) */
{
    s = s ? s : 0x6C8E9CF5u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

/* the filter's output as weights (Q10) of the input, the band-pass (kd v1) and the low-pass (v2): LP (0, 0, 1),
 * BP (0, 1, 0), HP (1, -1, -1), NOTCH (1, -1, 0) (exactly va_render's switch at 1024); FTYPE crossfades the weights
 * towards the next type of the cycle LP -> BP -> HP -> NOTCH -> LP (ff, Q10) */
typedef struct { int32_t x, b, l; } va_fw_t;
static const int8_t VA_FW[4][3] = {{0, 0, 1}, {0, 1, 0}, {1, -1, -1}, {1, -1, 0}};
static va_fw_t va_fweights(uint32_t ty, int32_t ff)
{
    const int8_t *a = VA_FW[ty & 3u], *b = VA_FW[(ty + 1u) & 3u];
    va_fw_t w;
    w.x = a[0] * 1024 + (b[0] - a[0]) * ff;      /* (x 1024, not << 10: a[k] may be -1; the same values) */
    w.b = a[1] * 1024 + (b[1] - a[1]) * ff;
    w.l = a[2] * 1024 + (b[2] - a[2]) * ff;
    return w;
}
/* one sample of the SVF c (state ic1, ic2) with the output weights w; the soft knee after it */
static inline int32_t va_svf(const tsvf_t *c, int32_t x, int32_t *ic1, int32_t *ic2, int32_t kd, va_fw_t w)
{
    int32_t v3 = x - *ic2, v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13, v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13), y;
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    y = (w.x * x + w.b * ((kd * v1) >> 12) + w.l * v2) >> 10;
    return soft_knee(clamp(y, -200000, 200000), 16000);
}

#define VA_AMX(x, mf, k) ((((mf) >> (k)) & 1u) ? ((x) * 205) >> 11 : (x))
#endif
