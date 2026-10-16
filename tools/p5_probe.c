/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* P5's measurements of HISTORY 3y45 (docs/PROPHET.md "An unplayed part", "Headroom", "A stolen voice"): the firmware's
 * sound side as the emulator builds it (tools/emu/emu_firmware.h, as tests/cr_prophet_test.c), with ALL 200 programs.
 * Not a pass / fail suite: run it after changing the engine's level, the allocator or the part's idle work.
 *   sh tools/p5_probe.sh                    builds build/host/p5_probe (and its variants) and prints every table
 *   build/host/p5_probe cost                what the BASS part costs on P5 in each state (host instructions; the
 *                                           device estimate is emu.c's: us = instructions / 259)
 *   build/host/p5_probe levels [v]          every stage's peak against its limit: all 200 programs, one voice, four
 *                                           voices (close, wide, low, high), UNISON x 4; then VA, FM6, CZ-1 and FM
 *                                           TONE through the same part bus, sends and master (v: a line a case)
 *   build/host/p5_probe hot DIR             listening files of the three hottest cases (16-bit WAV, the master's out)
 *   build/host/p5_probe steal [DIR]         the click of a stolen voice: P5 against VA and FM6, the fade's variants
 *                                           (P5_STEAL_BLOCKS); DIR: the WAVs of a five-note chord and of an arpeggio
 *                                           over a held chord */
#include <os/lock.h>
#include <time.h>
#include <libproc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
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
/* the taps: a stage's largest |value| and the samples beyond its limit (eng_prophet_test.c P5_TAP) */
enum { TP_MIX, TP_DRIVE, TP_U, TP_Z, TP_FILT, TP_VOICE, TP_PART, TP_DC, TP_BUS, TP_SENDC, TP_SENDD, TP_SENDR, TP_PREM, TP_OUT, TP_N };
static const char *const TP_NAME[TP_N] = {
    "osc mixer (into the mixer's tanh)", "ladder drive (input - k * feedback)", "ladder input u (into the tanh)",
    "ladder state z", "filter out (after the last tanh)", "voice out (after the VCA)", "part sum (4 voices, before the capacitor)",
    "part out (after the capacitor)", "part bus after LEVEL", "chorus send", "delay send", "reverb send",
    "mix into the master", "master out (the DAC word >> 7)"};
/* the limit of each stage, and what happens there: S soft (the table's last entry: the curve is flat from there),
 * H a hard clamp, W an integer wrap */
static const int64_t TP_LIM[TP_N] = {65536, 262143, 65536, 60000, 32767, 65534, 884000, 8388607, 524287, 65535, 0, 0, 0x7FFFFFFF, 32767};
static const char TP_KIND[TP_N] = {'S', 'H', 'S', 'H', 'S', '-', 'H', 'W', 'H', 'H', 'H', 'H', 'W', 'S'};
static int64_t tp_max[TP_N];
static uint64_t tp_over[TP_N];
static inline void p5_tap(int k, int64_t x)
{
    if (x < 0)
        x = -x;
    if (x > tp_max[k])
        tp_max[k] = x;
    if (TP_LIM[k] && x > TP_LIM[k])
        tp_over[k]++;
}
#define P5_TAP(k, x) p5_tap(k, x)
static void mix_hook(const int32_t *l, const int32_t *r, uint32_t n);
#define FX_MIX_HOOK(l, r, n) mix_hook(l, r, n)
#include "emu/emu_firmware.h"

static int32_t *cap;                                    /* the dry mix (left) kept, for the steal's measure */
static uint32_t cap_n, cap_max;
static void mix_hook(const int32_t *l, const int32_t *r, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (cap && cap_n < cap_max)
            cap[cap_n++] = l[i];
        p5_tap(TP_PREM, l[i]);
        p5_tap(TP_PREM, r[i]);
        p5_tap(TP_SENDC, send_c[i]);
        p5_tap(TP_SENDD, send_d[i]);
        p5_tap(TP_SENDR, send_r[i]);
        p5_tap(TP_BUS, part_buf[i]);                    /* (rewritten below for the part's level: see run()) */
    }
}

static uint64_t instr_now(void)
{
    struct rusage_info_v4 ri;
    return proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri) ? 0 : ri.ri_instructions;
}

