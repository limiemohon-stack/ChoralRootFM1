/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* quad_render: one FM TONE note (firmware/src/eng_quad.c, the firmware's own code on the host) to a raw float32 mono
 * file at 44.1 kHz, for tools/digitone/make_quad_times.py (the calibration through the Digitone measurement's proxies,
 * docs/QUAD.md "Times") and tools/digitone/test_quad_times.py.
 *
 *   quad_render OUT.f32 TOTAL_S ON_S OFF_S [bpm=N] [NAME=VALUE ...] [T:TABLE:INDEX=VALUE ...]
 *
 * The patch: the init patch (version 5 units) with NAME=VALUE (tests/quad_ref.py's names: ALGO, MIX, FTYPE, FREQ (in
 * hundredths), L0DEST ...). The note: C4 (60), velocity 100, on at ON_S (a control tick boundary), the gate off at OFF_S
 * (none if OFF_S >= TOTAL_S). T:TABLE:INDEX=VALUE writes one entry of quad_times.h's tables first (QUAD_T_AATK, ADEC,
 * FATK, FDEC, OATK, ODEC, FADE: the calibration's candidate times). The output: the voice's sum, / 32768.
 *   cc -std=c99 -O2 -w -DFELUCCA_QUAD=1 -Ibuild/gen -o build/host/quad_render tools/digitone/quad_render.c -lm */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define __attribute__(x)
#define QUAD_TIMES_CONST         /* (quad_times.h: the tables writable here) */
#ifndef FELUCCA_QUAD
#define FELUCCA_QUAD 1
#endif
#include "felucca_tables.h"
#include "../../firmware/src/core.h"
#include "../../firmware/src/dsp.c"
#include "../../firmware/src/eng_quad.c"

static const struct { const char *n; uint32_t i; } QN[] = {
    {"ALGO", QP_ALGO}, {"RC", QP_RC}, {"RA", QP_RA}, {"RB1", QP_RB1}, {"HARM", QP_HARM}, {"DTUN", QP_DTUN},
    {"FDBK", QP_FDBK}, {"MIX", QP_MIX}, {"OFSC", QP_OFSC}, {"OFSA", QP_OFSA}, {"OFSB1", QP_OFSB1}, {"OFSB2", QP_OFSB2},
    {"AATK", QP_AATK}, {"ADEC", QP_ADEC}, {"AEND", QP_AEND}, {"ALEV", QP_ALEV}, {"BATK", QP_BATK}, {"BDEC", QP_BDEC},
    {"BEND", QP_BEND}, {"BLEV", QP_BLEV}, {"ADLY", QP_ADLY}, {"ATRIG", QP_ATRIG}, {"ARST", QP_ARST}, {"PHRT", QP_PHRT},
    {"BDLY", QP_BDLY}, {"BTRIG", QP_BTRIG}, {"BRST", QP_BRST}, {"VEL", QP_VEL}, {"AKEY", QP_AKEY},
    {"B1KEY", QP_B1KEY}, {"B2KEY", QP_B2KEY}, {"FATK", QP_FATK}, {"FDEC", QP_FDEC}, {"FSUS", QP_FSUS},
    {"FREL", QP_FREL}, {"FREQ", QP_FREQ}, {"RESO", QP_RESO}, {"FTYPE", QP_FTYPE}, {"FDEPTH", QP_FDEPTH},
    {"FDLY", QP_FDLY}, {"FKTRK", QP_FKTRK}, {"BASE", QP_BASE}, {"WIDTH", QP_WIDTH}, {"EATK", QP_EATK},
    {"EDEC", QP_EDEC}, {"ESUS", QP_ESUS}, {"EREL", QP_EREL}, {"LEVEL", QP_LEVEL}, {"RB2", QP_RB2},
};
static const char *const LF[QL_N] = {"SPEED", "MULT", "FADE", "DEST", "WAVE", "PHASE", "TRIG", "DEPTH"};
static const struct { const char *n; uint32_t *t; uint32_t len; } TT[] = {
    {"AATK", QUAD_T_AATK, 128}, {"ADEC", QUAD_T_ADEC, 128}, {"FATK", QUAD_T_FATK, 128}, {"FDEC", QUAD_T_FDEC, 128},
    {"OATK", QUAD_T_OATK, 128}, {"ODEC", QUAD_T_ODEC, 128}, {"FADE", QUAD_T_FADE, 65},
};

