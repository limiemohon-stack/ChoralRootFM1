/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM1_INLINE: register helpers that must inline into their caller, so the
 * SFR access sequence (and its timing, in ISRs and IRQ-off sections) is the
 * one written at the call site; -Os would otherwise out-line a helper with
 * several callers. RAM-resident code (.ram_text) may only use these, never
 * a called helper (build.py checks .ram_text for calls). */
#pragma once

#define FM1_INLINE static inline __attribute__((always_inline))

/* FM1_FAR(fn): fn's address as a value the optimiser cannot see through, for a call between XIP (0x02001000..) and
 * RAMTEXT (0x01C00000..): they are 4 MiB + 4 KiB apart and pi32v2's direct call reaches 4 MiB (the linker refuses a
 * longer one: "relocation truncated to fit: R_PI32V2_LONG_JUMP_23M2"). The call becomes `rN = <fn>; call rN`, an
 * absolute address in a register: one instruction more than the direct call, nothing on the stack (hal/fm1_flash.h's
 * FL_FAR, older, goes through a volatile on the stack: the same call, three instructions more). The empty asm
 * statement emits nothing; it is here, in hal/, because src/ may hold no inline asm (tools/build.py mmio_check). */
FM1_INLINE void *fm1_far(void *p) { __asm__ volatile("" : "+r"(p)); return p; }
#define FM1_FAR(fn) ((__typeof__(&fn))fm1_far((void *)&fn))
/* Target board: 0 = M-Vave FM-1 (default), 1 = M-Vave SMK-37 (same AC791N, SPL and update loader; different
 * panel wiring). build.py sets it from BOARD=smk37. */
#ifndef BOARD_SMK37
#define BOARD_SMK37 0
#endif
/* SMK_MODEL: the model name the SMK build reports (its identity, its update loader's): the SMK-37 Pro,
 * SMK-37 Elite and MKE-P37 are the same keyboard under three names, and M-Vave's updater only takes a
 * unit's own factory package. build.py sets it from MODEL=elite / mke. */
#if BOARD_SMK37 && !defined(SMK_MODEL)
#define SMK_MODEL "SMK-37 Pro"
#endif
