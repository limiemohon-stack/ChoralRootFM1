/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The firmware's memset / memcpy / memcmp (firmware/src/libc.c, word at a time since 3y50) against the host's: every
 * alignment of both ends (0..7), every length 0..80 and a few long ones, the bytes around the area untouched, memcmp's
 * sign at every differing byte. The firmware's own are renamed here (the host has its libc) */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define memset fw_memset
#define memcpy fw_memcpy
#define memcmp fw_memcmp
#include "libc.c"
#undef memset
#undef memcpy
#undef memcmp

static int sgn(int v) { return (v > 0) - (v < 0); }
int main(void)
{
    static uint8_t a[600] __attribute__((aligned(8))), b[600] __attribute__((aligned(8))), r[600] __attribute__((aligned(8)));
    static const unsigned LONG[] = {127, 128, 129, 255, 256, 257, 511};
    unsigned da, sa, n, i, k, cases = 0, fails = 0;
    for (da = 0; da < 8u; da++)
        for (sa = 0; sa < 8u; sa++)
            for (k = 0; k < 81u + sizeof LONG / sizeof LONG[0]; k++) {
                n = k <= 80u ? k : LONG[k - 81u];
                for (i = 0; i < sizeof a; i++) {
                    a[i] = r[i] = (uint8_t)(i * 7u + 3u);
                    b[i] = (uint8_t)(i * 13u + 1u);
                }
                fw_memcpy(a + da, b + sa, n);
                memcpy(r + da, b + sa, n);
                fails += memcmp(a, r, sizeof a) != 0;
                fw_memset(a + da, (int)(0x5A + sa), n);
                memset(r + da, (int)(0x5A + sa), n);
                fails += memcmp(a, r, sizeof a) != 0;
                fw_memset(a + da, 0x1A5, n);                    /* (an int above a byte: its low byte) */
                memset(r + da, 0x1A5, n);
                fails += memcmp(a, r, sizeof a) != 0;
                memcpy(a + da, b + sa, n);
                fails += fw_memcmp(a + da, b + sa, n) != 0;
                for (i = 0; i < n; i++) {                       /* one byte different, above and below */
                    a[da + i] = (uint8_t)(b[sa + i] + 1u);
                    fails += sgn(fw_memcmp(a + da, b + sa, n)) != sgn(memcmp(a + da, b + sa, n));
                    a[da + i] = (uint8_t)(b[sa + i] - 1u);
                    fails += sgn(fw_memcmp(a + da, b + sa, n)) != sgn(memcmp(a + da, b + sa, n));
                    a[da + i] = b[sa + i];
                }
                cases++;
            }
    printf("cr_libc: %u cases (both ends at every alignment 0..7, lengths 0..80 and 7 long ones): %s\n", cases,
           fails ? "FAIL" : "memset, memcpy and memcmp give the host's bytes and signs");
    return fails != 0;
}
