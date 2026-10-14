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
 *   5. Harmony mode's map (what the UI plays now; 1-4 measure the retired Harmony Preview's suggestion engine): the
 *      layouts around each chord of a key, the same chord under other homes, where the left end and the right end
 *      lead when pressed again and again, and the voice leading of long walks
 *
 * The "musician's read" annotations are deliberately limited to progressions with an uncontroversial home; they
 * are expectations to argue with, not ground truth. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../firmware/src/cr_harmony.h"

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

/* ------------------------------------------------------------------ 5: the map --- */
static void map_keys(uint8_t home, uint8_t scale, const chord_t *on, char *out, size_t n)   /* "Am F Cm .." (* home) */
{
    cr_hmap_t m;
    int i;
    size_t o = 0;
    cr_harm_map(home, scale, on->pc, on->q, on->x, &m);
    out[0] = 0;
    for (i = 0; i < CR_HMAP_W && o + 12 < n; i++) {
        char nm[24];
        name_of(m.w[i].root_pc, m.w[i].quality, 0, nm, sizeof nm);
        o += (size_t)snprintf(out + o, n - o, "%s%s%s", i ? " " : "", nm, m.w[i].home ? "*" : "");
    }
}
typedef struct {
    int layouts[2];          /* different layouts across the 7 diatonic chords: C major, A minor */
    int home_hist[9];        /* the left end pressed again and again: presses to reach home (8: not in 8) */
    int home_max;
    int right_distinct;      /* the right end pressed 24 times from C: different chords met */
    int grad[CR_HMAP_W];     /* the white keys' mean tension (x10), over the 24 triads as the chord you are on */
    double move;             /* walks: semitones a note moves per chord change */
    int lo, hi, loop_lo, loop_hi;   /* walks: the lowest note's range; lowest note less the loop root's (range) */
} mapstats_t;