/* ------------------------------------------------- playing --- */
static int16_t *wav;                                    /* the master's out while recording (stereo) */
static uint32_t wav_n, wav_cap;
static int32_t lim_min = 32768, part_pk;
static void wav_start(void)
{
    wav_n = 0;
    if (!wav) {
        wav_cap = 2u * FS * 60u;
        wav = malloc(wav_cap * sizeof *wav);
    }
}
static void wav_save(const char *dir, const char *name)
{
    char path[512];
    FILE *f;
    uint32_t bytes = wav_n * 2u, h[11];
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    f = fopen(path, "wb");
    if (!f) {
        printf("cannot write %s\n", path);
        return;
    }
    h[0] = 0x46464952; h[1] = 36u + bytes; h[2] = 0x45564157; h[3] = 0x20746d66; h[4] = 16; h[5] = 0x00020001;
    h[6] = FS; h[7] = FS * 4u; h[8] = 0x00100004; h[9] = 0x61746164; h[10] = bytes;
    fwrite(h, 4, 11, f);
    fwrite(wav, 2, wav_n, f);
    fclose(f);
    printf("  wrote %s (%.2f s)\n", path, wav_n / 2.0 / FS);
}
static int rec;
static void run(uint32_t nb)
{
    int32_t out[2u * CTL];
    uint32_t b, i;
    for (b = 0; b < nb; b++) {
        mix_block(out, CTL);
        if (lim_g < lim_min)                            /* (the chain that runs: Classic's gain or Clean's) */
            lim_min = lim_g;
        if (cln_g < lim_min)
            lim_min = cln_g;
        for (i = 0; i < CTL; i++) {
            int32_t a = part_buf[i] < 0 ? -part_buf[i] : part_buf[i];
            if (a > part_pk)
                part_pk = a;
            p5_tap(TP_OUT, out[2u * i]);
            p5_tap(TP_OUT, out[2u * i + 1u]);
        }
        if (rec && wav_n + 2u * CTL <= wav_cap)
            for (i = 0; i < 2u * CTL; i++)
                wav[wav_n++] = (int16_t)clamp(out[i], -32768, 32767);
    }
}
static void taps_clear(void)
{
    memset(tp_max, 0, sizeof tp_max);
    memset(tp_over, 0, sizeof tp_over);
    lim_min = 32768;
    part_pk = 0;
    trk[0].peak = trk[1].peak = 0;
}
static uint32_t nact(uint32_t part)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += trk[part].v[i].active;
    return n;
}
static void quiet(void)                                 /* everything released and rung out */
{
    uint32_t b;
    cr_post(CRE_PANIC, 0, 0, 0);
    trk_all_off(&trk[0]);
    trk_all_off(&trk[1]);
    for (b = 0; b < 6u * FS / CTL && (nact(0) || nact(1)); b += 16u)
        run(16);
    run(600);                                           /* (the capacitor, the limiter, the reverb's first tail) */
}
static void p5_load(uint32_t part, uint32_t id)         /* Sequential's program id on the part (0: INIT) */
{
    cu_pool_load(part, ENGI_PROPHET, id ? p5_preset_of(id) : 0u);
    run(64);
}
static const char *p5_name(uint32_t id)
{
    static char b[16];
    pool_name(ENGI_PROPHET, p5_preset_of(id), b);
    return b;
}
static void mods(uint32_t mask, int down)
{
    uint32_t m;
    for (m = 0; m < 8u; m++)
        if (mask >> m & 1u)
            cr_post(CRE_MOD, m, 0, down);
}
#define M_MIN9 (1u << CR_MOD_MIN | 1u << CR_MOD_9)

/* ------------------------------------------------- cost: the bass part --- */
/* part 1's instructions a block over nb blocks (fx.c's own stage marks: mod_begin + track_render of trk[1], the
 * counter's cost left out), and the voices it rendered */
