/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of firmware/src/ota.c: a fake M-UPGRADE serves a real .fwsc over
 * the SysEx protocol (pack7, cmd 0x30 reads, 0xE0000000 -> "success") and a
 * simulated NOR plays the flash.
 *   cc -o ota_test tests/ota_test.c && ./ota_test FILE.fwsc
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FELUCCA_ID "FM-1_900"
#define FELUCCA_OTA_DRYRUN 0
/* the staging area (ota.c OTA_AREA): -DBOARD_SMK37=1 tests the SMK-37's (0xBB000, the parm identity of its model) */
#if BOARD_SMK37
#if T_MODEL == 1                         /* -DT_MODEL=1 / 2: the MODEL=elite / mke build */
#define SMK_MODEL "SMK-37 Elite"
#elif T_MODEL == 2
#define SMK_MODEL "MKE-P37"
#else
#define SMK_MODEL "SMK-37 Pro"
#endif
#define T_AREA 0xBB000u
#define T_PARM_ID "ota-" SMK_MODEL "_016"
#else
#define T_AREA 0xE0000u
#define T_PARM_ID "ota-FM-1_015"
#endif

static uint8_t nor[0x100000];
static uint8_t *logical;
static size_t logical_len;
static uint8_t rx[1024];                 /* one pending host frame (7-bit bytes) */
static uint32_t rx_len, rx_full;
static uint32_t now_ms, requests, ident_replies, mute_after = 0xFFFFFFFFu, bad_erase;
static int committed;
static uint8_t committed_parm[112];
static int32_t last_code;
static uint8_t *corrupt_at;              /* flip a served byte (loader body) */

static uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8;
        while (nb >= 7) { out[o++] = acc & 0x7F; acc >>= 7; nb -= 7; }
    }
    if (nb) out[o++] = acc & 0x7F;
    return o;
}

static uint32_t unpack7h(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 7;
        if (nb >= 8) { out[o++] = (uint8_t)acc; acc >>= 8; nb -= 8; }
    }
    return o;
}

static void host_reply(const uint8_t *msg, uint32_t n)       /* queue msg as a device-bound frame */
{
    rx_len = pack7(msg, n, rx);
    rx_full = 1;
}

/* ---- hooks ---- */
static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint8_t u[64], m[600];
    uint32_t d, addr, len, i, s = 0;
    if (p[0] != 0xF0 || p[n - 1] != 0xF7) { printf("bad wire framing\n"); exit(1); }
    d = unpack7h(p + 1, n - 2, u);
    if (u[2] == 0x11) { ident_replies++; return 0; }
    if (d != 15 || u[2] != 0x30) { printf("unexpected device msg cmd %02x len %u\n", u[2], d); exit(1); }
    addr = u[7] | u[8] << 8 | u[9] << 16 | (uint32_t)u[10] << 24;
    len = u[11] | u[12] << 8 | u[13] << 16;
    if (++requests > mute_after)
        return 0;                                         /* host gone */
    memcpy(m, "\x00\x59\x30", 3);
    m[3] = (uint8_t)(len + 8); m[4] = (uint8_t)((len + 8) >> 8); m[5] = 0;
    m[6] = 0;
    memcpy(m + 7, &addr, 4);
    m[11] = (uint8_t)len; m[12] = (uint8_t)(len >> 8); m[13] = 0;
    if (addr == 0xE0000000u) {
        memset(m + 14, 0, len);
        memcpy(m + 14, "success", 8);
    } else {
        if (addr + len > logical_len) { printf("read past the package %#x\n", addr); exit(1); }
        memcpy(m + 14, logical + addr, len);
        if (corrupt_at && corrupt_at >= logical + addr && corrupt_at < logical + addr + len)
            m[14 + (corrupt_at - (logical + addr))] ^= 0x01;
    }
    for (i = 6; i < 14 + len; i++) s += m[i];
    m[14 + len] = (uint8_t)~s;
    host_reply(m, 15 + len);
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!rx_full) return 0;
    *p = rx; *n = rx_len;
    return 1;
}
static void ota_frame_done(void) { rx_full = 0; }
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms += 1; }
static int ota_erase(uint32_t off)
{
    if (off < T_AREA || off >= T_AREA + 0x5000u || (off & 0xFFFu)) { bad_erase++; return -8; }
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int ota_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (off < T_AREA || off + n > T_AREA + 0x5000u) { bad_erase++; return -8; }
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static int ota_fread(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { if (step == 9) last_code = code; }
static void ota_commit(const uint8_t *parm) { committed = 1; memcpy(committed_parm, parm, 112); }

#include "../firmware/src/ota.c"
#include "../firmware/stub/stub_core.h"            /* the boot stub reads what this step leaves (OWN_PKG) */

static uint32_t ota_body_off(void)                  /* ota.bin data + 0x20 in the logical image */
{
    uint8_t h[0x400];
    uint32_t i, n;
    memcpy(h, logical, sizeof h);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 100) return ota_rd32(e + 8) + 0x20u;
    }
    return 0;
}

