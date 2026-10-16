/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the key sensing (firmware/src/cr_sense.c; docs/VELOCITY.md) on curves like the ones Velo measured on
 * the FM-1 (its notes/01-lab.md, DRIVE 2): the pulled-up samples 1023 open, ~180 seated; the driven ones ~400 for a
 * firm landing, ~550 for a light one. Samples 2.2 ms apart, driven and pulled up alternating as the scan delivers them
 * (the key's dnext decides, as hal/fm1_input.h asks FM1_INPUT_DRIVE). Checked:
 *   - a hard hit: velocity 127, known on the fifth sample after the touch (two driven samples: WAIT 2);
 *   - a gentle press: a middling velocity, the integer log map within a few steps of Velo's float law;
 *   - the map over the whole driven range against the float law: never more than 3 velocity steps apart;
 *   - a graze (touched, never seated) plays nothing; a bounce while held does not end the press; a release takes two
 *     samples above RELEASE; two keys at once are independent;
 *   - the pressure (MONK-1's rest-following lean): a key settling after its landing reads no pressure, a lean after
 *     it is full pressure within ~60 ms, an easing takes it back to 0, and a key let go reads 0.
 * The hooks' tables (FM1_KEYMAP) come from hal/fm1_input.h, compiled with the GPIO as memory, never called. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../firmware/hal/fm1_time.h"
#include "../firmware/hal/fm1_gpio.h"
static volatile uint32_t host_reg[16][64];
static uint32_t host_now;
static uint32_t host_ticks(void) { return host_now += 2400u; }
#undef FM1_PR
#define FM1_PR(p, r) (host_reg[(p) & 15u][((r) / 4u) & 63u])
#define fm1_ticks host_ticks
static volatile uint32_t host_adc_con, host_adc_res, host_wla;
#define FM1_ADC_CON_R host_adc_con
#define FM1_ADC_RES host_adc_res
#define FM1_WLA_CON0 host_wla
#include "../firmware/hal/fm1_input.h"
enum { CR_TOUCH_FIXED, CR_TOUCH_VEL, CR_TOUCH_VEL_AT };   /* (mod.c's: the LED hook reads the mode) */
static volatile uint8_t cr_touch_mode = CR_TOUCH_VEL;
#include "../firmware/src/cr_sense.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("sense: %-70s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
}

/* the next pulled-up sample of key k as the scan delivers it: a held key's driven sample comes first (the key asks
 * for it: dnext), so the state machine, which runs on the pulled-up ones, sees one every 4.4 ms while a key is down */
static uint32_t samples;
static void feed(uint32_t k, uint32_t pulled, uint32_t driven)
{
    if (sn_key[k].dnext) {
        cr_sense_sample(k, driven, 1);
        samples++;
    }
    cr_sense_sample(k, pulled, 0);
    samples++;
}
static void open_for(uint32_t k, uint32_t n) { while (n--) feed(k, 1023, 1023); }
/* a press: the touch sample `touch` (pulled up), then the key seated at `seat` with the driven reading `drv`;
 * returns the number of pulled-up samples after the touch until the velocity was known (0: never) */
static uint32_t press(uint32_t k, uint32_t touch, uint32_t seat, uint32_t drv)
{
    uint32_t i;
    cr_sense_land[k] = 0;
    feed(k, touch, drv);
    for (i = 0; i < 8u; i++) {
        if (cr_sense_take(k))
            return i;
        feed(k, seat, drv);
    }
    return 0;
}
static void release(uint32_t k) { feed(k, 1023, 1023); feed(k, 1023, 1023); }

/* Velo's float law: 1 + 126 (1 - t)^0.7, t = log(r / r_floor) / log(r_ceil / r_floor) */
static int float_vel(uint32_t d)
{
    double r, r0 = (double)(SN_FLOOR - SN_DBASE), r1 = (double)(SN_CEIL - SN_DBASE), t;
    if (d <= SN_FLOOR)
        return 127;
    if (d >= SN_CEIL)
        return 1;
    r = (double)(d - SN_DBASE);
    t = log(r / r0) / log(r1 / r0);
    return (int)(1.0 + 126.0 * pow(1.0 - t, 0.7) + 0.5);
}

int main(void)
{
    uint32_t k = 3, n, i, worst = 0, worst_d = 0;
    memset((void *)sn_key, 0, sizeof sn_key);
    for (i = 0; i < SN_NKEYS; i++)
        sn_key[i].v = 1023;

    /* a hard hit: lands seated at once (1023 -> 180), the driven readings 420 then 400 */
    open_for(k, 4);
    n = press(k, 180, 175, 400);
    check("a hard hit: velocity 127 (a driven landing at the floor)", cr_sense_take(k) == 127 && sn_key[k].st == SN_DOWN);
    check("known at the second pulled-up sample after the touch (WAIT 2: two driven ones between; ~9 ms)", n == 2u && samples == 9u);
    check("no graze, one press counted", sn_grazes == 0u && sn_presses == 1u);
    release(k);
    check("let go: open after two samples above RELEASE", sn_key[k].st == SN_OPEN);

    /* a gentle press: the touch sample half way down (700), seated at 340, driven 560 */
    open_for(k, 4);
    n = press(k, 700, 340, 560);
    check("a gentle press: a middling velocity (driven 560)", cr_sense_take(k) > 60 && cr_sense_take(k) < 115);
    {
        char what[96];
        int fv = float_vel(560);
        snprintf(what, sizeof what, "the integer map at 560: %u against the float law's %d (within 3)", (unsigned)cr_sense_take(k), fv);
        check(what, (int)cr_sense_take(k) - fv <= 3 && fv - (int)cr_sense_take(k) <= 3);
    }
    release(k);

    /* the map over the driven range against the float law */
    for (i = SN_FLOOR - 20u; i <= SN_CEIL + 20u; i++) {
        int a = (int)sn_velocity(i), b = float_vel(i), d = a > b ? a - b : b - a;
        if ((uint32_t)d > worst) {
            worst = (uint32_t)d;
            worst_d = i;
        }
    }
    {
        char what[96];
        snprintf(what, sizeof what, "the log-scale map 430..916: at most %u steps from the float law (at %u, the steep end); 127 and 1 at the ends",
                 (unsigned)worst, (unsigned)worst_d);
        check(what, worst <= 6u && sn_velocity(SN_FLOOR) == 127 && sn_velocity(SN_FLOOR - 1u) == 127 && sn_velocity(SN_CEIL) == 1);
    }
    check("the map falls with the reading (monotonic)", sn_velocity(500) > sn_velocity(600) && sn_velocity(600) > sn_velocity(700) && sn_velocity(700) > sn_velocity(850));

    /* a graze: touched (850), gone before it seats */
    open_for(k, 4);
    sn_presses = sn_grazes = 0;
    cr_sense_land[k] = 0;
    feed(k, 850, 900);
    feed(k, 1023, 1023);
    check("a graze (touched, never seated): no velocity, open again, counted as a graze",
          cr_sense_take(k) == 0 && sn_key[k].st == SN_OPEN && sn_grazes == 1u && sn_presses == 0u);

    /* a bounce while held: one sample open does not end the press; the velocity stands */
    open_for(k, 4);
    press(k, 180, 175, 410);
    i = cr_sense_take(k);
    feed(k, 1023, 1023);
    feed(k, 175, 410);
    feed(k, 175, 410);
    check("a bounce while held (one open sample): still down, the velocity kept", sn_key[k].st == SN_DOWN && cr_sense_take(k) == i && i != 0);
    feed(k, 1023, 1023);
    check("one pulled-up sample above RELEASE: lifting, not open", sn_key[k].st == SN_LIFT);
    feed(k, 1023, 1023);
    check("two above RELEASE: open", sn_key[k].st == SN_OPEN);

    /* two keys at once: a hit on 10 and a gentle press on 11, interleaved */
    open_for(10, 4);
    open_for(11, 4);
    cr_sense_land[10] = cr_sense_land[11] = 0;
    feed(10, 180, 400);
    feed(11, 700, 560);
    for (i = 0; i < 6u; i++) {
        feed(10, 175, 400);
        feed(11, 340, 560);
    }
    check("two keys at once: each its own velocity (127 and a middling one)",
          cr_sense_take(10) == 127 && cr_sense_take(11) > 60 && cr_sense_take(11) < 115);
    release(10);
    release(11);

    /* the pressure: a key seated at a driven 400 settles to 380 within 250 ms (no pressure), then leans to 300
     * (full pressure within ~60 ms), eases back to 400 (0 again), and let go reads 0 */
    {
        uint32_t ms = 1000, p_settle = 0, p_lean = 0, p_ease = 0, up = 0;
        open_for(k, 4);
        press(k, 180, 175, 400);
        for (i = 0; i < 50u; i++) {                     /* 250 ms: the reading creeps down 400 -> 380 */
            sn_key[k].d = (uint16_t)(400u - i * 20u / 50u);
            cr_sense_pressure_poll(ms, 0);
            ms += 5u;
            if (cr_sense_at[k] > p_settle)
                p_settle = cr_sense_at[k];
        }
        for (i = 0; i < 12u; i++) {                     /* a lean: 60 ms at 300 */
            sn_key[k].d = 300;
            cr_sense_pressure_poll(ms, 0);
            ms += 5u;
        }
        p_lean = cr_sense_at[k];
        for (i = 0; i < 24u; i++) {                     /* eased back: 120 ms (the ~30 ms smoothing) */
            sn_key[k].d = 400;
            cr_sense_pressure_poll(ms, 0);
            ms += 5u;
        }
        p_ease = cr_sense_at[k];
        release(k);
        cr_sense_pressure_poll(ms, 0);
        up = cr_sense_at[k];
        {
            char what[96];
            snprintf(what, sizeof what, "pressure: settling %u, a lean %u, eased %u, let go %u", (unsigned)p_settle,
                     (unsigned)p_lean, (unsigned)p_ease, (unsigned)up);
            check(what, p_settle < 20u && p_lean >= 110u && p_ease < 10u && up == 0u);
        }
        /* Aftertouch Sens: the same lean reaches 127 sooner at High (2) than at Low (1) */
        {
            uint32_t pl, ph;
            open_for(k, 4);
            press(k, 180, 175, 400);
            for (i = 0; i < 60u; i++) { sn_key[k].d = 400; cr_sense_pressure_poll(ms, 1); ms += 5u; }
            for (i = 0; i < 12u; i++) { sn_key[k].d = 360; cr_sense_pressure_poll(ms, 1); ms += 5u; }
            pl = cr_sense_at[k];
            release(k);
            cr_sense_pressure_poll(ms, 1);
            open_for(k, 4);
            press(k, 180, 175, 400);
            for (i = 0; i < 60u; i++) { sn_key[k].d = 400; cr_sense_pressure_poll(ms, 2); ms += 5u; }
            for (i = 0; i < 12u; i++) { sn_key[k].d = 360; cr_sense_pressure_poll(ms, 2); ms += 5u; }
            ph = cr_sense_at[k];
            check("Aftertouch Sens: a light lean (400 -> 360) reads more at High than at Low", ph > pl && pl > 0u);
            release(k);
        }
    }
    {   /* the top row through the probe: a baseline while open, the peak above it while pressed, two readings in
         * and the velocity may be taken; a touch of 20 reads light, a firm 43 full; let go: nothing */
        uint32_t kk = 26, v20, v43, i;
        for (i = 0; i < 8u; i++)
            cr_sense_probe(kk, 770u, 0);
        check("the probe: no velocity while open", cr_sense_take(kk) == 0u);
        cr_sense_probe(kk, 790u, 1);
        check("one reading in: not yet", cr_sense_take(kk) == 0u);
        cr_sense_probe(kk, 790u, 1);
        v20 = cr_sense_take(kk);
        cr_sense_land[kk] = 0;
        cr_sense_probe(kk, 770u, 0);
        cr_sense_probe(kk, 770u, 0);
        cr_sense_probe(kk, 813u, 1);
        cr_sense_probe(kk, 810u, 1);
        v43 = cr_sense_take(kk);
        cr_sense_land[kk] = 0;
        cr_sense_probe(kk, 770u, 0);
        {
            char what[96];
            snprintf(what, sizeof what, "the probe's map: a touch (+20) reads %u, a firm press (+43) %u, let go 0", (unsigned)v20, (unsigned)v43);
            check(what, v20 >= 15u && v20 <= 25u && v43 == 127u && cr_sense_take(kk) == 0u);
        }
    }
    printf("sense: %u samples; %s\n", (unsigned)samples, fails ? "FAILED" : "all ok");
    return fails != 0;
}