static int32_t field(const char *n)
{
    uint32_t k, f;
    for (k = 0; k < NELEM(QN); k++)
        if (!strcmp(QN[k].n, n))
            return (int32_t)QN[k].i;
    if (n[0] == 'L' && n[1] >= '0' && n[1] <= '2')
        for (f = 0; f < QL_N; f++)
            if (!strcmp(n + 2, LF[f]))
                return (int32_t)QP_LFO((uint32_t)(n[1] - '0'), f);
    return -1;
}

int main(int argc, char **argv)
{
    track_t *t = &trk[0];
    voice_t *v = &t->v[0];
    int16_t p[QP_NP];
    int32_t out[CTL];
    float buf[CTL];
    double total, on, off;
    uint32_t n, nt, ton, toff, i;
    FILE *fo;
    int a;
    if (argc < 5) {
        fprintf(stderr, "usage: quad_render OUT.f32 TOTAL_S ON_S OFF_S [bpm=N] [NAME=VALUE ...] [T:TABLE:INDEX=VALUE ...]\n");
        return 2;
    }
    total = atof(argv[2]), on = atof(argv[3]), off = atof(argv[4]);
    memset(t, 0, sizeof *t);
    memset(quad_vs, 0, sizeof quad_vs);
    memset(quad_lfo, 0, sizeof quad_lfo);
    t->p[P_VOICE] = V_POLY;
    t->eng_req = t->engine = ENGI_QUAD;
    song.g[G_BPM] = 120;
    quad_init_patch(p);
    for (a = 5; a < argc; a++) {
        char name[32], *eq = strchr(argv[a], '=');
        int32_t i0;
        if (!eq || eq - argv[a] >= (long)sizeof name) {
            fprintf(stderr, "quad_render: %s: NAME=VALUE\n", argv[a]);
            return 2;
        }
        memcpy(name, argv[a], (size_t)(eq - argv[a]));
        name[eq - argv[a]] = 0;
        if (!strcmp(name, "bpm")) {
            song.g[G_BPM] = (int16_t)atoi(eq + 1);
        } else if (!strncmp(name, "T:", 2)) {
            char *c = strchr(name + 2, ':');
            uint32_t k, idx;
            if (!c) {
                fprintf(stderr, "quad_render: %s: T:TABLE:INDEX=VALUE\n", argv[a]);
                return 2;
            }
            *c = 0;
            idx = (uint32_t)atoi(c + 1);
            for (k = 0; k < NELEM(TT) && strcmp(TT[k].n, name + 2); k++)
                ;
            if (k == NELEM(TT) || idx >= TT[k].len) {
                fprintf(stderr, "quad_render: no table entry %s[%u]\n", name + 2, idx);
                return 2;
            }
            TT[k].t[idx] = (uint32_t)strtoul(eq + 1, 0, 10);
        } else if ((i0 = field(name)) >= 0) {
            p[i0] = quad_clampv((uint32_t)i0, atoi(eq + 1));
        } else {
            fprintf(stderr, "quad_render: no field %s\n", name);
            return 2;
        }
    }
    memcpy(quad_patch[0], p, sizeof p);
    quad_macros_out(t);
    if (!(fo = fopen(argv[1], "wb"))) {
        perror(argv[1]);
        return 1;
    }
    nt = (uint32_t)(total * FS / CTL + 0.5);
    ton = (uint32_t)(on * FS / CTL + 0.5);
    toff = (uint32_t)(off * FS / CTL + 0.5);
    for (n = 0; n < nt; n++) {
        if (n == ton) {
            v->note = 60;
            v->vel = v->mvel = 100;
            v->gate = v->active = 1;
            v->stage = 1;
            v->age = 1;
            v->pitch16 = v->pitch_cur = 60 * 16;
            v->env = v->env_out = 0;
            t->m_vi = 0;
            quad_note_on(t, v);
        }
        if (n == toff && n > ton)
            v->gate = 0;
        memset(out, 0, sizeof out);
        quad_block(t);
        if (v->active) {
            if (quad_done(t, v)) {
                v->active = v->gate = 0;
                v->env = v->env_out = 0;
            } else {
                vmod_t m;
                v->env = 1 << 24;
                memset(&m, 0, sizeof m);
                m.amp0 = v->env_out;
                m.amp1 = v->env_out = 32767;
                m.pitch16 = v->pitch_cur;
                m.inc = pitch_inc((uint32_t)m.pitch16);
                m.shape = 64 << 8;
                quad_render(t, v, out, CTL, &m);
            }
        }
        for (i = 0; i < CTL; i++)
            buf[i] = (float)(out[i] / 32768.0);
        fwrite(buf, sizeof buf[0], CTL, fo);
    }
    fclose(fo);
    return 0;
}