static double part_cost(uint32_t part, uint32_t nb, uint32_t *voices)
{
    uint32_t b, k = part ? CRP_PART1 : CRP_PART0;
    uint64_t sum = 0;
    cr_dbg.prof_on = 1;
    for (b = 0; b < nb; b++) {
        cr_dbg.cur[k] = 0;
        CR_PROF_START();
        run(1);
        sum += cr_dbg.cur[k];
    }
    cr_dbg.prof_on = 0;
    if (voices)
        *voices = nact(part);
    return (double)sum / nb;
}
static void cost_line(const char *what, uint32_t nb)
{
    uint32_t v1, v0;
    double c0 = part_cost(0, nb, &v0), c1 = part_cost(1, nb, &v1);
    printf("  %-58s bass: %u voices %7.0f instr/block %6.1f /sample %6.1f us/half | chord: %u voices %6.1f us/half\n", what,
           (unsigned)v1, c1, c1 / CTL, c1 * 4.0 / 259.0, (unsigned)v0, c0 * 4.0 / 259.0);
}
static void cost_set(int dual, uint32_t bass_id, const char *bass_name)
{
    char w[96];
    quiet();
    cr_dbg.c1 = dual ? C1_OK : C1_OFF;
    p5_load(0, 1);
    p5_load(1, bass_id);
    trk[0].p[P_REV] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = 0;
    trk[1].p[P_REV] = trk[1].p[P_CHOR] = trk[1].p[P_DLY] = 0;
    printf(" Dual Core %s, the bass on P5 %s (voice mode %d), the chord on P5 ITS PROPHET5:\n", dual ? "On" : "Off", bass_name,
           (int)trk[1].p[P_VOICE]);
    cr_post(CRE_BASS, 0, 0, 0);
    run(8);
    cost_line("Bass off, nothing played", 400);
    mods(M_MIN9, 1);
    cr_post(CRE_KEY, 62, 100, 1);
    run(400);
    cost_line("Bass off, a chord held", 400);
    cr_post(CRE_KEY, 62, 0, 0);
    quiet();
    cr_post(CRE_BASS, 0, 0, 1);
    run(8);
    cost_line("Bass ON, nothing played", 400);
    mods(M_MIN9, 1);
    cr_post(CRE_KEY, 62, 100, 1);
    run(400);
    cost_line("Bass ON, a chord held (the bass follows it)", 400);
    cr_post(CRE_KEY, 62, 0, 0);
    run(2);
    cost_line("Bass ON, the chord let go (the releases)", 40);
    quiet();
    snprintf(w, sizeof w, "Bass ON, 6 s later (%s)", p5_tail(&trk[1]) ? "the capacitor not at rest" : "at rest");
    cost_line(w, 400);
    mods(0xFFu, 0);
    cr_post(CRE_BASS, 0, 0, 0);
    run(8);
}
static void cost(void)
{
    uint32_t e;
    cr_prof_calibrate();
    printf("cost: the BASS part on P5 (P5_IDLE_SKIP %d; host instructions, fx.c's stage marks; us/half: emu.c's estimate / 259)\n",
           P5_IDLE_SKIP);
    cost_set(0, 4, "FUNK BASS II");
    cost_set(1, 4, "FUNK BASS II");
    cost_set(0, 111, "LAGOON BASS (a UNISON program)");
    cost_set(1, 111, "LAGOON BASS (a UNISON program)");
    {   /* every program as the bass (Dual Core On: the part's own budget), one note D2, the chord part silent */
        uint32_t id, v, nu = 0, n1 = 0, mx1 = 0, mxu = 0, idu = 0, id1 = 0;
        double c, s1 = 0, su = 0;
        cr_dbg.c1 = C1_OK;
        for (id = 1; id <= 200u; id++) {
            quiet();
            p5_load(1, id);
            trk_note_on(&trk[1], 38, 100);
            run(200);
            c = part_cost(1, 100, &v) * 4.0 / 259.0;
            trk_note_off(&trk[1], 38);
            if (getenv("P5_PROBE_V"))
                printf("  bass %3u %-12s mode %d voices %u: %.0f us/half\n", (unsigned)id, p5_name(id), (int)trk[1].p[P_VOICE],
                       (unsigned)v, c);
            if (trk[1].p[P_VOICE] == V_UNISON) {
                nu++;
                su += c;
                if (c > mxu) { mxu = (uint32_t)c; idu = id; }
            } else {
                n1++;
                s1 += c;
                if (c > mx1) { mx1 = (uint32_t)c; id1 = id; }
            }
        }
        quiet();
        cr_dbg.c1 = C1_OFF;
        printf(" every program as the bass, one note held, Dual Core On (us/half, the estimate):\n"
               "  %u programs without UNISON (one voice): mean %.0f, the dearest %u (%u %s)\n", (unsigned)n1, s1 / n1, (unsigned)mx1,
               (unsigned)id1, p5_name(id1));
        printf("  %u programs with UNISON (the stack): mean %.0f, the dearest %u (%u %s)\n", (unsigned)nu, nu ? su / nu : 0.0,
               (unsigned)mxu, (unsigned)idu, p5_name(idu));
    }
    printf(" the bass part idle on the other engines (Bass off, nothing played):\n");
    for (e = 0; e < 4u; e++) {
        static const uint8_t EN[4] = {ENGI_FM6, ENGI_VA, ENGI_CZ, ENGI_QUAD};
        char w[64];
        quiet();
        cu_pool_load(1, EN[e], 1);
        run(64);
        snprintf(w, sizeof w, "%s", ENGINES[EN[e]]->name);
        cost_line(w, 400);
    }
}

/* ------------------------------------------------- levels --- */
typedef struct { const char *name; uint8_t n, note[4], unison; } lcase_t;
static const lcase_t LC[] = {
    {"1 voice C4", 1, {60}, 0},
    {"1 voice C2", 1, {36}, 0},
    {"4 close C4", 4, {60, 64, 67, 71}, 0},
    {"4 wide", 4, {36, 55, 64, 74}, 0},
    {"4 low C2", 4, {36, 40, 43, 47}, 0},
    {"4 high C6", 4, {84, 88, 91, 95}, 0},
    {"unison C3", 1, {48}, 1},
    {"unison C2", 1, {36}, 1},
};
#define NLC (sizeof LC / sizeof LC[0])
typedef struct { int64_t mx[TP_N]; uint64_t over[TP_N]; int32_t lim, bus; double rms; } lres_t;
/* one case on the chord part as it stands (engine and sound loaded): the notes struck at velocity vel, held 1.5 s,
 * released 0.6 s; the part's sends as the bank's defaults */