static void map_section(mapstats_t *ms)
{
    static const char *const DIA[2][7] = { { "C", "Dm", "Em", "F", "G", "Am", "Bdim" },
                                           { "Am", "Bdim", "C", "Dm", "Em", "F", "G" } };
    static const uint8_t HOME[2] = { 0, 9 }, SCALE[2] = { CR_SCALE_MAJOR, CR_SCALE_MINOR };
    char line[160], prev[160];
    int s, j, i;
    long moved = 0, changes = 0;
    memset(ms, 0, sizeof *ms);
    if (!json) printf("5. HARMONY MODE'S MAP: the white keys D4 .. G5 around the chord you are on, restful to pulling (* = home)\n\n");
    else printf("\"map\":{\"layouts\":[");
    for (s = 0; s < 2; s++) {
        int nd = 0;
        if (!json) printf("  home %s\n", s ? "A minor" : "C major");
        for (j = 0; j < 7; j++) {
            chord_t c;
            parse_chord(DIA[s][j], &c);
            map_keys(HOME[s], SCALE[s], &c, line, sizeof line);
            if (j == 0 || strcmp(line, prev)) nd++;
            if (!json) printf("  on %-5s %s\n", DIA[s][j], line);
            else { printf("{"); jstr("home", s ? "Am" : "C", 1); jstr("on", DIA[s][j], 1); jstr("keys", line, 0);
                   printf("}%s", s == 0 || j < 6 ? "," : ""); }
            memcpy(prev, line, sizeof prev);
        }
        ms->layouts[s] = nd;
        if (!json) printf("  -> %d different layouts across 7 chords\n\n", nd);
    }
    if (json) printf("],\"same_g\":[");
    {   /* the same chord, G major, under the homes it belongs to */
        static const struct { uint8_t home, scale; const char *name; } H[] = {
            { 0, CR_SCALE_MAJOR, "C major (G = V)" }, { 7, CR_SCALE_MAJOR, "G major (G = I)" },
            { 2, CR_SCALE_MAJOR, "D major (G = IV)" }, { 11, CR_SCALE_MINOR, "B minor (G = VI)" },
            { 4, CR_SCALE_MINOR, "E minor (G = III)" }, { 0, CR_SCALE_MINOR, "C minor (G = V)" } };
        chord_t g = { 7, CR_Q_MAJ, 0 };
        if (!json) printf("  the same G major under six homes\n");
        for (i = 0; i < (int)(sizeof H / sizeof H[0]); i++) {
            map_keys(H[i].home, H[i].scale, &g, line, sizeof line);
            if (!json) printf("  %-18s %s\n", H[i].name, line);
            else { printf("{"); jstr("home", H[i].name, 1); jstr("keys", line, 0); printf("}%s", i < 5 ? "," : ""); }
        }
        if (!json) printf("\n");
    }
    {   /* the left end again and again: does it lead home? From each of the 24 major and minor triads, home C major */
        int r, q;
        ms->home_max = 0;
        for (r = 0; r < 12; r++)
            for (q = 0; q < 2; q++) {
                uint8_t pc = (uint8_t)r, qq = q ? CR_Q_MIN : CR_Q_MAJ, x = 0;
                int n = 0;
                while (!(pc == 0 && qq == CR_Q_MAJ) && n < 8) {
                    cr_hmap_t m;
                    cr_harm_map(0, CR_SCALE_MAJOR, pc, qq, x, &m);
                    pc = m.w[0].root_pc; qq = m.w[0].quality; x = 0;
                    n++;
                }
                ms->home_hist[n]++;
                if (n > ms->home_max) ms->home_max = n;
            }
        for (i = 0; i < CR_HMAP_W; i++) {   /* the white keys' tension, mean over the 24 triads (home C major) */
            long t = 0;
            for (r = 0; r < 24; r++) {
                cr_hmap_t m;
                cr_harm_map(0, CR_SCALE_MAJOR, (uint8_t)(r / 2), r & 1 ? CR_Q_MIN : CR_Q_MAJ, 0, &m);
                t += m.w[i].tension;
            }
            ms->grad[i] = (int)(t * 10 / 24);
        }
    }
    {   /* the right end again and again from C (home C major): where it wanders */
        uint8_t pc = 0, q = CR_Q_MAJ, seen[24];
        int n;
        char walk[400];
        size_t o = 0;
        memset(seen, 0, sizeof seen);
        walk[0] = 0;
        for (n = 0; n < 24; n++) {
            cr_hmap_t m; char nm[24];
            cr_harm_map(0, CR_SCALE_MAJOR, pc, q, 0, &m);
            pc = m.w[CR_HMAP_W - 1].root_pc; q = m.w[CR_HMAP_W - 1].quality;
            if (q == CR_Q_MAJ || q == CR_Q_MIN) seen[pc * 2 + (q == CR_Q_MIN)] = 1;
            name_of(pc, q, 0, nm, sizeof nm);
            if (n < 12 && o + 8 < sizeof walk) o += (size_t)snprintf(walk + o, sizeof walk - o, " %s", nm);
        }
        for (n = 0; n < 24; n++) ms->right_distinct += seen[n];
        if (!json) {
            printf("  pressed again and again, home C major\n");
            printf("  the left end (D4) from each of the 24 major / minor triads reaches home in:");
            for (i = 0; i <= 8; i++) if (ms->home_hist[i]) printf(" %d press%s: %d,", i, i == 1 ? "" : "es", ms->home_hist[i]);
            printf(" (8 = not within 8)\n");
            printf("  the right end (G5) from C, 12 presses:%s .. (24 presses: %d different major / minor chords)\n", walk,
                   ms->right_distinct);
            printf("  the white keys' mean tension, left to right (x10):");
            for (i = 0; i < CR_HMAP_W; i++) printf(" %d", ms->grad[i]);
            printf("\n\n");
        } else {
            printf("],\"home_presses\":[");
            for (i = 0; i <= 8; i++) printf("%d%s", ms->home_hist[i], i < 8 ? "," : "");
            printf("],\"right_walk\":\"%s\",\"right_distinct\":%d,\"tension\":[", walk + 1, ms->right_distinct);
            for (i = 0; i < CR_HMAP_W; i++) printf("%d%s", ms->grad[i], i < CR_HMAP_W - 1 ? "," : "");
            printf("],");
        }
    }
    {   /* long walks: every key equally likely, every home and both scales, 6th with 7th both ways */
        int t, six;
        ms->lo = 127; ms->hi = 0; ms->loop_lo = 99; ms->loop_hi = -99;
        for (six = 0; six <= 1; six++)
            for (t = 0; t < 12; t++) {
                uint8_t cur[CR_CHORD_MAX], pc = (uint8_t)t, q = CR_Q_MAJ, x = 0;
                int n = cr_chord_base_ex(60 + t, CR_Q_MAJ, 0, six, cur), step;
                uint32_t rng = 777u + (uint32_t)t;
                for (step = 0; step < 2000; step++) {
                    cr_hmap_t m; const cr_hmap_chord_t *d; uint8_t notes[CR_CHORD_MAX], base[CR_CHORD_MAX], root;
                    int nv, k, a, b, dd;
                    rng = rng * 1103515245u + 12345u;
                    k = (int)((rng >> 16) % (CR_HMAP_W + CR_HMAP_B));
                    cr_harm_map((uint8_t)t, (uint8_t)(step / 500 % 2), pc, q, x, &m);
                    d = k < CR_HMAP_W ? &m.w[k] : &m.b[k - CR_HMAP_W];
                    nv = cr_harm_voice(d, cur, n, six, 0, notes, &root);
                    if (nv <= 0) continue;
                    for (a = 0; a < nv; a++) {
                        int best = 127;
                        for (b = 0; b < n; b++) { int e = notes[a] > cur[b] ? notes[a] - cur[b] : cur[b] - notes[a]; if (e < best) best = e; }
                        moved += best;
                    }
                    changes += nv;
                    cr_chord_base_ex(root, d->quality, d->ext, six, base);
                    dd = (int)notes[0] - (int)base[0];
                    if (dd < ms->loop_lo) ms->loop_lo = dd;
                    if (dd > ms->loop_hi) ms->loop_hi = dd;
                    if (notes[0] < ms->lo) ms->lo = notes[0];
                    if (notes[0] > ms->hi) ms->hi = notes[0];
                    memcpy(cur, notes, (size_t)nv); n = nv;
                    pc = d->root_pc; q = d->quality; x = d->ext;
                }
            }
        ms->move = changes ? (double)moved / (double)changes : 0.0;
        if (!json) {
            printf("  long walks (48 000 chords, any key, every home): a note moves %.2f semitones a chord on average;\n", ms->move);
            printf("  the lowest note stays in %d .. %d; a loop of the walk plays each chord from a root %+d .. %+d semitones\n",
                   ms->lo, ms->hi, -ms->loop_hi, -ms->loop_lo);
            printf("  from its lowest note (the looper keeps the root, not the inversion)\n\n");
        } else {
            printf("\"move\":%.3f,\"lo\":%d,\"hi\":%d,\"loop_lo\":%d,\"loop_hi\":%d},", ms->move, ms->lo, ms->hi, -ms->loop_hi,
                   -ms->loop_lo);
        }
    }
}

