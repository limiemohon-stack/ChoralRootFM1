/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* FM TONE's pops, looked for in the host render (docs/QUAD.md "Pops"), as tests/cz_pops.c looks for CZ-1's.
 *
 * A run: one FM TONE sound on part 0 as a player has it at power-on (LEVEL 104, the sends 0, its DRIVE: the preset's,
 * cr_ui.c cu_drive_load; the preset's voice mode), ChoralRoot's mix (fx_smooth, voice_fade_steal, Output Clean, MASTER
 * 2047), one of the scenarios below on a 4-, 5- or 6-note chord at a root and a velocity. Each run in a fork()ed child.
 * Looked at: the dry mix before the master (fx.c mix_l / mix_r: linear, 32768 = full scale at MASTER's top):
 *   jump   a sample-to-sample step above FLOOR that is at least 4 times the largest step the signal itself makes
 *          around it (600 samples, or 1.25 periods of the run's lowest note if longer): cz_pops.c's measure
 *   click  a 32-sample window of the third difference 12 times above the median of the windows before it and of those
 *          after it, each on its own, and 3 times above the largest within a period of the lowest note: cz_pops.c's
 *   hole   a 32-sample window of zeros between sounding windows
 * and, in the engine's state at each block's events (the voices of quad_vs before and after the note-ons and -offs of
 * the block, before it is rendered: a step there is one the render plays at its first sample):
 *   phase  an operator of a sounding voice restarted (PHRT) where it is heard: a carrier of a voice over -40 dB (an
 *          enveloped one at its envelope x level), a modulator over -66 dB in such a voice
 *   amp    a sounding voice whose amplitude ramp was set back by more than 1/16 of full scale (fresh state)
 *   oplev  a sounding voice whose operator envelope (A or B) was restarted from 0 above 1/4 of its level (RESET)
 *   filt   a sounding voice whose filter states were cleared
 * Usage (cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/quad_pops tests/quad_pops.c -lm):
 *   build/host/quad_pops all [A B]          the matrix over factory sounds A..B (16 = INIT; default 0 16): a line a run
 *   build/host/quad_pops list FILE          the matrix's chord 1 at C4, velocity 100, every scenario, over the sounds
 *                                           of FILE ("name|blob hex|drive|pan" lines: a .dnsnd import's, as
 *                                           tests/va_levels.c dnsnd reads them: node tools/dnsnd_list.mjs DIR)
 *   build/host/quad_pops one P S C R V [out.wav [mix.wav]]   one run of factory sound P (16 INIT), scenario S, chord C,
 *                                           root R, velocity V; its line, the samples around the worst jump, the WAVs
 *   build/host/quad_pops check              the suite's: every factory sound and INIT, every scenario, chords 0 and
 *                                           2, roots C2 C4, velocity 127; fails on a wrap (the sanitizer build),
 *                                           any jump, hole, or a step of a sounding voice's state (phase amp filt;
 *                                           oplev is the patch's RESET, the Digitone's: counted, not a failure)
 * From the environment, for "one": QUAD_TRACE=A,B a line for every live voice at each block between samples A and B;
 * QUAD_LIST=FILE: P is the P-th sound of FILE (a list's). */
#define FELUCCA_VA 1
#define FELUCCA_CZ 1
#define FELUCCA_QUAD 1
#ifndef FM6_POLY
#define FM6_POLY 8               /* as choralroot.c */
#endif
#define main hostsim_main
#include "hostsim.c"
#undef main

enum { S_HOLD, S_RETRIG, S_RETRIG_GAP, S_CHANGE, S_STEAL, S_ARP, S_RESTRIKE, S_ARP_UP, S_N };
static const char *const S_NAME[S_N] = {"hold", "retrig", "retrig-gap", "change", "steal", "arp", "restrike", "arp-up"};
static const int8_t CHORD[4][6] = {{0, 4, 7, 11, -1, -1}, {0, 4, 7, 10, 14, -1}, {0, 4, 7, 9, 11, 14}, {0, 7, 12, 16, 19, 24}};
static const char *const C_NAME[4] = {"4", "5", "6", "6wide"};
static const uint8_t ROOTS[3] = {36, 60, 84};
#define FLOOR 300                /* a jump's least step in the dry mix: -41 dB (cz_pops.c's at CZ_VOICE_GAIN 2) */
#define CLICK_FLOOR 66           /* a click's least third difference (RMS of a window) */
#define SND_INIT 16u             /* the sound "INIT": FM TONE's init patch (cr_ui.c cu_init_load) */
#define MAXS (7u * FS)

typedef struct { uint32_t blk; uint8_t note, vel, on; } ev_t;
static uint32_t lowest_period = 64;
static ev_t ev[1024];
static uint32_t nev, nblk;
static void ev_add(uint32_t ms, int note, uint32_t vel, uint32_t on)
{
    if (note < 0 || note > 127 || nev >= NELEM(ev))
        return;
    ev[nev].blk = ms * (FS / 100u) / (CTL * 10u);
    ev[nev].note = (uint8_t)note;
    ev[nev].vel = (uint8_t)vel;
    ev[nev++].on = (uint8_t)on;
}
static void chord_ev(uint32_t ms, const int8_t *c, int root, uint32_t vel, uint32_t on)
{
    uint32_t i;
    for (i = 0; i < 6u; i++)
        if (c[i] >= 0)
            ev_add(ms, root + c[i], vel, on);
}
/* cz_pops.c's scenarios without the bass part */
static void scenario(uint32_t s, uint32_t ci, int root, uint32_t vel)
{
    const int8_t *c = CHORD[ci];
    uint32_t i, k, nn = 0, end = 5000;
    nev = 0;
    for (i = 0; i < 6u; i++)
        nn += c[i] >= 0;
    switch (s) {
    case S_HOLD:
        chord_ev(0, c, root, vel, 1);
        chord_ev(3000, c, root, vel, 0);
        break;
    case S_RETRIG:                               /* the same chord struck again every 300 ms: off and on in one block */
        for (k = 0; k < 8u; k++) {
            if (k)
                chord_ev(k * 300u, c, root, vel, 0);
            chord_ev(k * 300u, c, root, vel, 1);
        }
        chord_ev(2400, c, root, vel, 0);
        end = 3600;
        break;
    case S_RETRIG_GAP:                           /* .. let go 100 ms before */
        for (k = 0; k < 8u; k++) {
            chord_ev(k * 300u, c, root, vel, 1);
            chord_ev(k * 300u + 200u, c, root, vel, 0);
        }
        end = 3600;
        break;
    case S_CHANGE: {                             /* four chords with common tones, changed in one block */
        static const int8_t PR[4] = {0, -3, 5, 7};
        for (k = 0; k < 8u; k++) {
            if (k)
                chord_ev(k * 500u, c, root + PR[(k - 1u) & 3u], vel, 0);
            chord_ev(k * 500u, c, root + PR[k & 3u], vel, 1);
        }
        chord_ev(4000, c, root + PR[3], vel, 0);
        end = 5200;
        break;
    }
    case S_STEAL:                                /* 12 notes 40 ms apart, all held: more than the 8 voices */
        for (k = 0; k < 12u; k++)
            ev_add(k * 40u, root + c[k % nn] + (k >= nn ? 12 : 0) + (k >= 2u * nn ? 12 : 0), vel, 1);
        for (k = 0; k < 12u; k++)
            ev_add(1500, root + c[k % nn] + (k >= nn ? 12 : 0) + (k >= 2u * nn ? 12 : 0), vel, 0);
        chord_ev(1700, c, root + 1, vel, 1);     /* then three chords with no common tone, the tails still ringing */
        chord_ev(2000, c, root + 1, vel, 0);
        chord_ev(2000, c, root + 6, vel, 1);
        chord_ev(2300, c, root + 6, vel, 0);
        chord_ev(2300, c, root + 3, vel, 1);
        chord_ev(2800, c, root + 3, vel, 0);
        end = 4000;
        break;
    case S_RESTRIKE:                             /* held 1.2 s, struck again 20 ms after its release three times, then
                                                  * two changes with common tones in one block */
        chord_ev(0, c, root, vel, 1);
        for (k = 0; k < 3u; k++) {
            chord_ev(1200u + k * 520u, c, root, vel, 0);
            chord_ev(1220u + k * 520u, c, root, vel, 1);
        }
        chord_ev(3060, c, root, vel, 0);
        chord_ev(3060, c, root + 7, vel, 1);
        chord_ev(3860, c, root + 7, vel, 0);
        chord_ev(3860, c, root, vel, 1);
        chord_ev(4660, c, root, vel, 0);
        end = 5800;
        break;
    case S_ARP_UP:                               /* sixteenths at 120 BPM, a note let go as the next starts, ten passes */
        for (k = 0; k < 10u * nn && k < 44u; k++) {
            ev_add(k * 125u, root + c[k % nn], vel, 1);
            ev_add(k * 125u + 125u, root + c[k % nn], vel, 0);
        }
        end = 5900;
        break;
    default:                                     /* the arp: up and down at 200 BPM sixteenths, each note twice */
        for (k = 0; k < 40u; k++) {
            uint32_t st = (k / 2u) % (2u * nn - 2u), j = st < nn ? st : 2u * nn - 2u - st;
            ev_add(k * 75u, root + c[j], vel, 1);
            ev_add(k * 75u + 40u, root + c[j], vel, 0);
        }
        end = 4200;
        break;
    }
    for (i = 1; i < nev; i++) {                  /* by time, the order of a block's events kept */
        ev_t x = ev[i];
        for (k = i; k && ev[k - 1u].blk > x.blk; k--)
            ev[k] = ev[k - 1u];
        ev[k] = x;
    }
    {
        uint32_t low = 127;
        for (i = 0; i < nev; i++)
            low = ev[i].note < low ? ev[i].note : low;
        lowest_period = (uint32_t)(FS / (440.0 * pow(2.0, ((double)low - 69.0) / 12.0)) * 1.25) + 32u;
    }
    nblk = end * (FS / 100u) / (CTL * 10u);
    if (nblk * CTL > MAXS)
        nblk = MAXS / CTL;
}

static int32_t *mx;                              /* the dry mix (mono) */
static int16_t *ost;                             /* the output, stereo */
typedef struct {
    int32_t part_pk, mix_pk, out_pk;
    uint32_t njump, jump_at, nhole, hole_at, nclick, click_at, click_x;
    int32_t jump_d, jump_ref, jump_ev;
    uint32_t nphase, phase_at, namp, amp_at, nop, op_at, nfilt, filt_at;
    int32_t phase_a, amp_d, op_d;
    uint64_t hash;                               /* FNV-1a 64 of the dry mix: two trees' runs compared bit for bit */
} res_t;

static int dcmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}
static double median(double *v, uint32_t m)
{
    if (!m)
        return 0;
    qsort(v, m, sizeof v[0], dcmp);
    return m & 1u ? v[m / 2u] : (v[m / 2u - 1u] + v[m / 2u]) / 2.0;
}
/* cz_pops.c clicks(), burst: two-sided, against the tone's own edges */
static uint32_t clicks(const int32_t *x, uint32_t n, double floor_, uint32_t *at, uint32_t *ratio)
{
    uint32_t nw = n / 32u, w, i, cnt = 0, K = 32, span = lowest_period / 32u + 1u < 19u ? 19u : lowest_period / 32u + 1u;
    int32_t last = -10;
    double *e = calloc(nw + 1u, sizeof *e), nb[64], na[64], best = 0;
    for (w = 0; w < nw; w++) {
        double sq = 0;
        for (i = 0; i < 32u; i++) {
            uint32_t k = w * 32u + i;
            double d3 = k >= 3u ? (double)x[k] - 3.0 * x[k - 1u] + 3.0 * x[k - 2u] - (double)x[k - 3u] : 0.0;
            sq += d3 * d3;
        }
        e[w] = sqrt(sq / 32.0);
    }
    for (w = 0; w < nw; w++) {
        uint32_t mb = 0, ma = 0;
        double med, r, tiny = 1e-7 * 32768.0, m0, m1, big = 0;
        if (e[w] < floor_)
            continue;
        for (i = w > K ? w - K : 0; i + 2u < w; i++)
            nb[mb++] = e[i];
        for (i = w + 3u; i < w + K && i < nw; i++)
            na[ma++] = e[i];
        m0 = median(nb, mb);
        m1 = median(na, ma);
        med = m0 > m1 ? m0 : m1;
        for (i = w > span ? w - span : 0; i <= w + span && i < nw; i++)
            if (i + 2u < w || i > w + 2u)
                big = e[i] > big ? e[i] : big;
        if (e[w] < 3.0 * big)
            continue;
        r = e[w] / (med > tiny ? med : tiny);
        if (r >= 12.0) {
            if ((int32_t)w - last > 4)
                cnt++;
            last = (int32_t)w;
            if (r > best) {
                best = r;
                *at = w * 32u;
                *ratio = r > 99999.0 ? 99999u : (uint32_t)r;
            }
        }
    }
    free(e);
    return cnt;
}
/* cz_pops.c jumps(): the count, the worst (by step / reference) */
static uint32_t jumps(const int32_t *x, uint32_t n, int32_t floor_, uint32_t *at, int32_t *dmax, int32_t *refmax)
{
    uint32_t nw = n / 32u, w, i, cnt = 0, last = 0, span = lowest_period / 32u + 1u < 19u ? 19u : lowest_period / 32u + 1u;
    int32_t *bm = calloc(nw + 1u, sizeof *bm);
    double best = 0;
    for (i = 1; i < nw * 32u; i++) {
        int32_t d = abs(x[i] - x[i - 1u]);
        if (d > bm[i / 32u])
            bm[i / 32u] = d;
    }
    for (i = 1; i < nw * 32u; i++) {
        int32_t d = abs(x[i] - x[i - 1u]), ref = 1;
        uint32_t j;
        if (d < floor_)
            continue;
        w = i / 32u;
        for (j = w > span ? w - span : 0; j <= w + span && j < nw; j++)
            if (j + 1u < w || j > w + 1u)
                ref = bm[j] > ref ? bm[j] : ref;
        for (j = (w > 0 ? w - 1u : 0) * 32u; j < (w + 2u) * 32u && j < nw * 32u; j++)
            if (j && (j + 2u < i || j > i + 2u)) {
                int32_t e = abs(x[j] - x[j - 1u]);
                ref = e > ref ? e : ref;
            }
        if (d >= 4 * ref) {
            if (!cnt || i > last + 4u)
                cnt++;
            last = i;
            if ((double)d / ref > best) {
                best = (double)d / ref;
                *at = i;
                *dmax = d;
                *refmax = ref;
            }
        }
    }
    free(bm);
    return cnt;
}

static const uint8_t *snd_blob;                  /* "list": an imported sound over preset 0, its Drive and Pan */
static int snd_drive, snd_pan;
static uint32_t tr_on, tr_a, tr_b;

/* sound k of a list file ("name|blob hex|drive|pan" lines) into snd_blob, snd_drive, snd_pan: its name, 0 past the end */
static const char *list_sound(const char *path, uint32_t k)
{
    static uint8_t bl[QUAD_BLOB + 16];
    static char ln[1024];
    FILE *f = fopen(path, "r");
    uint32_t i = 0;
    if (!f)
        return 0;
    while (fgets(ln, sizeof ln, f)) {
        char *nm = strtok(ln, "|"), *hx = strtok(0, "|"), *dr = strtok(0, "|"), *pn = strtok(0, "|\n");
        if (!nm || !hx || !dr || !pn || i++ != k)
            continue;
        memset(bl, 0, sizeof bl);
        for (i = 0; i < sizeof bl && hx[2 * i] && hx[2 * i + 1]; i++)
            sscanf(hx + 2 * i, "%2hhx", &bl[i]);
        snd_blob = bl;
        snd_drive = atoi(dr);
        snd_pan = atoi(pn);
        fclose(f);
        return nm;
    }
    fclose(f);
    return 0;
}

/* the engine's state at a block's events: what a note-on moved in a voice that sounds (the render plays it at the
 * block's first sample) */
static void probe(const quad_voice_t *b, const quad_voice_t *a, uint32_t at, res_t *r)
{
    uint32_t i, k;
    for (i = 0; i < QUAD_POLY; i++) {
        int32_t amp = b[i].amp < 0 ? -b[i].amp : b[i].amp;
        if (!b[i].live || amp < 33)              /* (-60 dB) */
            continue;
        for (k = 0; k < 4u; k++) {               /* an operator restarted where it is heard: a carrier of a voice
                                                  * over -40 dB (an enveloped one at its level), or a modulator over
                                                  * -66 dB in such a voice (the routings: QUAD_ALGO) */
            const quad_algo_t *al = &QUAD_ALGO[(uint32_t)clamp(quad_patch[0][QP_ALGO], 1, 8) - 1u];
            int32_t lv = k ? abs(b[i].lv[k - 1u]) : 32767, heard = 0;
            uint32_t mods = al->mod[0] | al->mod[1] | al->mod[2] | al->mod[3];
            if (b[i].ph[k] == a[i].ph[k])
                continue;
            if (((al->x | al->y) >> k) & 1u)
                heard = ((al->env >> k) & 1u ? (int32_t)(((int64_t)amp * lv) >> 15) : amp) >= 328;
            if ((((mods >> k) & 1u) || k == al->fb) && lv >= 16 && amp >= 328)
                heard = 1;
            if (heard) {
                if (!r->nphase++)
                    r->phase_at = at;
                r->phase_a = amp > r->phase_a ? amp : r->phase_a;
            }
        }
        if (b[i].amp - a[i].amp > 2048) {
            if (!r->namp++)
                r->amp_at = at;
            r->amp_d = b[i].amp - a[i].amp > r->amp_d ? b[i].amp - a[i].amp : r->amp_d;
        }
        for (k = 0; k < 2u; k++)
            if (a[i].env[k] == 0 && b[i].env[k] > (1 << 22) && b[i].lv[k ? 1 : 0] + (k ? b[i].lv[2] : 0) > 2048) {
                if (!r->nop++)
                    r->op_at = at;
                r->op_d = b[i].env[k] >> 9 > r->op_d ? b[i].env[k] >> 9 : r->op_d;
            }
        if ((b[i].f1 || b[i].f2) && !a[i].f1 && !a[i].f2) {
            if (!r->nfilt++)
                r->filt_at = at;
        }
    }
}

static void trace(uint32_t at)
{
    uint32_t i;
    for (i = 0; i < QUAD_POLY; i++) {
        const quad_voice_t *s = &quad_vs[0][i];
        const voice_t *v = &trk[0].v[i];
        if (!s->live && !v->active)
            continue;
        printf("trace %u voice %u note %u gate %u active %u vstage %u live %u st %u/%u/%u/%u env %d/%d/%d/%d amp %d lv "
               "%d/%d/%d ph %08x %08x %08x %08x\n", at, i, v->note, v->gate, v->active, v->stage, s->live, s->stage[0],
               s->stage[1], s->stage[2], s->stage[3], s->env[0] >> 9, s->env[1] >> 9, s->env[2] >> 9, s->env[3] >> 9,
               s->amp, s->lv[0], s->lv[1], s->lv[2], s->ph[0], s->ph[1], s->ph[2], s->ph[3]);
    }
}

static void render(uint32_t pr, uint32_t s, uint32_t ci, int root, uint32_t vel, res_t *r)
{
    int32_t out[2 * CTL];
    uint32_t i, b, e = 0;
    static quad_voice_t before[QUAD_POLY];
    const preset_t *p = &ENGINES[ENGI_QUAD]->presets[pr < SND_INIT ? pr : 0];
    memset(r, 0, sizeof *r);
    scenario(s, ci, root, vel);
    mx = calloc(nblk * CTL, sizeof *mx);
    ost = calloc(nblk * CTL * 2u, sizeof *ost);
    host_tracks_init();
    fx_smooth = 1;
    voice_fade_steal = 1;
    song.master_q12 = 2047;
    song.g[G_BPM] = 120;
    fx_output_set(1, 1);                         /* Options > Output Clean (the default) */
    host_preset(&trk[0], ENGI_QUAD, pr < SND_INIT ? pr : 0);
    for (i = P_DIST; i <= P_REV; i++)
        trk[0].p[i] = 0;
    trk[0].p[P_DIST] = (int16_t)(p->fx[0] ? p->fx[0] - 1 : 0);
    if (pr == SND_INIT) {
        quad_blob_preset(&trk[0], QUAD_NPRESETS);
        trk[0].p[P_DIST] = 0;
        trk[0].p[P_VOICE] = V_POLY;
    }
    if (snd_blob) {
        quad_blob_set(&trk[0], snd_blob);
        trk[0].p[P_DIST] = (int16_t)snd_drive;
        trk[0].p[P_PAN] = (int16_t)snd_pan;
        trk[0].p[P_VOICE] = V_POLY;
    }
    trk[0].p[P_LEVEL] = 104;
    for (i = 1; i < NPART; i++)
        trk[i].p[P_LEVEL] = 0;
    for (b = 0; b < nblk; b++) {
        memcpy(before, quad_vs[0], sizeof before);
        for (; e < nev && ev[e].blk <= b; e++) {
            if (ev[e].on)
                trk_note_on(&trk[0], ev[e].note, ev[e].vel);
            else
                trk_note_off(&trk[0], ev[e].note);
        }
        probe(before, quad_vs[0], b * CTL, r);
        if (tr_on && b * CTL + CTL > tr_a && b * CTL < tr_b)
            trace(b * CTL);
        trk[0].peak = 0;
        mix_block(out, CTL);
        r->part_pk = trk[0].peak > r->part_pk ? trk[0].peak : r->part_pk;
        for (i = 0; i < CTL; i++) {
            int32_t m = (mix_l[i] + mix_r[i]) / 2;
            mx[b * CTL + i] = m;
            ost[2u * (b * CTL + i)] = (int16_t)clamp(out[2u * i], -32768, 32767);
            ost[2u * (b * CTL + i) + 1u] = (int16_t)clamp(out[2u * i + 1u], -32768, 32767);
            r->mix_pk = abs(m) > r->mix_pk ? abs(m) : r->mix_pk;
            r->out_pk = abs(out[2u * i]) > r->out_pk ? abs(out[2u * i]) : r->out_pk;
        }
    }
    r->hash = 1469598103934665603ull;
    for (i = 0; i < nblk * CTL; i++)
        for (b = 0; b < 4u; b++) {
            r->hash ^= (uint8_t)((uint32_t)mx[i] >> (8u * b));
            r->hash *= 1099511628211ull;
        }
    r->njump = jumps(mx, nblk * CTL, FLOOR, &r->jump_at, &r->jump_d, &r->jump_ref);
    r->nclick = clicks(mx, nblk * CTL, CLICK_FLOOR, &r->click_at, &r->click_x);
    for (b = 1; b + 1u < nblk * CTL / 32u; b++) {   /* holes */
        int32_t pk0 = 0, pk1 = 0, pk2 = 0;
        for (i = 0; i < 32u; i++) {
            pk0 = abs(mx[(b - 1u) * 32u + i]) > pk0 ? abs(mx[(b - 1u) * 32u + i]) : pk0;
            pk1 = abs(mx[b * 32u + i]) > pk1 ? abs(mx[b * 32u + i]) : pk1;
            pk2 = abs(mx[(b + 1u) * 32u + i]) > pk2 ? abs(mx[(b + 1u) * 32u + i]) : pk2;
        }
        if (!pk1 && pk0 > 300 && pk2 > 300 && !r->nhole++)
            r->hole_at = b * 32u;
    }
    {   /* the event nearest before the worst jump: its distance in samples */
        int32_t best = -1;
        for (i = 0; i < nev; i++)
            if (ev[i].blk * CTL <= r->jump_at)
                best = (int32_t)(r->jump_at - ev[i].blk * CTL) * 2 + ev[i].on;
        r->jump_ev = best;
    }
}

static const char *snd_name(uint32_t pr)
{
    return pr == SND_INIT ? "INIT" : ENGINES[ENGI_QUAD]->presets[pr].name;
}
static void line(const char *name, uint32_t s, uint32_t ci, int root, uint32_t vel, const res_t *r)
{
    printf("\"%s\" %s %s %d %u | part %d mix %d out %d | jump %u at %u d %d ref %d ev %d %s | click %u at %u x %u | hole "
           "%u at %u | phase %u at %u amp %d | amp %u at %u d %d | oplev %u at %u from %d | filt %u at %u | %016llx\n",
           name, S_NAME[s], C_NAME[ci], root, vel, r->part_pk, r->mix_pk, r->out_pk, r->njump, r->jump_at, r->jump_d,
           r->jump_ref, r->jump_ev < 0 ? -1 : r->jump_ev / 2, r->jump_ev < 0 ? "-" : r->jump_ev & 1 ? "on" : "off",
           r->nclick, r->click_at, r->click_x, r->nhole, r->hole_at, r->nphase, r->phase_at, r->phase_a, r->namp,
           r->amp_at, r->amp_d, r->nop, r->op_at, r->op_d, r->nfilt, r->filt_at, (unsigned long long)r->hash);
}

/* a run in a child: its result through a pipe; its line (quiet: none) */
static int run_quiet;
static int run(const char *name, uint32_t pr, uint32_t s, uint32_t ci, int root, uint32_t vel, res_t *r)
{
    int fd[2], st = 0, ok;
    fflush(stdout);
    if (pipe(fd))
        exit(2);
    if (!fork()) {
        close(fd[0]);
        render(pr, s, ci, root, vel, r);
        _exit(write(fd[1], r, sizeof *r) != sizeof *r);
    }
    close(fd[1]);
    ok = read(fd[0], r, sizeof *r) == sizeof *r;
    close(fd[0]);
    wait(&st);
    if (!ok)
        memset(r, 0, sizeof *r);
    if (!run_quiet)
        line(name, s, ci, root, vel, r);
    return ok;
}

static void wav16(const char *path, const int16_t *st, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    wav_hdr(f, frames);
    fwrite(st, 4, frames, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    res_t r;
    if (argc >= 2 && !strcmp(argv[1], "all")) {
        uint32_t a = argc > 2 ? (uint32_t)atoi(argv[2]) : 0u, z = argc > 3 ? (uint32_t)atoi(argv[3]) : SND_INIT, pr, s,
                 ci, ri, v;
        for (pr = a; pr <= z && pr <= SND_INIT; pr++)
            for (s = 0; s < S_N; s++)
                for (ci = 0; ci < 4u; ci++)
                    for (ri = 0; ri < 3u; ri++)
                        for (v = 0; v < 2u; v++)
                            run(snd_name(pr), pr, s, ci, ROOTS[ri], v ? 127u : 100u, &r);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "list")) {
        uint32_t s, n = 0;
        const char *nm;
        while ((nm = list_sound(argv[2], n++)))
            for (s = 0; s < S_N; s++)
                run(nm, 0, s, 1, 60, 100, &r);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "check")) {
        static const uint8_t CI[2] = {0, 2};
        uint32_t pr, s, ci, ri, bad = 0, n = 0;
        run_quiet = 1;
        for (pr = 0; pr <= SND_INIT; pr++)
            for (s = 0; s < S_N; s++)
                for (ci = 0; ci < 2u; ci++)
                    for (ri = 0; ri < 2u; ri++) {
                        int ok = run(snd_name(pr), pr, s, CI[ci], ROOTS[ri], 127u, &r);
                        n++;
                        if (!ok || r.njump || r.nhole || r.nphase || r.namp || r.nfilt) {
                            bad++;
                            printf("FAIL ");
                            line(snd_name(pr), s, CI[ci], ROOTS[ri], 127u, &r);
                        }
                    }
        printf("quad_pops check: %u runs, %u with a wrap, a jump, a hole or a step of a sounding voice's state\n", n, bad);
        return bad != 0;
    }
    if (getenv("QUAD_TRACE") && sscanf(getenv("QUAD_TRACE"), "%u,%u", &tr_a, &tr_b) == 2)
        tr_on = 1;
    if (argc >= 7 && !strcmp(argv[1], "one")) {
        uint32_t pr = (uint32_t)atoi(argv[2]) % (SND_INIT + 1u), s = (uint32_t)atoi(argv[3]) % S_N,
                 ci = (uint32_t)atoi(argv[4]) % 4u, vel = (uint32_t)atoi(argv[6]), i;
        int root = atoi(argv[5]);
        const char *nm = snd_name(pr);
        if (getenv("QUAD_LIST"))                 /* QUAD_LIST=FILE: P is the line of FILE (a list's sound) */
            nm = list_sound(getenv("QUAD_LIST"), (uint32_t)atoi(argv[2]));
        if (!nm)
            return 2;
        render(pr, s, ci, root, vel, &r);
        line(nm, s, ci, root, vel, &r);
        if (argc > 7)
            wav16(argv[7], ost, nblk * CTL);
        if (argc > 8) {                          /* the dry mix, a quarter of its level */
            for (i = 0; i < nblk * CTL; i++)
                ost[2u * i] = ost[2u * i + 1u] = (int16_t)clamp(mx[i] / 4, -32768, 32767);
            wav16(argv[8], ost, nblk * CTL);
        }
        if (r.njump) {
            uint32_t k = r.jump_at;
            printf("dry mix around the worst jump (sample %u = %.4f s):", k, (double)k / FS);
            for (i = k > 6u ? k - 6u : 0; i < k + 6u && i < nblk * CTL; i++)
                printf(" %d", mx[i]);
            printf("\n");
        }
        return 0;
    }
    printf("usage: quad_pops all [A B] | list FILE | one P S C R V [out.wav [mix.wav]] | check\n");
    return 1;
}
