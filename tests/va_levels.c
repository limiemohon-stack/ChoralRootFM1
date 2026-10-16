/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The VA presets' levels against the master limiter (docs/INTEGRATION.md Defaults, docs/VA.md): a 6-note chord
 * held (D4 F#4 A4 B4 C#5 E5, as perf.sh (a)'s D6/9maj7) on each VA chord preset, part 0 at LEVEL 92, alone and with
 * the bank's SUB BASS (ANALOG, MONO, D2) on part 1 at 92; ChoralRoot's mix (fx_smooth, voice_fade_steal, MASTER as at
 * power-on: 2047); the share of 0.3..5.9 s under the limiter's gain (lim_g < 1), the post-master peak and RMS. The
 * basses: one note held, with TINE EP's chord. Not a pass / fail suite: run it after changing a preset's levels.
 *   cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/va_levels tests/va_levels.c -lm && build/host/va_levels
 * With "fm6": the bank's eight FM6 rows (cr_bank.c, by preset name; docs/FM6.md Levels; FM6_POLY 8 as choralroot.c;
 * PIANO, a preset of ChoralRoot's unit only (FELUCCA_SAMPLE 0), as TINE EP with PIANO's macros and sends):
 *   build/host/va_levels fm6
 * With "cz": the bank's six CZ-1 rows (docs/CZ1.md Levels; no trims since 0.19: the engine is 6 dB lower):
 *   build/host/va_levels cz
 * With "p5": PROPHET against the VA and FM6 (docs/PROPHET.md "Level"): a 4-note chord (D4 F#4 A4 C#5), every send 0
 * (the engines' own levels), alone, on P5's INIT and its shipped programs, on the VA's chord presets and on
 * FM6's presets; each sound's line, then each engine's median RMS and the programs more than 4 dB from P5's:
 *   build/host/va_levels p5
 * With "loud": every sound of FM TONE, the VA, FM6, CZ-1 and P5 by its BS.1770 loudness (LUFS) as a player hears it at
 * power-on, three phrases (below, loud_in); each line, then each engine's medians (docs/QUAD.md "Level";
 * "loud N ENGINES": the first N sounds of the engines named, e.g. loud 99 "FM TONE VA"):
 *   build/host/va_levels loud
 * With "dnsnd LIST": the same for imported Digitone sounds as FM TONE plays them (LIST: "name|blob hex|drive|pan" lines,
 * web/fm1dnsnd.js dnsndToQuad's, the device manager's import: node tools/dnsnd_list.mjs DIR > LIST):
 *   build/host/va_levels dnsnd LIST
 * From the environment ("loud"): LD_QINIT=1 FM TONE's INIT patch for its sounds, LD_QLEVEL=L its AMP LEVEL, LD_DRIVE=D
 * the part's DRIVE, LD_VEL=V the velocity (100). */
#define FELUCCA_VA 1
#define FELUCCA_CZ 1
#define FELUCCA_QUAD 1
#ifndef FELUCCA_PROPHET
#define FELUCCA_PROPHET 1
#endif
#ifndef FM6_POLY
#define FM6_POLY 8               /* as choralroot.c */
#endif
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <libproc.h>
#include <sys/resource.h>
static const int8_t *ovr_e;                      /* "fm6": PIANO's macros and sends over TINE EP's (preset_t's) */
static const uint8_t *ovr_fx;
static int8_t ovr_trim;                          /* "cz": the bank row's trim (track_t.trim) */
static uint32_t chord_n = 6;                     /* "p5": 4 notes, */
static int dry_all;                              /* .. every send 0 */
static uint64_t instr_now(void)
{
    struct rusage_info_v4 ri;
    return proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri) ? 0 : ri.ri_instructions;
}
static double cpu_ips;                           /* host instructions a sample over the hold (measure_in) */