int main(int argc, char **argv)
{
    int i, differ = 0, differ_same_key = 0, same_key_pairs = 0, key_ok = 0, key_wrong_at3 = 0, self = 0, same_root = 0;
    int distinct[2] = { 0, 0 };
    mapstats_t ms;
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
    map_section(&ms);

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
        printf("  Harmony mode's map: %d of 7 layouts differ in C major, %d of 7 in A minor; the left end reaches home "
               "from every triad within %d presses; a note moves %.2f semitones a chord\n", ms.layouts[0], ms.layouts[1],
               ms.home_max, ms.move);
        printf("cr_harmony_bench: same-chord pairs differing %d/%d (same key %d/%d), function sets %d/7 + %d/7, "
               "self %d/42 (same root %d), keys %d/%d; map layouts %d/7 + %d/7, home within %d, move %.2f\n", differ,
               NPAIRS, differ_same_key, same_key_pairs, distinct[0], distinct[1], self, same_root, key_ok, NCORPUS,
               ms.layouts[0], ms.layouts[1], ms.home_max, ms.move);
    } else {
        printf("\"summary\":{\"pairs\":%d,\"pairs_differ\":%d,\"same_key_pairs\":%d,\"same_key_differ\":%d,"
               "\"function_distinct\":%d,\"self\":%d,\"same_root\":%d,\"corpus\":%d,\"key_ok\":%d,\"wrong_at_3\":%d,"
               "\"map_layouts\":%d,\"map_home_max\":%d}}\n",
               NPAIRS, differ, same_key_pairs, differ_same_key, distinct[0] + distinct[1], self, same_root, NCORPUS, key_ok,
               key_wrong_at3, ms.layouts[0] + ms.layouts[1], ms.home_max);
    }
    return 0;
}
