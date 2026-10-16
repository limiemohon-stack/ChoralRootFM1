/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the compiled layouts (firmware/src/cr_layout.h, build/gen/cr_layouts.h by tools/gen_layout.py;
 * docs/LAYOUT-TOOL.md "Step 4, the firmware: what was built"): the header compiles as C after core.h; the build has
 * one layout, P5's rows (design/layouts/designs/p5-rows.json), a classic one: a screen plan of 11 screens over 17 pages
 * of its own; no free layout (CR_LAYOUTS_FREE 0: the free-page editor and renderer are not in the unit). Every column
 * names a built-in (page, col) within P5's 19 pages, each of P5's 56 parameters exactly once, its copy's label set
 * exactly then; the sections are the groups' first pages; the screens in group order (OSC 2, FILT 3, ENV 2, LFO 2,
 * MOD 2), a screen under a band one lane, the others two or one without a band; the styles are the p5 cell styles where
 * a column is bound, the editor's rule where not; the labels the design's (15 characters at most):
 *   python3 tools/gen_layout.py build/gen/cr_layouts.h
 *   cc -std=c11 -Wall -Wextra -Wno-unused-function -Werror -pedantic -Ibuild/gen -Ifirmware/src -o build/host/cr_layout_test tests/cr_layout_test.c
 * (tests/run_cr_tests.sh runs it; tests/cr_p5rows_test.c runs the editor on it, the copies against the engine's own.) */
#include <stdio.h>
#include <string.h>
#define __attribute__(x)                     /* (core.h's sections: the host has none) */
#include "core.h"
#include "cr_layouts.h"

#define P5_ENGINE 16u
#define P5_PAGES 19u            /* design/layouts/p5.json */

static int passed, failed;
static void ok(int c, const char *n)
{
    if (c) passed++;
    else { failed++; printf("FAIL %s\n", n); }
}

int main(void)
{
    const cr_layc_t *L = &CR_LAYC[0];
    static const uint8_t SECT[8] = {0, 4, 7, 9, 13, 255, 255, 255}, NG[5] = {2, 3, 2, 2, 2};
    static const char *const TITLE[11] = {"OSC A", "OSC B", "FILTER", "ENV 2 \267 filter", "ENV 2 +", "ENV 1 \267 amp",
                                          "ENV 1 +", "LFO", "WHEEL", "POLYMOD", "VOICE"};
    unsigned p, c, s, r, n = 0, bad_src = 0, bad_scr = 0, bad_cell = 0, lanes = 0, ng[5] = {0};
    unsigned seen[P5_PAGES][4] = {{0}};

    ok(CR_LAYOUTS_FREE == 0, "no free layout built (CR_LAYOUTS_FREE 0)");
#ifdef CR_LAY_NENGINES
    ok(0, "the free tables are not in a unit's header");
#endif
    ok(CR_LAYC_N == 1u && L->engine == P5_ENGINE && !strcmp(L->design, "design/layouts/designs/p5-rows.json"),
       "one classic layout: P5's rows");
    ok(L->npages == 17u && CR_LAYC_NPAGES == 17u && L->page0 == 0u, "P5: 17 pages of its own (a lane each)");
    ok(L->nscreens == 11u && CR_LAYC_NSCREENS == 11u && L->screen0 == 0u, "P5: 11 screens");
    ok(!memcmp(L->section, SECT, 8), "the sections: OSC 0, FILT 4, ENV 7, LFO 9, MOD 13");
    for (p = 0; p < L->npages; p++)
        for (c = 0; c < 4u; c++) {
            const uint8_t *x = CR_LAYC_SRC[L->page0 + p][c];
            const param_desc_t *d = &CR_LAYC_PAGES[L->page0 + p].col[c];
            if (x[0] == 255u) { if (d->label) bad_src++; continue; }
            if (x[0] >= P5_PAGES || x[1] >= 4u || !d->label || d->max < d->min || d->def < d->min || d->def > d->max) { bad_src++; continue; }
            seen[x[0]][x[1]]++;
            n++;
        }
    for (p = 0; p < P5_PAGES; p++)
        for (c = 0; c < 4u; c++)
            if (seen[p][c] > 1u) bad_src++;
    ok(!bad_src && n == 56u, "every column a built-in (page, col) of P5's 19 pages with its descriptor's copy, each of the "
       "56 parameters once, an empty column no copy");
    for (s = 0; s < L->nscreens; s++) {
        const eng_screen_t *h = &CR_LAYC_SCREENS[L->screen0 + s];
        unsigned rows = h->pg[1] == 255u ? 1u : 2u;
        if (h->sec > 4u || (s && h->sec < CR_LAYC_SCREENS[L->screen0 + s - 1u].sec) || h->pg[0] >= L->npages ||
            (rows == 2u && h->pg[1] >= L->npages) || (h->band != ENG_B_NONE && rows != 1u) || h->letter ||
            !h->title || strcmp(h->title, TITLE[s]) || (h->sec < 4u && h->pg[0] < L->section[h->sec]) ||
            (h->sec < 4u && h->pg[0] >= L->section[h->sec + 1u]))
            bad_scr++;
        if (h->sec <= 4u) ng[h->sec]++;
        lanes += rows;
        for (r = 0; r < 2u; r++)
            for (c = 0; c < 4u; c++) {
                int bound = r < rows && CR_LAYC_SRC[L->page0 + h->pg[r]][c][0] != 255u;
                if (bound ? h->cell[r][c] < ENG_C_P5KNOB || h->cell[r][c] > ENG_C_P5SWITCH
                          : h->cell[r][c] != ENG_C_RULE || h->label[r][c] != 0)
                    bad_cell++;
                if (h->label[r][c] && (!bound || strlen(h->label[r][c]) > 15u)) bad_cell++;
            }
    }
    ok(!bad_scr && ng[0] == NG[0] && ng[1] == NG[1] && ng[2] == NG[2] && ng[3] == NG[3] && ng[4] == NG[4] && lanes == 17u,
       "the screens in group order (OSC 2, FILT 3, ENV 2, LFO 2, MOD 2), their titles, a band's screen one lane, 17 lanes");
    ok(CR_LAYC_SCREENS[2].band == ENG_B_FILTER && CR_LAYC_SCREENS[3].band == ENG_B_ENV && CR_LAYC_SCREENS[5].band == ENG_B_ENV &&
       CR_LAYC_SCREENS[0].band == ENG_B_NONE, "FILTER under the filter's curve, ENV 2 and ENV 1 under the envelope");
    ok(!bad_cell, "a bound cell a p5 cell style, an empty one the editor's rule and no label; labels of 15 characters at most");
    ok(!strcmp(CR_LAYC_SCREENS[2].label[0][0], "CUTOFF") && CR_LAYC_SCREENS[2].cell[0][3] == ENG_C_P5LED2 &&
       CR_LAYC_SRC[CR_LAYC_SCREENS[2].pg[0]][0][0] == 5u && CR_LAYC_SRC[CR_LAYC_SCREENS[2].pg[0]][0][1] == 0u,
       "FILTER: KNOB 1 CUTOFF (FILTER col 0), KNOB 4 REV two LEDs");
    ok(!strcmp(CR_LAYC_SCREENS[7].label[0][0], "FREQ") && CR_LAYC_SRC[CR_LAYC_SCREENS[7].pg[0]][0][0] == 11u &&
       CR_LAYC_SRC[CR_LAYC_SCREENS[7].pg[0]][1][0] == 12u && CR_LAYC_SCREENS[7].cell[0][1] == ENG_C_P5LED_SAW,
       "LFO lane 1: KNOB 1 FREQ (LFO col 0), KNOB 2 the saw's LED button (LFO+ col 0)");
    ok(!strcmp(CR_LAYC_PAGES[CR_LAYC_SCREENS[4].pg[0]].col[2].label, "KTRK") && CR_LAYC_SCREENS[4].cell[0][2] == ENG_C_P5SWITCH &&
       CR_LAYC_PAGES[CR_LAYC_SCREENS[4].pg[0]].col[2].names && !strcmp(CR_LAYC_PAGES[CR_LAYC_SCREENS[4].pg[0]].col[2].names[1], "HALF"),
       "ENV 2 +: KNOB 3 KEY TRK, the lever, its copy's names OFF HALF FULL");
    printf("cr_layout: P5's rows: %u screens over %u pages, %u lanes, %u bound columns; %d passed, %d failed\n",
           L->nscreens, L->npages, lanes, n, passed, failed);
    return failed != 0;
}