static int check(const char *what, int ok)
{
    printf("%-52s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static int area_erased(void)
{
    uint32_t i;
    for (i = T_AREA; i < T_AREA + 0x5000u; i++) if (nor[i] != 0xFF) return 0;
    return 1;
}

static void reset_dev(void)
{
    memset(nor, 0xFF, sizeof nor);
    memcpy(nor, logical + 0x400, 0x4000);                 /* device flash head = package flash.bin head */
    rx_full = 0; requests = 0; ident_replies = 0; committed = 0; last_code = 0;
    mute_after = 0xFFFFFFFFu; corrupt_at = NULL; bad_erase = 0;
}

int main(int argc, char **argv)
{
    FILE *f;
    uint8_t *raw;
    size_t n, i;
    int bad = 0, rc;
    if (argc < 2) { printf("usage: ota_test FILE.fwsc\n"); return 2; }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    raw = malloc(0x200000);
    n = fread(raw, 1, 0x200000, f);
    fclose(f);
    logical = malloc(n);
    for (i = 0; i < 20; i++) memcpy(logical + i * 47, raw + i * 48, 47);   /* drop the marker bytes */
    memcpy(logical + 20 * 47, raw + 20 * 48, n - 20 * 48);
    logical_len = n - 20;

    /* handshake outside a session */
    reset_dev();
    {
        uint8_t m[7] = {0, 0x59, 0x11, 0, 0, 0, 0xFF};
        host_reply(m, 7);
        ota_service();
        bad += check("handshake answered", ident_replies == 1);
        bad += check("identity frame is " FELUCCA_ID,
                     ota_msg[2] == 0x11 && !memcmp(ota_msg + 6, FELUCCA_ID, sizeof FELUCCA_ID - 1));
    }

    /* full session */
    reset_dev();
    rc = ota_session();
    printf("  %u requests\n", requests);
    bad += check("session commits", rc == 0 && committed);
    bad += check("headers + loader + confirm, no head reads", requests >= 10 && requests <= 30);
    {
        uint8_t *h = nor + T_AREA, *p = committed_parm;
        bad += check("staged body == the package's loader", !memcmp(nor + T_AREA + 0x20u, logical + ota_body_off(), 64));
        bad += check("LOADER.BIN head CRC",
                     (h[0] | h[1] << 8) == ota_crc16(h + 2, 30, 0) && !memcmp(h + 16, "LOADER.BIN", 10));
        bad += check("record at the area + 0x4F00 == RAM record", !memcmp(nor + T_AREA + 0x4F00u, p, 112));
        bad += check("record CRC / type / magic / ota_addr",
                     (p[0] | p[1] << 8) == ota_crc16(p + 2, 78, 0) && (p[2] | p[3] << 8) == 0x5A0D &&
                     (p[6] | p[7] << 8) == 0x5441 && ota_rd32(p + 72) == T_AREA);
        bad += check("record: the loader's identity " T_PARM_ID ", zeros behind",
                     !memcmp(p + 8, T_PARM_ID, sizeof T_PARM_ID - 1u) && !p[8 + sizeof T_PARM_ID - 1u] && !p[0x27]);
        bad += check("nothing written outside the staging area", bad_erase == 0);
    }
#ifdef OWN_PKG
    {   /* the staging area's life (ota.c's header; firmware/stub/stub_core.h is the boot stub's view of it): the
         * staged loader passes the stub's check, the stub's record is the committed one, and the app's boot keeps
         * the loader for the rescue key, removing only the record */
        static uint8_t keep[0x5000];
        uint8_t parm[STUB_PARM_LEN];
        uint32_t len = ota_rd32(nor + T_AREA + 8u), k, t, agree = 1, oks = 0;
        stub_record(parm);
        bad += check("stub: the staged loader passes its check", stub_loader_check(nor + T_AREA) == STUB_LD_OK);
        bad += check("stub: its record == the committed one", !memcmp(parm, committed_parm, 112));
        bad += check("ota_staged == the stub's answer, the staged length", ota_staged() == len);
        bad += check("our loader ends below the record's sector", len <= 0x4000);
        memcpy(keep, nor + T_AREA, sizeof keep);
        /* the app's boot with the record still there (the loader never ran): the loader stays, the record goes */
        bad += check("boot: the loader kept, the record's sector erased (rc 2)",
                     ota_boot_cleanup() == 2 && !memcmp(nor + T_AREA, keep, 0x4000) && nor[T_AREA + 0x4F00u] == 0xFF &&
                     nor[T_AREA + 0x4F06u] == 0xFF && stub_loader_check(nor + T_AREA) == STUB_LD_OK && bad_erase == 0);
        bad += check("second boot: nothing to do, the loader still passes",
                     ota_boot_cleanup() == 0 && stub_loader_check(nor + T_AREA) == STUB_LD_OK);
        /* the next install: step 1 restages over the kept loader */
        committed = 0;
        rc = ota_session();
        bad += check("step 1 over a kept loader: commits, staged again", rc == 0 && committed &&
                     !memcmp(nor + T_AREA, keep, sizeof keep));
        ota_boot_cleanup();
        /* a package refused before the staging began: the kept loader is still there */
        corrupt_at = logical + ota_body_off() - 0x20 + 5;
        committed = 0;
        rc = ota_session();
        corrupt_at = NULL;
        bad += check("a package refused before the erase (-7): the kept loader stays", rc == -7 && !committed &&
                     stub_loader_check(nor + T_AREA) == STUB_LD_OK);
        /* a failure after the erase: nothing staged */
        {
            uint32_t total;
            requests = 0; mute_after = 0xFFFFFFFFu;
            ota_session();
            total = requests;
            ota_boot_cleanup();
            requests = 0; committed = 0;
            mute_after = total - 1;
            rc = ota_session();
            mute_after = 0xFFFFFFFFu;
            bad += check("no 'success' over a kept loader: -13, the area erased", rc == -13 && !committed && area_erased());
        }
        /* what is not our whole loader goes, as before */
        memcpy(nor + T_AREA, keep, sizeof keep);
        memset(nor + T_AREA, 0xFF, 32);
        bad += check("stub: no head (staging not finished) -> refused", stub_loader_check(nor + T_AREA) == STUB_LD_HEAD);
        bad += check("boot: a staging without its head is erased (rc 1)", ota_boot_cleanup() == 1 && area_erased());
        memcpy(nor + T_AREA, keep, sizeof keep);
        memset(nor + T_AREA + len / 2u, 0xFF, 0x4F00u - len / 2u);
        bad += check("stub: erased from half way on -> refused", stub_loader_check(nor + T_AREA) == STUB_LD_BLOCKS);
        bad += check("boot: a torn loader is erased", ota_boot_cleanup() == 1 && area_erased());
        memcpy(nor + T_AREA + 0x4F00u, keep + 0x4F00, 112);
        bad += check("boot: a record alone is erased", ota_boot_cleanup() == 1 && area_erased());
        memcpy(nor + T_AREA, keep, sizeof keep);
        nor[T_AREA + 0x4Cu] = 'X';
        bad += check("boot: not Felucca's loader is erased", ota_boot_cleanup() == 1 && area_erased());
        /* a loader that reaches the record's sector (not ours: build.py stops one): the record is left alone */
        {
            uint8_t *h = nor + T_AREA, *in = h + 32;
            uint32_t p = 0x40, n;
            for (k = 0; k < 5u; k++) {
                ota_wr32(h + p, 3900u);
                ota_wr32(h + p + 4, 4096u);
                memset(h + p + 8, 0x11 * (int)(k + 1u), 3900);
                p += 3908u;
            }
            memcpy(h + 0x4C, "FELUCCA-LOADER-1", 16);
            memset(in, 0, 32);
            ota_wr32(in + 4, 5u * 4096u);
            ota_wr32(in + 8, 0x01C0A800u);
            ota_wr16(in, ota_crc16(in + 2, 30, 0));
            ota_stage_head(p - 32u);
            memcpy(nor + T_AREA + 0x4F00u, keep + 0x4F00, 112);
            n = ota_staged();
            bad += check("a loader into the record's sector: kept, and the record with it (rc 0)",
                         n == p && n > 0x4000 && stub_loader_check(h) == STUB_LD_OK && ota_boot_cleanup() == 0 &&
                         !memcmp(nor + T_AREA + 0x4F00u, keep + 0x4F00, 112) && ota_staged() == p);
        }
        /* the two checks in step: the same answer on damaged copies of the staged loader */
        srand(1);
        for (t = 0; t < 20000u; t++) {
            memcpy(nor + T_AREA, keep, sizeof keep);
            for (k = 0; k < 1u + (uint32_t)rand() % 3u; k++) {
                uint32_t at = (uint32_t)rand() % (t & 1u ? 0x90u : len + 64u);
                if (rand() & 3) nor[T_AREA + at] ^= (uint8_t)(1u << (rand() & 7));
                else memset(nor + T_AREA + at, 0xFF, 0x5000u - at > 600u ? 600u : 0x5000u - at);
            }
            k = stub_loader_check(nor + T_AREA) == STUB_LD_OK;
            oks += k;
            agree &= k == (ota_staged() != 0);
        }
        printf("  %u of 20000 damaged copies still pass both checks (block contents are not checked)\n", oks);
        bad += check("ota_staged == stub_loader_check on 20000 damaged copies", agree && oks > 100u && oks < 19000u);
        memset(nor + T_AREA, 0xFF, 0x5000);
    }
#else
    /* boot cleanup erases it (not Felucca's loader) */
    bad += check("boot cleanup erases the area", ota_boot_cleanup() == 1 && area_erased());
    bad += check("second boot: nothing to do", ota_boot_cleanup() == 0);
#endif

    /* host disappears in the middle */
    reset_dev();
    mute_after = 4;
    rc = ota_session();
    bad += check("host gone -> no commit, area clean", rc < 0 && !committed && area_erased());

    /* host never confirms */
    reset_dev();
    mute_after = 0xFFFFFFFFu;
    {
        uint32_t total;
        rc = ota_session();
        total = requests;                                  /* requests of a full run incl. confirm */
        reset_dev();
        mute_after = total - 1;                            /* answer everything but the confirmation */
        rc = ota_session();
        bad += check("no 'success' -> -13, no commit, area clean", rc == -13 && !committed && area_erased());
    }

    printf("%s\n", bad ? "OTA TEST FAILED" : "ota test passed");
    return bad != 0;
}
