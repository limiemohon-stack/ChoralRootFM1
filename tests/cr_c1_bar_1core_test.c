/* SPDX-License-Identifier: GPL-3.0-only */
/* The persistent fallback (docs/DUALCORE.md "The persistent fallback") in the single-core package (CR_SINGLE_CORE 1,
 * "Two packages"): inert. Core 1 never starts there, so nothing can be barred and Dual Core is never written; a bar
 * record a Standard package left behind (a warm reset across an install) is cleared and decides nothing, and the
 * power-on still keeps core 1 off. The same record in the Standard build bars, as tests/cr_cpu1_test.c section 5 has
 * it (built here twice, -DCR_SINGLE_CORE=0 and =1, by tests/run_cr_tests.sh):
 *   cc -std=gnu11 -Ifirmware/src -DCR_SINGLE_CORE=1 tests/cr_c1_bar_1core_test.c */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CR_CPU1 1
#ifndef CR_SINGLE_CORE
#define CR_SINGLE_CORE 1
#endif
#include "cr_cpu1.h"

static int fails, checks;
#define CHECK(c, what)                                                  \
    do {                                                                \
        checks++;                                                       \
        if (!(c)) { fails++; printf("  FAIL %s\n", what); }             \
    } while (0)

int main(void)
{
    uint32_t w;
    for (w = C1_BAR_CRASH; w <= C1_BAR_GAVE_UP; w++) {
        cr_c1_bar_mock.magic = FM1_C1_BAR_MAGIC;     /* what a Standard package's crash, hang or give-up leaves */
        cr_c1_bar_mock.ran = 1;
        cr_c1_bar_mock.barred = w;
        cr_c1_off = 0;                               /* the record says Dual Core On */
        cr_c1_barred = 0;
        cr_c1_bar_boot(BG_RST_WDT);
        CHECK(!cr_c1_bar_mock.barred && !cr_c1_bar_mock.ran && cr_c1_bar_mock.magic == FM1_C1_BAR_MAGIC,
              "a bar record left behind: cleared");
        if (CR_SINGLE_CORE)
            CHECK(!cr_c1_barred && !cr_c1_off,
                  "single core: no bar decided (cr_c1_barred 0: cr_settings.c saves nothing, the record's On stays)");
        else
            CHECK(cr_c1_barred == w && cr_c1_off, "standard: the bar keeps core 1 off and says why");
        cr_c1_power_on(0, 0);
        CHECK(cr_c1_off && cr_dbg.c1 == C1_OFF, "the power-on: core 1 off (c1 off)");
    }
    cr_c1_bar_mock.magic = 0;
    cr_c1_off = 0;
    cr_c1_barred = 0;
    cr_c1_bar_boot(BG_RST_WDT);                      /* (no record: a power-on's garbage) */
    CHECK(!cr_c1_barred && !cr_c1_off, "no record: nothing decided");
    printf("cr_c1_bar_1core (CR_SINGLE_CORE %d): %d checks, %d failed\n", CR_SINGLE_CORE, checks, fails);
    return fails ? 1 : 0;
}