static lres_t lcase_run(const lcase_t *c, uint32_t vel, int record)
{
    lres_t r;
    uint32_t i;
    int16_t vm = trk[0].p[P_VOICE];
    quiet();
    if (c->unison)
        trk[0].p[P_VOICE] = V_UNISON;
    taps_clear();
    rec = record;
    for (i = 0; i < c->n; i++)
        trk_note_on(&trk[0], c->note[i], vel);
    run(3u * FS / 2u / CTL);
    for (i = 0; i < c->n; i++)
        trk_note_off(&trk[0], c->note[i]);
    run(6u * FS / 10u / CTL);
    rec = 0;
    memcpy(r.mx, tp_max, sizeof r.mx);
    memcpy(r.over, tp_over, sizeof r.over);
    r.mx[TP_BUS] = trk[0].peak;                         /* fx.c mix_part_loop's own peak, after LEVEL */
    r.over[TP_BUS] = 0;
    r.lim = lim_min;
    {   /* the part before LEVEL, for every engine: fx.c's peak is ((b >> 2) * lvl) >> 10 with the sound's trim */
        int32_t lv = trk[0].p[P_LEVEL] & 127;
        if (trk[0].trim && lv)
            lv = clamp(lv + trk[0].trim, 1, 127);
        r.bus = LEVEL_Q12[lv] ? (int32_t)((int64_t)trk[0].peak * 4096 / LEVEL_Q12[lv]) : 0;
    }
    trk[0].p[P_VOICE] = vm;
    return r;
}
static double db(double x, double ref) { return x > 0 ? 20.0 * log10(x / ref) : -999.0; }
static void engines(void);
static void levels(int verbose)
{
    static int64_t wmax[NLC][TP_N];
    static uint32_t wid[NLC][TP_N];
    static uint64_t nover[NLC][TP_N];
    static uint32_t progs_over[NLC][TP_N];
    uint32_t id, c, k, e;
    printf("levels: every stage's peak, all 200 programs (velocity 127, the part's LEVEL %d, the sends of the bank: chorus %d "
           "delay %d reverb %d; MASTER %u of 4096; Output %s)\n", (int)trk[0].p[P_LEVEL], (int)trk[0].p[P_CHOR], (int)trk[0].p[P_DLY],
           (int)trk[0].p[P_REV], (unsigned)song.master_q12, fx_out_mode ? "Clean" : "Classic");
    for (id = 1; id <= 200u; id++) {
        p5_load(0, id);
        if (trk[0].engine != ENGI_PROPHET || p5_patch_id(p5_patch_of(&trk[0])) != id) {
            printf("  program %u did not load (a build without all 200? -Ibuild/host/p5_all)\n", (unsigned)id);
            return;
        }
        for (c = 0; c < NLC; c++) {
            p5_patch_t keep = *p5_patch_of(&trk[0]);
            lres_t r;
            if (LC[c].unison) {                         /* the editor's UNISON switch: all four voices, the program's DETUNE */
                p5_patch_of(&trk[0])->raw[P5_UNISON] = 1;
                p5_patch_of(&trk[0])->raw[P5_UNISON_COUNT] = 0;
            }
            r = lcase_run(&LC[c], 127, 0);
            *p5_patch_of(&trk[0]) = keep;
            for (k = 0; k < TP_N; k++) {
                if (r.mx[k] > wmax[c][k]) {
                    wmax[c][k] = r.mx[k];
                    wid[c][k] = id;
                }
                nover[c][k] += r.over[k];
                progs_over[c][k] += r.over[k] != 0;
            }
            if (verbose)
                printf("  P5 %3u %-12s %-11s mix %6lld drive %6lld u %6lld z %6lld filt %6lld voice %6lld part %7lld dc %7lld bus %6lld "
                       "sendR %6lld prem %7lld out %6lld lim %5.1f dB\n", (unsigned)id, p5_name(id), LC[c].name, r.mx[0], r.mx[1],
                       r.mx[2], r.mx[3], r.mx[4], r.mx[5], r.mx[6], r.mx[7], r.mx[8], r.mx[TP_SENDR], r.mx[TP_PREM], r.mx[TP_OUT],
                       db(r.lim, 32768));
        }
    }
    for (c = 0; c < NLC; c++) {
        printf(" %s: the largest of the 200 at each stage\n", LC[c].name);
        printf("   %-44s %9s %9s %8s  %s\n", "stage", "peak", "limit", "margin", "kind, the program, programs / samples beyond");
        for (k = 0; k < TP_N; k++) {
            if (!TP_LIM[k]) {
                printf("   %-44s %9lld %9s %8s  (the effect's own input shift and clamp: cr_fx.c)\n", TP_NAME[k], wmax[c][k], "-", "-");
                continue;
            }
            printf("   %-44s %9lld %9lld %+7.1f dB  %c  %3u %-12s  %u programs, %llu samples beyond\n", TP_NAME[k], wmax[c][k],
                   (long long)TP_LIM[k], -db((double)wmax[c][k], (double)TP_LIM[k]), TP_KIND[k], (unsigned)wid[c][k],
                   p5_name(wid[c][k]), (unsigned)progs_over[c][k], (unsigned long long)nover[c][k]);
        }
    }
}
/* the other engines through the same harness: the part's own peak before LEVEL, after it, the mix, the limiter */
static void engines(void)
{
    uint32_t c, e;
    printf("engines: velocity 127, the part's LEVEL %d and each sound's trim, MASTER %u of 4096, Output %s (its limiter works above\n"
           " %d in the mix)\n", (int)trk[0].p[P_LEVEL], (unsigned)song.master_q12, fx_out_mode ? "Clean" : "Classic",
           fx_out_mode ? CLN_K * 4096 / CLN_TRIM : (int)((int64_t)LIM_T * 4096 / (song.master_q12 ? song.master_q12 : 1)));
    printf(" the engines side by side (every sound of the pool; the part bus before LEVEL: median / max of the sounds' peaks, in\n"
           " dB re 32768; the limiter's deepest gain; P5: all 200):\n");
    for (e = 0; e < 5u; e++) {
        static const uint8_t EN[5] = {ENGI_PROPHET, ENGI_VA, ENGI_FM6, ENGI_CZ, ENGI_QUAD};
        uint32_t np = pool_count(EN[e]), pos;
        for (c = 0; c < NLC; c++) {
            static double pk[260], lm[260], bs[260], pm[260];
            uint32_t n = 0, i, j, hot = 0;
            double pmx = 0;
            for (pos = 1; pos < np && n < 260u; pos++) {
                p5_patch_t keep;
                lres_t r;
                cu_pool_load(0, EN[e], pos);
                run(64);
                if (pool_entry(EN[e], pos).kind != PK_FACTORY)
                    continue;
                keep = *p5_patch_of(&trk[0]);
                if (LC[c].unison && EN[e] == ENGI_PROPHET) {
                    p5_patch_of(&trk[0])->raw[P5_UNISON] = 1;
                    p5_patch_of(&trk[0])->raw[P5_UNISON_COUNT] = 0;
                }
                r = lcase_run(&LC[c], 127, 0);
                if (EN[e] == ENGI_PROPHET)
                    *p5_patch_of(&trk[0]) = keep;
                pk[n] = (double)r.bus;
                bs[n] = (double)r.mx[TP_BUS];
                lm[n] = (double)r.lim;
                pm[n] = (double)r.mx[TP_PREM];
                if (pm[n] > pmx)
                    pmx = pm[n];
                if (pk[n] > pk[hot])
                    hot = n;
                n++;
            }
            for (i = 0; i < n; i++)                     /* (sorted by the part's peak: the median) */
                for (j = i + 1; j < n; j++)
                    if (pk[j] < pk[i]) {
                        double t = pk[i]; pk[i] = pk[j]; pk[j] = t;
                        t = lm[i]; lm[i] = lm[j]; lm[j] = t;
                        t = bs[i]; bs[i] = bs[j]; bs[j] = t;
                    }
            if (n) {
                double lmin = 32768;
                for (i = 0; i < n; i++)
                    if (lm[i] < lmin)
                        lmin = lm[i];
                printf("   %-8s %-11s %3u sounds: part peak median %+6.1f dB, max %+6.1f dB (%7.0f); after LEVEL max %+6.1f dB; "
                       "the mix max %6.0f; limiter down to %5.1f dB\n", ENGINES[EN[e]]->name, LC[c].name, (unsigned)n, db(pk[n / 2u], 32768),
                       db(pk[n - 1u], 32768), pk[n - 1u], db(bs[n - 1u], 32768), pmx, db(lmin, 32768));
            }
        }
    }
}

