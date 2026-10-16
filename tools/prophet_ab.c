/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* PROPHET, one engine file against another on the host (tools/prophet_listen.sh; docs/PROPHET.md "Where it comes
 * from"): the engine alone on core.h and dsp.c with a minimal voice driver (voice.c's control tick for an ownenv
 * engine: block, done, render, post), a fixed chord progression on one factory program, a 16-bit stereo WAV at a
 * fixed gain (no effects, no master: the engine's own samples).
 *   cc -O2 -w -Ibuild/gen -Ifirmware/src -DENGINE_FILE='"eng_prophet_test.c"' -o build/host/prophet_ab tools/prophet_ab.c -lm
 *   build/host/prophet_ab PROGRAM OUT.wav        (PROGRAM 1..200)
 * ENGINE_FILE: this tree's firmware/src/eng_prophet_test.c, or the one of Rosen's remix (Choralroot-remix-step20-
 * timing/firmware/src/eng_prophet_test.c: Melodee's b351a14 with the remix's stage 36), which brings its own
 * prophet_patch.c and its factory header (the 200 programs as 138-byte records) from its directory; the tables are this
 * tree's build/gen/felucca_tables.h for both (the same values: tools/prophet_listen.sh compares them with the
 * remix's choralroot_prophet_tables.h). -DAB_STAT: no WAV, the digest and the level of the render instead. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define __attribute__(x)
#define FELUCCA_VA 1
#define FELUCCA_CZ 1
#define FELUCCA_QUAD 1
#define FELUCCA_PROPHET 1
#include "felucca_tables.h"
#include "core.h"
#include "dsp.c"
#define P5_OVERSAMPLE 1                              /* (the remix has it in its tables' header) */
#include ENGINE_FILE
static p5_part_t ab_part;
static p5_part_t *p5_part(uint32_t part) { (void)part; return &ab_part; }

static track_t *T = &trk[0];
static void note_on(uint32_t i, uint32_t note, uint32_t vel)
{
    voice_t *v = &T->v[i];
    voice_was = v->active ? 2 : 0;
    v->note = (uint8_t)note;
    v->vel = v->mvel = (uint8_t)vel;
    v->gate = v->active = 1;
    v->stage = 1;
    v->age = i + 1u;
    v->pitch16 = v->pitch_cur = (int32_t)note * 16;
    ENG_P5_TEST.note_on(T, v);
}
static void tick(int32_t *out)
{
    uint32_t i, nr = 0;
    memset(out, 0, CTL * sizeof *out);
    ENG_P5_TEST.block(T);
    for (i = 0; i < P5_POLY; i++) {
        voice_t *v = &T->v[i];
        vmod_t m;
        if (!v->active)
            continue;
        if (ENG_P5_TEST.done(T, v)) {
            v->active = v->gate = 0;
            continue;
        }
        memset(&m, 0, sizeof m);
        m.pitch16 = v->pitch16;
        m.inc = pitch_inc((uint32_t)m.pitch16);
        m.amp0 = m.amp1 = 32767;
        m.shape = 64 << 8;
        ENG_P5_TEST.render(T, v, out, CTL, &m);
        nr++;
    }
    if (ENG_P5_TEST.post)
        ENG_P5_TEST.post(T, out, CTL, nr);
}

int main(int argc, char **argv)
{
    static const uint8_t CH[5][4] = {{50, 62, 66, 69}, {47, 62, 66, 71}, {43, 59, 62, 67}, {45, 61, 64, 69}, {50, 62, 66, 69}};
    static int16_t pcm[2 * FS * 16];
    int32_t out[CTL];
    uint32_t prog = argc > 1 ? (uint32_t)atoi(argv[1]) : 1u, c, k, b, i, n = 0;
    uint64_t h = 1469598103934665603ull;
    double s2 = 0;
    int32_t pk = 0;
    FILE *f;
    if (argc < 3 || prog < 1u || prog > 200u) {
        fprintf(stderr, "usage: prophet_ab PROGRAM(1..200) OUT.wav\n");
        return 2;
    }
    memset(T, 0, sizeof *T);
    T->p[P_VOICE] = V_POLY;
    T->preset = (uint8_t)prog;
    P5_DEEP.blob_preset(T, prog);                      /* the factory program (both files: preset k is program k) */
    for (c = 0; c < 5u; c++) {
        uint32_t hold = (c == 4u ? 5u : 3u) * FS / (2u * CTL), gap = FS / (8u * CTL);
        for (k = 0; k < 4u; k++)
            note_on(k, CH[c][k], 100);
        for (b = 0; b < hold + gap + (c == 4u ? 3u * FS / CTL : 0u); b++) {
            if (b == hold)
                for (k = 0; k < 4u; k++)
                    T->v[k].gate = 0;
            tick(out);
            for (i = 0; i < CTL && n < NELEM(pcm) / 2u; i++, n++) {
                int32_t x = out[i] / 6;                    /* four voices of 2 x VOICE_FS at most: 32000 */
                pcm[2 * n] = pcm[2 * n + 1] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
                h = (h ^ (uint32_t)out[i]) * 1099511628211ull;
                s2 += (double)out[i] * out[i];
                pk = abs(out[i]) > pk ? abs(out[i]) : pk;
            }
        }
    }
    printf("program %u: %u samples, digest %016llx, peak %d, rms %.0f\n", (unsigned)prog, (unsigned)n,
           (unsigned long long)h, (int)pk, sqrt(s2 / (n ? n : 1)));
    f = fopen(argv[2], "wb");
    if (!f)
        return 1;
    {
        uint32_t bytes = n * 4u, v32;
        uint16_t v16;
        fwrite("RIFF", 1, 4, f); v32 = 36u + bytes; fwrite(&v32, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
        v32 = 16; fwrite(&v32, 4, 1, f); v16 = 1; fwrite(&v16, 2, 1, f); v16 = 2; fwrite(&v16, 2, 1, f);
        v32 = FS; fwrite(&v32, 4, 1, f); v32 = FS * 4u; fwrite(&v32, 4, 1, f); v16 = 4; fwrite(&v16, 2, 1, f);
        v16 = 16; fwrite(&v16, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
        fwrite(pcm, 4, n, f);
    }
    fclose(f);
    return 0;
}
