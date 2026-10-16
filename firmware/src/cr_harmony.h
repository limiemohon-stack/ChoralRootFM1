/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: Harmony's pure engine. Portable C99, integer only, no allocation, no libc (stdint.h +
 * cr_engine.h for cr_chord_info_t, cr_chord_base_ex, cr_voice_apply), reusing the engine's own chord tables so a
 * "minor", "M7" or a voicing rotation here means exactly what it means everywhere else in ChoralRoot. Three parts:
 *  - the lanes (Harmony mode, the end of this file): a fixed palette of jazz / neo-soul chords on the 18 root keys,
 *    restful to pulling, in three voicing styles. cr_ui.c's L_HARM layer (LATCH held) plays it; a chord reaches the engine whole through the UI-to-audio queue (cr_out.c CRE_HSPEC, CRE_HNOTE,
 *    CRE_HKEY: cr_engine.c cr_key_chord);
 *  - the listener and the history: the chords the player strikes (cr_out.c's gesture ring, judged on the UI side);
 *  - the suggestion engine of the retired Harmony Preview (one REST / MOVE / PULL destination each, the Harmony
 *    Preview Edge Case Spec, revised 2026-10-13): kept with its tests and the musical benchmark
 *    (tests/cr_harmony_bench.c) as a reference; the UI no longer calls it.
 * No UI, no MIDI, no audio, and no notion of a UI-to-audio queue: this module only computes. */
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

/* --------------------------------------------------------------- the lanes --- */
/* Harmony mode (cr_ui.c's LATCH-held layer): the 18 root keys stop meaning C, D, E and play a fixed palette of
 * jazz / neo-soul chords on the HOME key. Fixed: a key plays the same chord, voiced the same way, every time, until
 * home moves (OCT+ in the mode) or the voicing style changes (KNOB 1) -- so a chord you find is there when you
 * come back to it, and the hands can learn where things are.
 *
 * The eleven white roots (D4 .. G5) are the palette, restful on the left to pulling on the right. Major home (C):
 *   Imaj9  vim9  iiim7  IVmaj9  iim9  ivm9  bVImaj9  bVII9  V9sus  V9  bII9
 *   Cmaj9  Am9   Em7    Fmaj9   Dm9   Fm9   Abmaj9   Bb9    G9sus  G9  Db9
 * Minor home (A):
 *   im9  bVImaj9  bIIImaj9  ivm9  vm7  bVII9  iim7b5  IV9  V9sus  V7  bII9
 *   Am9  Fmaj9    Cmaj9     Dm9   Em7  G9     Bm7b5   D9   E9sus  E7  Bb9
 * The seven black roots are approach chords: each leans into the white root to its right (a secondary dominant,
 * or a side-slip from a semitone above), so black then white is a move. Major (C): D#4 E7 (-> Am9), F#4 C9
 * (-> Fmaj9), G#4 A9 (-> Dm9), A#4 Gbmaj7 (-> Fm9), C#5 F9 (-> Bb9), D#5 D9 (-> G9sus), F#5 Ab9 (-> Db9).
 * Minor (A): C9, A9, B7, Abmaj7, Eb9, F9, F#m7b5.
 *
 * Every chord is one the engine names (no 13ths: cr_chord_name calls 6 + 7 + 9 "JAZZ"). */
#define CR_HMAP_W 11             /* white roots D4 E4 F4 G4 A4 B4 C5 D5 E5 F5 G5 */
#define CR_HMAP_B 7              /* black roots D#4 F#4 G#4 A#4 C#5 D#5 F#5 */
extern const uint8_t CR_HMAP_BW[CR_HMAP_B];   /* black root i sits just right of white root CR_HMAP_BW[i] */

enum { CR_HCAT_DIATONIC, CR_HCAT_NEAR, CR_HCAT_BORROWED, CR_HCAT_SECDOM, CR_HCAT_CHROMATIC };
typedef struct {
    uint8_t root_pc, quality, ext;
    uint8_t cat;                 /* CR_HCAT_DIATONIC: every note in the home scale; else CR_HCAT_CHROMATIC */
    int8_t tension;              /* its place, 0 = the most restful (white) / the white root it leans into (black) */
    uint8_t home;                /* 1: the home chord itself (the way home) */
    uint8_t approach;            /* 1: a black key's approach chord (it leans into the white key to its right) */
    uint8_t flav;                /* the chord keys held with it, bit cr_mod_t (cr_harm_flavour; 0 from the map) */
} cr_hmap_chord_t;
typedef struct {
    uint8_t home_pc, home_scale; /* cr_scale_t: the mode's family (major: Lydian, Ionian, Mixolydian; else minor) */
    uint8_t mode;                /* CR_HMODE_* */
    cr_hmap_chord_t w[CR_HMAP_W];   /* left to right: restful .. pulling */
    cr_hmap_chord_t b[CR_HMAP_B];
} cr_hmap_t;

void cr_harm_lanes(uint8_t home_pc, uint8_t home_scale, cr_hmap_t *map);   /* = Ionian / Aeolian below */

/* Modes: one home, six colours, bright to dark (the SELECT knob in the mode). Ionian and Aeolian are the major and
 * minor palettes above, unchanged. The others keep the palette's shape -- home on D4, the mode's own chords to its
 * right, then colour borrowed from outside it, and the same pull home at the right end (V9sus, V, bII9) -- with the
 * mode's signature chords where the hand looks first. In C:
 *   LYDIAN      Cmaj9 D9 Bm7 Em9 Gmaj9 Am9 F#m7b5 | Fmaj9      (II9: the raised 4th)
 *   MIXOLYDIAN  C9 Bbmaj9 Gm9 Fmaj9 Dm9 Am7 Em7b5 | Abmaj9      (a dominant home: the blues; bVII, v minor)
 *   DORIAN      Cm9 F9 Ebmaj9 Dm7 Gm9 Bbmaj9 Am7b5 | Abmaj9     (IV9: the raised 6th)
 *   PHRYGIAN    Cm7 Dbmaj9 Bbm9 Abmaj9 Fm9 Eb9 Gm7b5 | Bb9      (bII: the lowered 2nd)
 * Their black keys are approach chords made the same way as the palettes': the dominant of the white key to the right
 * (a 7th into a minor one), or when that chord is already on the keys its tritone substitute, or the major 7th a
 * semitone above; A#4's is the semitone-above major 7th, as in the major palette. */
enum { CR_HMODE_LYDIAN, CR_HMODE_IONIAN, CR_HMODE_MIXO, CR_HMODE_DORIAN, CR_HMODE_AEOLIAN, CR_HMODE_PHRYGIAN,
       CR_HMODE_N };
void cr_harm_lanes_mode(uint8_t home_pc, int mode, cr_hmap_t *map);
const char *cr_harm_mode_name(int mode);         /* "lydian", "major", "mixolydian", "dorian", "minor", "phrygian" */
int cr_harm_mode_family(int mode);               /* CR_SCALE_MAJOR (Lydian, Ionian, Mixolydian) or CR_SCALE_MINOR */

/* Voicing styles (KNOB 1 in the mode). Each is a pure function of the chord: no previous chord is consulted.
 *   SHELL   root low, then the 3rd and 7th (the guide tones): three notes, the top near E4
 *   SPREAD  root low, the 7th, the 3rd, the 9th (or the 5th) on top: open, about two octaves
 *   COLOUR  root low, then the 3rd, 5th, 7th and 9th in close position as one hand would, the top near A4
 * The root sits in E2 .. D#3, the upper notes about C3 .. C5; shift (the player's octave, 12 an OCT step) moves
 * it all. Because every chord's top note lands in the same narrow band, moving between keys makes a line of small
 * steps on top. root_note is the root in the octave of the upper notes: what a looper records (it plays chords at
 * the live voicing). rootless (the Bass part on: it plays the root): no low root -- Shell is the 3rd, 7th and the
 * 9th (or 5th) above, Spread its three upper notes, Colour its four-note hand. Returns the note count (3 .. 5). */
enum { CR_HSTYLE_SHELL, CR_HSTYLE_SPREAD, CR_HSTYLE_COLOUR, CR_HSTYLE_N };

/* Colour worlds (KNOB 1 in the mode): each turns every chord into its version of that world, by the chord's job
 * (a major chord, a dominant, a minor, a sus, a diminished):
 *   BARE     root and 5th: C5
 *   TRIAD    the plain triad: C, Am, G; with chord keys held, just their chord: 6 C6, m7 C7, M7 Cmaj7, 6 + 9 C6/9
 *   JAZZ     the palette as written: Cmaj9, Am9, G9 (the default)
 *   LUSH     11ths and 13ths: Cmaj9#11, Am11, G13, G13sus, Bm11b5
 *   QUARTAL  stacked 4ths: C6/9 (E A D G), Am11 (A D G C E), G13 (F B E A), G7sus (D G C F)
 *   ALTERED  the dark side: G7sus b9, Bo7; a dominant on a black key (an approach) 7b9: A7b9 into Dm, on a white
 *            key 7b9b13: G7b9b13 (flavour 9: #9, A7#9, G7#9b13)
 * Each world recolours the chords it suits and leaves the others as JAZZ plays them (crh_world_suits): QUARTAL and
 * UPPER the major, minor and sus chords of the white keys (dominants and approaches keep their pull); ALTERED only
 * the pulling chords (dominants, sus, diminished, approaches: home and the resting chords stay as written).
 *   UPPER    a triad floating over the root: D/C (Cmaj13#11), G/Am (Am11), A/G7 (G13#11), F/G (G11sus)
 * cr_harm_voice_world voices d (flavours already applied to it) in the world and the style; root, qual and sup
 * are its name for the screen (as cr_chord_name's three strings); loop_q / loop_x the nearest chord the engine
 * knows (what a looper records). Returns the note count (3 .. 5). */
enum { CR_HCOL_BARE, CR_HCOL_TRIAD, CR_HCOL_JAZZ, CR_HCOL_LUSH, CR_HCOL_QUARTAL, CR_HCOL_ALTERED, CR_HCOL_UPPER,
       CR_HCOL_N };
/* the chord keys held with a root key (flav: bit cr_mod_t; MAJ, MIN and DIM are the player's own chord, not a
 * flavour, and are ignored here): SUS makes it sus4, m7 a flat 7th, M7 a major 7th, 6 a 6th (each of the three
 * replaces the others), 9 adds the 9th. out = d flavoured, out->flav = flav (a world may read it: TRIAD, ALTERED) */
void cr_harm_flavour(const cr_hmap_chord_t *d, unsigned flav, cr_hmap_chord_t *out);
/* Approaches (Harmony mode: a lower chord key held with a root, cr_ui.c): the chord that leads into the key's chord
 * `to` instead of it; out is voiced like any chord (cr_harm_voice_world). In C, into Dm9:
 *   SLIP     Ebm9: the same chord a semitone up (in its world: a quartal shape slides as itself)
 *   DOM      A7: its V (a 7th into a minor chord, a 9th into a major one)
 *   DIM      C#dim7: the diminished 7th a semitone under it (a dim7 in every world)
 *   TRITONE  Eb9: the dominant a semitone over it (V's tritone substitute) */
enum { CR_HAPP_SLIP, CR_HAPP_DOM, CR_HAPP_DIM, CR_HAPP_TRITONE, CR_HAPP_N };
void cr_harm_approach(const cr_hmap_chord_t *to, int kind, cr_hmap_chord_t *out);
int cr_harm_voice_world(const cr_hmap_chord_t *d, int world, int style, int shift, int rootless,
                        uint8_t notes[CR_CHORD_MAX], uint8_t *root_note, char root[3], char qual[5],
                        char sup[CR_SUP_MAX], uint8_t *loop_q, uint8_t *loop_x);
int cr_harm_voice_style(const cr_hmap_chord_t *d, int style, int shift, int rootless, uint8_t notes[CR_CHORD_MAX],
                        uint8_t *root_note);

#endif