/* ------------------------------------------------- a stolen voice --- */
static double pct(double *x, uint32_t n, double q)
{
    uint32_t i, j;
    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++)
            if (x[j] < x[i]) {
                double t = x[i];
                x[i] = x[j];
                x[j] = t;
            }
    return n ? x[(uint32_t)(q * (n - 1u))] : 0;
}
/* one steal on the chord part as it stands: nh notes held 0.6 s + ph blocks, then one more. drop: the new note is
 * let go in the same block (it never starts: the window holds the stolen voice's end and nothing else; an engine
 * that restarts the voice in place, FM6, cannot: its window holds the new note's start too). The click: the largest
 * |third difference| of the dry mix in the 10 blocks from the steal over the largest in the 150 ms before it (dB: 0
 * = nothing the held chord does not do itself); *step: the same of the first difference; *vmax: the most voices of
 * the part in a block of the window; *late: blocks from the key to the new note's voice (drop 0) */
#define ST_REF (150u * FS / 1000u / CTL)
#define ST_WIN 10u
static double steal_one(uint32_t nh, uint32_t ph, int drop, double *step, uint32_t *vmax, uint32_t *late)
{
    static const uint8_t N[9] = {60, 64, 67, 71, 74, 77, 81, 84, 88};
    static int32_t buf[(ST_REF + ST_WIN) * CTL];
    uint32_t i, b;
    double r3 = 1, r1 = 1, w3 = 0, w1 = 0, rpk = 0;
    quiet();
    for (i = 0; i < nh; i++)
        trk_note_on(&trk[0], N[i], 100);
    run(6u * FS / 10u / CTL + ph);
    cap = buf;
    cap_n = 0;
    cap_max = sizeof buf / sizeof buf[0];
    run(ST_REF);
    trk_note_on(&trk[0], N[nh], 100);
    if (drop)
        vsq_drop(&trk[0], N[nh]);
    *vmax = 0;
    *late = 0;
    for (b = 0; b < ST_WIN; b++) {
        uint32_t on = 0;
        run(1);
        if (nact(0) > *vmax)
            *vmax = nact(0);
        for (i = 0; i < NVOICE; i++)
            on |= trk[0].v[i].active && trk[0].v[i].gate && trk[0].v[i].note == N[nh];
        if (!on && !drop)
            *late = b + 1u;
    }
    cap = 0;
    for (i = 3; i < cap_n; i++) {
        double d1 = (double)buf[i] - buf[i - 1], d3 = (double)buf[i] - 3.0 * buf[i - 1] + 3.0 * buf[i - 2] - buf[i - 3];
        d1 = fabs(d1);
        d3 = fabs(d3);
        if (i < ST_REF * CTL) {
            if (fabs((double)buf[i]) > rpk) rpk = fabs((double)buf[i]);
            if (d3 > r3) r3 = d3;
            if (d1 > r1) r1 = d1;
        } else if (i >= ST_REF * CTL + 3u) {
            if (d3 > w3) w3 = d3;
            if (d1 > w1) w1 = d1;
        }
    }
    for (i = 0; i <= nh; i++)
        trk_note_off(&trk[0], N[i]);
    *step = db(w1 > 1 ? w1 : 1, r1);
    if (rpk < 2000.0)                                   /* (a sound that has died away under the held keys: no measure) */
        return -999.0;
    return db(w3 > 1 ? w3 : 1, r3);
}
static uint32_t steal_worst;                            /* the P5 program whose steal clicks most (the listening files) */
static void steal_engine(uint32_t e, uint32_t nh, int drop)
{
    static double c3[1300], c1[1300];
    uint32_t np = pool_count(e), pos, ph, n = 0, vm = 0, lt = 0, over6 = 0, over12 = 0;
    double worst = -999;
    for (pos = 1; pos < np; pos++) {
        cu_pool_load(0, e, pos);
        run(64);
        if (pool_entry(e, pos).kind != PK_FACTORY)
            continue;
        trk[0].p[P_REV] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = 0;
        if (e == ENGI_PROPHET && trk[0].p[P_VOICE] != V_POLY)
            continue;                                   /* (a UNISON program: one note, nothing to steal) */
        {
            double mine[6], lo = 999;
            uint32_t nm = 0;
            for (ph = 0; ph < 6u && n < 1300u; ph++) {
                uint32_t v, l;
                double s, c = steal_one(nh, ph * 7u, drop, &s, &v, &l);
                if (c < -900.0)
                    continue;
                c3[n] = c;
                c1[n++] = s;
                mine[nm++] = c;
                if (c < lo) lo = c;
                over6 += c > 6.0;
                over12 += c > 12.0;
                if (v > vm) vm = v;
            }
            /* the program whose steal clicks most at EVERY phase (its smallest of the six: a program that clicks by
             * itself now and then, an S&H, a noise, is not the steal's) */
            if (nm == 6u && lo > worst) {
                worst = lo;
                if (e == ENGI_PROPHET)
                    steal_worst = p5_patch_id(p5_patch_of(&trk[0]));
            }
            if (nm == 6u && getenv("P5_PROBE_V"))
                printf("   %s %3u: the six steals' clicks, least %+5.1f dB, median %+5.1f\n", ENGINES[e]->name, (unsigned)pos, lo, pct(mine, nm, 0.5));
        }
        {                                               /* the wait of the new note (one phase) */
            uint32_t v, l;
            double s;
            (void)steal_one(nh, 3, 0, &s, &v, &l);
            if (l > lt) lt = l;
        }
    }
    printf("  %-8s %u held + 1, %4u steals: click median %+5.1f dB, 90 %% %+5.1f, 99 %% %+5.1f; above +6 dB %3u (%.1f %%), above +12 dB "
           "%3u; step median %+5.1f, 99 %% %+5.1f; most voices in a block %u; the new note %u blocks after its key\n",
           ENGINES[e]->name, (unsigned)nh, (unsigned)n, pct(c3, n, 0.5), pct(c3, n, 0.9), pct(c3, n, 0.99), (unsigned)over6,
           100.0 * over6 / (n ? n : 1u), (unsigned)over12, pct(c1, n, 0.5), pct(c1, n, 0.99), (unsigned)vm, (unsigned)lt);
}
/* the listening scenes on Sequential's program id, dry: (1) a chord of four and then a new note every 0.4 s, each on a
 * stolen voice; (2) three notes held and a fast arpeggio over them */
