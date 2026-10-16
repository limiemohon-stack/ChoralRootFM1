/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the sound editor on P5's compiled rows (design/layouts/designs/p5-rows.json, compiled by
 * tools/gen_layout.py into build/gen/cr_layouts.h CR_LAYC; firmware/src/cr_pages.c cp_deep serves it, cr_edit.c shows
 * its screen plan; docs/LAYOUT-TOOL.md "Step 4"), on the firmware as the emulator builds it:
 * - the served pages: 17 of the layout's own, each column a copy of the built-in descriptor it binds (field for field:
 *   the label, the format, the range, the default, the value names, the unit), get / set / desc through to the built-in
 *   (page, col); the blob, its size and the presets the engine's; every one of P5's 56 parameters on exactly one cell;
 * - the groups: OSC 2 screens, FILT 3, ENV 2, LFO 2, MOD 2 and EXPR after VOICE (0.22, every engine), FX and MIX the
 *   platform's; a screen under a band (FILTER, ENV 2 · filter, ENV 1 · amp) one lane, the band's; the plan's titles;
 * - every cell of the eleven screens turned through KNOB 1..4 (ce_knob): its parameter moves at its built-in address,
 *   the sound is edited, the cell hot, the screen model CR_K_EDIT8 with the cell's label (the design's) and the title;
 *   an empty slot does nothing; the blob carries the edits;
 * - SELECT walks the lanes and the screens of each group and wraps; another engine (the VA) keeps its classic screens. */
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../tools/emu/emu_hooks.h"

emu_hal_t emu_hal;
const int8_t emu_keymap[6][EMU_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
#include "../tools/emu/emu_firmware.h"

static int fails, checks;
#define OK(c, ...)                                                      \
    do {                                                                \
        checks++;                                                       \
        if (!(c)) {                                                     \
            fails++;                                                    \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);               \
        } else {                                                        \
            printf("ok   ");                                            \
        }                                                               \
        printf(__VA_ARGS__);                                            \
        printf("\n");                                                   \
    } while (0)

static int same_str(const char *a, const char *b) { return a == b || (a && b && !strcmp(a, b)); }
/* a copied descriptor is the built-in one, field for field */
static int same_desc(const param_desc_t *a, const param_desc_t *b)
{
    int32_t i;
    if (!same_str(a->label, b->label) || a->fmt != b->fmt || a->min != b->min || a->max != b->max || a->def != b->def ||
        !same_str(a->unit, b->unit) || !a->names != !b->names)
        return 0;
    for (i = 0; a->names && i <= a->max - a->min; i++)
        if (!same_str(a->names[i], b->names[i]))
            return 0;
    return 1;
}

