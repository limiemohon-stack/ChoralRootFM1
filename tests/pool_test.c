/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the sound pool's floating objects (firmware/src/storage.c sp_*) on a simulated NOR flash:
 * erase -> 0xFF, program only clears bits, no page wrap. Power cuts at every erase / program step of a save (before
 * the step, and inside it after a random number of bytes), torn headers, equal seq, seq wrap, blank and stale pools,
 * a full pool written slot by slot under cuts, long random sequences against a reference model, the spare's erases. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint8_t nor[0x100000];
static long cut_at = -1;               /* power cut: the cut_at-th erase/program (0..) is torn, nothing after it */
static long cut_bytes;                 /* bytes of the torn step that still reach the flash */
static long steps;                     /* erase/program steps since the counter was reset */
static int dead;                       /* the power is gone: every access fails */
static uint32_t n_erase, n_prog;

static int st_read(uint32_t off, void *dst, uint32_t n) { if (dead) return -9; memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    if (dead) return -9;
    n_erase++;
    if (steps++ == cut_at) { memset(nor + off, 0xFF, (size_t)(cut_bytes % 4097)); dead = 1; return -9; }
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i, m = n;
    if (dead) return -9;
    if ((off & 0xFFu) + n > 256u) { printf("page wrap at %#x\n", off); exit(1); }
    n_prog++;
    if (steps++ == cut_at) { m = (uint32_t)(cut_bytes % (n + 1)); dead = 1; }
    for (i = 0; i < m; i++) nor[off + i] &= s[i];
    return dead ? -9 : 0;
}
#define FELUCCA_CZ 1
#define FELUCCA_QUAD 1
#define ST_AHEAD 1
#define SP_TEST 1                      /* (the sound pool: storage.c sp_*) */
#include "../firmware/src/storage.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t ref[SP_SLOTS * SP_SLOT];        /* what the pool holds */
static uint32_t rng = 12345u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void boot(void) { dead = 0; cut_at = -1; sp_scanned = 0; sp_scan(); }
static void fresh(void) { memset(nor, 0xFF, sizeof nor); memset(ref, 0, sizeof ref); boot(); }
static int bank_is(uint32_t b, const uint8_t *want)   /* bank b reads want */
{
    static uint8_t got[ST_PAYLOAD_MAX];
    uint32_t k;
    for (k = 0; k < SP_PER; k++)
        if (sp_read(b * SP_PER + k, 0, got + k * SP_SLOT, SP_SLOT) < 0) return 0;
    return !memcmp(got, want, ST_PAYLOAD_MAX);
}
static int pool_is_ref(void)
{
    uint32_t b;
    for (b = 0; b < SP_NBANK; b++) if (!bank_is(b, ref + b * ST_PAYLOAD_MAX)) return 0;
    return 1;
}
static void fill(uint8_t *p, uint32_t n) { uint32_t i; for (i = 0; i < n; i++) p[i] = (uint8_t)rnd(); }

/* one save of slot k (random bytes) cut at step `at` (-1: no cut); afterwards (a reboot) the pool must be the old
 * one or the new one; ref follows. Returns 1 when the new one is there */
static int cut_save(uint32_t k, long at, long bytes)
{
    static uint8_t data[SP_SLOT], before[sizeof ref];
    uint32_t b = k / SP_PER, i;
    int rc, isnew, isold;
    fill(data, SP_SLOT);
    memcpy(before, ref, sizeof ref);
    steps = 0; cut_at = at; cut_bytes = bytes;
    rc = sp_write(k, 0, data, SP_SLOT);
    boot();
    memcpy(ref + k * SP_SLOT, data, SP_SLOT);
    isnew = bank_is(b, ref + b * ST_PAYLOAD_MAX);
    isold = bank_is(b, before + b * ST_PAYLOAD_MAX);
    CHECK(isnew || isold, "slot %u cut at %ld+%ld: a mix", k, at, bytes);
    CHECK(at >= 0 || (rc == 0 && isnew), "slot %u uncut save rc %d", k, rc);
    if (!isnew) memcpy(ref, before, sizeof ref);
    for (i = 0; i < SP_NBANK; i++)
        if (i != b) CHECK(bank_is(i, ref + i * ST_PAYLOAD_MAX), "bank %u changed by a save of slot %u", i, k);
    return isnew;
}