static void steal_scenes(const char *dir, const char *tag, uint32_t id)
{
    static const uint8_t ADD[10] = {74, 77, 72, 79, 76, 81, 69, 83, 78, 86};
    static const uint8_t ARP[8] = {72, 76, 79, 84, 88, 84, 79, 76};
    char name[96];
    uint32_t i, kills0;
    quiet();
    p5_load(0, id);
    trk[0].p[P_REV] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = 0;
    if (trk[0].p[P_VOICE] != V_POLY)
        return;
    wav_start();
    rec = 1;
    kills0 = voice_kills;
    trk_note_on(&trk[0], 48, 100);
    trk_note_on(&trk[0], 60, 100);
    trk_note_on(&trk[0], 64, 100);
    trk_note_on(&trk[0], 67, 100);
    run(FS / CTL);
    for (i = 0; i < 10u; i++) {
        trk_note_on(&trk[0], ADD[i], 100);
        run(4u * FS / 10u / CTL);
    }
    trk_all_off(&trk[0]);
    run(FS / CTL);
    rec = 0;
    snprintf(name, sizeof name, "%s-chord5-p%03u", tag, (unsigned)id);
    wav_save(dir, name);
    printf("    (%u voices faded for a new note)\n", (unsigned)(voice_kills - kills0));
    quiet();
    wav_start();
    rec = 1;
    kills0 = voice_kills;
    trk_note_on(&trk[0], 48, 100);
    trk_note_on(&trk[0], 55, 100);
    trk_note_on(&trk[0], 64, 100);
    run(FS / 2u / CTL);
    for (i = 0; i < 48u; i++) {                         /* sixteenths at 150 BPM: 100 ms a note, 70 ms held */
        trk_note_on(&trk[0], ARP[i & 7u], 100);
        run(70u * FS / 1000u / CTL);
        trk_note_off(&trk[0], ARP[i & 7u]);
        run(30u * FS / 1000u / CTL);
    }
    trk_all_off(&trk[0]);
    run(FS / CTL);
    rec = 0;
    snprintf(name, sizeof name, "%s-arp-p%03u", tag, (unsigned)id);
    wav_save(dir, name);
    printf("    (%u voices faded for a new note)\n", (unsigned)(voice_kills - kills0));
}
static void steal(const char *dir, const char *tag)
{
    printf("steal: the click of a voice taken for a new note (P5_STEAL_BLOCKS %d; dry; the dry mix before the master)\n", P5_STEAL_BLOCKS);
    steal_engine(ENGI_PROPHET, 4, 1);
    steal_engine(ENGI_VA, 8, 1);
    steal_engine(ENGI_CZ, 8, 1);
    steal_engine(ENGI_FM6, 8, 0);
    printf("  (FM6 restarts the voice in place, as Dexed: its window holds the new note's start; the others: the stolen voice alone)\n");
    if (dir) {
        const char *w = getenv("P5_STEAL_WORST");
        uint32_t ids[3] = {1, 16, w ? (uint32_t)atoi(w) : steal_worst}, i;
        printf("  the P5 program whose steal clicks most: %u %s\n", (unsigned)steal_worst, p5_name(steal_worst));
        for (i = 0; i < 3u; i++)
            steal_scenes(dir, tag, ids[i]);
    }
}
/* the three hottest cases of the level table, as the master gives them (the limiter and all) */
static void hot(const char *dir, const char *tag)
{
    static const struct { uint16_t id; uint8_t c; const char *n; } H[3] = {
        {86, 6, "unison-C3-p086-uptight-bass"}, {151, 1, "onekey-C2-p151-tight-bass"}, {132, 6, "unison-C3-p132-sync-swellpad"}};
    uint32_t i;
    for (i = 0; i < 3u; i++) {
        char name[96];
        p5_patch_t keep;
        lres_t r;
        p5_load(0, H[i].id);
        keep = *p5_patch_of(&trk[0]);
        p5_patch_of(&trk[0])->raw[P5_UNISON] = 1;
        p5_patch_of(&trk[0])->raw[P5_UNISON_COUNT] = 0;
        wav_start();
        r = lcase_run(&LC[H[i].c], 127, 1);
        *p5_patch_of(&trk[0]) = keep;
        snprintf(name, sizeof name, "%s-%s", tag, H[i].n);
        wav_save(dir, name);
        printf("    part %lld (%+.1f dB re 32768), after LEVEL %lld, the mix %lld, the limiter down to %.1f dB\n", r.mx[TP_DC],
               db((double)r.mx[TP_DC], 32768), r.mx[TP_BUS], r.mx[TP_PREM], db(r.lim, 32768));
    }
}


