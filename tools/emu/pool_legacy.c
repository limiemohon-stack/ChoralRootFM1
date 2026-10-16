/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* tools/emu/test_pool.sh's 0.21-format side: built against 377264f's firmware (the emulator's firmware build of that
 * tree: -DEMU_TREE="<its root>"), it writes and reads a flash image as that firmware does.
 *   pool_legacy make IMAGE   a fresh image: 32 user sounds, eight of each engine with a store (FM6, FM TONE, VA, CZ-1),
 *                            each a factory preset with its deep patch edited, saved through cr_bank.c cb_store (the
 *                            SAVE dialog's path); slots 1, 6, 14, 32 marked as favourites (slot 14 typed Bass) and
 *                            FM6's third factory preset; written as the firmware writes them (crf_flush)
 *   pool_legacy show IMAGE   power on from the image (as the unit does) and list what that firmware sees: per used slot
 *                            its engine, name, the record's CRC-32 and the patch's (from the engine's store); the marks */
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define STR2(x) #x
#define STR(x) STR2(x)
#include STR(EMU_TREE/tools/emu/emu_hooks.h)

emu_hal_t emu_hal;
const int8_t emu_keymap[6][EMU_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
#include STR(EMU_TREE/tools/emu/emu_firmware.h)

static void boot(const char *path)
{
    snprintf(emu_flash_path, sizeof emu_flash_path, "%s", path);
    emu_flash_open();
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();
}

static int patch_of(uint32_t k, uint32_t e, uint8_t *b, uint32_t *n)   /* the store's patch of slot k (0.21's readers) */
{
    if (e == ENGI_VA) { *n = VA_BLOB; return va_store_get(k, b); }
    if (e == ENGI_FM6) { *n = FM6_BLOB; return fm6u_get(k, b); }
    if (e == ENGI_CZ) { *n = CZ_BYTES; return czu_get(k, b); }
    if (e == ENGI_QUAD) { *n = QUAD_BLOB; return quad_store_get(k, b); }
    *n = 0;
    return 1;
}

int main(int argc, char **argv)
{
    static const uint32_t ENG[4] = {ENGI_FM6, ENGI_QUAD, ENGI_VA, ENGI_CZ};
    track_t *t = &trk[CR_PART_CHORD];
    uint32_t g, i, k;
    if (argc != 3)
        return 2;
    if (!strcmp(argv[1], "make")) {
        remove(argv[2]);
        boot(argv[2]);
        for (g = 0, k = 0; g < 4u; g++)
            for (i = 0; i < 8u; i++, k++) {
                const eng_deep_t *d = ENGINES[ENG[g]]->deep;
                uint32_t pos = 1u + (i * 3u) % (ENGINES[ENG[g]]->npresets - 1u);
                char nm[13];
                int32_t v;
                cu_pool_load(0, ENG[g], pos);
                v = d->get(t, 0, 0);                 /* the deep patch edited: its first column a step, and the second */
                d->set(t, 0, 0, v > 0 ? v - 1 : v + 1);
                v = d->get(t, 0, 1);
                d->set(t, 0, 1, v > 0 ? v - 1 : v + 1);
                snprintf(nm, sizeof nm, "%.6s %02u", ENGINES[ENG[g]]->name, (unsigned)k + 1u);
                if (cb_store(t, 0, k, nm, 0) != 0) {
                    printf("make: slot %u: save failed\n", (unsigned)k + 1u);
                    return 1;
                }
            }
        (void)crf_set(CRF_T_USER, 0, 1);
        (void)crf_set(CRF_T_USER, 5, 1);
        (void)crf_set(CRF_T_USER, 13, 1);
        (void)crf_set_type(CRF_T_USER, 13, CRF_BASS);
        (void)crf_set(CRF_T_USER, 31, 1);
        (void)crf_set(ENGI_FM6, 2, 1);
        if (crf_flush() != 0) {
            printf("make: the favourites not written\n");
            return 1;
        }
        printf("make: %u slots, %u marks\n", (unsigned)up_count(), (unsigned)crf_count());
        return 0;
    }
    if (!strcmp(argv[1], "show")) {
        boot(argv[2]);
        for (k = 0; k < UP_SLOTS; k++) {
            uint8_t b[256];
            uint32_t n = 0;
            char nm[13];
            const up_rec_t *r = up_rec(k);
            if (!up_used(k))
                continue;
            up_name(k, nm);
            printf("U%02u %-7s %-12s rec %08x patch ", (unsigned)k + 1u, ENGINES[up_engine(k)]->name, nm,
                   (unsigned)st_crc32(r, sizeof *r));
            if (patch_of(k, r->engine, b, &n) == 0)
                printf("%u %08x\n", (unsigned)n, (unsigned)st_crc32(b, n));
            else
                printf("-\n");
        }
        printf("marks %u:", (unsigned)crf_count());
        for (k = 0; k < UP_SLOTS; k++)
            if (crf_has(CRF_T_USER, k))
                printf(" U%02u%s", (unsigned)k + 1u, crf_type(CRF_T_USER, k) == CRF_BASS ? "(bass)" : "");
        printf(" FM6:%d\n", crf_has(ENGI_FM6, 2));
        return 0;
    }
    return 2;
}
