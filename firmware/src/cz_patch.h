/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MELODEE_CZ_PATCH_H
#define MELODEE_CZ_PATCH_H
/* Casio CZ-1 MIDI specification, pp. 83-98. Keep the native 144-byte
 * tone verbatim: 128 synthesis bytes followed by the 16-byte LCD name.
 * Transport uses low nibble first. No conversion to common ADSR controls. */
/* ChoralRoot: ENGI_CZ is core.h's (14 on ChoralRoot; Melodee's is 15) */
#define CZ_BYTES 144u
#define CZ_NATIVE 2u
typedef struct { uint8_t raw[CZ_BYTES]; } cz_patch_t;

/* an LZ4 block (tools/lz4blk.py's format: literals and matches, no frame) into dst, n bytes: the factory bank's tones
 * (melodee_cz1.h CZ_FACTORY_LZ4, 3y129). The packer's blocks are well formed; a bad one leaves the rest of dst as it
 * was and stops at its end */
static void cz_lz4_block(const uint8_t *src, uint32_t len, uint8_t *dst, uint32_t n)
{
    uint32_t i = 0, o = 0;
    while (i < len && o < n) {
        uint32_t tok = src[i++], ll = tok >> 4, ml, off, b;
        if (ll == 15u) {
            do { b = i < len ? src[i++] : 0u; ll += b; } while (b == 255u);
        }
        while (ll-- && o < n && i < len)
            dst[o++] = src[i++];
        if (i + 2u > len)
            break;
        off = src[i] | (uint32_t)src[i + 1u] << 8;
        i += 2u;
        ml = tok & 15u;
        if (ml == 15u) {
            do { b = i < len ? src[i++] : 0u; ml += b; } while (b == 255u);
        }
        if (!off || off > o)
            break;
        for (ml += 4u; ml-- && o < n; o++)
            dst[o] = dst[o - off];
    }
}
static const uint8_t CZ_ENV_BASE[2][3] = {{55,38,21},{112,95,78}};
static const uint8_t CZ_ENV_END[2][3] = {{54,37,20},{111,94,77}};
static __attribute__((noinline)) int cz_patch_valid(const uint8_t *b)
{
    if ((b[0] & 0xF0u) || (b[0] >> 2) > 2u || b[1] > 1u || (b[2] & 3u) || b[3] > 47u) return 0;
    for (uint32_t l = 0; l < 2u; l++) {
        uint32_t off = l ? 57u : 0u;
        if ((b[16u + off] & 15u) > 9u || (b[16u + off] >> 4) > 14u || b[18u + off] > 9u) return 0;
        for (uint32_t e = 0; e < 3u; e++)
            if (b[CZ_ENV_END[l][e]] & 8u) return 0;
    }
    /* LCD data may contain padding or custom characters; display sanitizes it. */
    return 1;
}
static __attribute__((noinline)) void cz_patch_init(uint8_t *b)
{
    memset(b, 0, CZ_BYTES);
    b[4] = 8;                                      /* triangle vibrato, depth zero */
    b[20] = b[37] = 0xF1; b[54] = 0xF0;           /* no velocity sensitivity */
    b[21] = 0x77; b[22] = 0xFF; b[23] = 0xF7;
    b[38] = 0x7F; b[39] = 0xFF; b[40] = 0xFF; b[55] = 0x7F;
    memcpy(b + 71, b + 14, 57);
    memset(b + 128, ' ', 16); memcpy(b + 128, "INIT", 4);
}
#endif
