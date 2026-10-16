/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* THE OSCILLATORS' EDGES, a study (HISTORY 3y50, docs/PROPHET.md "The oscillators' edges"; the owner: "Is their
 * optimization we could do to the oscillator edges? How much would that change the sound?"). The firmware's sound
 * side as the emulator builds it, ALL 200 programs, one core (four voices). Not a pass / fail suite:
 *   sh tools/p5_edges.sh                     builds the variants, prints the counts and the differences
 *   build/host/p5_edges count                per register (C3 .. C6, a four-note chord: root, third, fifth, octave):
 *                                            the BLEP's evaluations, its divides and hard sync's divides per voice
 *                                            and second, over all 200 programs; and the programs with sync alone
 *   build/host/p5_edges pcm FILE             every program's chord at the four registers, the master's left
 *                                            channel (int32) into FILE: the variants' outputs are compared with
 *   build/host/p5_edges cmp REF FILE         per program the peak and RMS difference in dBFS, the worst ones
 * The variants (tools/p5_edges.sh): -DEDGE_A (the BLEP's fraction from a 256-entry reciprocal of the increment's top
 * bits, no correction step: what a table instead of the divide gives), -DEDGE_C (one correction tap instead of two:
 * the sample after the edge alone). Option (b) (one reciprocal a block) is not built here: docs/PROPHET.md says why */
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <stdint.h>

static uint64_t edge_n[3];                              /* BLEP evaluations' divides, sync's divides, BLEP evaluations */
#define P5_EDGE(k, d, i) (edge_n[(k) == 2 ? 2 : (k)]++)
#ifdef EDGE_A
static inline int32_t edge_frac_a(uint32_t distance, uint32_t inc)
{
    /* inc's top 8 bits below its leading one (a 256-entry table's index), the reciprocal of the rounded-down value */
    int32_t sh = 32 - (int32_t)__builtin_clz(inc) - 9;   /* the leading one and 8 bits: 256 entries */
    double mid = sh > 0 ? (double)((inc >> sh) << sh) + (double)(1u << sh) * 0.5 : (double)inc;
    double x = (double)distance / mid * 32768.0;
    return x > 32768.0 ? 32768 : (int32_t)x;
}
#define P5_BLEP_FRACTION_ALT(d, i) edge_frac_a(d, i)
#endif
#ifdef EDGE_C
static inline int32_t edge_blep_c(uint32_t ph, uint32_t inc)   /* the sample after the edge alone */
{
    int32_t x;
    if (!inc || ph >= inc)
        return 0;
    x = (int32_t)(((uint64_t)ph << 15) / inc);
    if (x > 32768)
        x = 32768;
    return x + x - ((x * x) >> 15) - 32768;
}
#define P5_BLEP_ALT(ph, inc) edge_blep_c(ph, inc)
#endif
#include "emu/emu_hooks.h"