static long steps_of_save(uint32_t k)      /* how many steps an uncut save of slot k takes now (on a copy) */
{
    static uint8_t keep[SP_NSEC * ST_SECTOR], keepref[sizeof ref];
    long n;
    memcpy(keep, nor + SP_BASE, sizeof keep);
    memcpy(keepref, ref, sizeof ref);
    steps = 0; cut_at = -1;
    sp_write(k, 0, ref, SP_SLOT);
    n = steps;
    memcpy(nor + SP_BASE, keep, sizeof keep);
    memcpy(ref, keepref, sizeof ref);
    boot();
    return n;
}

static void put_hdr(uint32_t s, uint32_t bank, uint32_t seq, const uint8_t *payload)   /* a copy made by hand */
{
    st_hdr_t h;
    memset(nor + sp_addr(s), 0xFF, ST_SECTOR);
    memcpy(nor + sp_addr(s) + ST_PAYLOAD_OFF, payload, ST_PAYLOAD_MAX);
    memset(&h, 0xFF, sizeof h);
    h.magic = ST_MAGIC; h.type = SP_TYPE; h.slot = (uint16_t)bank; h.seq = seq; h.len = ST_PAYLOAD_MAX;
    h.crc = st_crc32(payload, ST_PAYLOAD_MAX); h.rsv[0] = SP_VER; h.hcrc = st_crc32(&h, sizeof h - 4u);
    memcpy(nor + sp_addr(s), &h, sizeof h);
}

static uint8_t bulk[ST_PAYLOAD_MAX];
static int bulk_fill(uint32_t j, uint8_t *slot) { memcpy(slot, bulk + j * SP_SLOT, SP_SLOT); return 0; }
static int abort_fill(uint32_t j, uint8_t *slot) { (void)slot; return j == 4 ? -3 : 0; }

