# Chord Layout: Classic and FM-1 (the design)

Status: **built** (HISTORY 3y163: the firmware, the emulators, the manual; [The build](#the-build-history-3y163) says where
it differs from the design below), and **since 0.25.1 FM-1 is the default** (HISTORY 3y172; the owner, 2026-10-16: "It's
been decided, the new FM-1 chord key layout is significantly better on this device than the classic, let's make it
the default", then "Units from old should get put on the new chord key layout"): a unit with no settings record,
Flash Data and Reset Settings start in FM-1, and every unit updated from 0.25 or older is moved to FM-1 once,
whatever it had stored; after that the player's choice stands, Classic included. The move is marked in bit 1 of the
settings block's `leds` byte, with no version ([SETTINGS.md "0.25.1: FM-1 the default"](SETTINGS.md#0251-fm-1-the-default)
has the reasons, the downgrade and the backups). The manual and the site now lead with FM-1, Classic being the
option. Where the text below says "Classic, the default", it is the design of 0.24 / 0.25. The design: every question answered by the owner on 2026-10-16 (HISTORY 3y155, with the owner's answers to its open questions
the same day: [the second round](#the-second-round-the-owners-answers-to-the-open-questions-2026-10-16)).
No firmware implements it yet. The pictures
are in [design/chord-layout-mockups.html](../design/chord-layout-mockups.html) (rendered:
[design/chord-layout-mockups.png](../design/chord-layout-mockups.png)).

Today's chord keys follow the Orchid ([MANUAL.md "3. The panel"](MANUAL.md#the-keybed) and
["4. Chords"](MANUAL.md#4-chords)). The owner wants a second layout that fits the FM-1's keybed and their hand: the
three small black keys of the left end are the chord types, SUS is two of them pressed with one finger, the four
white keys are the extensions, LOCK moves to a button, and two more keys become roots. **Options > Chord Layout**
chooses between the two (on the screen: **Chord Keys**): **Classic** (today's, the default, unchanged in every
gesture) and **FM-1**.

Contents: [The decisions](#the-owners-decisions-2026-10-16) · [The two layouts](#the-two-layouts) ·
[The Options](#the-options-and-where-they-are-kept) · [The combo table](#the-combo-table) ·
[The combine window](#the-combine-window) · [LOCK and LATCH](#lock-and-latch-in-fm-1) ·
[Picking keys](#picking-keys-in-layers) · [MIDI in](#midi-in) · [The Full keyboard](#the-full-keyboard-both-layouts) ·
[Unchanged](#unchanged) · [Every place that reads the keys](#every-place-that-reads-the-chord-blocks-keys) ·
[Cost](#cost) · [Tests](#the-tests-the-build-will-need) · [Open questions](#open-questions)

## The owner's decisions (2026-10-16)

Recorded as decided; the sections below work them out.

1. **Options > Chord Layout: Classic / FM-1.** Classic is today's layout and the default. It lives in the settings
   block (below: `sticky`'s spare bits); an older firmware ignores it and starts in Classic; it is hidden on the
   SMK-37 build.
2. **The FM-1 layout.** F#3 G#3 A#3 = DIM MIN MAJ. F3 G3 A3 B3 = 6 m7 M7 9. C4 .. G5 are roots: 20 instead of 18
   (C4 and C#4 become roots). **SUS = G# + A# held together, always, whatever Secret Chords says** (the owner presses
   both small black keys with one finger).
3. **Secret chords in FM-1**, only with Secret Chords on; each keeps the key it has in Classic: F# + G# + A# = the
   power chord (DIM plus the SUS pair, as DIM + SUS); F# + G# = m add4 (keeps MIN, as MIN + SUS); F# + A# = augmented
   (keeps MAJ, as MAJ + SUS). With Secret Chords off only G# + A# combine; the other pairs keep "last pressed wins".
4. **The combine window** ("maybe even 50ms or more"): a chord starts at once with what is held. If G#'s or A#'s
   partner lands within the window, the sounding chord becomes SUS in place, only its third moving to the fourth,
   with no restrike. A G# or A# held from earlier never waits. The window is an Option, 30 .. 80 ms, default 50.
   (**Replaced in the second and third rounds**: a root pressed inside an open window waits for it and plays once;
   nothing is amended in place: [the combine window](#the-combine-window).)
5. **LOCK in FM-1**: LFO tap = LOCK on / off; LFO held = the LOCK layer; **GLO + LFO = LATCH**. A latched SUS shows
   as two lit keys, and pressing that same pair again on its own switches it off (as one type key does today). The
   LFO light shows LOCK steadily; while LATCH is on it pulses. The screen's `Latch` stays. LOCK cannot be switched
   inside the sound editor, where LFO is the editor's LFO button; that is fine.
6. **Picking keys in layers** (loop slots, naming letters, the KEY layer's tonic, the pickers) stay on D4 .. G5 in both
   layouts. The default; the owner may revisit it ([Open questions](#open-questions) 1).
7. **MIDI in that plays the FM-1's keys follows the layout** ([MIDI in](#midi-in): what the code does today).
8. **The Full keyboard (both layouts)**: KNOB 1 is the **voicing**, not the octave (chords can be played there now);
   the octave stays on OCT- / OCT+. A new Option, **Full keyboard shortcuts On / Off**, default Off. When on: **F#5**
   brings the active layout's chord keys back on the low keys; **G5** switches between single notes and a chord on
   every key. Both **toggle on a quick press and switch only while held on a long press** (the platform's hold time).
   F#5 and G5 stay the shortcut keys while the chord keys are shown, so the roots end at F5 there. The Full keyboard
   keeps F3 .. F5 as notes (25). G5's chord is the latched chord if there is one, otherwise Key Mode's chord for that
   key. LOCK works on the Full keyboard in FM-1 (it is on LFO); in Classic it is still B3, a note there, so latching
   happens with F#5's chord keys showing.
9. **Unchanged**: loops (they store chords, not keys), the looper, chord names, the voicing rules, Key Mode's scale
   picks (MAJ and MIN in the KEY layer are still A#3 and G#3).

### The second round: the owner's answers to the open questions (2026-10-16)

The owner saw the mock-ups ("Mockups are good") and answered the first round's open questions; the numbers are those
of [Open questions](#open-questions), which now says what is decided and what still waits.

- **2. Classic stays exactly as it is now.** LFO is LATCH there, a tap; GLO + LFO stays unused in Classic. No
  gesture of Classic changes.
- **3. GLO held, then LFO = LATCH (FM-1), and only that order.** Holding GLO must not bring up the Options; only a
  quick click of GLO does. Holding LFO brings up the LOCK layer, so the reverse (LFO held, then GLO) is not LATCH:
  it is a combo that opens the LOCK layer. (Found in the code: today already, GLO held past the Hold Time never opens
  the Options: `cr_ui.c cu_scan` line 5404 marks the hold "opened" and the release is no tap; this is kept and tested.)
- **4. The LATCH pulse** as designed (lit, middle, glow, middle, 300 ms each; with LOCK on, lit ↔ middle).
- **5. The secret combos use the window too** ("Probably"): adopted, to be confirmed on the unit.
- **7. The KEY layer (keys on pick) needs no combo**: G#3 and A#3 are the scale keys there and nothing else; a
  one-finger pair picks once (the first key's pick stands; the second within the window is ignored).
- **8. Wait the window before changing what already sounds** ("I don't want to hear it quickly play a MIN then a SUS
  when I am intending to do a SUS chord"). A type key that would change a chord already sounding (LOCK's latch with
  roots held in Advanced or Free, and Advanced / Free's type change over a held root) waits for the window or for
  the pair, then makes one change. A new chord still starts at once (decision 4; superseded in the third round:
  a root inside an open window waits too, and every decision falls at the window's close, or at the pair when
  nothing more can change it: question 16).
- **11. The names on the screen**: **Chord Keys** (the Chord Layout option), **SUS Window** (the combine window),
  **Full K Shorts** (Full keyboard shortcuts).
- **12. SUS Window: 5 ms a detent, hidden in Classic.**
- **13. MIDI Keys: yes**, with the same key layout as on the FM-1's keybed ([MIDI in](#midi-in)).
- **15. A layout change releases the keys held**, as designed.

### The third round (2026-10-16)

The five questions the second round left open, answered:

- **1. Picking keys stay where they are** (D4 .. G5 in both layouts; C4 and C#4 do nothing in the pick maps).
- **6. A root waits for an open window** (the owner: "if root detected and no top row chord keys are pushed, only
  root plays. If root detected and there are top key chord keys being pushed, then ask, has the window passed? If
  the window has passed, play the chord, if the window is still in progress, nothing plays until window is complete,
  adding a delay"). This replaces decision 4's "start at once, amend in place": nothing is amended any more
  ([the combine window](#the-combine-window)). A root that lands before any type key stays a single note in Simple.
- **9. G5 plays nothing when the shortcuts are on** (it is a switch); with G5 on and nothing to play (no latch, Key
  Mode off) the keys play single notes, as normal (no message).
- **10. G5's starting state as proposed**: on when Key Mode is on with Key plays Chords, otherwise off; not saved.
- **14. A second panel picture for FM-1**: `web/site/img/panel-main-fm1.png` (1757 x 1072, the Classic picture's
  size and crop), rendered by the designer's own plate export from `design/site-plate-main-fm1.json` (the Classic
  plate's one-state design with FM-1's labels: MIN and MAJ lit as the SUS pair, m7 latched, LOCK on the LFO button,
  `Dsus` `m7` on the screen). The site links it when the firmware has the layout.

One new question from answer 6, [Open questions](#open-questions) 16, answered the same evening: **play at the
pair** (when nothing more can change the chord). With that the design was settled for the build ("Then we're good
to build it").

## The build (HISTORY 3y163)

Built on 355fd58 the same day (rebased on cd8358b), in this order of the design. Where the code differs from the sections below:

- **The Options' place.** The four entries sit together **right after Secret Chords** (Chord Keys, SUS Window, Full K
  Shorts, MIDI Keys; the owner, after playing 3y163's build where they came after Output). `cu_opt_vis` /
  `cu_opt_next` skip what is not listed (the SUS Window in Classic, all four on the SMK-37); the picker's dots count
  what is.
- **The SUS Window's default is 30 ms** (the owner: "Default to 30ms window, worked fine for me"), stored as
  (ms - 30) / 5 in `sticky` bits 3 .. 6.
- **LOCK and a lone F#3** (3y164; the owner: "With lock on, the top row ... is not always reseting when I push them"):
  F#3 cannot combine while Secret Chords does not apply, so it opened no gesture and was added to LOCK's latch (MIN
  latched, F#3: MIN + DIM). A lone type key that cannot combine is now a gesture of one key, decided at once: it
  replaces the latch as every type key does.
- **The window's clock** is `cu_now()` when the scan takes the key, for both the type keys and the roots (a root with
  Key Touch Dynamic is taken when its velocity is known, 7 .. 15 ms after the touch: the same for every root), not the
  scan stamps `note_t0` (TIMER4 ticks).
- **A type key pressed during an open gesture** joins it whatever it is (F# with Secret Chords not applying too), and
  every key of the gesture is posted at the decision in the order pressed, so "last pressed wins" stays true. A type
  key tapped and let go inside the window still counts at the decision (Advanced re-types the held root once; Free
  sounds the last root and stops).
- **No engine amend, no looper amendment** (the third round removed them from the design): the engine gained two forms
  of `cr_mod_ex` (`CR_MOD_HELD`: down without a press; `CR_MOD_PRESS`: a press of a mod held) and `cr_set_full_chords`
  (0 as before, 1 chords on every key, 2 single notes: G5 off must also stop Key plays Chords' chords).
- **The SMK-37** reads the bits as Classic / Off / MIDI Keys Off and writes them as 0 (its own record); the four
  entries are not listed there.
- **The emulator** shows the layout: the web page's key stickers and the LFO button's role follow it (`web_layout`),
  the native one's help names both; its dumps have a `chord keys:` line (the type keys, the gesture, the waiting roots,
  MIDI Keys, the Full keyboard's switches, LATCH and the LFO light's level) and a `screen foot:` line; the Options line
  counts the entries listed.
- **Measured**: XIP 537 804 -> 541 948 B (+4 144, 94.5 %), RAM 80 176 -> 80 368 B (+192), POOL unchanged; the SMK-37
  builds (XIP 541 060 B on main cd8358b).

## The two layouts

The firmware numbers the keys 0 (F3) .. 26 (G5); a root key k plays MIDI note 53 + k + 12 x octave
(`cr_ui.c cu_key_press`), in both layouts.

```
CLASSIC (today, the default)
black      DIM   MIN   MAJ         SUS
           F#3   G#3   A#3         C#4     D#4         F#4   G#4   A#4         C#5   D#5         F#5
white    6     m7    M7    LOCK  9       D4    E4    F4    G4    A4    B4    C5    D5    E5    F5    G5
         F3    G3    A3    B3    C4      |----------------- roots: D4 .. G5 (18) -----------------|
buttons  LFO = LATCH (tap)                GLO + LFO: nothing

FM-1
black      DIM   MIN   MAJ         C#4   D#4         F#4   G#4   A#4         C#5   D#5         F#5
           F#3   G#3   A#3
                 \__ SUS __/   (G# + A# together, one finger)
white    6     m7    M7    9     C4    D4    E4    F4    G4    A4    B4    C5    D5    E5    F5    G5
         F3    G3    A3    B3    |--------------------- roots: C4 .. G5 (20) ---------------------|
buttons  LFO = LOCK (tap on / off; held: the LOCK layer)    GLO + LFO = LATCH
```

| Key (firmware k) | Classic | FM-1 |
| --- | --- | --- |
| F3 (0) | 6 | 6 |
| F#3 (1) | DIM | DIM |
| G3 (2) | m7 | m7 |
| G#3 (3) | MIN | MIN; with A#3: SUS |
| A3 (4) | M7 | M7 |
| A#3 (5) | MAJ | MAJ; with G#3: SUS |
| B3 (6) | LOCK (tap; held: the LOCK layer) | **9** |
| C4 (7) | 9 | **root** (MIDI 60 at octave 0) |
| C#4 (8) | SUS | **root** (61) |
| D4 .. G5 (9 .. 26) | roots | roots |
| LFO button | LATCH | **LOCK** (tap); the LOCK layer (held) |
| GLO + LFO | nothing | **LATCH** |

The extensions keep their order (6 m7 M7 9 on the four white keys from F3) and the types theirs (DIM MIN MAJ from
F#3); what moves is SUS (from its own key to the pair), 9 (C4 to B3) and LOCK (B3 to LFO).

## The Options, and where they are kept

Four new entries. On the SMK-37 build all four are hidden (`BOARD_SMK37`: its 37 keys are all roots, the chord
block is on its pads, `CU_FN_KEYS` 0; the bits are kept as they are and read as the defaults there).

| Entry (the screen's name, the owner's 2nd round) | Values | Default | Place in the list | What it does |
| --- | --- | --- | --- | --- |
| **Chord Keys** (the Chord Layout) | Classic, FM-1 | Classic in 0.24 / 0.25; **FM-1 since 0.25.1** (an older record moved once) | after Secret Chords | which keys are the chord keys (above) |
| **SUS Window** (the combine window) | 30 .. 80 ms, 5 ms a detent (11 values) | 30 ms (the owner, after playing; 50 in the design) | after Chord Keys, **listed only while Chord Keys is FM-1** (SELECT skips it in Classic; its value is kept) | [the combine window](#the-combine-window) |
| **Full K Shorts** (Full keyboard shortcuts) | Off, On | Off | after Split Point | [F#5 and G5 on the Full keyboard](#the-full-keyboard-both-layouts) |
| **MIDI Keys** | Off, ch 1 .. ch 16 (the channels of MIDI Perform and MIDI Bass skipped) | Off | after MIDI Raw Chord | notes 53 .. 79 on that channel press the FM-1's keys F3 .. G5 through the active layout ([MIDI in](#midi-in)) |

All four are **automatic** settings ([SETTINGS.md "Manual and automatic"](SETTINGS.md#manual-and-automatic-018)),
like every Option: written by `cr_settings_poll` when the unit is quiet, reset by Options > Reset Settings (Classic,
50 ms, Off, Off). The list is the only place in the Options that hides an entry by another's value (`cu_opt_n`'s
walk skips `O_SUSWIN` while the layout is Classic; the dots row counts what is listed).

**Changing the layout.** The new map takes over at once. Every chord-block key held at that moment is released under
the old map first (its `CRE_MOD` up, a held root's note off, as the KEY layer's Keyboard switch does); what LOCK has
latched is kept, because the latch is a set of `cr_mod_t` (DIM .. 9), not of keys: a latched SUS + 9 shows on C#4 and
C4 in Classic and on G#3 + A#3 and B3 in FM-1. LOCK itself stays as it was (on or off).

### The settings block: `sticky`

`cr_settings_t` keeps its 192 bytes and its version (15). Three of the settings take six spare bits of `sticky`,
the LATCH byte (its bit 0, manual, `CRS_FIX(sticky, 0, 1)` today); MIDI Keys takes five of `raw_sound` (below):

| bits of `sticky` | field | values | 0 means |
| --- | --- | --- | --- |
| 0 | LATCH on (as today) | 0 / 1 | off |
| 1 | Chord Keys, the layout (`CRS_LAYOUT_FM1`) | 0 Classic, 1 FM-1 | Classic |
| 2 | Full K Shorts (`CRS_FULL_SHORT`) | 0 Off, 1 On | Off |
| 3 .. 6 | SUS Window (`CRS_SUSWIN`) | (ms - 50) / 5, 4-bit two's complement, -4 .. +6 | 50 ms |
| 7 | free | | |

- **Zero is every default**, so there is no migration and no version bump: every older record reads Classic, 50 ms,
  Off. This is the spare-bit case of SETTINGS.md "How to add a field" (steps 3, 5, 6).
- `crs_sanitize`: the byte's range becomes 0 .. 0x7F, and a window code of 7 or -5 .. -8 takes 0 (50 ms) alone, the
  other bits kept.
- `CRS_STICKY_AUTO` (0x7E): bits 1 .. 6 are automatic, bit 0 stays manual (the `CRS_PICK_AUTO` pattern:
  `cr_settings.c` line 172's merge).
- **The engine reads this byte**: `cr_settings_apply` (`cr_set_sticky(c, s->sticky)`, line 525) must pass
  `s->sticky & 1`, and `cr_settings_capture` (line 560) and the glue's `crs_capture` (line 764) must merge their bit 0
  into the byte instead of writing it whole, or the new bits would be lost or switch LATCH on.
- **A downgrade.** Every older firmware checks the byte with `CRS_FIX(sticky, 0, 1)`: with any new bit set it takes
  the byte's default, LATCH off, and touches nothing else. So an older firmware ignores the layout and starts in
  Classic; the one thing it loses is a LATCH left on at the last save (it is off then). With the three `sticky` settings at
  their defaults the byte is one it knows. That firmware's next save writes the byte without the bits; back on the
  newer firmware the unit is Classic, 50 ms, Off.
- Why this byte: `pick_roots` and `bass_on` are full (Velocity Curve, Key Touch), and an older firmware that met a
  bit in `key_on` would reset Key Mode, in `scale` the scale, in `fx_on` the effects. `sticky` costs only LATCH.
- **MIDI Keys: `raw_sound` bits 1 .. 5** (`CRS_MIDI_KEYS`: 0 Off, 1 .. 16 the channel; bit 0 stays Raw Chord Sound;
  bits 6, 7 free). Zero is the default, so no version, no migration. The engine maps this byte too
  (`cr_settings_apply` line 527 and the route at 535 must read `raw_sound & 1`; `cr_settings_capture` line 570 must
  merge its bit 0 into the byte), the glue maps bits 1 .. 5 to `cs.midi_keys`. An older firmware's
  `CRS_FIX(raw_sound, 0, 1)` reads a byte with a channel set as Raw Chord Sound Off (its default) and touches nothing
  else; with MIDI Keys Off the byte is one it knows. `CRS_RAW_AUTO`: the whole byte is automatic (both are Options).
- **The session** (`cr_session_t`) needs nothing new: LOCK's latch is `latched` (bits `cr_mod_t`) as today, and the
  FM-1's lit keys are derived from it (the map is one to one: SUS = G# + A#; MIN + SUS = F# + G#; MAJ + SUS = F# + A#;
  DIM + SUS = all three; any other set as Classic's keys). Backups carry the block as ever (the same 192 bytes).

## The combo table

The type keys of FM-1 are F#3, G#3, A#3. "Secret applies" is the engine's rule today
(`cr_engine.c crx_secret_quality`): Secret Chords **All**, or **Simple** while Play Style is Simple. The extensions
(F3 G3 A3 B3) stack on any row as ever.

| Type keys held (FM-1) | Secret does not apply | Secret applies | Classic's keys for the same chord | On the screen (root D) |
| --- | --- | --- | --- | --- |
| none | a single note (Key Mode, Key plays Chords: the scale's chord) | the same | none | `D` |
| F# | DIM | DIM | F#3 | `Ddim` |
| G# | MIN | MIN | G#3 | `Dm` |
| A# | MAJ | MAJ | A#3 | `DM` |
| **G# + A#** | **SUS** | **SUS** | C#4 | `Dsus` |
| F# + G# | DIM or MIN, the last pressed | **m add4** (MIN + SUS) | G#3 + C#4 | `Dm` `add4` |
| F# + A# | DIM or MAJ, the last pressed | **augmented** (MAJ + SUS) | A#3 + C#4 | `D+` |
| F# + G# + A# | DIM or SUS, the last pressed (the pair counts as pressed when it became a pair) | **power chord** (DIM + SUS) | F#3 + C#4 | `D` `5` |

So with Secret Chords off, F# + G# + A# is either DIM (F# pressed after the pair) or SUS (the pair completed after
F#), and F# + G# or F# + A# is whichever was pressed last, exactly as two Classic type keys are. Classic's own table
is unchanged (MANUAL "Secret chords"); its Dim + Min, Dim + Maj, Min + Maj stay "last pressed" (unmapped), and FM-1
has no way to hold MIN and MAJ apart from SUS, which is the point of the layout.

**How the UI tells the engine.** The engine keeps its model (chord keys = `cr_mod_t`, `cr_mod_ex` with a stack of
held types, `crx_secret_quality` unchanged). A small resolver in `cr_ui.c` turns the FM-1's held type keys into the
`cr_mod_t` set the engine would see from Classic's keys for the chord in the table, and posts the difference (ups
first, quietly, then the downs):

- the gesture decided as the pair: MIN or MAJ up, SUS down (at the decision: [the window](#the-combine-window));
- with secret applying, F# with the pair or one of it: DIM / MIN / MAJ and SUS as the table's Classic column, so the
  engine's secret rule names the chord; without, each key posts its own type in press order and the pair posts SUS;
- **a key of the pair let go while the other stays**: SUS up and the remaining type down **without a press** (a new
  flag on `CRE_MOD`: held, not pressed: `cr_mod_ex` adds it to `mods_down` and the stack but does not run
  `crx_modifier_press`). The chord that sounds is left alone, as letting go of a type key leaves it today in every
  style; the next root plays the remaining type. Without the flag, Advanced would re-strike the held root as MIN.

The chord-key lights in FM-1 come from the UI's own key state (held or latched type keys, and the extension mods
through the key table), not from the engine's `mods_active`, which would light A#3 for m add4's SUS.

## The combine window

One finger on G# and A# never lands on both in the same scan. The first round's design played at once and fixed the
chord in place (decision 4); the owner's later answers replaced that with **wait, then play once** (8 in the second
round, 6 in the third:
"If root detected and no top row chord keys are pushed, only root plays. If root detected and there are top key
chord keys being pushed, then ask, has the window passed? If the window has passed, play the chord, if the window is
still in progress, nothing plays until window is complete, adding a delay"). Nothing is ever amended in place, so
no MIN is heard on the way to a SUS.

**The gesture.** A top-row type key (F#3, G#3, A#3) pressed while no type key is held opens a *gesture* at that key's
press time (the scan stamp `fm1_in.note_t0[k]`, `CU_KEY_T0`; a MIDI key's arrival stamp: not the UI frame, so the
window does not move with the frame rate). It stays open for the window (Options > SUS Window, 30 .. 80 ms, default
50). Type keys pressed while it is open join it. **The gesture is decided at the window's close, or earlier, the
moment no further type key could change it** (the owner, question 16: "Play at the pair"): with Secret Chords not
applying, **when G# and A# are both held** (F# cannot combine then); with them applying, only when all three are held
(F# could still turn the pair, or a pair with F#, into the power chord), else at the window's close. The type keys
held at the decision are the chord type, by [the combo table](#the-combo-table) (the pair SUS, with Secret Chords
applying the secret combos, a lone key its own type). Extension keys never open a gesture and act at their press, as today.

**A root.** When a root key goes down (the keybed, MIDI Keys, the Full keyboard's roots with the chord keys showing):

| At the root's press | It plays |
| --- | --- |
| no type key held (nothing latched; or LOCK's latch with no type key pushed) | **at once**: the root alone (a single note; with Key Mode, Key plays Chords, the scale's chord; with LOCK, the latched chord), as today. A type key pressed after it changes nothing in Simple (today's rule) |
| type keys held, their gesture **decided** (the window closed, or the pair complete) | **at once**: the chord of the keys held (a G# held from earlier: MIN at once; it never waits) |
| type keys held, their gesture **not decided yet** (the window still open) | **nothing yet**: the root waits, and plays the decided chord **at the decision** (the pair, or the window's close), with its own velocity. Several roots pressed in the window all start together then. A root let go before that still plays, as a short chord ended at once (a stab is not lost) |

So the delay lasts until the gesture is decided: G#, a root 10 ms later and A# at 25 ms sound Dsus at 25 ms (the
pair); G# and a root with no partner sound the MIN chord at the window's close (50 ms by default); a root pressed
with chord keys held from before sounds at once.

**What already sounds** (rule B, answer 8). A chord sounding when a gesture opens is not touched until the gesture
is decided (the pair, or the window's close); then it changes **once**, to the decided type: Advanced and Free's type change over a held root (one
re-strike), LOCK's latch with roots held in Advanced and Free (`crx_lock_sync`, one re-strike when the type
changed). Free with no root held: a type key sounds the last root again, and that waits for the window like a root
(it is a chord's start). Simple: a type key never changes what sounds (today's rule).

**Everything after the decision is today's code**: the chord starts (or re-types) once, so the voices, MIDI out, the
RAW stream, the performance modes, the bass, the chord name, the lights and the looper see one ordinary chord; no
engine amend, no looper amendment. The looper records the chord when it sounds (what you heard).

**The root and the pair together** (R = the root, a and b the pair's keys; W = the window, from a's press):

| Order | Simple | Advanced, Free |
| --- | --- | --- |
| a, b, R, all within W | SUS at once (the pair was decided before R; Secret Chords applying: R waits, SUS at W) | the same |
| a, R, b, both within W | R waits; SUS at b (Secret Chords applying: at W) | the same |
| a, R within W, no b | R waits; MIN or MAJ at W | the same |
| a, then R after W | MIN or MAJ at once | the same |
| a, R after W, then b | MIN or MAJ at once; b changes nothing sounding (SUS for the next root) | MIN or MAJ at once; b opens no gesture (a is held, its window long closed): the pair is decided at b, SUS re-struck at once |
| R, then a, b within W of a | R alone at once (a single note, or Key Mode's chord), and it stays | R alone at once; at b, R re-struck once, as SUS (rule B; Secret Chords applying: at W) |

**With LOCK on** (FM-1). The type keys latch as gestures, decided as above (the pair, or the window's close):

- The pair pressed and let go latches SUS (G#3 and A#3 lit). Keys held together latch together, as today.
- A type key pressed with nothing held opens a gesture; the latch is left as it is until the gesture is decided. Decided as
  the **same set again** (the pair over a latched SUS; F# + G# over a latched m add4; G# alone over a latched MIN): the
  latch stays and that type **goes off when let go** (the owner's rule: "pressing that same pair again on its own
  switches it off, as one type key does today"; its extensions stay lit and wait for a type, as today). Decided as
  **another set**: it replaces the latch (Clears / Keeps as the LOCK layer says). G# landing first over a latched
  SUS never sounds a MIN.
- A root pressed during the window waits and plays the decided latch; roots already held follow at the decision
  (Advanced, Free), once.
- A single type key over a latched pair replaces it as any type key does (F# alone over SUS is DIM, not the power
  chord: the power chord is the three held together or latched together).

**Key Mode.** A held type overrides the scale's choice as today: the pair gives SUS on any root, a lone G# MIN, and a
root with no type key pushed plays the scale's chord at once. Key plays Notes and Snap are untouched. **The KEY layer
with its keys on pick needs no combo** (the owner, 2nd round): G#3 and A#3 are its scale keys and nothing else; the
first one pressed picks the scale at once, and the other pressed within the window of it is ignored, so a one-finger
pair picks once. With its keys on play they are the FM-1 chord keys, with the window.

**Classic** has no window: none of this applies to it (two Classic type keys still combine only as secret chords,
by the engine, at the root's press, as today).

## LOCK and LATCH in FM-1

| Control | Classic (today) | FM-1 |
| --- | --- | --- |
| LOCK on / off | B3 tap | **LFO tap** |
| the LOCK layer | B3 held past Hold Time | **LFO held past Hold Time**, as a layer button: it stays open, **LFO blinks**, OCT- / OCT+ / HOME / LFO held again close it, holding another layer button switches to that layer |
| LOCK + a chord in one motion | B3 down, then a chord key: LOCK's tap at that moment (`cu.b3_pend`) | the same rule on LFO: a key pressed while LFO is down (before the hold time) is LOCK's tap at that moment, no layer; a knob or a button meanwhile opens the layer (a combo, as every layer button) |
| LATCH on / off | LFO tap (unchanged: the owner, 2nd round; GLO + LFO stays unused in Classic) | **GLO held, then LFO**, and only in that order (as GLO + SEQ is the metronome and GLO + REC the record mode): LFO is swallowed, GLO's release opens no Options |
| LFO held, then GLO | (LFO is LATCH: a tap's button, no layer; GLO meanwhile: nothing, as today) | **the LOCK layer opens** (a combo on a held layer button, `cu_activity`); GLO is swallowed: no LATCH, no Options (the owner: "the reverse combo of hold LFO then GLO doesn't work") |
| GLO held alone | the Options open **only on a quick click** (released before Hold Time with nothing else touched); held past it, nothing opens | the same. This is today's code in both layouts (`cr_ui.c cu_scan` line 5404: GLO past the hold time is marked opened, `cu_opened` has no layer for it, and its release is no tap); kept, and a test pins it |
| the LOCK layer's footer | `B: on/off · OCT-: back · HOME: home` | `LFO: on/off · OCT-: back · HOME: home` |
| the screen | `lock` and `Latch` at the top right, as today | the same words; `lock` also shows on the Full keyboard now (LOCK works there) |
| in the sound editor | B3 works | LFO is the editor's LFO group button and GLO its SHIFT: LOCK and LATCH cannot be switched there (decided: fine). The keys still play the latch |

**The LFO light** (the button's LED; its levels are those of the key LEDs: lit, middle, glow, faint):

| State | LFO light |
| --- | --- |
| LOCK off, LATCH off | the idle button glow, as every button |
| LOCK on | **lit, steady** |
| LATCH on (or a performance mode's Hold: the engine's `latching`, as today) | **pulses**: lit, middle, glow, middle, 300 ms each (1.2 s round), so it never reads as the layer's 250 ms blink |
| LOCK and LATCH on | pulses between lit and middle (never down to the glow), so LOCK still reads as on |
| the LOCK layer open | blinks (250 ms, as every open layer's button) |

**The chord keys' lights in FM-1** (`cr_leds`, Glow): a type or extension key held or latched is lit, an idle one
faint, as today; a latched SUS lights **G#3 and A#3**, m add4 F#3 + G#3, augmented F#3 + A#3, the power chord all
three. B3 (9) is an extension key and never blinks; there is no LOCK key light (LOCK is on the LFO light). C4 and
C#4 are root keys: dark unless they sound, the key's glow with Key Mode on, as every root. Stock (two levels) keeps
its rule: idle lit, active dark. The KEY layer on pick: G#3 and A#3 glow, the scale's lit, the five others dark.

## Picking keys in layers

**Default, both layouts: the keys that pick stay on D4 .. G5** (decision 6). What that means: the root keys have a
second job in some layers, and in FM-1 two more keys (C4, C#4) are roots on the view. Those two take **no** picking
job; every map keeps its keys where it is today, so the muscle memory and the manual's pictures stay true:

| Layer or page | The pick map (both layouts) | C4, C#4 in FM-1 |
| --- | --- | --- |
| the PLAY menu with Keys = Loops | the white roots D4 .. C5 = slots 1 .. 10, D#4 CLEAR, F#4 UNDO (`cu_white_idx` from D4) | dark, do nothing |
| naming a sound | the white roots type, D#4 a space, F#4 deletes | dark, do nothing |
| the KEY layer, keys on pick | any root D4 .. G5 sets the tonic (all twelve names are there) | dark, do nothing |
| the view menu, the pickers that list on white roots (`cu_layer_keys`, `cu_layer_sel`) | the white roots from D4 | dark, do nothing |
| the layers where the keys play (PERF, FX, LOCK, the engine picker's preview, Options, the loop editor, the PLAY menu with Keys = Play, the KEY layer on play) | they play | they play, as roots |

In code: `CU_ROOT0` (9) splits into `cu_root0()` (the first key that plays: 9 Classic, 7 FM-1) and `CU_PICK0` (9 in
both: `cu_white_idx`, `cu_white_key`, the naming keys 10 and 13, the KEY layer's tonic marks). Revisiting it later
would move only `CU_PICK0`'s users: [Open questions](#open-questions) 1.

## MIDI in

**What the code does today.** No MIDI message plays the FM-1's keys. MIDI in (USB and TRS) is drained in the audio
ISR by `cr_out.c cr_midi_in`: `cr_midi.c crm_map` sends a note on the Perform (chord) or Bass channel to
`midi_event` → `trk_note_on` / `trk_note_off` on that part: **the sound plays the note directly, no chord is built**
(MANUAL "MIDI in": "as an ordinary synth"). CC 7 / 91 / 93 / 94 and program changes go to the UI through `cr_min_q`
as knob turns. Nothing reaches `cr_ui.c cu_key_press`, the chord block, `CRE_KEY` or `CRE_MOD`. The emulators'
MIDI inputs (CoreMIDI in `tools/emu/emu.c`, Web MIDI in `tools/emu/web/emu.js midiIn` and the SMK page) feed that
same firmware queue (`emu_fw_midi_in`), so the same holds there. The only things that press firmware keys from
outside are the emulators' computer-keyboard maps and scripts (`KM_KEY`, firmware key index = MIDI note - 53), and
those send key indices, which the firmware reads through the active layout by itself.

**The change: Options > MIDI Keys** (the owner, 2nd round: "use the same key layouts as on the FM-1 keyboard").
A channel whose notes press the FM-1's own keys, so another keyboard (or a sequencer) plays ChoralRoot exactly as the
keybed does: the chord keys, the roots, the active layout, the SUS window, LOCK, Key Mode, the Full keyboard and its
shortcuts, the picking maps in layers.

| | |
| --- | --- |
| **The map** | MIDI note n = the FM-1's key n - 53: **53 .. 79 = F3 .. G5**, the keybed's own notes. So in FM-1, 54 / 56 / 58 = DIM / MIN / MAJ, 56 + 58 together = SUS, 53 / 55 / 57 / 59 = 6 / m7 / M7 / 9, 60 .. 79 the roots; in Classic, 54 56 58 61 = DIM MIN MAJ SUS, 53 55 57 60 = 6 m7 M7 9, 59 = LOCK, 62 .. 79 the roots. OCT- / OCT+ shift the roots as for the keys (the map is by key, not by pitch). Notes outside 53 .. 79: ignored |
| **The channel** | Options > MIDI Keys: Off (default), ch 1 .. ch 16; the values skip the channels of MIDI Perform and MIDI Bass (a channel is either the keys or a part's notes, never both; setting MIDI Perform or MIDI Bass to the Keys' channel switches MIDI Keys Off, with the message `MIDI Keys off`). On that channel only notes act (note on, note off, note on with velocity 0); CC 123 / 120 release every key MIDI holds; everything else on it is ignored |
| **The velocity** | the note's velocity plays the key's sound (as Key Touch Dynamic's measured velocity does: `cu_press_vel`), whatever Key Touch says; the chord keys ignore it as they do today |
| **The path** | `cr_midi.c crm_map` returns a new kind, `CRM_KEY` (key index, velocity, down), for the MIDI Keys channel, checked before the parts' channels. `cr_out.c cr_midi_in` (the audio ISR) puts it on a **queue of its own**, `cr_mkq` (64 entries, ISR → UI), not on `cr_min_q` (32 entries, which drops on a burst: a lost note-off would hang a key). When it is full a note-on is dropped, never a note-off (an off for a key MIDI does not hold is ignored). The UI drains it in `cr_ui_input` before `cu_scan_keys`, and calls `cu_key_press` / `cu_key_release` with the key index: from there it is a key like the panel's |
| **Held by both** | a key is down while the panel or MIDI holds it (`cu.kheld` from the scan, `cu.kmidi` from MIDI; the press acts at the first, the release at the last), so a key pressed on the keybed and over MIDI at once is one press |
| **The timing** | the window measures from the message's arrival stamp (`midi_in_ms`, as the clock follower uses), so a DAW's pair 20 ms apart combines as the keybed's does |
| **The lights, the screen** | as for the keybed: the keys MIDI holds light as held keys, the chord shows |
| **MIDI out** | what the keys play goes out as the keybed's does (the Perform / Bass / Raw channels). Since MIDI Keys can never be the Perform or Bass channel, a computer that echoes ChoralRoot's output back does not press keys, unless it remaps that output onto the MIDI Keys channel |
| **The SMK-37** | hidden (its keybed is not the FM-1's) |
| **The emulators** | nothing to add: their Web MIDI and CoreMIDI inputs reach the firmware's MIDI in already; a script's `midi 90 3A 64` presses A#3 |

The rule for any other path stays: enter at `cu_key_press` / `cu_key_release` with the key index (note - 53), never
post `CRE_MOD` or `CRE_KEY` directly.

## The Full keyboard (both layouts)

Today (KEY layer, page 2, Keyboard: Full): all 27 keys F3 .. G5 are notes, no chord keys, no LOCK (B3 is a note),
KNOB 1 is the octave, and with Key Mode on and Key plays Chords every key plays its scale chord (0.22).

**KNOB 1 is the voicing on the Full keyboard**, in both layouts, with or without the shortcuts: chords are played
there (Key plays Chords; G5 below), and the octave is on OCT- / OCT+ already. The read-out says `voicing`; a single
note ignores the voicing, as on the Chords keyboard. (`cr_ui.c` line 4737's Full-keyboard branch goes.)

**Options > Full K Shorts** (Full keyboard shortcuts, Off by default). Off: the Full keyboard is as today, but for KNOB 1. On:

| Key | Job |
| --- | --- |
| F3 .. F5 (25 keys) | notes, as the Full keyboard's |
| **F#5** | **chord keys**: the active layout's chord keys on the low keys, or back to notes |
| **G5** | **chords**: every key plays a chord, or single notes |

- **Quick press = toggle, long press = momentary.** The state flips at the press (no wait). Let go before the hold
  time (Options > Hold Time, 300 .. 600 ms, `HOLD_MS`, the layer buttons' and B3's): it stays flipped. Held past it:
  it flips back at the release. Keys played meanwhile play under the flipped state, and each key's release ends what
  its press started (as Snap's rule).
- **Chord keys showing (F#5 lit).** The low keys are the layout's chord block, and the rest are the Chords keyboard's
  roots **up to F5**: Classic F3 .. C#4 the nine chord keys with B3 = LOCK, D4 .. F5 roots (16); FM-1 F3 .. B3 the
  seven chord keys, C4 .. F5 roots (18). F#5 and G5 stay the shortcuts. Everything follows the Chords keyboard while
  they show: the engine is told `CRE_KEYBOARD` 0 (chords from roots, a lone root's Key Mode chord, Single Notes' split
  by the Split Point), the latch and the play styles, the window in FM-1, the chord-key lights. Hiding them again:
  the chord keys held are released, as a layout change does; the latch stays.
- **Chords on every key (G5 lit).** Every note key plays a chord: **the latched chord** (LOCK on with a latch: its
  type and extensions, the lit keys) **if there is one, otherwise Key Mode's chord for that key** (as Key plays Chords
  plays it). G5's state starts, each time the Full keyboard is entered, from today's behaviour: on when Key Mode is
  on with Key plays Chords, off otherwise; G5 then switches it. With neither a latch nor Key Mode, G5 lights but the
  keys play single notes, as normal (the owner, 3rd round). G5 itself never plays a note while the shortcuts are on.
  With the chord keys showing the roots already follow the Chords keyboard's rules (a lone root plays the latch or
  the scale's chord), so G5's state only matters for the note keys. The engine: a Full-keyboard voice (`zone` set)
  takes the latched spec (`mods_down`'s type and extensions) instead of none while G5 is lit (a new flag,
  `cr_set_full_chords`); today the engine ignores the latch on the Full keyboard.
- **LOCK on the Full keyboard.** FM-1: LFO works everywhere, so LOCK is on / off and its layer opens on the Full
  keyboard too, and the latch is set by showing the chord keys (F#5), holding a chord and letting go. Classic: B3 is
  a note while the chord keys are hidden, and LOCK (tap, hold) while they show, so latching happens with F#5's chord
  keys showing. The top right shows `lock` on the Full keyboard now whenever LOCK is on.
- **The lights.** F#5 and G5 are function keys: faint idle, lit while on (Stock: the reverse). The note keys as the
  Full keyboard's today; with the chord keys showing, the chord block and the roots as the Chords keyboard's.
- **The split** (Single: Split Oct, Split Bass) stays at the second F on the note keys (F3 .. E4 left, F4 .. F5 right;
  F#5, G5 are no notes); with the chord keys showing it is the Chords keyboard's Split Point.
- The KEY layer with its keys on pick still owns the keys (a root sets the tonic, MAJ / MIN the scale); F#5 and G5 do
  nothing there.

## Unchanged

- **Loops** store chords (`cr_mod_t` type and extensions with the root), not keys: a loop recorded in one layout
  plays the same in the other. The looper, its modes, its editor, its step entry ("every key up after a chord": the
  FM-1's pair counts as keys up when both are) are unchanged.
- **Chord names** (Orchid and Standard), CHORD-NAMES.md; the secret chords keep their names (`D5`, `D+`, `Dm add4`).
- **The voicing rules**, KNOB 1, 6th with 7th, the octave.
- **Key Mode's scale picks**: MAJ and MIN in the KEY layer are A#3 and G#3 in both layouts.
- The play styles, Extension Addition, Secret Chords' values, the LOCK layer's two styles (Type key, Ext keys),
  Key Mode, Snap, Numerals, Single Notes, the bass, the performance modes, MIDI out, the views.
- Classic is today's firmware, key for key, light for light: every existing test and emulator script runs in Classic.

## Every place that reads the chord block's keys

Found in the code (2026-10-16, origin/main 4ecd5be). "Needs" is the build's work.

| Place | What it does today | Needs |
| --- | --- | --- |
| `cr_ui.c:61` `CU_MOD_OF_KEY[9]` | key 0 .. 8 → `cr_mod_t`, 0xFF for B3 | a second table for FM-1 (keys 0 .. 6: 6 DIM m7 MIN M7 MAJ 9), chosen by the layout; the pair handled by the resolver |
| `cr_ui.c:63` `CU_ROOT0` (9), `:67` `cu_white_idx`, `:76` `cu_white_key` | the first root; the white roots' index from D4 | `cu_root0()` (9 / 7) for "plays"; `CU_PICK0` 9 for the pick maps ([Picking keys](#picking-keys-in-layers)) |
| `cr_ui.c:3706` `CU_KEY_LOCK` 6, `CU_KEY_MIN` 3, `CU_KEY_MAJ` 5 | B3 = LOCK; the KEY layer's scale keys | LOCK key only in Classic; MIN / MAJ unchanged |
| `cr_ui.c:4018` `cu_mods_phys` | the chord keys held → `cr_mod_t` bits (LOCK's `CRE_MODTOUCH`) | through the resolver in FM-1 |
| `cr_ui.c:4063 .. 4165` `cu_lock_menu`, `cu_lock_tap`, `cu_mod_press`, `cu_mod_release` | LOCK, its layer, the latch's reset / toggle / push rules | the type-key gesture: the pair as one type, the waiting roots and rule B (a root in an open window, a sounding chord and the latch change once, at the window's close), "the same set again" off; the silent down; the KEY layer's pick ignoring the pair's second key |
| `cr_ui.c:4194` `CU_KEY_SENSED` | velocity sensing for keys `>= CU_ROOT0` (or Full) | `cu_root0()`: C4, C#4 get their velocity in FM-1 (all 27 keys have the converter, `cr_sense.c CR_SENSE_KEYS` 0x07FFFFFF) |
| `cr_ui.c:4211` `cu_key_press` (B3 at 4217 .. 4226, the chord block at 4231, the layers at 4239, naming at 4245, the note at 4262) | where a key goes | B3 → LOCK only in Classic; the chord block by `cu_root0()`; F#5 / G5 shortcuts on the Full keyboard; the root note formula unchanged |
| `cr_ui.c:4272` `cu_key_release` | the same, released | the same |
| `cr_ui.c:4296` `cu_btn_press`, `cu_tap` | LFO tap = LATCH; GLO + LFO nothing | FM-1 only: LFO tap = LOCK, LFO a layer button (`L_BTN[L_LOCK]` = `BT_LATCH`, line 50), LFO + key = LOCK's tap now, GLO held + LFO = LATCH (LFO held + GLO: the LOCK layer). Classic: untouched |
| `cr_ui.c:5412` (FM-1), `:5208` (SMK) `b3_pend` hold | B3 held past Hold Time: the LOCK layer | Classic only; FM-1 through the layer-button path |
| `cr_ui.c:4737` KNOB 1 | the octave on the Full keyboard | the voicing everywhere |
| `cr_ui.c:5004` `cu_scan_step` | step entry's "keys up" | unchanged (kheld and the latch) |
| `cr_ui.c:5536` `cr_leds`: `key0` (5542), the chord block (5622 .. 5642), B3's LED (5623), `BT_LATCH`'s LED (5572), the layer button's blink (5591), the loop-slot map (5644), the pickers (5659), the KEY pick (5666), naming (5681), the playing keys (5704, 5720, 5731) | every key light and its four levels | the chord block by layout from the UI's key state; no LOCK key in FM-1; LFO: steady / pulse / blink; maps from `CU_PICK0`, playing keys from `cu_root0()`; F#5 / G5 |
| `cr_ui.c:5874` the top right | `lock` unless the Full keyboard | `lock` on the Full keyboard too |
| `cr_ui.c:6471`, `:7242` | "a root held" (`kheld >> CU_ROOT0`) | `cu_root0()`, and the Full keyboard's 25 |
| `cr_ui.c:6631` the KEY layer's screen | the tonic marked on D4 .. G5 | unchanged (`CU_PICK0`) |
| `cr_ui.c:6676` the LOCK layer's screen | footer `B: on/off` | `LFO: on/off` in FM-1 |
| `cr_ui.c:1037` Options (`O_*`, `O_NAME`, `O_MAX`, the get / set / text switches at 1087, 1119, 1207), `cu_opt_n` | the entries | four entries (Chord Keys, SUS Window, Full K Shorts, MIDI Keys); SUS Window skipped in Classic; MIDI Keys skipping the parts' channels; all four hidden on the SMK |
| `cr_ui.c:1024` `cu_route`, Options > MIDI Perform / Bass | the parts' channels to `cr_route` | `cr_route.keys_ch` (MIDI Keys, 0 = off); a part set to the Keys' channel switches MIDI Keys off |
| `cr_midi.c:76` `crm_map`, `cr_out.c:661` `cr_midi_in` | the parts' notes to `midi_event`, CCs and programs to `cr_min_q` | `CRM_KEY` for the MIDI Keys channel (notes 53 .. 79; CC 120 / 123: all keys up), checked first; the new queue `cr_mkq` (64, ISR → UI, never dropping a note-off) |
| `cr_ui.c` `cr_ui_input` (the scan) | the keybed's edges to `cu_scan_keys` | drains `cr_mkq` first: `cu_key_press` / `cu_key_release` with the key index and the note's velocity; `cu.kmidi` beside `cu.kheld` |
| `cr_ui.c:111` `CU_PAD_FN` (SMK) | pads → chord block keys | unchanged (Classic's table; the layout hidden there) |
| `cr_engine.c` `cr_mod_ex` (1060), `crx_modifier_press` (865), `crx_transform_voice`, `crx_build_base`, `crx_secret_quality` (253) | types, secrets, re-strikes, the Full keyboard's zones | a held-not-pressed down; `cr_set_full_chords`; the secret rule unchanged (no amend: the UI waits and posts one change) |
| `cr_out.c:443` `CRE_MOD` (a: the mod, b: quiet, v: down) | the UI → engine queue | the silent flag on `CRE_MOD`; `CRE_FULLCHORD` |
| `cr_loop.c` the gesture capture | one event a gesture serial | nothing (one chord a gesture, as today) |
| `cr_ui.c` `cu_key_press`, `cu_scan` (the waiting roots) | a root plays at its press | a root pressed inside an open gesture is held back (`cu.kwait` bits, its velocity, its release if it comes first) and posted at the window's close, every waiting root in one batch; the gesture's timer checked each scan |
| `cr_settings.h` / `.c` `sticky` (167; 208, 525, 560, 764, 1104), `raw_sound` (173; 166, 223, 527, 535, 570) | LATCH; Raw Chord Sound | the bits, the masks, the merges (above) |
| **the calibration** (`cr_ui.c:4796`, `panel.c:12 B_NAME`) | teaches the buttons (printed names: FX SCL ENV LFO EDIT GLO ...) and the knobs; the keys are not calibrated and no key is named there | **nothing**: it names LFO by its print, which does not change |
| `tools/emu/keymap.c` (the native emulator) | F1 .. F4 = keys 1 3 5 8, 2 .. 5 = keys 0 2 4 7, Tab = key 6, its comment and `--help` say DIM MIN MAJ SUS / 6 m7 M7 9 / LOCK; the panel draws note names, not chord labels (`emu.c:380`) | the keys stay (they are indices); the comment and `--help` say both layouts (FM-1: F1 F2 F3 = DIM MIN MAJ, F2 + F3 = SUS, 2 3 4 Tab = 6 m7 M7 9, F4 / W and 5 / A = the roots C#4 / C4, F8 (LFO) = LOCK, F10 (GLO) held + F8 = LATCH) |
| `tools/emu/web/keymap.js` `CHORD`, `tools/emu/web/emu.js:27, 54` | the sticker labels on keys 0 .. 8 and their colours | a `CHORD_FM1` map ({0: 6, 1: DIM, 2: m7, 3: MIN, 4: M7, 5: MAJ, 6: 9}) and the label switched by the firmware's layout (a `web_*` export of `cs.layout`, read each frame as the LEDs are); the LFO button's role label LOCK in FM-1 |
| `tools/emu/web/index.html:92` | the legend: F1 .. F4 DIM MIN MAJ SUS, 2 .. 5, Tab LOCK | the FM-1 line beside it (or switched by the layout) |
| `tools/emu/README.md:80`, `tools/emu/emu.h:9` | "key index = MIDI note - 53" | unchanged |
| the layout tool (`tools/layout/emu_sheet.mjs`, `layout_web.c`) | drives the browser emulator's panel; no key map of its own; LAYOUT-TOOL.md "the 27 keys always play" | nothing beyond keymap.js |
| `tools/emu/web/smk/` (the SMK page) | pads | nothing (hidden there) |
| `web/editor.html` | no keybed; its "chord keys" (lines 1211, 1749: `CHRD`, `VOIC`) are Felucca's sequencer parameters, not ChoralRoot's chord block | **nothing** |
| `web/site/index.html:107 .. 131` | the panel picture's alt text, the keybed SVG (CHORD BLOCK / ROOTS D4 … G5), the chord block SVG, the text on chord types, LOCK and secret chords | a second keybed / chord block picture for FM-1 and a sentence on the Option; the alt text |
| `web/site/img/panel-main.png`, `design/choralroot-fm1-panels.png` (`design/make_mockups.py`) | the panel with the Classic sticker | done: `web/site/img/panel-main-fm1.png` from `design/site-plate-main-fm1.json` (the 3rd round); the site links it when the firmware ships the layout |
| `design/make_mockups.py`, `make_layers_mockups.py`, `make_looper_mockups.py` | the designer sheets' chord block | Classic as they are; the FM-1's are this page's mock-ups |
| `docs/MANUAL.md` | 3 The keybed (table and drawing), The buttons (LFO), The knobs (KNOB 1 on Full), The lights (B3), 4 Types and extensions, LOCK, Secret chords, Key Mode's Full keyboard, LATCH, 14 Options, 18 Quick reference (Buttons, Keys) | an FM-1 column or paragraph in each; the four Options |
| `docs/manual/*.png` | `layer-key.png` (the tonic on D4 .. G5), `options.png` (Play Style), `chord.png` | unchanged; new: the Chord Keys option and the FM-1 LOCK layer's footer |
| Felucca's own UI (`perform.c:7`, `seq.c:390`, `ui_layer.c:7`, `ui_name.c:17`) | Felucca's black-key mutes and naming | not ChoralRoot's keys: nothing |
| `docs/SETTINGS.md` | the block's table, the "every setting" table | `sticky`'s and `raw_sound`'s bits, the four automatic settings |
| `docs/MANUAL.md` "MIDI in" (15) | the Perform / Bass channels' notes play the sound | a MIDI Keys line: the map 53 .. 79 = F3 .. G5 through the layout |

## Cost

Estimates, to be measured by the build (`tools/build.py`'s report, docs/FLASH.md's account). main today: XIP
537 068 B (93.7 %, about 36 KB spare), RAM 80 176 B, POOL 306 196 B (89.0 %).

| | Estimate | What |
| --- | --- | --- |
| **XIP** | +2.5 .. 4.5 KB | the resolver, the gesture and the waiting roots (about 1.2 KB), LOCK on LFO and GLO + LFO (0.3 KB), the LED changes and the pulse (0.5 KB), the four Options with their strings (0.5 KB), the Full keyboard's shortcuts and `cr_set_full_chords` (0.8 KB), the engine's silent down (0.1 KB), MIDI Keys (`CRM_KEY`, the queue, the drain, the second held mask: 0.5 KB), the settings bits (0.1 KB) |
| **RAM** | about +160 B .bss | MIDI Keys' queue `cr_mkq` (64 x 2 B: key and velocity with the down bit) and `cu.kmidi` (4 B); `cs.layout`, `cs.suswin`, `cs.full_short`, `cs.midi_keys`; the gesture's start and its keys, the waiting roots (`cu.kwait` 4 B, their velocities 27 B), the shortcut keys' press times (8 B) and states. The engine: nothing (`cr_t` keeps its size) |
| **POOL** | 0 | |
| **Settings** | 0 bytes | six spare bits of the block's `sticky`, five of `raw_sound`; the session record unchanged |
| **Flash data** | 0 | no new object, no new sector |
| **CPU** | negligible | a compare a scan while a gesture is open |

## The tests the build will need

Host (`tests/`):

1. `cr_settings_test`: `sticky`'s bits round trip; every older byte (0, 1) reads Classic, 50 ms, Off; a window code
   out of range reads 50 ms and keeps the other bits; the bits are automatic, bit 0 manual (the `CRS_STICKY_AUTO`
   merge); `cr_settings_apply` gives the engine `sticky & 1`, capture keeps the bits; Reset Settings gives the
   defaults; the older firmwares' rule modelled (`CRS_FIX(sticky, 0, 1)`: an FM-1 record reads LATCH off, nothing
   else touched). `raw_sound`'s MIDI Keys bits the same way (round trip, 0 / 1 read Off, a channel above 16 reads
   Off, the engine gets `raw_sound & 1`, an older firmware reads Raw Chord Sound Off).
2. `cr_engine_test`: the silent down does not re-strike (Advanced, Free, LOCK on and off) and the next root takes
   it; `cr_set_full_chords` (a zone voice takes the latched spec; off: as today).
3. `cr_loop_test`: unchanged (a waiting root records as one chord, at the time it sounded: the emulator's scripts
   check it).
4. `cr_midi_test`: `crm_map` gives `CRM_KEY` for notes 53 .. 79 on the MIDI Keys channel (on, off, on at velocity
   0; CC 120 / 123 all up), nothing for notes outside it or other messages on it, and the parts' channels as before;
   MIDI Keys Off: exactly today's mapping. `midi_control_test` unchanged.

Emulator scripts (`tools/emu/test_cr.sh`, with the layout set through the Options first; every existing script runs
in Classic untouched, the regression guard):

5. The keys: in FM-1 C4 and C#4 play 60 and 61; B3 adds the 9; F#3, G#3, A#3 alone DIM MIN MAJ; G#3 + A#3 SUS; the
   combo table's every row, Secret Chords Off, Simple (with Play Style Simple and Advanced) and All.
6. The window, by the scripts' ms timing, every row of the root-and-pair table: G#, D4 10 ms later, A# at 30 ms:
   nothing goes out until 30 ms (the pair), then Dsus's note-ons at once (MIDI out and the voices), and never an F;
   the same with Secret Chords All: Dsus at 50 ms (F# could still join), and F# at 40 ms makes it the power chord; G# and D4
   with no A#: Dm at 50 ms; a root with no type key: at once; G# held for a second, then a root: Dm at once; several
   roots in the window start together; a root let go at 20 ms still plays (a short chord); the window at 30 and 80;
   the loop records the chord when it sounded; Free with no root held waits too; a pair released one key at a time (no
   re-strike, the next root MIN). Rule B: Advanced with a root held, the pair 20 ms apart: one re-strike, as SUS, at
   the pair, and no MIN note-on ever goes out; G# alone:
   the re-strike as MIN at the window's close and not before; an extension key during the window acts at its press.
   The KEY layer on pick: G#3 then A#3 20 ms later picks minor and stays minor; 80 ms later picks major.
7. LOCK: LFO tap on / off (`lock`, the LFO light lit), LFO held: the layer, LFO blinking, its footer; LFO + a chord key:
   LOCK's tap at once; GLO + LFO: LATCH (`Latch`, the light pulsing: `expect_led` at its four phases), LOCK + LATCH
   (pulse lit / middle); the pair latched: G#3 and A#3 lit; the same pair again alone: off at the release, its
   extensions still lit; the secret combos latched and off; Clears / Keeps and Toggle / Pushed with the pair; LOCK
   not switchable in the sound editor; PANIC clears it. LOCK with roots held in Advanced: the latched pair pressed
   again: no MIN sounds, the roots fall back at the release. The buttons: GLO held past Hold Time alone opens no
   Options (both layouts), a quick click does; LFO held, then GLO: the LOCK layer, no LATCH, no Options; Classic: LFO
   tap is LATCH and GLO + LFO does nothing, as today.
8. Picking keys: the loop slots, naming, the KEY layer's tonic on D4 .. G5 in both layouts, C4 / C#4 dark and silent
   in those maps and playing in the others; the KEY layer's MAJ / MIN.
9. The layout switch: from the Options with keys held (released), with a latch (kept, its lights moved: SUS + 9 on
   C#4 + C4, then G#3 + A#3 + B3).
10. The Full keyboard: KNOB 1 is the voicing (both layouts, shortcuts Off and On); Full K Shorts On: F#5 tap / hold
    (the chord keys, the roots ending at F5, both layouts), G5 tap / hold (the latch's chord, the Key Mode chord, the
    message with neither), the split, LOCK on the Full keyboard (FM-1 by LFO; Classic B3 with the chord keys
    showing), the lights of F#5 and G5.
11. MIDI Keys (scripts' `midi` lines): ch 5, FM-1: 56 + 58 then 62: D sus, as the keys; 56 and 58 36 ms apart over
    62: Dsus once, at the pair; Classic: 61 is SUS, 59 LOCK's tap; notes 52 and 80 do nothing; the velocity plays; a key held on
    the panel and over MIDI is one press, released at the last; CC 123 releases every MIDI key; a burst of 100 notes:
    no key left held; the values skip the parts' channels, and MIDI Perform set to ch 5 switches MIDI Keys Off; MIDI
    Keys Off: notes on that channel do what they do today.
12. `test_persist.sh`: FM-1, a SUS Window of 65 ms, Full K Shorts On and MIDI Keys ch 5 come back after a power cycle
    without SAVE held (automatic); LOCK with SUS latched in FM-1 comes back lit on G#3 and A#3 after a global save.
13. The browser emulator: `test_web_emu.mjs` bit-exact with the native one as ever; the sticker labels follow the
    layout; `test_web_emu_smk.mjs` unchanged (the Options hidden). `BOARD=smk37 ./build.sh` builds.
14. Size: `tools/build.py`'s report against the estimate; docs/FLASH.md's account.

On the unit (the owner): the one-finger pair at 50 ms (and whether 40 or 60 suits better), the pulse's look, LFO as
LOCK in the hands.

## Open questions

The first round's fifteen and one more, all answered by the owner (2026-10-16, second and third rounds, and 16 the
same evening). None is open.

| # | Question | Status |
| --- | --- | --- |
| 1 | Picking keys stay on D4 .. G5 in FM-1? | **decided** (3rd round): yes, they stay |
| 2 | GLO + LFO in Classic | **decided**: Classic stays exactly as it is; LFO is LATCH, no GLO combo |
| 3 | Which order is LATCH | **decided**: GLO held, then LFO; holding GLO never opens the Options (only a quick click); LFO held, then GLO is the LOCK layer |
| 4 | The LATCH pulse | **decided**: as designed |
| 5 | The window for the secret combos | **decided** ("probably"): yes; confirm on the unit |
| 6 | A type key just after the root in Simple | **decided** (3rd round): a root with no type key pushed plays alone; a root inside an open window waits for it to close, then plays the chord |
| 7 | The KEY layer's pick and the one-finger pair | **decided**: no combo there; the pair's second key within the window is ignored |
| 8 | LOCK / Advanced: a MIN heard before SUS | **decided**: wait the window (rule B) |
| 9 | G5 with no latch and Key Mode off | **decided** (3rd round): G5 plays nothing with the shortcuts on; the keys play single notes as normal |
| 10 | G5's starting state | **decided** (3rd round): as proposed, from Key plays, not saved |
| 11 | The names | **decided**: Chord Keys, SUS Window, Full K Shorts |
| 12 | The window's step, Classic | **decided**: 5 ms; hidden in Classic |
| 13 | MIDI that plays the keys | **decided**: yes, the FM-1's key map ([MIDI in](#midi-in)) |
| 14 | The panel pictures | **decided** (3rd round): a second picture for FM-1, made |
| 15 | The layout change with keys held | **decided**: released |
| 16 | A root waiting for a pair already complete | **decided**: play at the pair when nothing more can change the chord |

**16. Play at the pair** (the owner: "Play at the pair. Then we're good to build it"). A root waiting in an open
window plays as soon as nothing more can change the chord: at the pair with Secret Chords not applying, when all
three type keys are held with them applying, otherwise at the window's close. Every question is answered; the design
is ready to build.
