/* SPDX-License-Identifier: GPL-3.0-only
 * Harmony's musical benchmark (firmware/src/cr_harmony.c against the real chord engine, cr_engine.c).
 * Not a pass/fail test: a report to read with your ears, and a handful of numbers to re-run after any change to the
 * suggestion engine, so "is it more musical now?" has a before and after.
 *
 *   cc -std=c99 -Wall -Wextra -O2 -o build/host/cr_harmony_bench tests/cr_harmony_bench.c \
 *      firmware/src/cr_harmony.c firmware/src/cr_engine.c
 *   build/host/cr_harmony_bench            the report
 *   build/host/cr_harmony_bench --json     the same results as JSON (one object)
 *
 * Every progression is PLAYED, not pushed: each chord goes through the played-chord listener exactly as the keys
 * send it (struck, held 600 ms, released, 100 ms apart), key inference runs after each chord that commits (Key Mode
 * off, as the UI does), and the last chord is voiced as the engine voices a struck root key (the lowest root key of
 * that pitch class, D4..C#5, voicing 0) -- the "sounding chord" the layer suggests from.
 *
 *   1. Same chord, different past: pairs of progressions ending on the same chord (the handover's benchmark)
 *   2. Function: one key held (Key Mode), each diatonic chord as the sounding chord
 *   3. Key reading: what key it hears after each chord of common progressions, against a musician's read
 *   4. Confirm feedback: play D, confirm PULL, confirm REST -- what it hears afterwards
 *   5. Harmony mode's lanes (what the UI plays now; 1-4 measure the retired Harmony Preview's suggestion engine): the
 *      palette of each home key in its three voicing styles, the top line of a progression, the top notes' band
 *   6. The research pack's 40 phrases (docs/research/harmony_presets.json, tests/cr_harmony_pack.h): which ones the
 *      palette plays as written, home C in any mode, any key, colour world and chord keys held -- in one world,
 *      only with a KNOB 1 turn, only with a bass of its own (slash chords, pedals), or not (a chord it can't reach)
 *
 * The "musician's read" annotations are deliberately limited to progressions with an uncontroversial home; they
 * are expectations to argue with, not ground truth. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../firmware/src/cr_harmony.h"
#include "cr_harmony_pack.h"

static int json;

/* ------------------------------------------------------------------ chord symbols --- */
/* "C" "Am" "G7" "Fmaj7" "Dm7" "Bdim" "Bm7b5" "Csus" "Cm9" "Ab" "F#m": the subset the tables use */
typedef struct { uint8_t pc, q, x; } chord_t;

static int parse_chord(const char *s, chord_t *c)
{
    static const int8_t L[7] = { 9, 11, 0, 2, 4, 5, 7 };   /* A B C D E F G */
    int pc;
    if (*s < 'A' || *s > 'G') return 0;
    pc = L[*s - 'A'];
    s++;
    if (*s == '#') { pc++; s++; } else if (*s == 'b') { pc--; s++; }
    c->pc = (uint8_t)((pc + 12) % 12);
    c->q = CR_Q_MAJ; c->x = 0;
    if (!strncmp(s, "maj7", 4)) { c->x = CR_EXT_MAJ7; s += 4; }
    else if (!strncmp(s, "m7b5", 4)) { c->q = CR_Q_DIM; c->x = CR_EXT_M7; s += 4; }
    else if (!strncmp(s, "dim", 3)) { c->q = CR_Q_DIM; s += 3; }
    else if (!strncmp(s, "sus", 3)) { c->q = CR_Q_SUS; s += 3; }
    else if (*s == 'm') { c->q = CR_Q_MIN; s++; }
    if (*s == '7') { c->x |= CR_EXT_M7; s++; }
    else if (*s == '9') { c->x |= CR_EXT_M7 | CR_EXT_9; s++; }
    else if (*s == '6') { c->x |= CR_EXT_6; s++; }
    return *s == 0;
}

static void name_of(uint8_t pc, uint8_t q, uint8_t x, char *out, size_t n)
{
    char r[3], qq[5], sup[CR_SUP_MAX];
    cr_chord_name(pc, q, x, r, qq, sup);
    snprintf(out, n, "%s%s%s", r, qq, sup);
}

