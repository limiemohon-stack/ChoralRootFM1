/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the browsing cost of a full sound pool (3y60; the owner, on 0d227e0: "with all the user presets filled,
 * my FM-1's UI is now very laggy"): 128 user slots (FM TONE most of them, some bound to factory places, some marked
 * favourites), then the flash reads (storage.c st_read: calls and bytes) and the host time of a UI frame, a PRESETS
 * detent and an ALGORITHM detent, the favourites filter off and on, against an empty pool. The rule: no flash read
 * on a frame without a load; a detent reads the one sound it loads (its record and its patch), no more. */
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

static void frame(void)                             /* main.c's loop body (emu_fw.c emu_fw_frame), 16 ms apart */
{
    fm1_ms += 16u;
    cr_ui_input();
    cr_ui_frame();
    cr_ui_draw();
}
static double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}
typedef struct { uint32_t reads, bytes; double us; } cost_t;
static uint32_t r0, b0;
static double t0;
static void m_begin(void) { r0 = st_reads; b0 = st_read_bytes; t0 = now_us(); }
static cost_t m_end(uint32_t n)
{
    cost_t c;
    c.us = (now_us() - t0) / n;
    c.reads = (st_reads - r0) / n;
    c.bytes = (st_read_bytes - b0) / n;
    return c;
}
#define N_DET 16u
static cost_t frames(uint32_t n)
{
    uint32_t i;
    m_begin();
    for (i = 0; i < n; i++)
        frame();
    return m_end(n);
}
static cost_t detents(uint32_t role)
{
    uint32_t i;
    m_begin();
    for (i = 0; i < N_DET; i++) {
        cu_knob(role, 1);
        frame();
    }
    return m_end(N_DET);
}
static void report(const char *what, cost_t c)
{
    printf("     %-44s %6u reads %8u bytes %10.1f us (host)\n", what, (unsigned)c.reads, (unsigned)c.bytes, c.us);
}

int main(void)
{
    track_t *t = &trk[CR_PART_CHORD];
    uint32_t k, f;
    char nm[13];
    cost_t c;

    emu_flash_open();
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();
    for (k = 0; k < 8u; k++)
        frame();

    /* ---- an empty pool ---- */
    trk[CR_PART_CHORD].eng_req = ENGI_QUAD;
    cu_pool_load(0, ENGI_QUAD, 1);
    cu_pos_sync();
    c = frames(32);
    report("empty pool: a frame", c);
    OK(c.reads == 0, "empty pool: a frame reads no flash");
    c = detents(EN_PRESET);
    report("empty pool: a PRESETS detent + its frame", c);

    /* ---- the owner's pool: 128 slots, 96 FM TONE (16 of them bound to factory places), 32 of the other engines ---- */
    for (k = 0, f = 0; k < UP_SLOTS; k++) {
        static const uint32_t OTH[] = {ENGI_VA, ENGI_FM6, ENGI_CZ, ENGI_PROPHET};
        uint32_t e = k % 4u == 3u ? OTH[(k / 4u) % 4u] : ENGI_QUAD, bind = 0;
        cu_pool_load(0, e, 1u + k % 5u);
        if (e == ENGI_QUAD && k % 6u == 0u && f < 16u)
            bind = pool_f0(e) + 1u + f++;           /* "Replace factory": bound to factory place f */
        snprintf(nm, sizeof nm, "DT %03u", (unsigned)k + 1u);
        cb_store(t, 0, k, nm, bind);
        if (k % 3u == 0u)
            crf_set(CRF_T_USER, k, 1);
    }
    OK(up_count() == UP_SLOTS && cb_free_slot() == UP_SLOTS, "a full pool: 128 slots, %u bound to FM TONE factory places",
       (unsigned)f);
    trk[CR_PART_BASS].eng_req = ENGI_QUAD;
    cu_pool_load(0, ENGI_QUAD, pool_count(ENGI_QUAD) - 1u);   /* the chord part on the last added sound */
    cu_pos_sync();
    for (k = 0; k < 4u; k++)
        frame();
    c = frames(32);
    report("full pool: a frame (no input)", c);
    OK(c.reads == 0, "full pool: a frame reads no flash (%u reads, %u bytes)", (unsigned)c.reads, (unsigned)c.bytes);
    c = detents(EN_PRESET);
    report("full pool: a PRESETS detent + its frame", c);
    OK(c.reads <= 4u, "full pool: a PRESETS detent reads at most 4 times (the sound it loads): %u reads, %u bytes",
       (unsigned)c.reads, (unsigned)c.bytes);
    c = detents(EN_ALGO);
    report("full pool: an ALGORITHM detent + its frame", c);
    OK(c.reads <= 4u, "full pool: an ALGORITHM detent reads at most 4 times: %u reads, %u bytes", (unsigned)c.reads,
       (unsigned)c.bytes);
    c = frames(32);
    report("full pool: a frame with the read-out up", c);
    OK(c.reads == 0, "full pool: a frame with the read-out up reads no flash (%u)", (unsigned)c.reads);
    c = detents(EN_K1);
    report("full pool: a knob 1 detent + its frame", c);
    OK(c.reads == 0, "full pool: a knob turn reads no flash (%u)", (unsigned)c.reads);

    crf_set_filter(1);
    c = detents(EN_PRESET);
    report("full pool, favourites filter: a PRESETS detent", c);
    OK(c.reads <= 4u, "favourites filter: a PRESETS detent reads at most 4 times: %u", (unsigned)c.reads);
    crf_set(CRF_T_USER, 1, 1);                      /* a mark changes: the filter's list is built again */
    c = detents(EN_PRESET);
    report("full pool, filter, after a mark: a PRESETS detent", c);
    OK(c.reads <= 4u, "favourites filter after a mark: a PRESETS detent reads at most 4 times: %u", (unsigned)c.reads);
    crf_set_filter(0);
    {   /* the pool's order after a save: one rebuild, from the table */
        m_begin();
        (void)pool_count(ENGI_QUAD);
        (void)pool_entry(ENGI_QUAD, 2);
        c = m_end(1);
        OK(c.reads == 0, "pool_count / pool_entry read no flash (%u)", (unsigned)c.reads);
    }

    printf("%s: %d checks, %d failed\n", fails ? "CR POOLPERF TESTS FAILED" : "cr poolperf tests passed", checks, fails);
    return fails ? 1 : 0;
}