static void measure_in(uint32_t chord_e, uint32_t chord_p, int with_bass, uint32_t bass_e, uint32_t bass_p,
                    double *lim, double *pk, double *rms)
{
    static const uint8_t CH6[6] = {62, 66, 69, 71, 73, 76}, CH4[4] = {62, 66, 69, 73};
    const uint8_t *CH = chord_n == 4u ? CH4 : CH6;
    int32_t out[2 * CTL];
    uint32_t i, n, nb = 6u * FS / CTL, b0 = (uint32_t)(0.3 * FS / CTL), b1 = (uint32_t)(5.9 * FS / CTL), under = 0, cnt = 0;
    double sq = 0, peak = 0;
    host_tracks_init();
    fx_smooth = 1;
    voice_fade_steal = 1;
    song.master_q12 = 2047;
    song.g[G_BPM] = 120;
    host_preset(&trk[0], chord_e, chord_p);
    if (ovr_e)
        for (i = 0; i < 8u; i++)
            trk[0].p[P_E0 + i] = ovr_e[i];
    if (ovr_fx)
        for (i = 0; i < 4u; i++)
            trk[0].p[P_DIST + i] = (int16_t)(ovr_fx[i] - 1);
    if (dry_all)
        for (i = P_DIST; i <= P_REV; i++)
            trk[0].p[i] = 0;
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_LEVEL] = 92;
    trk[0].trim = ovr_trim;
    for (i = 1; i < NPART; i++)
        trk[i].p[P_LEVEL] = 0;
    if (with_bass) {
        host_preset(&trk[1], bass_e, bass_p);
        trk[1].p[P_VOICE] = V_MONO;
        trk[1].p[P_LEVEL] = 92;
        for (i = P_DIST; i <= P_REV; i++)       /* (the bass part's sends: ChoralRoot's FX are part 0's) */
            trk[1].p[i] = trk[0].p[i];
    }
    for (i = 0; i < chord_n; i++)
        trk_note_on(&trk[0], CH[i], 100);
    if (with_bass)
        trk_note_on(&trk[1], 38, 100);
    uint64_t i0 = 0;
    for (n = 0; n < nb; n++) {
        if (n == b0)
            i0 = instr_now();
        if (n == b1)
            cpu_ips = (double)(instr_now() - i0) / ((b1 - b0) * CTL);
        mix_block(out, CTL);
        if (n < b0 || n >= b1)
            continue;
        for (i = 0; i < CTL; i++) {
            double l = out[2 * i] / 32768.0, r = out[2 * i + 1] / 32768.0;
            sq += (l * l + r * r) / 2;
            peak = fabs(l) > peak ? fabs(l) : peak;
            peak = fabs(r) > peak ? fabs(r) : peak;
            cnt++;
        }
        under += lim_g < 32768;                  /* (per block: the gain at its end) */
    }
    *lim = 100.0 * under / (b1 - b0);
    *pk = 20 * log10(peak + 1e-9);
    *rms = 10 * log10(sq / cnt + 1e-12);
}

/* each measurement in a fork()ed child: every state as at boot (voices, FX buffers, the limiter) */
static void measure(uint32_t chord_e, uint32_t chord_p, int with_bass, uint32_t bass_e, uint32_t bass_p,
                    double *lim, double *pk, double *rms)
{
    double r[4] = {0, 0, 0, 0};
    int fd[2];
    fflush(stdout);
    if (pipe(fd))
        exit(2);
    if (!fork()) {
        close(fd[0]);
        measure_in(chord_e, chord_p, with_bass, bass_e, bass_p, &r[0], &r[1], &r[2]);
        r[3] = cpu_ips;
        if (write(fd[1], r, sizeof r) != sizeof r)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], r, sizeof r) != sizeof r)
        exit(2);
    close(fd[0]);
    wait(0);
    *lim = r[0];
    *pk = r[1];
    *rms = r[2];
    cpu_ips = r[3];
}

static uint32_t preset_named(uint32_t eng, const char *name)
{
    const engine_t *e = ENGINES[eng];
    uint32_t k;
    for (k = 0; k < e->npresets; k++)
        if (!strcmp(e->presets[k].name, name))
            return k;
    return 0;
}