static const char *const NOTE[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

/* --------------------------------------------------------------- playing it in --- */
typedef struct {
    cr_harm_listen_t L;
    cr_harm_hist_t h;
    cr_harm_key_t key;
    uint32_t t, gid;
    char trace[256];       /* the key after each chord: "-,-,F,Am" */
} player_t;

static void key_str(const cr_harm_key_t *k, char *out, size_t n)
{
    if (!k->held) snprintf(out, n, "-");
    else snprintf(out, n, "%s%s", NOTE[k->tonic_pc % 12], k->scale == CR_SCALE_MINOR ? "m" : "");
}

static void play_start(player_t *p)
{
    memset(p, 0, sizeof *p);
    cr_harm_listen_clear(&p->L); cr_harm_hist_clear(&p->h); cr_harm_key_clear(&p->key);
    p->t = 1000; p->gid = 1;
}

/* the root key a player strikes for a pitch class: the lowest root key (D4 = 62 .. C#5 = 73) */
static int root_key(uint8_t pc) { return 62 + (pc + 10) % 12; }

static void play_chord(player_t *p, const chord_t *c)
{
    char k[8];
    int16_t root = (int16_t)root_key(c->pc);
    cr_harm_listen_event(&p->L, &p->h, p->gid, root, c->q, c->x, 1, p->t);
    if (cr_harm_listen_tick(&p->L, &p->h, p->t + 300)) cr_harm_key_infer(&p->key, &p->h);
    if (cr_harm_listen_event(&p->L, &p->h, p->gid, root, c->q, c->x, 0, p->t + 600)) cr_harm_key_infer(&p->key, &p->h);
    p->gid++; p->t += 700;
    key_str(&p->key, k, sizeof k);
    if (p->trace[0]) strncat(p->trace, ",", sizeof p->trace - strlen(p->trace) - 1);
    strncat(p->trace, k, sizeof p->trace - strlen(p->trace) - 1);
}

/* the sounding chord as the engine shows it after that key was struck: cr_chord_info_t of a fresh voice */
static void sounding(const chord_t *c, cr_chord_info_t *ci)
{
    uint8_t base[CR_CHORD_MAX];
    int n = cr_chord_base_ex(root_key(c->pc), c->q, c->x, 0, base);
    memset(ci, 0, sizeof *ci);
    ci->valid = 1; ci->sounding = 1;
    ci->root_pc = c->pc; ci->quality = c->q; ci->ext = c->x;
    ci->nnotes = (uint8_t)cr_voice_apply(base, n, 0, ci->notes);
}

static int play_prog(player_t *p, const char *prog, chord_t *last)
{
    char buf[128], *tok;
    int n = 0;
    snprintf(buf, sizeof buf, "%s", prog);
    for (tok = strtok(buf, " "); tok; tok = strtok(0, " ")) {
        chord_t c;
        if (!parse_chord(tok, &c)) { fprintf(stderr, "bench: bad chord '%s' in '%s'\n", tok, prog); exit(2); }
        play_chord(p, &c);
        *last = c;
        n++;
    }
    return n;
}

/* ---------------------------------------------------------------- the results --- */
typedef struct {
    char name[3][24];
    uint8_t pc[3], q[3], x[3], kind[3];
    int self[3];           /* the slot is the sounding chord itself (root, quality and extensions) */
    int same_root[3];      /* .. or the same root and quality, extensions changed (a colour, not a move) */
} sugg_t;

static void suggest(const cr_harm_key_t *key, const chord_t *src, sugg_t *s)
{
    cr_chord_info_t ci; cr_harm_result_t out[3]; int i;
    sounding(src, &ci);
    memset(s, 0, sizeof *s);
    memset(out, 0, sizeof out);
    cr_harm_suggest(key, &ci, 0, out);
    for (i = 0; i < 3; i++) {
        s->kind[i] = out[i].kind;
        if (out[i].kind == CR_HARM_UNAVAILABLE) { snprintf(s->name[i], sizeof s->name[i], "(none)"); continue; }
        s->pc[i] = out[i].root_pc; s->q[i] = out[i].quality; s->x[i] = out[i].ext;
        name_of(out[i].root_pc, out[i].quality, out[i].ext, s->name[i], sizeof s->name[i]);
        if (out[i].kind == CR_HARM_VOICING) strncat(s->name[i], "(v)", sizeof s->name[i] - strlen(s->name[i]) - 1);
        s->self[i] = out[i].root_pc == src->pc && out[i].quality == src->q && out[i].ext == src->x;
        s->same_root[i] = out[i].root_pc == src->pc && out[i].quality == src->q;
    }
}

static int sugg_equal(const sugg_t *a, const sugg_t *b)
{
    int i;
    for (i = 0; i < 3; i++)
        if (a->pc[i] != b->pc[i] || a->q[i] != b->q[i] || a->x[i] != b->x[i] || a->kind[i] != b->kind[i]) return 0;
    return 1;
}

/* a musician's key: "C" major, "Am" minor */
static int key_matches(const cr_harm_key_t *k, const char *want)
{
    chord_t c;
    if (!want || !k->held) return 0;
    if (!parse_chord(want, &c)) return 0;
    return k->tonic_pc == c.pc && (k->scale == CR_SCALE_MINOR) == (c.q == CR_Q_MIN);
}

/* ----------------------------------------------------------------- 1. the pairs --- */
typedef struct { const char *a, *a_read, *a_key, *b, *b_read, *b_key; } pair_t;
static const pair_t PAIRS[] = {
    { "C F Dm G",   "G is V, wants C",                  "C",
      "G C D G",    "G is home",                         "G" },
    { "C G Am F",   "F is IV, home is C",               "C",
      "F Bb C7 F",  "F is home",                         "F" },
    { "C Am Dm",    "Dm is ii, heading for G7-C",       "C",
      "Dm Bb A7 Dm","Dm is home",                        "Dm" },
    { "C F G Am",   "Am is a deceptive vi",             "C",
      "Am Dm E7 Am","Am is home",                        "Am" },
    { "C Dm G",     "G as V of C",                      "C",
      "C D7 G",     "G tonicised (V/V -> V)",            0 },
    { "C Am F G",   "G is V (I-vi-IV-V)",               "C",
      "C F Am G",   "G is V, same key, other route",    "C" },
};
#define NPAIRS (int)(sizeof PAIRS / sizeof PAIRS[0])

/* ------------------------------------------------------- 3. key reading corpus --- */
typedef struct { const char *prog, *key, *what; } prog_t;
static const prog_t CORPUS[] = {
    { "C Am F G",     "C",  "I-vi-IV-V (doo-wop)" },
    { "C G Am F",     "C",  "I-V-vi-IV (pop)" },
    { "C F G C",      "C",  "I-IV-V-I" },
    { "Dm7 G7 Cmaj7", "C",  "ii-V-I" },
    { "C Dm G7 C",    "C",  "I-ii-V7-I" },
    { "G D Em C",     "G",  "I-V-vi-IV in G" },
    { "D Bm G A",     "D",  "I-vi-IV-V in D" },
    { "F Dm Bb C",    "F",  "I-vi-IV-V in F" },
    { "Am Dm E7 Am",  "Am", "i-iv-V7-i (harmonic minor)" },
    { "Cm Fm G7 Cm",  "Cm", "i-iv-V7-i" },
    { "Am F G Am",    "Am", "i-VI-VII-i (Aeolian)" },
    { "Cm Ab Bb Cm",  "Cm", "i-VI-VII-i" },
    { "Em C D Em",    "Em", "i-VI-VII-i in E minor" },
    { "Dm Gm A7 Dm",  "Dm", "i-iv-V7-i in D minor" },
};
#define NCORPUS (int)(sizeof CORPUS / sizeof CORPUS[0])

/* ------------------------------------------------------------------- the report --- */
static void jstr(const char *k, const char *v, int comma) { printf("\"%s\":\"%s\"%s", k, v, comma ? "," : ""); }

static void print_sugg_text(const sugg_t *s)
{
    int i;
    for (i = 0; i < 3; i++)
        printf("  %-4s %-10s%s", i == 0 ? "REST" : i == 1 ? "MOVE" : "PULL", s->name[i],
               s->self[i] ? "*" : s->same_root[i] ? "~" : " ");
}
static void print_sugg_json(const char *key, const sugg_t *s)
{
    int i;
    printf("\"%s\":[", key);
    for (i = 0; i < 3; i++)
        printf("{\"name\":\"%s\",\"self\":%d,\"same_root\":%d}%s", s->name[i], s->self[i], s->same_root[i], i < 2 ? "," : "");
    printf("]");
}

/* ---------------------------------------------------------------- 5: the lanes --- */
typedef struct {
    double step[CR_HSTYLE_N];   /* the demo progression: semitones the top note moves a chord, by style */
    int band[CR_HSTYLE_N];      /* every chord of every key: the top notes' spread (semitones), by style */
    int same;                   /* 1: every key gives the same notes on a second press, whatever came between */
} lanestats_t;

static const char *const STYLE_NAME[CR_HSTYLE_N] = { "shell", "spread", "colour" };
static void notes_str(const uint8_t *n, int c, char *out, size_t sz)
{
    static const char *const NN[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    size_t o = 0;
    int i;
    out[0] = 0;
    for (i = 0; i < c && o + 6 < sz; i++) o += (size_t)snprintf(out + o, sz - o, "%s%s%d", i ? " " : "", NN[n[i] % 12], n[i] / 12 - 1);
}

static void lanes_section(lanestats_t *ls)
{
    static const char *const WK[CR_HMAP_W] = { "D4", "E4", "F4", "G4", "A4", "B4", "C5", "D5", "E5", "F5", "G5" };
    static const char *const BK[CR_HMAP_B] = { "D#4", "F#4", "G#4", "A#4", "C#5", "D#5", "F#5" };
    static const int DEMO[] = { 3, 2, 1, 4, 8, 9, 0 };   /* Fmaj9 Em7 Am9 Dm9 G9sus G9 Cmaj9: G4 F4 E4 A4 E5 F5 D4 */
    cr_hmap_t m;
    int sc, i, st, t;
    memset(ls, 0, sizeof *ls);
    if (!json) printf("5. HARMONY MODE'S LANES: a fixed palette per home key, three voicing styles (* = home)\n\n");
    else printf("\"lanes\":{\"keys\":[");
    for (sc = 0; sc < 2; sc++) {
        cr_harm_lanes(sc ? 9 : 0, (uint8_t)sc, &m);
        if (!json) printf("  home %s            shell              spread                 colour\n", sc ? "A minor" : "C major");
        for (i = 0; i < CR_HMAP_W + CR_HMAP_B; i++) {
            const cr_hmap_chord_t *d = i < CR_HMAP_W ? &m.w[i] : &m.b[i - CR_HMAP_W];
            char nm[24], v[CR_HSTYLE_N][40];
            name_of(d->root_pc, d->quality, d->ext, nm, sizeof nm);
            for (st = 0; st < CR_HSTYLE_N; st++) {
                uint8_t n[CR_CHORD_MAX], r;
                notes_str(n, cr_harm_voice_style(d, st, 0, 0, n, &r), v[st], sizeof v[st]);
            }
            if (!json) printf("  %-4s %-10s%s  %-18s %-22s %s\n", i < CR_HMAP_W ? WK[i] : BK[i - CR_HMAP_W], nm,
                              d->home ? "*" : " ", v[0], v[1], v[2]);
            else { printf("{"); jstr("home", sc ? "Am" : "C", 1); jstr("key", i < CR_HMAP_W ? WK[i] : BK[i - CR_HMAP_W], 1);
                   jstr("chord", nm, 1); jstr("shell", v[0], 1); jstr("spread", v[1], 1); jstr("colour", v[2], 0);
                   printf("}%s", sc == 0 || i < CR_HMAP_W + CR_HMAP_B - 1 ? "," : ""); }
        }
        if (!json) printf("\n");
    }
    cr_harm_lanes(0, CR_SCALE_MAJOR, &m);
    if (!json) printf("  Fmaj9 Em7 Am9 Dm9 G9sus G9 Cmaj9 (G4 F4 E4 A4 E5 F5 D4), the top line by style:\n");
    for (st = 0; st < CR_HSTYLE_N; st++) {
        int prev = -1, moved = 0;
        char line[160];
        size_t o = 0;
        line[0] = 0;
        for (i = 0; i < (int)(sizeof DEMO / sizeof DEMO[0]); i++) {
            uint8_t n[CR_CHORD_MAX], r;
            int c = cr_harm_voice_style(&m.w[DEMO[i]], st, 0, 0, n, &r);
            char nn[8];
            notes_str(&n[c - 1], 1, nn, sizeof nn);
            o += (size_t)snprintf(line + o, sizeof line - o, "%s%s", i ? " " : "", nn);
            if (prev >= 0) moved += n[c - 1] > prev ? n[c - 1] - prev : prev - n[c - 1];
            prev = n[c - 1];
        }
        ls->step[st] = (double)moved / (double)((int)(sizeof DEMO / sizeof DEMO[0]) - 1);
        if (!json) printf("    %-7s %s  (%.1f semitones a chord)\n", STYLE_NAME[st], line, ls->step[st]);
    }
    for (st = 0; st < CR_HSTYLE_N; st++) {      /* every chord of every key: the band the top notes stay in */
        int lo = 127, hi = 0;
        for (sc = 0; sc < 2; sc++)
            for (t = 0; t < 12; t++) {
                cr_harm_lanes((uint8_t)t, (uint8_t)sc, &m);
                for (i = 0; i < CR_HMAP_W + CR_HMAP_B; i++) {
                    uint8_t n[CR_CHORD_MAX], r;
                    int c = cr_harm_voice_style(i < CR_HMAP_W ? &m.w[i] : &m.b[i - CR_HMAP_W], st, 0, 0, n, &r);
                    if (n[c - 1] < lo) lo = n[c - 1];
                    if (n[c - 1] > hi) hi = n[c - 1];
                }
            }
        ls->band[st] = hi - lo;
    }
    ls->same = 1;                               /* (a pure function of the chord: checked, not assumed) */
    {
        uint8_t a[CR_CHORD_MAX], b[CR_CHORD_MAX], r;
        int ca, cb;
        cr_harm_lanes(0, CR_SCALE_MAJOR, &m);
        ca = cr_harm_voice_style(&m.w[1], CR_HSTYLE_COLOUR, 0, 0, a, &r);
        for (i = 0; i < CR_HMAP_W; i++) { uint8_t z[CR_CHORD_MAX]; cr_harm_voice_style(&m.w[i], CR_HSTYLE_COLOUR, 0, 0, z, &r); }
        cb = cr_harm_voice_style(&m.w[1], CR_HSTYLE_COLOUR, 0, 0, b, &r);
        if (ca != cb || memcmp(a, b, (size_t)ca)) ls->same = 0;
    }
    if (!json) printf("  top notes stay within %d (shell), %d (spread), %d (colour) semitones over every chord of every key;\n"
                      "  a key gives the same notes every time: %s\n\n", ls->band[0], ls->band[1], ls->band[2], ls->same ? "yes" : "NO");
    else printf("],\"step\":[%.2f,%.2f,%.2f],\"band\":[%d,%d,%d],\"same\":%d},", ls->step[0], ls->step[1], ls->step[2],
                ls->band[0], ls->band[1], ls->band[2], ls->same);
}

/* 6: every chord the palette's keys can play (home C in each of the six modes, 18 keys, 7 worlds -- KNOB 1 or a world
 * button --; the key itself, one of the four approaches into it, or it with notes added: the lower chord keys pressed
 * before or after the root) against each chord of each phrase: 3 the same notes, 2 the same chord (an inessential note left out,
 * or a 5th added), 1 the same chord over the wrong bass, 0 not it */
typedef struct { unsigned char sc, key, world, fl, nfl, root, bass; unsigned short pcs; } pal_t;
typedef struct { int one, change, bass, missing, playable; char miss[512]; } packstats_t;
static pal_t PAL[CR_HMODE_N * 18 * CR_HCOL_N * (1 + CR_HAPP_N + 15)];
static const char *const WNAME[CR_HCOL_N] = { "bare", "triads", "jazz", "lush", "quartal", "altered", "upper" };
static const int WORDER[CR_HCOL_N] = { CR_HCOL_JAZZ, CR_HCOL_LUSH, CR_HCOL_TRIAD, CR_HCOL_BARE, CR_HCOL_QUARTAL,
                                       CR_HCOL_ALTERED, CR_HCOL_UPPER };
static const char *const APN[CR_HAPP_N] = { "slip", "dom", "dim", "tritone" };   /* the approach buttons 6 m7 M7 9 */
static const char *const ADN[4] = { "6", "m7", "M7", "9" };                        /* .. as added notes */
static const cr_mod_t ADM[4] = { CR_MOD_6, CR_MOD_M7, CR_MOD_MAJ7, CR_MOD_9 };
static const char *const KEYN[18] = { "D4", "E4", "F4", "G4", "A4", "B4", "C5", "D5", "E5", "F5", "G5",
                                      "D#4", "F#4", "G#4", "A#4", "C#5", "D#5", "F#5" };
static int npal;
static void pal_build(void)
{
    cr_hmap_t m;
    int sc, k, w, f, c, i;
    for (sc = 0; sc < CR_HMODE_N; sc++) {
        cr_harm_lanes_mode(0, sc, &m);
        for (k = 0; k < 18; k++)
            for (w = 0; w < CR_HCOL_N; w++)
                for (f = 0; f <= CR_HAPP_N + 15; f++) {   /* 0: the key; 1 + CR_HAPP_*: an approach into it; then
                                                           * the key with added notes (15 sets of 6 m7 M7 9) */
                    const cr_hmap_chord_t *d = k < CR_HMAP_W ? &m.w[k] : &m.b[k - CR_HMAP_W];
                    cr_hmap_chord_t e = *d;
                    uint8_t n[CR_CHORD_MAX], r, lq, lx;
                    char a[3], q[5], su[CR_SUP_MAX];
                    pal_t *p = &PAL[npal++];
                    if (f >= 1 && f <= CR_HAPP_N) cr_harm_approach(d, f - 1, &e);
                    else if (f > CR_HAPP_N) {
                        unsigned fl = 0, b;
                        for (b = 0; b < 4; b++) if ((f - CR_HAPP_N) >> b & 1) fl |= 1u << ADM[b];
                        cr_harm_flavour(d, fl, &e);
                    }
                    c = cr_harm_voice_world(&e, w, CR_HSTYLE_COLOUR, 0, 0, n, &r, a, q, su, &lq, &lx);
                    p->sc = (unsigned char)sc; p->key = (unsigned char)k; p->world = (unsigned char)w;
                    p->fl = (unsigned char)f; p->root = e.root_pc; p->bass = (unsigned char)(n[0] % 12);
                    p->nfl = (unsigned char)(f == 0 ? 0 : f <= CR_HAPP_N ? 1 :
                                             ((f - CR_HAPP_N) & 1) + ((f - CR_HAPP_N) >> 1 & 1) + ((f - CR_HAPP_N) >> 2 & 1) +
                                             ((f - CR_HAPP_N) >> 3 & 1));
                    p->pcs = 0; for (i = 0; i < c; i++) p->pcs = (unsigned short)(p->pcs | 1u << (n[i] % 12));
                }
    }
}
static int pk_level(const pal_t *p, const pk_ev_t *e)
{
    unsigned allow = e->pcs | 1u << ((e->root + 7) % 12), need = e->ess & ~(1u << e->bass);
    if (p->root != e->root) return 0;
    if (p->pcs == e->pcs && p->bass == e->bass) return 3;
    if ((need & ~p->pcs) || (p->pcs & ~allow)) return 0;
    return p->bass == e->bass ? 2 : 1;
}
static int pk_better(const pal_t *a, int la, const pal_t *b, int lb)   /* a over b: closer, fewer keys held, jazz */
{
    if (!b) return 1;
    if (la != lb) return la > lb;
    if (a->nfl != b->nfl) return a->nfl < b->nfl;
    return a->world == CR_HCOL_JAZZ && b->world != CR_HCOL_JAZZ;
}
static void pack_section(packstats_t *ps)
{
    int i, j, sc, w, x;
    const char *mname[64];
    int mcount[64], nm = 0;
    memset(ps, 0, sizeof *ps);
    pal_build();
    if (!json) printf("6. THE RESEARCH PACK'S 40 PHRASES on the palette (home C in any of the six modes; key world [+approach, or "
                      "notes added]; ~ the same chord, not every note; (bass!) needs a bass of its own)\n\n");
    else printf("\"pack\":[");
    for (i = 0; i < NPACK; i++) {
        const pk_phrase_t *ph = &PACK[i];
        int brank = 9, bmiss = 99, bsc = 0, bw = -1;
        const pal_t *bpick[PK_MAXEV];
        int blev[PK_MAXEV];
        static const int MORDER[CR_HMODE_N] = { CR_HMODE_IONIAN, CR_HMODE_AEOLIAN, CR_HMODE_DORIAN, CR_HMODE_MIXO,
                                                CR_HMODE_LYDIAN, CR_HMODE_PHRYGIAN };   /* (a tie: the plainer home) */
        int mi;
        for (mi = 0; mi < CR_HMODE_N; mi++) {
            const pal_t *any[PK_MAXEV], *one[PK_MAXEV];
            int alev[PK_MAXEV], olev[PK_MAXEV], rank, miss = 0, onew = -1, onenf = 99, allsame = 1, allright = 1;
            sc = MORDER[mi];
            for (j = 0; j < ph->n; j++) {
                any[j] = 0; alev[j] = 0;
                for (x = 0; x < npal; x++) {
                    int l;
                    if (PAL[x].sc != sc) continue;
                    l = pk_level(&PAL[x], &ph->ev[j]);
                    if (l && pk_better(&PAL[x], l, any[j], alev[j])) { any[j] = &PAL[x]; alev[j] = l; }
                }
                if (!any[j]) { miss++; allright = 0; allsame = 0; }
                else if (alev[j] < 2) allsame = 0;
            }
            for (w = 0; w < CR_HCOL_N; w++) {                /* the whole phrase in one world */
                const pal_t *pk[PK_MAXEV];
                int lv[PK_MAXEV], nf = 0, ok = 1;
                for (j = 0; j < ph->n && ok; j++) {
                    pk[j] = 0; lv[j] = 0;
                    for (x = 0; x < npal; x++) {
                        int l;
                        if (PAL[x].sc != sc || PAL[x].world != WORDER[w]) continue;
                        l = pk_level(&PAL[x], &ph->ev[j]);
                        if (l >= 2 && pk_better(&PAL[x], l, pk[j], lv[j])) { pk[j] = &PAL[x]; lv[j] = l; }
                    }
                    if (!pk[j]) ok = 0; else nf += pk[j]->nfl;
                }
                if (ok && nf < onenf) { onenf = nf; onew = WORDER[w]; memcpy(one, pk, sizeof pk); memcpy(olev, lv, sizeof lv); }
            }
            rank = onew >= 0 ? 0 : allsame ? 2 : allright ? 3 : 4;
            if (rank < brank || (rank == brank && miss < bmiss)) {
                brank = rank; bmiss = miss; bsc = sc; bw = onew;
                memcpy(bpick, onew >= 0 ? one : any, sizeof bpick);
                memcpy(blev, onew >= 0 ? olev : alev, sizeof blev);
            }
        }
        if (brank == 0) { ps->one++; ps->playable++; }
        else if (brank == 2) { ps->change++; ps->playable++; }
        else if (brank == 3) ps->bass++;
        else ps->missing++;
        if (!json) {
            static const char *const V[5] = { "one world", "", "playable with a KNOB 1 turn", "needs its own bass",
                                              "a chord missing" };
            printf("  %s %-8s %-28s C %-10s %s%s%s\n    ", ph->id, ph->bank, ph->name, cr_harm_mode_name(bsc), V[brank],
                   brank == 0 ? " " : "", brank == 0 ? WNAME[bw] : "");
            for (j = 0; j < ph->n; j++) {
                const pal_t *p = bpick[j];
                printf("%s%s ", j ? " | " : "", ph->ev[j].sym);
                if (!p) { printf("MISSING"); continue; }
                printf("%s %s", KEYN[p->key], WNAME[p->world]);
                if (p->fl && p->fl <= CR_HAPP_N) printf(" +%s", APN[p->fl - 1]);
                else if (p->fl) { printf(" add"); for (x = 0; x < 4; x++) if ((p->fl - CR_HAPP_N) >> x & 1) printf(" %s", ADN[x]); }
                printf("%s", blev[j] == 1 ? " (bass!)" : blev[j] == 2 ? " ~" : "");
            }
            printf("\n");
        } else {
            static const char *const VJ[5] = { "one_world", "", "world_change", "needs_bass", "missing" };
            printf("{\"id\":\"%s\",\"home\":\"C %s\",\"verdict\":\"%s\"}%s", ph->id, cr_harm_mode_name(bsc), VJ[brank],
                   i < NPACK - 1 ? "," : "");
        }
        for (j = 0; j < ph->n; j++)
            if (!bpick[j]) {
                for (x = 0; x < nm && strcmp(mname[x], ph->ev[j].sym); x++) {}
                if (x == nm && nm < 64) { mname[nm] = ph->ev[j].sym; mcount[nm++] = 0; }
                if (x < 64) mcount[x]++;
            }
    }
    {
        size_t o = 0;
        for (x = 0; x < nm; x++)
            o += (size_t)snprintf(ps->miss + o, o < sizeof ps->miss ? sizeof ps->miss - o : 0, "%s%s%s", x ? ", " : "",
                                  mname[x], mcount[x] > 1 ? (mcount[x] > 2 ? " x3+" : " x2") : "");
    }
    if (!json) printf("\n  %d of %d playable from the palette (%d in one colour world, %d with a KNOB 1 turn); %d need a bass "
                      "of their own; %d have a chord it can't reach: %s\n\n",
                      ps->playable, NPACK, ps->one, ps->change, ps->bass, ps->missing, ps->miss);
    else printf("],");
}

int main(int argc, char **argv)
{
    packstats_t pks;
    int i, differ = 0, differ_same_key = 0, same_key_pairs = 0, key_ok = 0, key_wrong_at3 = 0, self = 0, same_root = 0;
    int distinct[2] = { 0, 0 };
    lanestats_t ms;
    json = argc > 1 && !strcmp(argv[1], "--json");
    if (json) printf("{");

    /* 1 ------------------------------------------------------------------------ */
    if (!json) printf("1. SAME CHORD, DIFFERENT PAST (played through the listener; * = the chord you're on, ~ = same root)\n\n");
    else printf("\"pairs\":[");
    for (i = 0; i < NPAIRS; i++) {
        player_t pa, pb; chord_t la, lb; sugg_t sa, sb; char ka[8], kb[8]; int d;
        play_start(&pa); play_start(&pb);
        play_prog(&pa, PAIRS[i].a, &la); play_prog(&pb, PAIRS[i].b, &lb);
        suggest(&pa.key, &la, &sa); suggest(&pb.key, &lb, &sb);
        key_str(&pa.key, ka, sizeof ka); key_str(&pb.key, kb, sizeof kb);
        d = !sugg_equal(&sa, &sb);
        differ += d;
        if (pa.key.held && pb.key.held && pa.key.tonic_pc == pb.key.tonic_pc && pa.key.scale == pb.key.scale) {
            same_key_pairs++;
            differ_same_key += d;
        }
        if (!json) {
            printf("  %-14s heard in %-3s (%s)\n", PAIRS[i].a, ka, PAIRS[i].a_read);
            print_sugg_text(&sa); printf("\n");
            printf("  %-14s heard in %-3s (%s)\n", PAIRS[i].b, kb, PAIRS[i].b_read);
            print_sugg_text(&sb); printf("\n");
            printf("  -> %s\n\n", d ? "different suggestions" : "IDENTICAL suggestions");
        } else {
            printf("{"); jstr("a", PAIRS[i].a, 1); jstr("a_read", PAIRS[i].a_read, 1); jstr("a_heard", ka, 1);
            jstr("a_trace", pa.trace, 1); print_sugg_json("a_suggest", &sa); printf(",");
            jstr("b", PAIRS[i].b, 1); jstr("b_read", PAIRS[i].b_read, 1); jstr("b_heard", kb, 1);
            jstr("b_trace", pb.trace, 1); print_sugg_json("b_suggest", &sb);
            printf(",\"differ\":%d}%s", d, i < NPAIRS - 1 ? "," : "");
        }
    }
    if (json) printf("],");

    /* 2 ------------------------------------------------------------------------ */
    {
        static const char *const DIA[2][7] = { { "C", "Dm", "Em", "F", "G", "Am", "Bdim" },
                                               { "Am", "Bdim", "C", "Dm", "Em", "F", "G" } };
        static const char *const KEYN[2] = { "C major (Key Mode)", "A minor (Key Mode)" };
        int s, j;
        sugg_t prev;
        if (!json) printf("2. FUNCTION: the key held by Key Mode, each diatonic chord sounding\n\n");
        else printf("\"function\":[");
        for (s = 0; s < 2; s++) {
            cr_harm_key_t key;
            int nd = 0;
            cr_harm_key_explicit(&key, s ? 9 : 0, s ? CR_SCALE_MINOR : CR_SCALE_MAJOR);
            if (!json) printf("  %s\n", KEYN[s]);
            else printf("{\"key\":\"%s\",\"rows\":[", KEYN[s]);
            for (j = 0; j < 7; j++) {
                chord_t c; sugg_t sg; int k;
                parse_chord(DIA[s][j], &c);
                suggest(&key, &c, &sg);
                if (j == 0 || !sugg_equal(&sg, &prev)) nd++;
                for (k = 0; k < 3; k++) { self += sg.self[k]; same_root += sg.same_root[k]; }
                if (!json) { printf("  on %-5s", DIA[s][j]); print_sugg_text(&sg); printf("\n"); }
                else { printf("{"); jstr("on", DIA[s][j], 1); print_sugg_json("suggest", &sg); printf("}%s", j < 6 ? "," : ""); }
                prev = sg;
            }
            distinct[s] = nd;
            if (!json) printf("  -> %d different suggestion sets across 7 chords\n\n", nd);
            else printf("],\"distinct\":%d}%s", nd, s == 0 ? "," : "");
        }
        if (json) printf("],");
    }

    /* 3 ------------------------------------------------------------------------ */
    if (!json) printf("3. KEY READING: the key it hears after each chord (- = none yet), against a musician's read\n\n");
    else printf("\"keys\":[");
    for (i = 0; i < NCORPUS; i++) {
        player_t p; chord_t last; char k[8]; int okk, w3;
        play_start(&p);
        play_prog(&p, CORPUS[i].prog, &last);
        key_str(&p.key, k, sizeof k);
        okk = key_matches(&p.key, CORPUS[i].key);
        key_ok += okk;
        {   /* the key after the third chord: the first it ever holds */
            const char *c3 = p.trace;
            int commas = 0;
            while (*c3 && commas < 2) if (*c3++ == ',') commas++;
            { char k3[8]; int z = 0; while (c3[z] && c3[z] != ',' && z < 7) { k3[z] = c3[z]; z++; } k3[z] = 0;
              w3 = strcmp(k3, CORPUS[i].key) != 0; }
        }
        key_wrong_at3 += w3;
        if (!json) printf("  %-14s %-28s heard %-14s -> %-3s musician: %-3s %s\n", CORPUS[i].prog, CORPUS[i].what, p.trace, k,
                          CORPUS[i].key, okk ? "ok" : "WRONG");
        else { printf("{"); jstr("prog", CORPUS[i].prog, 1); jstr("what", CORPUS[i].what, 1); jstr("trace", p.trace, 1);
               jstr("heard", k, 1); jstr("musician", CORPUS[i].key, 1); printf("\"ok\":%d,\"wrong_at_3\":%d}%s", okk, w3, i < NCORPUS - 1 ? "," : ""); }
    }
    if (!json) printf("\n");
    else printf("],");

    /* 4 ------------------------------------------------------------------------ */
    {
        player_t p; chord_t last; sugg_t s1, s2; char k[8], src2[24];
        play_start(&p);
        play_prog(&p, "D", &last);
        suggest(&p.key, &last, &s1);                         /* on D: PULL is .. */
        cr_harm_hist_push(&p.h, s1.pc[2], s1.q[2], s1.x[2]); /* .. confirmed (the UI pushes it; no key yet) */
        cr_harm_key_infer(&p.key, &p.h);
        { chord_t c = { s1.pc[2], s1.q[2], s1.x[2] }; suggest(&p.key, &c, &s2); last = c; }
        name_of(last.pc, last.q, last.x, src2, sizeof src2);
        cr_harm_hist_push(&p.h, s2.pc[0], s2.q[0], s2.x[0]);  /* .. then its REST confirmed */
        cr_harm_key_infer(&p.key, &p.h);
        key_str(&p.key, k, sizeof k);
        if (!json) {
            printf("4. CONFIRM FEEDBACK: D played, PULL confirmed, then REST confirmed\n\n");
            printf("  on D (no key yet)   PULL = %s\n", s1.name[2]);
            printf("  on %-5s (no key)   REST = %s   (a musician resolves A7 to D)\n", src2, s2.name[0]);
            printf("  history D %s %s -> key heard: %s\n\n", src2, s2.name[0], k);
        } else {
            printf("\"feedback\":{"); jstr("pull_on_d", s1.name[2], 1); jstr("rest_after", s2.name[0], 1);
            jstr("key_after", k, 0); printf("},");
        }
    }

    /* 5 ------------------------------------------------------------------------ */
    lanes_section(&ms);

    /* 6 ------------------------------------------------------------------------ */
    pack_section(&pks);

    /* summary ------------------------------------------------------------------ */
    if (!json) {
        printf("SUMMARY\n");
        printf("  pairs ending on the same chord with different suggestions: %d of %d\n", differ, NPAIRS);
        printf("    .. of the pairs heard in the SAME key, different suggestions: %d of %d\n", differ_same_key, same_key_pairs);
        printf("  different suggestion sets across a key's 7 diatonic chords: %d of 7 in C major, %d of 7 in A minor\n",
               distinct[0], distinct[1]);
        printf("  slots that offer the chord you're on (of 42): %d exactly, %d same root and quality\n", self, same_root);
        printf("  progressions ending in the musician's key: %d of %d; first key held (after chord 3) wrong: %d of %d\n",
               key_ok, NCORPUS, key_wrong_at3, NCORPUS);
        /* the scorecard on one line (tests/run_cr_tests.sh shows it on every run) */
        printf("  Harmony mode's lanes: top notes within %d / %d / %d semitones (shell / spread / colour), the demo's top "
               "line moves %.1f / %.1f / %.1f a chord; the same notes every time: %s\n", ms.band[0], ms.band[1], ms.band[2],
               ms.step[0], ms.step[1], ms.step[2], ms.same ? "yes" : "NO");
        printf("cr_harmony_bench: same-chord pairs differing %d/%d (same key %d/%d), function sets %d/7 + %d/7, "
               "self %d/42 (same root %d), keys %d/%d; lanes top band %d/%d/%d, repeatable %s; pack %d/%d playable "
               "(%d one world), %d need a bass, %d missing\n", differ,
               NPAIRS, differ_same_key, same_key_pairs, distinct[0], distinct[1], self, same_root, key_ok, NCORPUS,
               ms.band[0], ms.band[1], ms.band[2], ms.same ? "yes" : "NO", pks.playable, NPACK, pks.one, pks.bass,
               pks.missing);
    } else {
        printf("\"summary\":{\"pairs\":%d,\"pairs_differ\":%d,\"same_key_pairs\":%d,\"same_key_differ\":%d,"
               "\"function_distinct\":%d,\"self\":%d,\"same_root\":%d,\"corpus\":%d,\"key_ok\":%d,\"wrong_at_3\":%d,"
               "\"lanes_band_colour\":%d,\"lanes_same\":%d,\"pack_playable\":%d,\"pack_one_world\":%d,"
               "\"pack_needs_bass\":%d,\"pack_missing\":%d}}\n",
               NPAIRS, differ, same_key_pairs, differ_same_key, distinct[0] + distinct[1], self, same_root, NCORPUS, key_ok,
               key_wrong_at3, ms.band[2], ms.same, pks.playable, pks.one, pks.bass, pks.missing);
    }
    return 0;
}