int main(void)
{
    static uint8_t pa[ST_PAYLOAD_MAX], pb[ST_PAYLOAD_MAX];
    uint32_t k, i, b;
    long n, at, total_cuts = 0;

    /* the CRC continues */
    { uint8_t x[100]; fill(x, 100); CHECK(st_crc_run(st_crc32(x, 37), x + 37, 63) == st_crc32(x, 100), "crc run"); }

    /* all sectors blank: every slot reads 0, empty */
    fresh();
    for (b = 0; b < SP_NBANK; b++) CHECK(!sp_used(b), "blank pool: bank %u used", b);
    CHECK(pool_is_ref(), "blank pool reads zeros");

    /* a cut at every step (and inside it) of a save, first saves and later ones, with and without a pre-erased spare */
    fresh();
    for (k = 0; k < 40; k++) {
        uint32_t slot = (k * 7u) % SP_SLOTS;
        if (k & 1) sp_prep();
        n = steps_of_save(slot);
        CHECK(n >= 21 && n <= 22, "a save takes %ld steps", n);
        for (at = 0; at < n; at++) {
            if (k & 1) sp_prep();
            cut_save(slot, at, 0); total_cuts++;
            if (k & 1) sp_prep();
            cut_save(slot, at, (long)(rnd() % 4097)); total_cuts++;
        }
        cut_save(slot, -1, 0);
        CHECK(pool_is_ref(), "pool after the cuts of slot %u", slot);
    }

    /* a torn header: only part of the commit record reaches the flash -> the old bank */
    fresh();
    cut_save(3, -1, 0);
    for (i = 0; i < 32; i++) {
        memcpy(pa, ref, ST_PAYLOAD_MAX);
        n = steps_of_save(3);
        CHECK(!cut_save(3, n - 1, i), "a header torn after %u bytes committed", i);
        CHECK(bank_is(0, pa), "torn header: old bank");
    }
    CHECK(cut_save(3, n - 1, 32), "a whole header commits");

    /* two copies of one bank with equal seq (this writer never makes them): the lower sector wins, stable */
    memset(nor, 0xFF, sizeof nor);
    fill(pa, ST_PAYLOAD_MAX); fill(pb, ST_PAYLOAD_MAX);
    put_hdr(9, 4, 77, pb); put_hdr(2, 4, 77, pa);
    boot();
    CHECK(sp_cur[4] == 2 && bank_is(4, pa), "equal seq: the lower sector (got %u)", sp_cur[4]);
    memset(ref, 0, sizeof ref); memcpy(ref + 4 * ST_PAYLOAD_MAX, pa, ST_PAYLOAD_MAX);
    cut_save(41, -1, 0);
    CHECK(sp_top == 78 && pool_is_ref(), "after equal seq the next save is newer than both");
    /* ... and when the lower one's payload is bad, the other */
    memset(nor, 0xFF, sizeof nor); put_hdr(2, 4, 77, pa); nor[sp_addr(2) + 300] ^= 1; put_hdr(9, 4, 77, pb); boot();
    CHECK(sp_cur[4] == 9 && bank_is(4, pb), "equal seq, bad payload: the other copy");

    /* a payload bit flipped in the newest copy: the older valid one */
    memset(nor, 0xFF, sizeof nor);
    put_hdr(5, 0, 10, pa); put_hdr(6, 0, 11, pb); nor[sp_addr(6) + 4000] ^= 0x10; boot();
    CHECK(sp_cur[0] == 5, "a bad newest payload: the older copy");

    /* seq wrap: saves across 0xFFFFFFFF stay ordered */
    memset(nor, 0xFF, sizeof nor); memset(ref, 0, sizeof ref);
    fill(pa, ST_PAYLOAD_MAX); put_hdr(0, 1, 0xFFFFFFF0u, pa); memcpy(ref + ST_PAYLOAD_MAX, pa, ST_PAYLOAD_MAX);
    boot();
    for (k = 0; k < 64; k++) cut_save(10 + (k % 20), k % 3 ? -1 : (long)(rnd() % 22), (long)rnd());
    CHECK(pool_is_ref() && sp_top < 0x40u, "seq wrap (top %#x)", sp_top);

    /* all stale: every sector a copy, most of them old ones of the same bank */
    memset(nor, 0xFF, sizeof nor); memset(ref, 0, sizeof ref);
    for (i = 0; i < SP_NSEC; i++) { fill(pa, ST_PAYLOAD_MAX); put_hdr(i, 7, 100 + i, pa); }
    memcpy(ref + 7 * ST_PAYLOAD_MAX, pa, ST_PAYLOAD_MAX);
    boot();
    CHECK(sp_cur[7] == SP_NSEC - 1 && pool_is_ref(), "all stale: the newest");
    for (k = 0; k < 30; k++) cut_save((k * 13u) % SP_SLOTS, -1, 0);
    CHECK(pool_is_ref(), "all stale, then saves");

    /* a full pool (130 slots) written slot by slot, every save cut once at a random step and then made */
    fresh();
    for (k = 0; k < SP_SLOTS; k++) {
        n = steps_of_save(k);
        cut_save(k, (long)(rnd() % (uint32_t)n), (long)rnd()); total_cuts++;
        cut_save(k, -1, 0);
    }
    CHECK(pool_is_ref(), "the full pool");
    for (b = 0; b < SP_NBANK; b++) CHECK(sp_used(b), "full pool: bank %u", b);

    /* long random sequences against the reference: saves, partial writes, erases (zeros), preps, cuts, reboots */
    fresh();
    {
        uint32_t wear[SP_NSEC] = {0}, e0, mx = 0, mn = ~0u;
        for (k = 0; k < 20000; k++) {
            uint32_t slot = rnd() % SP_SLOTS, r = rnd() % 10;
            if (r == 0) { sp_prep(); continue; }
            if (r == 1) { boot(); CHECK(pool_is_ref(), "random: reboot at %u", k); continue; }
            if (r == 2) {                       /* a slot's part changed (the record only), or cleared */
                uint8_t part[192]; uint32_t off = rnd() % 2 ? 0 : 192; int clr = rnd() % 4 == 0;
                fill(part, 192);
                e0 = n_erase;
                if (sp_write(slot, off, clr ? 0 : part, 192) == 0) {
                    if (clr) memset(ref + slot * SP_SLOT + off, 0, 192); else memcpy(ref + slot * SP_SLOT + off, part, 192);
                }
                if (n_erase != e0) wear[sp_last]++;
                continue;
            }
            if (r == 3) { n = steps_of_save(slot); cut_save(slot, (long)(rnd() % (uint32_t)n), (long)rnd()); total_cuts++; continue; }
            e0 = n_erase;
            cut_save(slot, -1, 0);
            if (n_erase != e0) wear[sp_last]++;
        }
        CHECK(pool_is_ref(), "random sequence end");
        for (i = 0; i < SP_NSEC; i++) { if (wear[i] > mx) mx = wear[i]; if (wear[i] < mn) mn = wear[i]; }
        printf("pool: wear over the 16 sectors after 20000 random steps: min %u max %u erases\n", mn, mx);
        CHECK(mn > 0 && mx < 2u * mn + 50u, "wear rotates (min %u max %u)", mn, mx);
    }

    /* a whole bank at once (the migration's sp_commit with a fill), cut at every step; a fill that aborts */
    fresh();
    for (b = 0; b < 4; b++) {
        long nb;
        fill(bulk, ST_PAYLOAD_MAX);
        memcpy(pa, ref + b * ST_PAYLOAD_MAX, ST_PAYLOAD_MAX);
        for (nb = 0; nb < 22; nb++) {
            steps = 0; cut_at = nb; cut_bytes = (long)rnd();
            sp_commit(b, bulk_fill);
            boot();
            CHECK(bank_is(b, pa) || bank_is(b, bulk), "bulk bank %u cut at %ld: a mix", b, nb);
            if (bank_is(b, bulk)) memcpy(pa, bulk, ST_PAYLOAD_MAX);
        }
        CHECK(sp_commit(b, bulk_fill) == 0 && (boot(), bank_is(b, bulk)), "bulk bank %u", b);
        memcpy(ref + b * ST_PAYLOAD_MAX, bulk, ST_PAYLOAD_MAX);
        CHECK(sp_commit(b, abort_fill) == -3 && (boot(), bank_is(b, bulk)), "an aborted fill commits nothing");
    }
    CHECK(pool_is_ref(), "bulk banks");

    /* the spare: with it pre-erased a save erases nothing; without, exactly one erase, before the first program */
    fresh();
    for (k = 0; k < 30; k++) cut_save(k * 3u % SP_SLOTS, -1, 0);
    for (k = 0; k < 20; k++) {
        uint32_t e0, p0;
        CHECK(sp_prep() >= 0 && sp_ready(), "prep");
        e0 = n_erase; p0 = n_prog;
        cut_save(k, -1, 0);
        CHECK(n_erase == e0 && n_prog - p0 == 21u, "a save on a pre-erased spare: %u erases %u programs", n_erase - e0, n_prog - p0);
        CHECK(!sp_ready(), "a save uses the spare");
        CHECK(sp_prep() == 1 || sp_prep() == 0, "prep again");
        e0 = n_erase;
        CHECK(sp_prep() == 0 && n_erase == e0, "a second prep erases nothing");
    }
    for (k = 0; k < 20; k++) {               /* no prep between saves */
        uint32_t e0 = n_erase, p0 = n_prog;
        steps = 0;
        cut_save(k + 50u, -1, 0);
        CHECK(n_erase - e0 <= 1u && n_prog - p0 == 21u, "a save without a spare: %u erases", n_erase - e0);
    }

    /* a bank outside the pool's sectors is never touched: the sectors around stay as they are */
    {
        uint32_t c0 = st_crc32(nor + 0xB4000, 0x4000), c1 = st_crc32(nor + 0xC8000, 0x4000);
        for (k = 0; k < 500; k++) cut_save(rnd() % SP_SLOTS, rnd() % 2 ? -1 : (long)(rnd() % 22), (long)rnd());
        CHECK(c0 == st_crc32(nor + 0xB4000, 0x4000) && c1 == st_crc32(nor + 0xC8000, 0x4000), "outside the pool");
    }
    /* the older firmware's own reader never sees a pool sector as one of its objects */
    for (i = 0; i < SP_NSEC; i++) { st_hdr_t h; uint32_t o; for (o = 0; o < OBJ_COUNT; o++) { (void)h; CHECK(sp_addr(i) != st_sector(o, 0) && sp_addr(i) != st_sector(o, 1), "sector %u is object %u's", i, o); } }

    printf("pool: %ld power cuts, %s\n", total_cuts, fails ? "FAIL" : "ok");
    return fails != 0;
}