/* ---- "loud": every sound's loudness, BS.1770 (K-weighted, gated integrated LUFS re the Q15 output's full scale),
 * as a player hears it at power-on: the part at its default LEVEL (104) and sends (0), FM TONE's Drive its preset's
 * (cr_ui.c cu_drive_load), velocity 100, Output Clean (the default), MASTER as at power-on. Two phrases:
 *   HELD   the 6-note chord above (D6/9maj7) held 6 s, read 0.3 .. 5.9 s;
 *   PHRASE four 4-note chords (Dmaj7 Bm7 Gmaj7 A7sus4) struck a second apart and held 0.9 s, read 0 .. 5.5 s
 *          (a bass sound, MONO: D2 B1 G1 A1 the same way);
 *   NOTES  the chords' top notes alone, the same way (C#5 A4 F#4 G4): one voice.
 * Each also at LEVEL 80 (-12 dB) and read +12 dB ("lin"): the sound's own level, under the limiter's threshold; the
 * gap between the two is what Clean's limiter takes. */
typedef struct {
    double b0, b1, b2, a1, a2, z1, z2;
} bq_t;
static double bq(bq_t *f, double x)
{
    double y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}
static void kw_init(bq_t *f)                     /* f[0], f[1]: the shelf and the high-pass (BS.1770-4, at FS) */
{
    double K = tan(M_PI * 1681.9744509555319 / FS), Q = 0.7071752369554193, Vh = pow(10, 3.99984385397 / 20),
           Vb = pow(Vh, 0.499666774155), a0 = 1 + K / Q + K * K;
    memset(f, 0, 2 * sizeof *f);
    f[0].b0 = (Vh + Vb * K / Q + K * K) / a0;
    f[0].b1 = 2 * (K * K - Vh) / a0;
    f[0].b2 = (Vh - Vb * K / Q + K * K) / a0;
    f[0].a1 = 2 * (K * K - 1) / a0;
    f[0].a2 = (1 - K / Q + K * K) / a0;
    K = tan(M_PI * 38.13547087613982 / FS);
    Q = 0.5003270373253953;
    a0 = 1 + K / Q + K * K;
    f[1].b0 = 1;
    f[1].b1 = -2;
    f[1].b2 = 1;
    f[1].a1 = 2 * (K * K - 1) / a0;
    f[1].a2 = (1 - K / Q + K * K) / a0;
}
#define LD_MAXS (7u * FS)
static float ld_k[2][LD_MAXS];                   /* the K-weighted output, L R */
static double ld_lufs(uint32_t s0, uint32_t s1)  /* gated integrated loudness of samples s0 .. s1 (BS.1770-4) */
{
    uint32_t blk = FS * 4u / 10u, hop = blk / 4u, nb = 0, i, j;
    static double z[256];
    double sum = 0, gate;
    uint32_t n = 0;
    for (i = s0; i + blk <= s1 && nb < 256u; i += hop) {
        double q = 0;
        for (j = i; j < i + blk; j++)
            q += (double)ld_k[0][j] * ld_k[0][j] + (double)ld_k[1][j] * ld_k[1][j];
        z[nb++] = q / blk;
    }
    for (i = 0; i < nb; i++)
        if (-0.691 + 10 * log10(z[i] + 1e-20) > -70)
            sum += z[i], n++;
    if (!n)
        return -99;
    gate = -0.691 + 10 * log10(sum / n) - 10;
    sum = 0, n = 0;
    for (i = 0; i < nb; i++)
        if (-0.691 + 10 * log10(z[i] + 1e-20) > gate)
            sum += z[i], n++;
    return n ? -0.691 + 10 * log10(sum / n) : -99;
}
/* one render of sound (e, pr): phrase ph (0 held, 1 struck), the part at LEVEL lv; r: LUFS, peak dB, the share of
 * blocks under Clean's limiter (cln_g < unity) */