int main(void)
{
    static cr_screen_t s;
    track_t *t = &trk[CR_PART_CHORD];
    const eng_deep_t *b = ENGINES[ENGI_PROPHET]->deep, *d;
    const cr_layc_t *L = &CR_LAYC[0];
    uint32_t g, k, ln, c, ns, n, turned = 0, inert = 0, bad = 0, badinert = 0, badscr = 0, lanes = 0, copies = 0;
    uint32_t bp, bc, seen[19][4] = {{0}}, once = 1;
    uint8_t b0[ENG_BLOB_MAX], b1[ENG_BLOB_MAX];
    ce_scr_t sc;
    static const uint32_t NSCR[CE_NSEC] = {2, 3, 2, 2, 3, 1, 1};   /* (MOD: POLYMOD, VOICE, EXPR) */

    emu_flash_open();
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();
    cu_pool_load(0, ENGI_PROPHET, 1);
    cu_edit_open(0);
    d = cp_deep(t);

    /* the served pages */
    OK(CR_LAYC_N == 1u && L->engine == ENGI_PROPHET && L->npages == 17u && L->nscreens == 11u && !CR_LAYOUTS_FREE &&
       !strcmp(L->design, "design/layouts/designs/p5-rows.json"),
       "one compiled classic layout: P5's rows (17 pages, 11 screens, %s); no free layout (CR_LAYOUTS_FREE 0)", L->design);
    OK(d == &cp_lay_deep[0] && ce_deep(t) == d && d->npages == 17u && d->pages == CR_LAYC_PAGES && d->screens == CR_LAYC_SCREENS &&
       d->nscreens == 11u && d->get == cp_lay_get && d->set == cp_lay_set && d->desc == cp_lay_desc &&
       d->blob_size == b->blob_size && d->blob_get == b->blob_get && d->blob_set == b->blob_set &&
       d->blob_preset == b->blob_preset && !memcmp(d->section, L->section, 8),
       "cp_deep: the layout's pages, plan and sections; the blob (%u B), its get / set and the presets the engine's",
       (unsigned)d->blob_size);
    for (k = 0; k < d->npages; k++)
        for (c = 0; c < 4u; c++) {
            const param_desc_t *pd = &d->pages[k].col[c];
            if (!cp_lay_src(t, k, c, &bp, &bc)) {
                if (pd->label) bad++;
                continue;
            }
            if (bp >= b->npages || bc > 3u || !same_desc(pd, &b->pages[bp].col[bc]) || d->desc(t, k, c) != &b->pages[bp].col[bc] ||
                d->get(t, k, c) != b->get(t, bp, bc))
                bad++;
            seen[bp][bc]++;
            copies++;
        }
    for (k = 0; k < b->npages; k++)
        for (c = 0; c < 4u; c++)
            if ((b->pages[k].col[c].label != 0) != (seen[k][c] == 1u)) once = 0;
    OK(!bad && copies == 56u && once, "every column a copy of its built-in descriptor (%u: label, format, range, default, "
       "names, unit), desc and get its; all 56 parameters once", copies);
    bad = 0;

    /* the groups */
    for (g = 0; g < CE_NSEC; g++) {
        ns = ce_scr(t, g, 0, &sc);
        if (ns != NSCR[g]) badscr++;
        for (k = 0; g <= CE_MOD && k < ns; k++) {
            const eng_screen_t *h;
            ce_scr(t, g, k, &sc);
            if (g == CE_MOD && k == ns - 1u) {
                if (sc.type != CE_S_EXPR) badscr++;
                continue;
            }
            h = ce_plan(d, sc.hs);
            if (sc.type != CE_S_EDIT || !h || h->sec != g || ce_lanes(t, g, &sc) != (h->pg[1] == 0xFFu ? 1u : 2u) ||
                (h->band != ENG_B_NONE && h->pg[1] != 0xFFu))
                badscr++;
            lanes += ce_lanes(t, g, &sc);
        }
    }
    OK(!badscr && lanes == 17u, "the groups: OSC 2 screens, FILT 3, ENV 2, LFO 2, MOD 2 + EXPR, FX and MIX the platform's; "
       "%u lanes, a band's screen one lane", lanes);
    {   /* the screens under a band, and the titles on the title line */
        static const struct { uint32_t g, k; const char *title; uint32_t wide; } W[] = {
            {CE_OSC, 0, "OSC A", CR_W_NONE}, {CE_OSC, 1, "OSC B", CR_W_NONE}, {CE_FILT, 0, "FILTER", CR_W_FILTER},
            {CE_FILT, 1, "ENV 2 \267 filter", CR_W_ENV}, {CE_FILT, 2, "ENV 2 +", CR_W_NONE},
            {CE_ENV, 0, "ENV 1 \267 amp", CR_W_ENV}, {CE_ENV, 1, "ENV 1 +", CR_W_NONE}, {CE_LFO, 0, "LFO", CR_W_NONE},
            {CE_LFO, 1, "WHEEL", CR_W_NONE}, {CE_MOD, 0, "POLYMOD", CR_W_NONE}, {CE_MOD, 1, "VOICE", CR_W_NONE}};
        for (k = 0, bad = 0; k < NELEM(W); k++) {
            cx.grp[0] = (uint8_t)W[k].g;
            cx.scr[0][W[k].g] = (uint8_t)W[k].k;
            cx.lane[0][W[k].g] = 0;
            cx.hot_r = 0;
            ce_screen(&s, cu_now());
            if (strcmp(s.page, W[k].title) || s.wide != W[k].wide || s.kind != CR_K_EDIT8) {
                bad++;
                printf("  screen %s %u: \"%s\" wide %u kind %u\n", CE_SEC_NAME[W[k].g], (unsigned)W[k].k + 1u, s.page, s.wide, s.kind);
            }
        }
        OK(!bad, "the eleven screens' titles, kinds and bands (FILTER under the filter's curve, ENV 2 \267 filter and "
           "ENV 1 \267 amp under the envelope)");
    }
    /* MOD's last screen: Expression (cr_edit.c CE_S_EXPR, 0.22), after VOICE, as on every engine */
    cx.grp[0] = CE_MOD;
    cx.scr[0][CE_MOD] = 2;
    cx.lane[0][CE_MOD] = 1;
    ce_screen(&s, cu_now());
    OK(s.kind == CR_K_STACK && !strcmp(s.page, "EXPR \267 AT") && !strcmp(s.cell[1][0].value, "AT"),
       "MOD 3: EXPR after POLYMOD and VOICE (a stack of two lanes, \"%s\")", s.page);

    /* every cell turned */
    b->blob_get(t, b0);
    for (g = 0, bad = 0; g <= CE_MOD; g++) {
        ns = ce_scr(t, g, 0, &sc) - (g == CE_MOD);
        for (k = 0; k < ns; k++) {
            const eng_screen_t *h;
            ce_scr(t, g, k, &sc);
            h = ce_plan(d, sc.hs);
            for (ln = 0; ln < sc.n; ln++)
                for (c = 0; c < 4u; c++) {
                    uint32_t pg = sc.pg[ln];
                    cx.grp[0] = (uint8_t)g;
                    cx.scr[0][g] = (uint8_t)k;
                    cx.lane[0][g] = (uint8_t)ln;
                    cx.hot_r = 0;
                    psnd[0].edited = 0;
                    if (!cp_lay_src(t, pg, c, &bp, &bc)) {   /* an empty slot: nothing moves, nothing is hot or edited */
                        b->blob_get(t, b1);
                        ce_knob(c, 3, 0);
                        {
                            uint8_t b2[ENG_BLOB_MAX];
                            b->blob_get(t, b2);
                            if (memcmp(b1, b2, b->blob_size) || cx.hot_r || psnd[0].edited) badinert++;
                        }
                        inert++;
                    } else {
                        int32_t v0 = b->get(t, bp, bc), v1, dir = v0 >= b->pages[bp].col[bc].max ? -1 : 1;
                        const char *want = h->label[ln][c] ? h->label[ln][c] : 0;
                        ce_knob(c, dir, 0);
                        v1 = b->get(t, bp, bc);
                        ce_screen(&s, cu_now());
                        if (v1 == v0 || !psnd[0].edited || cx.hot_r != ln + 1u || cx.hot_c != c || s.kind != CR_K_EDIT8 ||
                            s.active != ln || s.hot_r != ln + 1u || s.hot_c != c || !(s.cell[ln][c].flags & CR_CF_ON) ||
                            (want && strcmp(s.cell[ln][c].label, want)) || strcmp(s.page, h->title)) {
                            bad++;
                            printf("  cell %s %u.%u K%u (built-in page %u col %u): %d -> %d, edited %u, hot %u.%u, kind %u, "
                                   "\"%s\"\n", CE_SEC_NAME[g], (unsigned)k + 1u, (unsigned)ln + 1u, (unsigned)c + 1u,
                                   (unsigned)bp, (unsigned)bc, (int)v0, (int)v1, psnd[0].edited, cx.hot_r, cx.hot_c, s.kind,
                                   s.cell[ln][c].label);
                        }
                        turned++;
                    }
                }
        }
    }
    OK(turned == 56u && !bad, "every cell of the eleven screens turned through KNOB 1..4 (%u): its parameter moves at its "
       "built-in address, the sound edited, the cell hot, CR_K_EDIT8 with the design's label and the screen's title", turned);
    OK(inert == 17u * 4u - 56u && !badinert, "every empty slot (%u): a knob on it changes nothing, marks nothing", inert);
    b->blob_get(t, b1);
    OK(memcmp(b0, b1, b->blob_size) != 0, "the patch blob carries the edits (%u bytes)", (unsigned)b->blob_size);

    /* the cells the brief names: FILTER KNOB 1 the cutoff; LFO lane 1 KNOB 1 FREQ, KNOB 2 Saw; OSC A lane 2 KNOB 2 Mix A */
    {
        static const struct { uint32_t g, k, ln, c, bp, bc; const char *label; } N[] = {
            {CE_FILT, 0, 0, 0, 5, 0, "CUTOFF"}, {CE_LFO, 0, 0, 0, 11, 0, "FREQ"}, {CE_LFO, 0, 0, 1, 12, 0, "SAW"},
            {CE_OSC, 0, 1, 1, 4, 0, "MIX A"}};
        for (k = 0, bad = 0; k < NELEM(N); k++) {
            int32_t v0 = b->get(t, N[k].bp, N[k].bc), v1;
            cx.grp[0] = (uint8_t)N[k].g;
            cx.scr[0][N[k].g] = (uint8_t)N[k].k;
            cx.lane[0][N[k].g] = (uint8_t)N[k].ln;
            ce_knob(N[k].c, v0 >= b->pages[N[k].bp].col[N[k].bc].max ? -1 : 1, 0);
            v1 = b->get(t, N[k].bp, N[k].bc);
            ce_screen(&s, cu_now());
            if (v1 == v0 || strcmp(s.cell[N[k].ln][N[k].c].label, N[k].label)) bad++;
        }
        OK(!bad, "FILTER KNOB 1 turns the cutoff (FILTER col 0), LFO lane 1 KNOB 1 FREQ (LFO col 0 RATE) and KNOB 2 the "
           "saw (LFO+ col 0), OSC A lane 2 KNOB 2 Mix A (MIXER col 0)");
    }

    /* SELECT: across the lanes and the screens of each group (MOD's EXPR too), round to the start */
    for (g = 0, bad = 0; g <= CE_MOD; g++) {
        uint32_t want = 0, steps = 0;
        uint32_t nlk[8];
        ns = ce_scr(t, g, 0, &sc);
        for (k = 0; k < ns && k < 8u; k++) {
            ce_scr(t, g, k, &sc);
            want += nlk[k] = ce_lanes(t, g, &sc);
        }
        cx.grp[0] = (uint8_t)g;
        cx.scr[0][g] = 0;
        cx.lane[0][g] = 0;
        do {
            uint32_t k0 = cx.scr[0][g], l0 = cx.lane[0][g];
            ce_select(1);
            steps++;
            if (!(cx.lane[0][g] == l0 + 1u && cx.scr[0][g] == k0) &&
                !(l0 + 1u == nlk[k0] && cx.lane[0][g] == 0 && cx.scr[0][g] == (k0 + 1u) % ns))
                bad++;
        } while ((cx.scr[0][g] || cx.lane[0][g]) && steps < 40u);
        if (steps != want) bad++;
        ce_select(-1);
        n = ns - 1u;
        if (cx.scr[0][g] != n || cx.lane[0][g] != nlk[n] - 1u) bad++;
    }
    OK(!bad, "SELECT: lane by lane across each group's screens (a band's screen one stop), wrapping; back from the first: "
       "the last lane");

    /* another engine keeps its classic screens */
    cu_pool_load(0, ENGI_VA, 1);
    ce_scr(&trk[CR_PART_CHORD], CE_OSC, 0, &sc);
    OK(cp_deep(&trk[CR_PART_CHORD]) == ENGINES[ENGI_VA]->deep && !sc.hs,
       "the VA: no compiled layout, its own pages and classic screens");
    printf("cr_p5rows: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
