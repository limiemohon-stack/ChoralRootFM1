/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: Harmony Preview's pure suggestion engine (see the Harmony Preview Edge Case Spec, revised
 * 2026-10-13). Portable C99, integer only, no allocation, no libc (stdint.h + cr_engine.h for cr_chord_info_t,
 * cr_chord_base_ex, cr_voice_apply). Given the currently sounding chord and a short history of committed
 * chords, infers (or is told) a key, proposes one REST / MOVE / PULL destination each, and voice-leads every
 * candidate against the sounding chord by minimum-cost alignment, reusing the engine's own chord tables so a
 * "minor", "M7" or a voicing rotation here means exactly what it means everywhere else in ChoralRoot.
 *
 * No UI, no MIDI, no audio, and no notion of a UI-to-audio queue: this module only computes. The UI (cr_ui.c's
 * L_HARM layer, LATCH held) calls it; a confirmed chord reaches the engine through the UI-to-audio queue
 * (cr_out.c CRE_HARM_CONFIRM), and the chords the player strikes reach history through the listener below (fed
 * by cr_out.c's gesture ring, judged on the UI side). An audible audition while browsing is not wired yet. */
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

/* ------------------------------------------------------------- listener --- */
/* Played chords -> history. The engine reports every chord the player strikes as a gesture (cr_out_t.gesture:
 * begin, end, and CR_G_KEYUP for a latched one) -- keyed chords and chord pads only: never a loop voice (so never
 * loop playback, and never Harmony's own confirm voice, which plays on one), never MIDI in. cr_out.c copies those
 * reports unjudged from the audio ISR into a lock-free ring; the UI drains them into the two calls below, so every
 * decision here is made outside the audio path. Pure: no clock of its own (the caller's milliseconds, wrap-safe),
 * no allocation.
 *
 * A gesture is committed (pushed with cr_harm_hist_push, whose own rules then apply: fewer than 3 pitch classes
 * rejected, a repeat of the newest entry absorbed) once it has SOUNDED for CR_HARM_SETTLE_MS, held or latched:
 *   - a brush or a corrected mis-press (begun and ended inside the window) never commits;
 *   - a latched chord (Sticky, a Perform mode's Hold) commits after the window though its key came up at once;
 *   - Advanced / Free can change a held chord after it began; the engine reports the final state at key-up / end,
 *     and a final identity different from the one committed is committed too (the chord the player arrived at);
 *   - a same-root retrigger is a new gesture of the same identity: history absorbs it as a repeat;
 *   - a Split Bass bass-side note (ext & CR_GX_BASS) is not a chord: ignored outright.
 * Up to CR_HARM_LISTEN_N gestures are followed at once (a chord struck while another still sounds: each settles on
 * its own, committed in the order they began); past that, the oldest one is forgotten (a committed one first). */
#define CR_HARM_SETTLE_MS 120
#define CR_HARM_LISTEN_N 4

typedef struct {
    uint8_t live;                /* this slot follows a gesture */
    uint8_t committed;           /* done holds the identity last committed for it */
    uint16_t gid;                /* the gesture's id (cr_voice_t.gid, low 16 bits: only ever matched while live) */
    uint16_t ord;                /* begin order (ties of t0 broken by it) */
    uint32_t t0;                 /* when it began, the caller's ms */
    cr_harm_chord_t cur, done;   /* its latest reported identity; the last one committed */
} cr_harm_follow_t;
typedef struct {
    cr_harm_follow_t f[CR_HARM_LISTEN_N];
    uint16_t next_ord;
} cr_harm_listen_t;

void cr_harm_listen_clear(cr_harm_listen_t *L);
/* One gesture report: gid, root (absolute, transpose and Key Mode applied: only its pitch class is used), the
 * resolved quality and extension mask, on (1 begin, 0 end, CR_G_KEYUP), and when it happened (t_ms). Returns the
 * number of chords pushed into *h now (0..1; history's own rules may still absorb a repeat). */
int cr_harm_listen_event(cr_harm_listen_t *L, cr_harm_hist_t *h, uint32_t gid, int16_t root, uint8_t quality,
                         uint8_t ext, int on, uint32_t t_ms);
/* The passage of time: commits every followed gesture that has sounded CR_HARM_SETTLE_MS by now_ms, oldest first;
 * returns how many were pushed. Read now_ms BEFORE draining the reports that precede this call (a report stamped
 * after it then never makes a chord look older than it is). */
int cr_harm_listen_tick(cr_harm_listen_t *L, cr_harm_hist_t *h, uint32_t now_ms);

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