static int ld_vel = 100, ld_qlevel = -1, ld_drive = -1, ld_qinit;   /* LD_VEL, LD_QLEVEL, LD_DRIVE, LD_QINIT */
static const uint8_t *ld_blob;                   /* "dnsnd": an imported sound's blob over FM TONE's preset 0, */
static int ld_bdrive, ld_bpan;                   /* its Drive and Pan (the import's P_DIST, P_PAN) */
static void loud_in(uint32_t e, uint32_t pr, int ph, int lv, double *r)
{
    static const uint8_t CH6[6] = {62, 66, 69, 71, 73, 76};
    static const uint8_t PH[4][4] = {{62, 66, 69, 73}, {59, 62, 66, 69}, {55, 59, 62, 66}, {57, 62, 64, 67}};
    static const uint8_t PB[4] = {38, 35, 31, 33};
    int32_t out[2 * CTL];
    bq_t kl[2], kr[2];
    const preset_t *p = &ENGINES[e]->presets[pr];
    uint32_t i, j, n, ns = (uint32_t)((ph ? 5.5 : 6.0) * FS), nb = ns / CTL, on = (uint32_t)(1.0 * FS / CTL),
                      off = (uint32_t)(0.9 * FS / CTL), mono = p->mono, under = 0;
    double peak = 0;
    kw_init(kl);
    kw_init(kr);
    host_tracks_init();
    fx_smooth = 1;
    voice_fade_steal = 1;
    song.master_q12 = 2047;
    song.g[G_BPM] = 120;
    fx_output_set(1, 1);                         /* Clean, at once (its MASTER too) */
    host_preset(&trk[0], e, pr);
    for (i = P_DIST; i <= P_REV; i++)
        trk[0].p[i] = 0;
    if (e == ENGI_QUAD)
        trk[0].p[P_DIST] = (int16_t)(p->fx[0] ? p->fx[0] - 1 : 0);
    trk[0].p[P_VOICE] = mono ? V_MONO : V_POLY;
    trk[0].p[P_LEVEL] = (int16_t)lv;
    trk[0].trim = 0;
    if (e == ENGI_QUAD && ld_blob) {
        quad_blob_set(&trk[0], ld_blob);
        trk[0].p[P_DIST] = (int16_t)ld_bdrive;
        trk[0].p[P_PAN] = (int16_t)ld_bpan;
    }
    if (e == ENGI_QUAD && ld_qinit)                  /* (FM TONE's INIT: the init patch) */
        quad_blob_preset(&trk[0], QUAD_NPRESETS);
    if (e == ENGI_QUAD && ld_qlevel >= 0)            /* (FM TONE's AMP LEVEL instead of the patch's) */
        quad_patch[0][QP_LEVEL] = (int16_t)ld_qlevel;
    if (ld_drive >= 0)
        trk[0].p[P_DIST] = (int16_t)ld_drive;
    for (i = 1; i < NPART; i++)
        trk[i].p[P_LEVEL] = 0;
    if (!ph)
        for (i = 0; i < 6u; i++)
            trk_note_on(&trk[0], CH6[i], (uint8_t)ld_vel);
    for (n = 0; n < nb; n++) {
        if (ph && n % on == 0u && n / on < 4u) {
            for (j = 0; j < (mono || ph == 2 ? 1u : 4u); j++)
                trk_note_on(&trk[0], mono ? PB[n / on] : ph == 2 ? PH[n / on][3] : PH[n / on][j], (uint8_t)ld_vel);
        }
        if (ph && n % on == off && n / on < 4u)
            for (j = 0; j < (mono || ph == 2 ? 1u : 4u); j++)
                trk_note_off(&trk[0], mono ? PB[n / on] : ph == 2 ? PH[n / on][3] : PH[n / on][j]);
        mix_block(out, CTL);
        for (i = 0; i < CTL; i++) {
            double l = out[2 * i] / 32768.0, rr = out[2 * i + 1] / 32768.0;
            ld_k[0][n * CTL + i] = (float)bq(&kl[1], bq(&kl[0], l));
            ld_k[1][n * CTL + i] = (float)bq(&kr[1], bq(&kr[0], rr));
            peak = fabs(l) > peak ? fabs(l) : peak;
            peak = fabs(rr) > peak ? fabs(rr) : peak;
        }
        under += cln_wg < 32768;
    }
    r[0] = ph ? ld_lufs(0, nb * CTL) : ld_lufs((uint32_t)(0.3 * FS), (uint32_t)(5.9 * FS));
    r[1] = 20 * log10(peak + 1e-9);
    r[2] = 100.0 * under / nb;
}
static void loud(uint32_t e, uint32_t pr, int ph, int lv, double *r)   /* (in a fork, as measure) */
{
    int fd[2];
    fflush(stdout);
    if (pipe(fd))
        exit(2);
    if (!fork()) {
        double q[3];
        close(fd[0]);
        loud_in(e, pr, ph, lv, q);
        if (write(fd[1], q, sizeof q) != sizeof q)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], r, 3 * sizeof *r) != (ssize_t)(3 * sizeof *r))
        exit(2);
    close(fd[0]);
    wait(0);
}
static double median(double *x, uint32_t n)
{
    uint32_t a, b;
    for (a = 0; a < n; a++)
        for (b = a + 1u; b < n; b++)
            if (x[b] < x[a]) {
                double t = x[a];
                x[a] = x[b];
                x[b] = t;
            }
    return n ? (x[(n - 1u) / 2u] + x[n / 2u]) / 2 : 0;
}
/* the three phrases at LEVEL 104 and 80: r[phrase][level][LUFS, peak, lim%] */
#define HDR3 "  HELD: LUFS (lin)  lim% |  PHRASE: LUFS (lin)  lim% |  NOTES: LUFS (lin)   pk"
static void loud3(uint32_t e, uint32_t k, double r[3][2][3])
{
    int ph;
    for (ph = 0; ph < 3; ph++) {
        loud(e, k, ph, 104, r[ph][0]);
        loud(e, k, ph, 80, r[ph][1]);
    }
}
static void row3(double r[3][2][3])
{
    printf("  %6.1f (%6.1f) %4.0f |  %6.1f (%6.1f) %4.0f |  %6.1f (%6.1f) %5.1f\n", r[0][0][0], r[0][1][0] + 12, r[0][0][2],
           r[1][0][0], r[1][1][0] + 12, r[1][0][2], r[2][0][0], r[2][1][0] + 12, r[2][0][1]);
}
/* "dnsnd LIST": the sounds of LIST (lines "name|blob hex|drive|pan", from web/fm1dnsnd.js dnsndToQuad: the device
 * manager's import) as FM TONE plays them; then the median */