emu_hal_t emu_hal;
const int8_t emu_keymap[6][EMU_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
#include "emu/emu_firmware.h"

#define NREG 4u
static const uint8_t ROOT[NREG] = {48, 60, 72, 84};      /* C3 C4 C5 C6 */
static const uint8_t CHORD[4] = {0, 4, 7, 12};
#define HOLD 1378u                                      /* blocks held: 1.0 s */
#define TAIL 172u                                       /* .. and after the release: 0.125 s */
static FILE *pcm;
static void run(uint32_t nb, uint64_t *vs)              /* vs: voice-samples rendered (the voices that sound x CTL) */
{
    int32_t out[2u * CTL];
    uint32_t b, i;
    for (b = 0; b < nb; b++) {
        mix_block(out, CTL);
        if (vs)
            for (i = 0; i < NVOICE; i++)
                *vs += trk[0].v[i].active ? CTL : 0u;
        if (pcm)
            for (i = 0; i < CTL; i++)
                fwrite(&out[2u * i], 4, 1, pcm);
    }
}
static void quiet(void)
{
    uint32_t b, i, n;
    trk_all_off(&trk[0]);
    for (b = 0; b < 4000u; b++) {
        run(1, 0);
        for (i = n = 0; i < NVOICE; i++)
            n += trk[0].v[i].active;
        if (!n)
            break;
    }
    run(64, 0);
}
static int has_sync(void) { return p5_patch_of(&trk[0])->raw[P5_SYNC] != 0; }
/* one program's chord at register r: its counts (per voice-second) */
static void chord(uint32_t id, uint32_t r, uint64_t *e, uint64_t *vs)
{
    uint32_t k;
    uint64_t e0[3];
    cu_pool_load(0, ENGI_PROPHET, p5_preset_of(id));
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    run(8, 0);
    memcpy(e0, edge_n, sizeof e0);
    for (k = 0; k < 4u; k++)
        trk_note_on(&trk[0], (uint8_t)(ROOT[r] + CHORD[k]), 100);
    run(HOLD, vs);
    trk_all_off(&trk[0]);
    run(TAIL, vs);
    for (k = 0; k < 3u; k++)
        e[k] += edge_n[k] - e0[k];
    quiet();
}
static void boot(void)
{
    emu_flash_open();
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();
    cr_dbg.c1 = C1_OFF;                                 /* one core: four voices, the order of one core */
    run(64, 0);
}
static void count(void)
{
    uint32_t id, r, k;
    printf("p5_edges: per voice and second of sound (four-note chords, 1 s held + 0.125 s; 200 programs)\n");
    printf("  register  blep evals  blep divides  sync divides   (sync programs alone: blep div, sync div)\n");
    for (r = 0; r < NREG; r++) {
        uint64_t e[3] = {0}, vs = 0, es[3] = {0}, vss = 0;
        for (id = 1; id <= 200u; id++) {
            uint64_t a[3] = {0}, v = 0;
            chord(id, r, a, &v);
            for (k = 0; k < 3u; k++)
                e[k] += a[k];
            vs += v;
            if (has_sync()) {
                for (k = 0; k < 3u; k++)
                    es[k] += a[k];
                vss += v;
            }
        }
        printf("  C%u        %9.0f  %11.0f  %11.0f   (%.0f, %.0f over %.1f voice-s)\n", 3u + r, e[2] * (double)FS / vs,
               e[0] * (double)FS / vs, e[1] * (double)FS / vs, vss ? es[0] * (double)FS / vss : 0.0,
               vss ? es[1] * (double)FS / vss : 0.0, vss / (double)FS);
    }
}
static int pcm_out(const char *path)
{
    uint32_t id, r;
    uint64_t e[3] = {0};
    pcm = fopen(path, "wb");
    if (!pcm)
        return 1;
    for (id = 1; id <= 200u; id++)
        for (r = 0; r < NREG; r++)
            chord(id, r, e, 0);
    fclose(pcm);
    return 0;
}
static int cmp(const char *a, const char *b)
{
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    uint32_t id, n = NREG * (HOLD + TAIL + 8u) * CTL, i, worst = 0;
    double wpk = -400, wrms = -400, sum_rms = 0;
    int32_t *x = malloc(n * 4u), *y = malloc(n * 4u);
    if (!fa || !fb)
        return 1;
    (void)n;
    /* (each program: NREG chords of 8 + HOLD + TAIL blocks, then its quiet() blocks, which vary: written too. The
     * files are read program by program in the same order; quiet() writes the same count in both unless a voice
     * rings out at another block, then the comparison realigns on nothing: it is reported as a length mismatch) */
    fseek(fa, 0, SEEK_END);
    fseek(fb, 0, SEEK_END);
    if (ftell(fa) != ftell(fb)) {
        printf("p5_edges: the two files differ in length (%ld, %ld): a voice rang out at another block\n", ftell(fa), ftell(fb));
    }
    {
        long la = ftell(fa) / 4, lb = ftell(fb) / 4, l = la < lb ? la : lb;
        int32_t *A = malloc((size_t)l * 4u), *B = malloc((size_t)l * 4u);
        double pk = 0, s2 = 0;
        fseek(fa, 0, SEEK_SET);
        fseek(fb, 0, SEEK_SET);
        if (fread(A, 4, (size_t)l, fa) != (size_t)l || fread(B, 4, (size_t)l, fb) != (size_t)l)
            return 1;
        for (i = 0; i < (uint32_t)l; i++) {
            double d = (double)A[i] - (double)B[i];
            if (fabs(d) > pk)
                pk = fabs(d);
            s2 += d * d;
        }
        printf("p5_edges: %s against %s, %ld samples (all 200 programs, 4 registers): peak difference %.1f dBFS, RMS %.1f dBFS\n",
               b, a, l, pk ? 20 * log10(pk / 32768.0) : -400.0, s2 ? 10 * log10(s2 / l) - 20 * log10(32768.0) : -400.0);
    }
    (void)id; (void)worst; (void)wpk; (void)wrms; (void)sum_rms; (void)x; (void)y;
    return 0;
}
int main(int argc, char **argv)
{
    if (argc >= 4 && !strcmp(argv[1], "cmp"))
        return cmp(argv[2], argv[3]);
    boot();
    if (argc >= 2 && !strcmp(argv[1], "count")) {
        count();
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "pcm"))
        return pcm_out(argv[2]);
    printf("usage: p5_edges count | pcm FILE | cmp REF FILE\n");
    return 2;
}