/* ------------------------------------------------- eight: the listening files of 3y46 --- */
/* eight DIR TAG (docs/PROPHET.md "Voices", "Level"; sh tools/p5_probe.sh eight DIR): TWO CORES (the host's core 1 runs
 * its job inline: the samples are the unit's). D, Bm, G, A in six-note and in eight-note voicings on four programs,
 * the part's default sends, the master as the unit's; the same with a P5 bass under them (a UNISON program: its
 * stack of four has two voices under six chord notes and NONE under eight: the chord first; and a plain one); a
 * UNISON lead and a UNISON bass key with one core and with two. TAG names the build ("after": this tree; "before":
 * -DP5_OUT_SHIFT=1 -DP5_STACK_PHASE=0, 3y45's level and stack) */
static void prog_chords(const char *dir, const char *tag, uint32_t id, uint32_t nk, uint32_t bass_id)
{
    static const uint8_t CH[4][8] = {{50, 57, 62, 66, 69, 73, 76, 81}, {47, 54, 59, 62, 66, 69, 74, 78},
                                     {43, 50, 55, 59, 62, 67, 71, 74}, {45, 52, 57, 61, 64, 69, 73, 76}};
    static const uint8_t BASS[4] = {38, 35, 31, 33};
    char name[128], pn[16];
    uint32_t c, k, vc = 0, vb = 0, rep;
    quiet();
    p5_load(0, id);
    if (bass_id)
        p5_load(1, bass_id);
    strcpy(pn, p5_name(id));
    for (k = 0; pn[k]; k++)
        if (pn[k] == ' ')
            pn[k] = '-';
    cr_dbg.p5_c0 = cr_dbg.p5_c1 = cr_dbg.p5_bass = 0;
    wav_start();
    rec = 1;
    for (rep = 0; rep < 2u; rep++)
        for (c = 0; c < 4u; c++) {
            if (bass_id)
                trk_note_on(&trk[1], BASS[c], 100);
            for (k = 0; k < nk; k++)
                trk_note_on(&trk[0], CH[c][k], 96);
            run(40);
            if (nact(0) > vc)
                vc = nact(0);
            if (bass_id && p5_sounding(&trk[1]) > vb)
                vb = p5_sounding(&trk[1]);
            run(1500u * FS / 1000u / CTL - 40u);
            for (k = 0; k < nk; k++)
                trk_note_off(&trk[0], CH[c][k]);
            if (bass_id)
                trk_note_off(&trk[1], BASS[c]);
            run(200u * FS / 1000u / CTL);
        }
    run(2u * FS / CTL);
    rec = 0;
    if (bass_id)
        snprintf(name, sizeof name, "%s-%s-p%03u-%s-bass-p%03u", tag, nk == 6u ? "six" : "eight", (unsigned)id, pn, (unsigned)bass_id);
    else
        snprintf(name, sizeof name, "%s-%s-p%03u-%s", tag, nk == 6u ? "six" : "eight", (unsigned)id, pn);
    wav_save(dir, name);
    printf("    (%u keys a chord: the chord part %u voices at the most; core 0 %u, core 1 %u P5 voices in a block at the most%s",
           (unsigned)nk, (unsigned)vc, (unsigned)cr_dbg.p5_c0, (unsigned)cr_dbg.p5_c1, bass_id ? "" : ")\n");
    if (bass_id)
        printf("; the bass part (%s) %u voices at the most under the chord)\n", p5_name(bass_id), (unsigned)vb);
}
static void uni_phrase(const char *dir, const char *tag, uint32_t id, uint32_t part, int two, const char *what)
{
    static const uint8_t LEAD[12] = {62, 65, 69, 72, 74, 72, 69, 65, 67, 69, 65, 62};
    static const uint8_t CH[4] = {57, 62, 65, 69};
    char name[128];
    uint32_t i;
    quiet();
    cr_dbg.c1 = two ? C1_OK : C1_OFF;
    quiet();
    p5_load(part, id);
    if (part)
        p5_load(0, 1);
    wav_start();
    rec = 1;
    if (part)                                           /* a bass key under a held chord, four times */
        for (i = 0; i < 4u; i++)
            trk_note_on(&trk[0], CH[i], 80);
    for (i = 0; i < (part ? 4u : 12u); i++) {
        uint32_t n = part ? 38u : LEAD[i];
        trk_note_on(&trk[part], n, 110);
        run((part ? 700u : 260u) * FS / 1000u / CTL);
        trk_note_off(&trk[part], n);
        run((part ? 200u : 40u) * FS / 1000u / CTL);
    }
    trk_all_off(&trk[0]);
    run(FS / CTL);
    rec = 0;
    snprintf(name, sizeof name, "%s-%s-p%03u-%s", tag, what, (unsigned)id, two ? "two-cores" : "one-core");
    wav_save(dir, name);
    printf("    (%s: the limiter down to %.1f dB at the most)\n", p5_name(id), db(lim_min, 32768));
    cr_dbg.c1 = C1_OK;
}
static void eight(const char *dir, const char *tag)
{
    static const uint16_t PROG[4] = {1, 44, 3, 131};    /* ITS PROPHET5, CARS STRINGS, FOREVER KEYS, ICONIC BRASS */
    uint32_t i, old = P5_OUT_SHIFT != 0;                /* (3y45's level: one program's chords and the UNISON keys) */
    cr_dbg.c1 = C1_OK;
    printf("eight: two cores, the part's default sends (%s; P5_OUT_SHIFT %d, P5_STACK_PHASE %d)\n", tag, P5_OUT_SHIFT, P5_STACK_PHASE);
    for (i = 0; i < (old ? 1u : 4u); i++) {
        prog_chords(dir, tag, PROG[i], 6, 0);
        prog_chords(dir, tag, PROG[i], 8, 0);
        prog_chords(dir, tag, PROG[i], 6, 111);         /* LAGOON BASS: a UNISON stack of four */
        prog_chords(dir, tag, PROG[i], 8, 111);
    }
    if (!old) {
        prog_chords(dir, tag, 1, 6, 4);                 /* FUNK BASS II: one voice */
        prog_chords(dir, tag, 1, 8, 4);
    }
    lim_min = 32768;
    uni_phrase(dir, tag, 82, 0, 1, "unison-lead");      /* UNISAWYER */
    lim_min = 32768;
    uni_phrase(dir, tag, 101, 0, 1, "unison-lead");     /* VELO RES LD2 */
    lim_min = 32768;
    uni_phrase(dir, tag, 111, 1, 1, "unison-bass-under-chord");
    lim_min = 32768;
    if (!old)
        uni_phrase(dir, tag, 111, 1, 0, "unison-bass-under-chord");
    lim_min = 32768;
    uni_phrase(dir, tag, 99, 1, 1, "unison-bass-under-chord");     /* IMPACT BASS: one voice four times */
    lim_min = 32768;
    if (!old)
        uni_phrase(dir, tag, 99, 1, 0, "unison-bass-under-chord");
    cr_dbg.c1 = C1_OFF;
}

int main(int argc, char **argv)
{
    emu_flash_open();
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();
    run(64);
    if (argc >= 2 && !strcmp(argv[1], "cost")) {
        cost();
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "levels")) {
        levels(argc >= 3);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "engines")) {
        engines();
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "steal")) {
        steal(argc >= 3 ? argv[2] : 0, argc >= 4 ? argv[3] : "now");
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "eight")) {
        eight(argv[2], argc >= 4 ? argv[3] : "after");
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "hot")) {
        hot(argv[2], argc >= 4 ? argv[3] : "now");
        return 0;
    }
    printf("usage: p5_probe cost | levels [v] | hot DIR | steal [DIR] | eight DIR [TAG]\n");
    return 2;
}