static int dnsnd_main(const char *list)
{
    static double M[6][1024];
    static uint8_t b[QUAD_BLOB + 16];
    char line[1024];
    uint32_t n = 0, i;
    FILE *f = fopen(list, "r");
    if (!f)
        return 2;
    printf("%-23s |%s\n", "sound (drive)", HDR3);
    while (fgets(line, sizeof line, f) && n < 1024u) {
        char *nm = strtok(line, "|"), *hx = strtok(0, "|"), *dr = strtok(0, "|"), *pn = strtok(0, "|\n");
        double r[3][2][3];
        if (!nm || !hx || !dr || !pn)
            continue;
        memset(b, 0, sizeof b);
        for (i = 0; i < sizeof b && hx[2 * i] && hx[2 * i + 1]; i++)
            sscanf(hx + 2 * i, "%2hhx", &b[i]);
        ld_blob = b;
        ld_bdrive = atoi(dr);
        ld_bpan = atoi(pn);
        loud3(ENGI_QUAD, 0, r);
        printf("%-17.17s (%3d) |", nm, ld_bdrive);
        row3(r);
        for (i = 0; i < 3u; i++)
            M[2 * i][n] = r[i][0][0], M[2 * i + 1][n] = r[i][1][0] + 12;
        n++;
    }
    fclose(f);
    printf("median %u sounds  HELD %6.1f (%6.1f)  PHRASE %6.1f (%6.1f)  NOTES %6.1f (%6.1f)\n", n, median(M[0], n),
           median(M[1], n), median(M[2], n), median(M[3], n), median(M[4], n), median(M[5], n));
    return 0;
}
static int loud_main(int argc, char **argv)
{
    static const uint8_t ENG[] = {ENGI_QUAD, ENGI_VA, ENGI_FM6, ENGI_CZ, ENGI_PROPHET};
    static double M[NELEM(ENG)][6][256];
    uint32_t g, k, j, nm[NELEM(ENG)];
    if (getenv("LD_VEL"))
        ld_vel = atoi(getenv("LD_VEL"));
    if (getenv("LD_QLEVEL"))
        ld_qlevel = atoi(getenv("LD_QLEVEL"));
    if (getenv("LD_DRIVE"))
        ld_drive = atoi(getenv("LD_DRIVE"));
    ld_qinit = getenv("LD_QINIT") != 0;
    printf("%-8s %-13s %4s |%s\n", "engine", "sound", "mono", HDR3);
    for (g = 0; g < NELEM(ENG); g++) {
        const engine_t *en = ENGINES[ENG[g]];
        uint32_t cap = argc > 2 ? (uint32_t)atoi(argv[2]) : 999u;   /* (loud N: the first N sounds of each) */
        nm[g] = 0;
        if (argc > 3 && !strstr(argv[3], en->name))
            continue;
        for (k = 0; k < en->npresets && k < cap; k++) {
            double r[3][2][3];
            loud3(ENG[g], k, r);
            printf("%-8s %-13s %4s |", en->name, en->presets[k].name, en->presets[k].mono ? "mono" : "");
            row3(r);
            if (en->presets[k].mono)
                continue;
            for (j = 0; j < 3u; j++)
                M[g][2 * j][nm[g]] = r[j][0][0], M[g][2 * j + 1][nm[g]] = r[j][1][0] + 12;
            nm[g]++;
        }
    }
    printf("medians over the chord sounds (MONO left out): LUFS (lin)\n");
    for (g = 0; g < NELEM(ENG); g++)
        if (nm[g])
            printf("median %-8s %3u sounds  HELD %6.1f (%6.1f)  PHRASE %6.1f (%6.1f)  NOTES %6.1f (%6.1f)\n",
                   ENGINES[ENG[g]]->name, nm[g], median(M[g][0], nm[g]), median(M[g][1], nm[g]), median(M[g][2], nm[g]),
                   median(M[g][3], nm[g]), median(M[g][4], nm[g]), median(M[g][5], nm[g]));
    (void)argv;
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t k;
    const engine_t *e = ENGINES[ENGI_VA];
    if (argc > 1 && !strcmp(argv[1], "loud"))
        return loud_main(argc, argv);
    if (argc > 2 && !strcmp(argv[1], "dnsnd"))
        return dnsnd_main(argv[2]);
    if (argc > 1 && !strcmp(argv[1], "cz")) {    /* the bank's CZ-1 rows: display name, preset, trim (cr_bank.c) */
        static const struct { const char *row, *pr; int8_t trim; } ROW[] = {
            {"CZ BRASS 1", "BRASS 1", 0}, {"CZ PIANO", "PIANO 1", 0}, {"CZ STRINGS 2", "STRINGS 2", 0},
            {"CZ PIPE", "PIPE ORGAN 1", 0}, {"CZ VIBES", "VIBRAPHONE", 0}, {"CZ BASS", "SYNTH.BASS", 0}};
        printf("%-13s %26s | %26s | %s\n", "CZ-1 bank row", "chord alone: lim  pk  rms", "with SUB BASS: lim  pk  rms",
               "CPU with the bass: host instr / sample, device estimate (1.7 % per 100)");
        for (k = 0; k < NELEM(ROW); k++) {
            double l0, p0, r0, l1, p1, r1;
            uint32_t pr = preset_named(ENGI_CZ, ROW[k].pr);
            ovr_trim = ROW[k].trim;
            if (k == NELEM(ROW) - 1u) {          /* the bass: TINE EP's chord with it */
                ovr_trim = 0;
                measure(ENGI_FM6, 0, 1, ENGI_CZ, pr, &l1, &p1, &r1);
                printf("%-13s %26s | %7.1f %% %6.1f %6.1f dB | %5.0f  %4.1f %%  (with TINE EP's chord)\n", ROW[k].row, "",
                       l1, p1, r1, cpu_ips, cpu_ips * 0.017);
                continue;
            }
            measure(ENGI_CZ, pr, 0, 0, 0, &l0, &p0, &r0);
            measure(ENGI_CZ, pr, 1, 0, 7, &l1, &p1, &r1);   /* ANALOG SUB BASS */
            printf("%-13s %7.1f %% %6.1f %6.1f dB | %7.1f %% %6.1f %6.1f dB | %5.0f  %4.1f %%\n", ROW[k].row, l0, p0, r0,
                   l1, p1, r1, cpu_ips, cpu_ips * 0.017);
        }
        ovr_trim = 0;
        return 0;
    }
#if FELUCCA_PROPHET
    if (argc > 1 && !strcmp(argv[1], "p5")) {    /* P5's programs against the VA's and FM6's presets, dry */
        static const uint8_t ENG[3] = {ENGI_PROPHET, ENGI_VA, ENGI_FM6};
        static double R[3][256];
        double med[3];
        uint32_t n[3] = {0, 0, 0}, g, a, b;
        chord_n = 4;
        dry_all = 1;
        printf("%-22s %s\n", "sound", "a 4-note chord alone, dry: limiter %, peak dB, RMS dB (after the master)");
        for (g = 0; g < 3u; g++) {
            const engine_t *en = ENGINES[ENG[g]];
            for (k = 0; k < en->npresets; k++) {
                double l0, p0, r0;
                if (en->presets[k].mono)
                    continue;                    /* (a bass: not a chord sound) */
                measure(ENG[g], k, 0, 0, 0, &l0, &p0, &r0);
                printf("%-8s %-13s %6.1f %% %6.1f %6.1f\n", en->name, en->presets[k].name, l0, p0, r0);
                R[g][n[g]++] = r0;
            }
            for (a = 0; a < n[g]; a++)           /* the median: sorted in place (the lines above keep the order) */
                for (b = a + 1u; b < n[g]; b++)
                    if (R[g][b] < R[g][a]) {
                        double x = R[g][a];
                        R[g][a] = R[g][b];
                        R[g][b] = x;
                    }
            med[g] = n[g] ? (R[g][(n[g] - 1u) / 2u] + R[g][n[g] / 2u]) / 2 : 0;
        }
        for (g = 0; g < 3u; g++)
            printf("median %-8s %6.1f dB RMS over %u sounds (quartiles %.1f .. %.1f, range %.1f .. %.1f)\n",
                   ENGINES[ENG[g]]->name, med[g], (unsigned)n[g], R[g][n[g] / 4u], R[g][n[g] * 3u / 4u], R[g][0],
                   R[g][n[g] - 1u]);
        printf("P5's median against the VA's: %+.1f dB, against FM6's: %+.1f dB\n", med[0] - med[1], med[0] - med[2]);
        return 0;
    }
#endif
    if (argc > 1 && !strcmp(argv[1], "fm6")) {   /* the bank's FM6 rows: name (display), preset */
        static const char *const ROW[8][2] = {{"TINE EP", "TINE EP"}, {"FM BELL", "BELL"}, {"PIANO", "PIANO"},
                                              {"FM PAD", "PAD"}, {"FM ORGAN", "ORGAN"}, {"MARIMBA", "MARIMBA"},
                                              {"FM PLUCK", "PLUCK"}, {"FM BASS", "FM BASS"}};
        printf("%-13s %26s | %26s | %s\n", "FM6 bank row", "chord alone: lim  pk  rms", "with SUB BASS: lim  pk  rms",
               "CPU with the bass: host instr / sample, device estimate (1.7 % per 100)");
        for (k = 0; k < 8u; k++) {
            double l0, p0, r0, l1, p1, r1;
            static const int8_t PIANO_E[8] = {0, 1, 10, 1, -16, 2, 12, 0};   /* eng_fm6.c's PIANO */
            static const uint8_t PIANO_FX[4] = {1, 36, 16, 46};
            uint32_t pr = preset_named(ENGI_FM6, ROW[k][1]);
            ovr_e = 0;
            ovr_fx = 0;
            if (strcmp(ENGINES[ENGI_FM6]->presets[pr].name, ROW[k][1])) {   /* (PIANO: not in this unit) */
                ovr_e = PIANO_E;
                ovr_fx = PIANO_FX;
                pr = 0;
            }
            if (k == 7u) {                       /* the bass: TINE EP's chord with it */
                measure(ENGI_FM6, 0, 1, ENGI_FM6, pr, &l1, &p1, &r1);
                printf("%-13s %26s | %7.1f %% %6.1f %6.1f dB | %5.0f  %4.1f %%  (with TINE EP's chord)\n", ROW[k][0], "",
                       l1, p1, r1, cpu_ips, cpu_ips * 0.017);
                continue;
            }
            measure(ENGI_FM6, pr, 0, 0, 0, &l0, &p0, &r0);
            measure(ENGI_FM6, pr, 1, 0, 7, &l1, &p1, &r1);   /* ANALOG SUB BASS */
            printf("%-13s %7.1f %% %6.1f %6.1f dB | %7.1f %% %6.1f %6.1f dB | %5.0f  %4.1f %%\n", ROW[k][0], l0, p0, r0,
                   l1, p1, r1, cpu_ips, cpu_ips * 0.017);
        }
        return 0;
    }
    printf("%-13s %26s | %26s | %s\n", "VA preset", "chord alone: lim  pk  rms", "with SUB BASS: lim  pk  rms",
           "CPU with the bass: host instr / sample, device estimate (1.7 % per 100)");
    for (k = 0; k < e->npresets; k++) {
        double l0, p0, r0, l1, p1, r1;
        if (e->presets[k].mono) {               /* a bass: TINE EP's chord (FM6 preset 0) with it */
            measure(ENGI_FM6, 0, 1, ENGI_VA, k, &l1, &p1, &r1);
            printf("%-13s %26s | %7.1f %% %6.1f %6.1f dB | %5.0f  %4.1f %%  (with TINE EP's chord)\n", e->presets[k].name, "",
                   l1, p1, r1, cpu_ips, cpu_ips * 0.017);
            continue;
        }
        measure(ENGI_VA, k, 0, 0, 0, &l0, &p0, &r0);
        measure(ENGI_VA, k, 1, 0, 7, &l1, &p1, &r1);   /* ANALOG SUB BASS */
        printf("%-13s %7.1f %% %6.1f %6.1f dB | %7.1f %% %6.1f %6.1f dB | %5.0f  %4.1f %%\n", e->presets[k].name, l0, p0, r0,
               l1, p1, r1, cpu_ips, cpu_ips * 0.017);
    }
    {   /* references: bank sounds */
        static const uint8_t REF[][2] = {{0, 1}, {0, 11}, {12, 0}, {12, 4}, {12, 8}};   /* .. FM6 PIANO (PRESETS 03) */
        for (k = 0; k < NELEM(REF); k++) {
            double l0, p0, r0, l1, p1, r1;
            measure(REF[k][0], REF[k][1], 0, 0, 0, &l0, &p0, &r0);
            measure(REF[k][0], REF[k][1], 1, 0, 7, &l1, &p1, &r1);
            printf("ref %-9s %7.1f %% %6.1f %6.1f dB | %7.1f %% %6.1f %6.1f dB | %5.0f  %4.1f %%\n",
                   ENGINES[REF[k][0]]->presets[REF[k][1]].name, l0, p0, r0, l1, p1, r1, cpu_ips, cpu_ips * 0.017);
        }
    }
    return 0;
}
