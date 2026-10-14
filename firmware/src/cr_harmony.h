/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: Harmony Preview's pure suggestion engine (see the Harmony Preview Edge Case Spec, revised
 * 2026-10-13). Portable C99, integer only, no allocation, no libc (stdint.h + cr_engine.h for cr_chord_info_t,
 * cr_chord_base_ex, cr_voice_apply). Given the currently sounding chord and a short history of committed
 * chords, infers (or is told) a key, proposes one REST / MOVE / PULL destination each, and voice-leads every
 * candidate against the sounding chord by minimum-cost alignment, reusing the engine's own chord tables so a
 * "minor", "M7" or a voicing rotation here means exactly what it means everywhere else in ChoralRoot.
 *
 * No UI, no MIDI, no audio, and no notion of a UI-to-audio queue: this module only computes. It is not wired
 * into the device build yet (see LICENSING.md / BUILDING.md on registering a new translation unit) and not
 * reachable from the panel or from audio output. Staging a result through the existing UI-to-audio queue,
 * auditioning it, and routing a confirmed chord are the second, separately reviewed integration step. */
#ifndef CR_HARMONY_H
#define CR_HARMONY_H

#include <stdint.h>
#include "cr_engine.h"

/* -------------------------------------------------------------- history --- */
#define CR_HARM_HIST 4           /* chords scored for key inference ("the last 4") */

typedef struct { uint8_t root_pc, quality, ext; } cr_harm_chord_t;   /* a committed chord's identity (pitch
                                                                       * classes only; no octave, no voicing) */
typedef struct {
    uint8_t n;                               /* entries held, 0..CR_HARM_HIST, oldest first, newest at e[n-1] */
    cr_harm_chord_t e[CR_HARM_HIST];
} cr_harm_hist_t;

void cr_harm_hist_clear(cr_harm_hist_t *h);
/* Push a committed chord (an ordinary chord-key/pad gesture, or a confirmed Harmony suggestion -- never an
 * audition, a cancellation, loop playback, or incoming MIDI: that filtering is the caller's, at the call site).
 * Rejects (returns 0, *h unchanged) a chord with fewer than 3 distinct pitch classes -- it still plays
 * normally, it just doesn't train or disturb the inferred key. A repeat of the most recently pushed identity
 * is accepted but does not consume a new slot (returns 1, *h unchanged beyond that). Otherwise shifts the
 * oldest out past CR_HARM_HIST and appends (returns 1). */
int cr_harm_hist_push(cr_harm_hist_t *h, uint8_t root_pc, uint8_t quality, uint8_t ext);

/* ------------------------------------------------------------------ key --- */
typedef struct {
    uint8_t held;            /* 0: no key acquired yet, or cleared -- candidate generation then uses a
                               * provisional per-chord tonic (see cr_harm_suggest) and suppresses borrowed
                               * candidates */
    uint8_t tonic_pc;
    uint8_t scale;            /* cr_scale_t */
    uint16_t score;           /* the held key's own last computed score, 0..50; diagnostic, and the base the
                               * next cr_harm_key_infer() measures a challenger's margin against */
} cr_harm_key_t;

void cr_harm_key_clear(cr_harm_key_t *key);                /* boot, panic-equivalent, or Key Mode turned off
                                                              * (clear cr_harm_hist_t too when it's the latter) */
/* Key Mode is on: use its tonic/scale immediately, bypassing inference (no hysteresis, no history needed). */
void cr_harm_key_explicit(cr_harm_key_t *key, uint8_t tonic_pc, uint8_t scale);
/* Key Mode is off: rescans hist and updates *key in place. No key is held until hist has 3 qualifying entries
 * (raised from 2: with only 2, the newest chord's recency weight can transiently outweigh the evidence for
 * the musically correct key -- see cr_harm_key_infer's history comment in cr_harmony.c and the Edge Case
 * Spec's key-inference section). Once held, a challenger only takes over by beating the held key's score by
 * 3 points or more; smaller or
 * equal margins (including exact ties) keep the held key. Ties among challengers for "the best candidate"
 * (relevant both for first acquisition and for who the margin is measured against) break by: the most
 * recent chord whose root and major/minor quality match that candidate's tonic/mode, then ascending tonic
 * pitch class, then major before minor. Deterministic, pure in (hist, previous *key). */
void cr_harm_key_infer(cr_harm_key_t *key, const cr_harm_hist_t *hist);

/* -------------------------------------------------------------- results --- */
typedef enum { CR_HARM_REST, CR_HARM_MOVE, CR_HARM_PULL } cr_harm_tension_t;
typedef enum {
    CR_HARM_UNAVAILABLE,      /* no distinct candidate could be built for this slot at all */
    CR_HARM_VOICING,          /* a different rotation of the CURRENT chord (fallback; not a tension destination) */
    CR_HARM_SUGGEST           /* a real REST / MOVE / PULL destination */
} cr_harm_kind_t;

typedef struct {
    uint8_t kind;              /* cr_harm_kind_t */
    uint8_t tension;           /* cr_harm_tension_t; meaningful when kind == CR_HARM_SUGGEST */
    uint8_t root_pc, quality, ext;
    uint8_t nnotes, notes[CR_CHORD_MAX];    /* the chosen voicing, low to high (0 notes when kind == UNAVAILABLE) */
    uint8_t root_note;         /* the absolute MIDI root cr_chord_base_ex was called at; with rotation, replays
                                 * this exact voicing: cr_chord_base_ex(root_note, quality, ext, sixth_up, base)
                                 * then cr_voice_apply(base, n, rotation, out) reproduces notes[] exactly. Meant
                                 * for a confirm step that must sound precisely what's shown, not a re-derived
                                 * approximation that could land on a different octave or rotation */
    int8_t rotation;           /* the cr_voice_apply rotation used, -12..12 */
    uint8_t common;            /* common PITCH CLASSES with the source chord (0..12), voicing-independent */
    uint8_t added, removed;    /* voices gained / lost vs. the source chord's own voicing, under the chosen
                                 * alignment */
    uint16_t move;             /* summed semitone movement of the alignment's matched voices (raw, undoubled --
                                 * this is what an explanation code like "move 5" reports, not the search's
                                 * internal penalized cost) */
} cr_harm_result_t;

/* Fills out[3] in REST, MOVE, PULL order from the sounding chord *source* and the held or provisional key.
 * Returns 0 and leaves out[] untouched when source has no valid chord at all (source->valid == 0) -- the
 * caller shows "Play a chord", not three empty or fabricated slots. Returns 1 otherwise: every slot is
 * written, and a slot's kind tells the caller how to label and whether to grey it out (CR_HARM_UNAVAILABLE).
 * No two slots ever carry the same sounding note set. sixth_up is cr_t's Options > 6th with 7th setting,
 * threaded through to cr_chord_base_ex exactly as the engine itself does. Pure: no state is kept here between
 * calls (cr_harm_key_t is the caller's, updated separately by cr_harm_key_infer / cr_harm_key_explicit). */
int cr_harm_suggest(const cr_harm_key_t *key, const cr_chord_info_t *source, int sixth_up, cr_harm_result_t out[3]);

#endif
