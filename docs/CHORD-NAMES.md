# ChoralRoot FM-1: chord names

Files: `firmware/src/cr_engine.c` (`cr_chord_name`, the one function every screen names a chord through;
`crx_name_std`, `crx_figure`), `firmware/src/cr_draw.c` (`cr_name`: how the three strings are drawn),
`firmware/src/cr_ui.c` (the Options entries), `tests/cr_engine_test.c` (`s_names_std`: this document's table, cell for
cell; `s_sixth_tables`, `s_sixth`: the 6th above a seventh), `tests/cr_draw_test.c` ("chord names": the fit),
`tools/emu/scripts/cr_chord_names.txt`, `tools/emu/scripts/cr_sixth.txt`.

**Options > Chord Names: Orchid / Standard** (0.20). Orchid is the default.

- **Orchid** names a chord by the buttons held, as the Telepathic Instruments Orchid (ORC-1) does: its manual's
  "Standard Chord Naming Framework" (one extension as a superscript, two in a row, three = `JAZZ`, four = `WTF`;
  "we don't ... turn a 6 into a 13"), with ChoralRoot's one departure since 0.18: a plain major triad reads `CM`,
  because `C` alone is a single note. This is what every version up to 0.19 showed, and it is unchanged, string for
  string (`tests/cr_engine_test.c`: 12 roots x 8 qualities x 16 extension sets against a copy of 0.19's function).
- **Standard** names the notes that sound, as a lead sheet would: `A9` for A + 7 + 9 (Orchid: `A7 9`), `Aadd9` for
  A + 9 (Orchid: `A9`), `A9sus4` for Asus + 7 + 9 (Orchid: `Asus m7 9`), `A13` for A + 7 + 6.

A user asked for it ("something simpler/more accurate would be better (e.g. A9 for A7 + 9th rather than A79, Aadd9
for A major triad + 9th which is incorrectly displayed as A9 atm, Asus9 for Asus + 7 + 9 etc...)"); the owner: "We
can add option for the chord names but I would prefer we match the ORC-1 by default."

**The name is display only.** Neither setting changes a note, a stored loop event (a step keeps its root, quality
and extension bits, and is named when it is shown), a MIDI byte or a saved sound. Switching the option renames what
is on the screen at once, the last chord included.

A second option, **Options > 6th with 7th: In place / Octave up** (0.20), is the one that changes a note: with it on,
the 6th of a chord that also has a seventh sounds an octave higher, as a 13th. It is off by default and has its own
section: [The 6th above a seventh](#the-6th-above-a-seventh-options--6th-with-7th). It changes no name.

## What sounds

The names are derived from the engine's tables (`cr_engine.c`), not from the buttons' labels:

| | semitones above the root |
| --- | --- |
| DIM | 0 3 6 |
| MIN | 0 3 7 |
| MAJ | 0 4 7 |
| SUS | 0 5 7 (the **fourth**: a sus4) |
| MAJ + SUS (secret) | 0 4 8 (augmented) |
| DIM + SUS (secret) | 0 7 12 (the power chord: root, fifth, octave) |
| MIN + SUS (secret) | 0 3 5 (`m add4`: **no fifth**) |
| 6 | adds 9, under every chord (a major sixth: under a minor chord `m6`, under a diminished one the diminished seventh); with Options > 6th with 7th at Octave up and a seventh held: adds 21 instead |
| 7 (the m7 key) | adds 10, under every chord (a minor seventh: under a diminished chord that is half-diminished, `m7b5`, not `dim7`) |
| M7 | adds 11 |
| 9 | adds 14, always a major ninth, always in the octave above |

An extension is added once, in the octave the table says. The notes are then voiced (KNOB 1) without changing the
name. The tables below give the notes with 6th with 7th at In place, the default.

## Standard's rules

1. **The triad.** `CM` (see "The major triad" below), `Cm`, `Cdim`, `Csus4`, `C+`, `C5`, `Cmadd4`.
2. **The figure** of the extensions, when the two sevenths are not both held:

   | held | figure | |
   | --- | --- | --- |
   | 6 | `6` | |
   | 9 | `add9` | no seventh under it: a `9` would promise one |
   | 6 + 9 | `6/9` | |
   | 7 | `7` | |
   | 7 + 9 | `9` | |
   | 7 + 6, 7 + 6 + 9 | `13` | the 6th above a seventh is the 13th; a 13th chord may or may not have its 9th |
   | M7, M7 + 9, M7 + 6 (+ 9) | `M7`, `M9`, `M13` | the major seventh is `M`, as the display already writes `M7` (and `maj` would not fit) |

3. **Where the figure goes.** After the triad for major, minor and augmented (`C9`, `Cm9`, `C+9`, `CmM7`); before
   `sus4` (`C7sus4`, `C9sus4`, `C6sus4`; `Csus4add9` when the 9th is the only extension); before `add4` on the
   Min + Sus chord (`Cm7add4`).
4. **Diminished.** The 6 key makes it `Cdim7`; the 7 key makes it half-diminished, `Cm7b5` (`Cm9b5`, `Cm13b5`); the
   major seventh is written on the triad (`CdimM7`, `CdimM9`, `CdimM13`).
5. **The power chord.** `C5`. With the 9th it is a sus2 chord (`Csus2`, `C7sus2`, `C6sus2`); without it the name is
   the figure and `no3` (`C7no3`, `C6no3`).
6. **Both sevenths at once** (7 + M7) have no lead-sheet name: those 28 cells (4 of each quality's 16) keep
   Orchid's name exactly: `C7 M7`, `JAZZ` with one more extension, `WTF` with both. Nowhere else does Standard say
   `JAZZ` or `WTF`: the 14 other `JAZZ` cells (7 + 6 + 9 and M7 + 6 + 9) are `13` and `M13`.
7. **Sharps**, as before: `C#`, `D#`, `F#`, `G#`, `A#`.

**The 13 names a sixth voiced inside the octave, unless you ask for the octave.** By default the engine plays the
6th where it always did, a major sixth above the root, under the seventh; the Orchid does not raise it and Chord
Names changes no voicing: `C13` is then "a C7 with its 6th", not a promise of a note above the octave. With
**Options > 6th with 7th** at Octave up the 6th of such a chord is 21 semitones above the root and `C13` names a real
13th (the next section). The two options are independent: either naming with either voicing.

**One name, one set of notes**, with one exception: `13` stands for the chord with and without its 9th (12 pairs:
six qualities, the minor and the major seventh). And one name says less than it could: the Min + Sus chord has no
fifth, and `Cmadd4` does not say so (neither does Orchid's `Cm add4`). `tests/cr_engine_test.c` reads every Standard
name with a small lead-sheet reader that knows nothing of the engine and compares the pitch classes it gets with
`cr_chord_base`'s, with exactly those two allowances.

## The table

Root C. The names are written as the looper's lines write them: root, quality and superscript in a row (on the big
views the last part is the small raised one: `Cm` with a raised `7 9`, `C` with a raised `9sus4`). "Sounds" is in
semitones above the root, before the voicing. 7 qualities x 16 extension sets = 112 chords; the eighth row of the
engine's table is the single note (a root with no chord key and Key Mode off), which reads `C` whatever is held, in
both settings: 128 cells.

### MAJ (0 4 7)

| Buttons | Sounds | Orchid | Standard | Why |
| --- | --- | --- | --- | --- |
| MAJ | 0 4 7 | `CM` | `CM` (the same) | kept: `C` alone is a single note on every screen |
| MAJ + 6 | 0 4 7 9 | `C6` | `C6` (the same) |  |
| MAJ + 7 | 0 4 7 10 | `C7` | `C7` (the same) |  |
| MAJ + 6 + 7 | 0 4 7 9 10 | `C7 6` | `C13` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| MAJ + M7 | 0 4 7 11 | `CM7` | `CM7` (the same) |  |
| MAJ + 6 + M7 | 0 4 7 9 11 | `CM7 6` | `CM13` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| MAJ + 7 + M7 | 0 4 7 10 11 | `C7 M7` | `C7 M7` (the same) | both sevenths: no lead-sheet name, Orchid's literal stands |
| MAJ + 6 + 7 + M7 | 0 4 7 9 10 11 | `CJAZZ` | `CJAZZ` (the same) | both sevenths: Orchid's name stands |
| MAJ + 9 | 0 4 7 14 | `C9` | `Cadd9` | no seventh: `add9`, not `9` |
| MAJ + 6 + 9 | 0 4 7 9 14 | `C6 9` | `C6/9` | 6th and 9th, no seventh |
| MAJ + 7 + 9 | 0 4 7 10 14 | `C7 9` | `C9` | seventh and 9th |
| MAJ + 6 + 7 + 9 | 0 4 7 9 10 14 | `CJAZZ` | `C13` | seventh, 6th and 9th: a 13th chord |
| MAJ + M7 + 9 | 0 4 7 11 14 | `CM7 9` | `CM9` | seventh and 9th |
| MAJ + 6 + M7 + 9 | 0 4 7 9 11 14 | `CJAZZ` | `CM13` | seventh, 6th and 9th: a 13th chord |
| MAJ + 7 + M7 + 9 | 0 4 7 10 11 14 | `CJAZZ` | `CJAZZ` (the same) | both sevenths: Orchid's name stands |
| MAJ + 6 + 7 + M7 + 9 | 0 4 7 9 10 11 14 | `CWTF` | `CWTF` (the same) | both sevenths: Orchid's name stands |

### MIN (0 3 7)

| Buttons | Sounds | Orchid | Standard | Why |
| --- | --- | --- | --- | --- |
| MIN | 0 3 7 | `Cm` | `Cm` (the same) |  |
| MIN + 6 | 0 3 7 9 | `Cm6` | `Cm6` (the same) |  |
| MIN + 7 | 0 3 7 10 | `Cm7` | `Cm7` (the same) |  |
| MIN + 6 + 7 | 0 3 7 9 10 | `Cm7 6` | `Cm13` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| MIN + M7 | 0 3 7 11 | `CmM7` | `CmM7` (the same) | minor triad, major seventh |
| MIN + 6 + M7 | 0 3 7 9 11 | `CmM7 6` | `CmM13` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| MIN + 7 + M7 | 0 3 7 10 11 | `Cm7 M7` | `Cm7 M7` (the same) | both sevenths: no lead-sheet name, Orchid's literal stands |
| MIN + 6 + 7 + M7 | 0 3 7 9 10 11 | `CmJAZZ` | `CmJAZZ` (the same) | both sevenths: Orchid's name stands |
| MIN + 9 | 0 3 7 14 | `Cm9` | `Cmadd9` | no seventh: `add9`, not `9` |
| MIN + 6 + 9 | 0 3 7 9 14 | `Cm6 9` | `Cm6/9` | 6th and 9th, no seventh |
| MIN + 7 + 9 | 0 3 7 10 14 | `Cm7 9` | `Cm9` | seventh and 9th |
| MIN + 6 + 7 + 9 | 0 3 7 9 10 14 | `CmJAZZ` | `Cm13` | seventh, 6th and 9th: a 13th chord |
| MIN + M7 + 9 | 0 3 7 11 14 | `CmM7 9` | `CmM9` | seventh and 9th |
| MIN + 6 + M7 + 9 | 0 3 7 9 11 14 | `CmJAZZ` | `CmM13` | seventh, 6th and 9th: a 13th chord |
| MIN + 7 + M7 + 9 | 0 3 7 10 11 14 | `CmJAZZ` | `CmJAZZ` (the same) | both sevenths: Orchid's name stands |
| MIN + 6 + 7 + M7 + 9 | 0 3 7 9 10 11 14 | `CmWTF` | `CmWTF` (the same) | both sevenths: Orchid's name stands |

### SUS (0 5 7: the fourth)

| Buttons | Sounds | Orchid | Standard | Why |
| --- | --- | --- | --- | --- |
| SUS | 0 5 7 | `Csus` | `Csus4` | the engine's SUS is the fourth (5 semitones), so `sus4` |
| SUS + 6 | 0 5 7 9 | `Csus6` | `C6sus4` | the figure goes before `sus4` |
| SUS + 7 | 0 5 7 10 | `Csusm7` | `C7sus4` | Orchid's `m7` is the plain minor seventh: a dominant 7 with the fourth |
| SUS + 6 + 7 | 0 5 7 9 10 | `Csusm7 6` | `C13sus4` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| SUS + M7 | 0 5 7 11 | `CsusM7` | `CM7sus4` |  |
| SUS + 6 + M7 | 0 5 7 9 11 | `CsusM7 6` | `CM13sus4` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| SUS + 7 + M7 | 0 5 7 10 11 | `Csusm7 M7` | `Csusm7 M7` (the same) | both sevenths: no lead-sheet name, Orchid's literal stands |
| SUS + 6 + 7 + M7 | 0 5 7 9 10 11 | `CsusJAZZ` | `CsusJAZZ` (the same) | both sevenths: Orchid's name stands |
| SUS + 9 | 0 5 7 14 | `Csus9` | `Csus4add9` | fourth and ninth, no third, no seventh: `sus4` with the added 9th |
| SUS + 6 + 9 | 0 5 7 9 14 | `Csus6 9` | `C6/9sus4` | 6th and 9th, no seventh |
| SUS + 7 + 9 | 0 5 7 10 14 | `Csusm7 9` | `C9sus4` | seventh and 9th |
| SUS + 6 + 7 + 9 | 0 5 7 9 10 14 | `CsusJAZZ` | `C13sus4` | seventh, 6th and 9th: a 13th chord |
| SUS + M7 + 9 | 0 5 7 11 14 | `CsusM7 9` | `CM9sus4` | seventh and 9th |
| SUS + 6 + M7 + 9 | 0 5 7 9 11 14 | `CsusJAZZ` | `CM13sus4` | seventh, 6th and 9th: a 13th chord |
| SUS + 7 + M7 + 9 | 0 5 7 10 11 14 | `CsusJAZZ` | `CsusJAZZ` (the same) | both sevenths: Orchid's name stands |
| SUS + 6 + 7 + M7 + 9 | 0 5 7 9 10 11 14 | `CsusWTF` | `CsusWTF` (the same) | both sevenths: Orchid's name stands |

### DIM (0 3 6)

| Buttons | Sounds | Orchid | Standard | Why |
| --- | --- | --- | --- | --- |
| DIM | 0 3 6 | `Cdim` | `Cdim` (the same) |  |
| DIM + 6 | 0 3 6 9 | `Cdim6` | `Cdim7` | the 6 key adds 9 semitones: the diminished seventh |
| DIM + 7 | 0 3 6 10 | `Cdim7` | `Cm7b5` | the 7 key adds 10 semitones, a minor seventh: half-diminished, not `dim7` |
| DIM + 6 + 7 | 0 3 6 9 10 | `Cdim7 6` | `Cm13b5` | half-diminished with the 6th as its 13th |
| DIM + M7 | 0 3 6 11 | `CdimM7` | `CdimM7` (the same) | diminished triad, major seventh |
| DIM + 6 + M7 | 0 3 6 9 11 | `CdimM7 6` | `CdimM13` | the triad's name and the figure of M7 + 6 (the 6 is the diminished seventh's note) |
| DIM + 7 + M7 | 0 3 6 10 11 | `Cdim7 M7` | `Cdim7 M7` (the same) | both sevenths: no lead-sheet name, Orchid's literal stands |
| DIM + 6 + 7 + M7 | 0 3 6 9 10 11 | `CdimJAZZ` | `CdimJAZZ` (the same) | both sevenths: Orchid's name stands |
| DIM + 9 | 0 3 6 14 | `Cdim9` | `Cdimadd9` | no seventh: `add9`, not `9` |
| DIM + 6 + 9 | 0 3 6 9 14 | `Cdim6 9` | `Cdim7add9` | the diminished seventh chord and a 9th |
| DIM + 7 + 9 | 0 3 6 10 14 | `Cdim7 9` | `Cm9b5` | half-diminished with the 9th |
| DIM + 6 + 7 + 9 | 0 3 6 9 10 14 | `CdimJAZZ` | `Cm13b5` | half-diminished with the 6th as its 13th |
| DIM + M7 + 9 | 0 3 6 11 14 | `CdimM7 9` | `CdimM9` | as `CmM9`, on the diminished triad |
| DIM + 6 + M7 + 9 | 0 3 6 9 11 14 | `CdimJAZZ` | `CdimM13` | the triad's name and the figure of M7 + 6 (the 6 is the diminished seventh's note) |
| DIM + 7 + M7 + 9 | 0 3 6 10 11 14 | `CdimJAZZ` | `CdimJAZZ` (the same) | both sevenths: Orchid's name stands |
| DIM + 6 + 7 + M7 + 9 | 0 3 6 9 10 11 14 | `CdimWTF` | `CdimWTF` (the same) | both sevenths: Orchid's name stands |

### MAJ + SUS, the secret augmented chord (0 4 8)

| Buttons | Sounds | Orchid | Standard | Why |
| --- | --- | --- | --- | --- |
| MAJ + SUS | 0 4 8 | `C+` | `C+` (the same) | `+` as Orchid's (short; `aug` is the same chord) |
| MAJ + SUS + 6 | 0 4 8 9 | `C+6` | `C+6` (the same) |  |
| MAJ + SUS + 7 | 0 4 8 10 | `C+7` | `C+7` (the same) | also written `C7#5` |
| MAJ + SUS + 6 + 7 | 0 4 8 9 10 | `C+7 6` | `C+13` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| MAJ + SUS + M7 | 0 4 8 11 | `C+M7` | `C+M7` (the same) |  |
| MAJ + SUS + 6 + M7 | 0 4 8 9 11 | `C+M7 6` | `C+M13` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| MAJ + SUS + 7 + M7 | 0 4 8 10 11 | `C+7 M7` | `C+7 M7` (the same) | both sevenths: no lead-sheet name, Orchid's literal stands |
| MAJ + SUS + 6 + 7 + M7 | 0 4 8 9 10 11 | `C+JAZZ` | `C+JAZZ` (the same) | both sevenths: Orchid's name stands |
| MAJ + SUS + 9 | 0 4 8 14 | `C+9` | `C+add9` | no seventh: `add9`, not `9` |
| MAJ + SUS + 6 + 9 | 0 4 8 9 14 | `C+6 9` | `C+6/9` | 6th and 9th, no seventh |
| MAJ + SUS + 7 + 9 | 0 4 8 10 14 | `C+7 9` | `C+9` | seventh and 9th |
| MAJ + SUS + 6 + 7 + 9 | 0 4 8 9 10 14 | `C+JAZZ` | `C+13` | seventh, 6th and 9th: a 13th chord |
| MAJ + SUS + M7 + 9 | 0 4 8 11 14 | `C+M7 9` | `C+M9` | seventh and 9th |
| MAJ + SUS + 6 + M7 + 9 | 0 4 8 9 11 14 | `C+JAZZ` | `C+M13` | seventh, 6th and 9th: a 13th chord |
| MAJ + SUS + 7 + M7 + 9 | 0 4 8 10 11 14 | `C+JAZZ` | `C+JAZZ` (the same) | both sevenths: Orchid's name stands |
| MAJ + SUS + 6 + 7 + M7 + 9 | 0 4 8 9 10 11 14 | `C+WTF` | `C+WTF` (the same) | both sevenths: Orchid's name stands |

### DIM + SUS, the secret power chord (0 7 12)

| Buttons | Sounds | Orchid | Standard | Why |
| --- | --- | --- | --- | --- |
| DIM + SUS | 0 7 12 | `C5` | `C5` (the same) |  |
| DIM + SUS + 6 | 0 7 9 12 | `C5 6` | `C6no3` | root and fifth only: the figure, and `no3` says the third is missing |
| DIM + SUS + 7 | 0 7 10 12 | `C5 7` | `C7no3` | root and fifth only: the figure, and `no3` says the third is missing |
| DIM + SUS + 6 + 7 | 0 7 9 10 12 | `C5 7 6` | `C13no3` | root and fifth only: the figure, and `no3` says the third is missing |
| DIM + SUS + M7 | 0 7 11 12 | `C5 M7` | `CM7no3` | root and fifth only: the figure, and `no3` says the third is missing |
| DIM + SUS + 6 + M7 | 0 7 9 11 12 | `C5 M7 6` | `CM13no3` | root and fifth only: the figure, and `no3` says the third is missing |
| DIM + SUS + 7 + M7 | 0 7 10 11 12 | `C5 7 M7` | `C5 7 M7` (the same) | both sevenths: no lead-sheet name, Orchid's literal stands |
| DIM + SUS + 6 + 7 + M7 | 0 7 9 10 11 12 | `C5 JAZZ` | `C5 JAZZ` (the same) | both sevenths: Orchid's name stands |
| DIM + SUS + 9 | 0 7 12 14 | `C5 9` | `Csus2` | root, fifth, ninth: that is a sus2 chord |
| DIM + SUS + 6 + 9 | 0 7 9 12 14 | `C5 6 9` | `C6sus2` | with the 9th it is a sus2 chord; the figure names the rest |
| DIM + SUS + 7 + 9 | 0 7 10 12 14 | `C5 7 9` | `C7sus2` | with the 9th it is a sus2 chord; the figure names the rest |
| DIM + SUS + 6 + 7 + 9 | 0 7 9 10 12 14 | `C5 JAZZ` | `C13sus2` | with the 9th it is a sus2 chord; the figure names the rest |
| DIM + SUS + M7 + 9 | 0 7 11 12 14 | `C5 M7 9` | `CM7sus2` | with the 9th it is a sus2 chord; the figure names the rest |
| DIM + SUS + 6 + M7 + 9 | 0 7 9 11 12 14 | `C5 JAZZ` | `CM13sus2` | with the 9th it is a sus2 chord; the figure names the rest |
| DIM + SUS + 7 + M7 + 9 | 0 7 10 11 12 14 | `C5 JAZZ` | `C5 JAZZ` (the same) | both sevenths: Orchid's name stands |
| DIM + SUS + 6 + 7 + M7 + 9 | 0 7 9 10 11 12 14 | `C5 WTF` | `C5 WTF` (the same) | both sevenths: Orchid's name stands |

### MIN + SUS, the secret chord `m add4` (0 3 5: no fifth)

| Buttons | Sounds | Orchid | Standard | Why |
| --- | --- | --- | --- | --- |
| MIN + SUS | 0 3 5 | `Cmadd4` | `Cmadd4` (the same) | as Orchid's, without the space; the fifth does not sound and the name does not say so |
| MIN + SUS + 6 | 0 3 5 9 | `Cmadd4 6` | `Cm6add4` |  |
| MIN + SUS + 7 | 0 3 5 10 | `Cmadd4 7` | `Cm7add4` |  |
| MIN + SUS + 6 + 7 | 0 3 5 9 10 | `Cmadd4 7 6` | `Cm13add4` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| MIN + SUS + M7 | 0 3 5 11 | `Cmadd4 M7` | `CmM7add4` |  |
| MIN + SUS + 6 + M7 | 0 3 5 9 11 | `Cmadd4 M7 6` | `CmM13add4` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| MIN + SUS + 7 + M7 | 0 3 5 10 11 | `Cmadd4 7 M7` | `Cmadd4 7 M7` (the same) | both sevenths: no lead-sheet name, Orchid's literal stands |
| MIN + SUS + 6 + 7 + M7 | 0 3 5 9 10 11 | `Cmadd4 JAZZ` | `Cmadd4 JAZZ` (the same) | both sevenths: Orchid's name stands |
| MIN + SUS + 9 | 0 3 5 14 | `Cmadd4 9` | `Cmadd9add4` | the minor chord's name (`madd9`) and `add4` |
| MIN + SUS + 6 + 9 | 0 3 5 9 14 | `Cmadd4 6 9` | `Cm6/9add4` | 6th and 9th, no seventh |
| MIN + SUS + 7 + 9 | 0 3 5 10 14 | `Cmadd4 7 9` | `Cm9add4` | seventh and 9th |
| MIN + SUS + 6 + 7 + 9 | 0 3 5 9 10 14 | `Cmadd4 JAZZ` | `Cm13add4` | seventh, 6th and 9th: a 13th chord |
| MIN + SUS + M7 + 9 | 0 3 5 11 14 | `Cmadd4 M7 9` | `CmM9add4` | seventh and 9th |
| MIN + SUS + 6 + M7 + 9 | 0 3 5 9 11 14 | `Cmadd4 JAZZ` | `CmM13add4` | seventh, 6th and 9th: a 13th chord |
| MIN + SUS + 7 + M7 + 9 | 0 3 5 10 11 14 | `Cmadd4 JAZZ` | `Cmadd4 JAZZ` (the same) | both sevenths: Orchid's name stands |
| MIN + SUS + 6 + 7 + M7 + 9 | 0 3 5 9 10 11 14 | `Cmadd4 WTF` | `Cmadd4 WTF` (the same) | both sevenths: Orchid's name stands |

Counted (`tests/cr_engine_test.c`): of the 112 chords, 84 have a Standard name of their own and 28 (both sevenths)
keep Orchid's: 7 read `7 M7`, 14 `JAZZ`, 7 `WTF`. 16 of the 84 are the same in both settings: `CM`, `C6`, `C7`, `CM7`,
`Cm`, `Cm6`, `Cm7`, `CmM7`, `Cdim`, `CdimM7`, `C+`, `C+6`, `C+7`, `C+M7`, `C5`, `Cmadd4`.

## Choices

- **The major triad stays `CM`.** A lead sheet writes `C`. But a single note is named by its root alone, without an
  octave, on every screen that names both: the Chord, Keyboard, Notes, Scope and Geek Out views, the looper's name
  line and its playing name all go through `cr_chord_name`, which gives `C` for `CR_Q_NONE` (the octave appears
  only in the notes list and in the loop editor's Transpose cell). With `C` for the triad the two would read the
  same, which is what the owner's rule of 0.18 removed ("major chords should use capital M to differentiate from
  single notes. So C major would read CM"). So Standard keeps it. The owner's to change: see "Open questions".
- **`M` for the major seventh** (`CM7`, `CM9`, `CM13`, `CmM7`), not `maj`: Orchid mode already writes `M7`, and
  `G#mmaj13add4` would not fit anywhere.
- **`Cmadd9`**, no brackets (and `Cmadd4`, `Cdimadd9`, `Csus4add9`): the superscript sets the `add9` apart on the
  big views, and brackets cost width on the small ones.
- **`C6/9`**, with the slash: the superscript is drawn with the 40 px face, which has it (below).
- **`C+`** for the augmented chord, as in Orchid mode (`aug` is three letters wider); `C+7` is the chord also
  written `C7#5`.
- **`sus4`, not `sus`**: the engine's SUS is the fourth, and the power chord with a 9th is a `sus2`, so the number
  tells them apart. Orchid mode keeps `sus`.
- **`Csus4add9`** for SUS + 9 (root, 2nd, 4th, 5th), rather than `Csus2sus4`.
- **`no3`** for the power chord with a 6th or a seventh (`C7no3`): there is no third to call it major or minor.
- **`dim` + M7 + 6 is `CdimM13`**, not `Cdim7M7`: next to the fallback `Cdim7 M7` (DIM + 7 + M7, Orchid's name) the
  two would differ by a space.
- **The fallback is Orchid's whole name**, quality included: `Csusm7 M7` in Standard too (not `Csus4 ...`), so that
  a fallback is recognisable as one. Note that Orchid's `dim` + `7` there means the minor seventh (10 semitones),
  as everywhere in Orchid mode.

## How the name is drawn, and what fits

`cr_chord_name` writes three strings: `root` (3 bytes), `qual` (5) and `sup` (`CR_SUP_MAX`, 12). `cr_draw.c cr_name`
draws the root at the view's size, the quality at 0.58 of it on the same baseline, and `sup` as a superscript at
0.38, orange (red for a secret chord); the whole is squeezed horizontally to the room it has (224 px on the Chord,
Keyboard and arp views, 200 px in the PERF layer's arp band, 132 px on Geek Out, where the name is centred on x 70)
and never cut. The looper's name line, its playing name, the Scope view's header and the Notes view's title write
the three in a row as text.

**Glyphs.** The root and the quality of the Chord view (104 px, 60 px) come from the big face `CRX`, which has 37
characters only: the space and `# + - . 0 1 2 3 4 5 6 7 8 9 ? A B C D E F G I J M N P T W Z b d i m s u`
(`tools/gen_aa_font.py CRX_CHARS`; a string it does not cover falls back to the 40 px face scaled up). Standard's
qualities are `M`, `m`, `dim`, `sus4`, `sus2`, `+`: all in it. Everything else Standard writes (`add9`, `6/9`,
`no3`, `b5`, `M13`) is in the superscript, which at every size is the 40 px face `CRB`: full ASCII, so `/`, `a`,
`n`, `o` are there. No glyph was added and the font generator was not touched. (`°` and `ø` are in neither face:
`dim` and `m7b5` are spelled out.)

**The fit**, checked for all 112 chords under G# and A# (the widest roots) in both settings by
`tests/cr_draw_test.c` ("chord names": 0 lint findings, nothing ellipsised, nothing off the screen):

| where | room | Orchid's widest | Standard's widest |
| --- | --- | --- | --- |
| the Chord view's big name, 104 px | squeezed to 224 px | `G#m` `add4 JAZZ`, to 56 % | `G#m` `add9add4`, to 58 %; `G#sus4` `add9`, to 59 % |
| the Keyboard view (40 px), the arp (44 px), the PERF layer's arp band (20 px) | 224 / 224 / 200 px | fits unsqueezed | fits unsqueezed |
| Geek Out (40 px, centred on x 70) | 132 px | `G#m` `add4 JAZZ`, to 85 % | `G#m` `add9add4`, to 88 % |
| the looper's name line beside `step 64/64`, with a blue voicing `-12` | 15 px bold, what the step count and the voicing leave | `G#madd4 JAZZ` 110 px | `G#madd9add4` 105 px |
| the loop editor's playing name | 78 px, at the largest of 13, 11 and 10 px that fits | `G#madd4 JAZZ` 74 px at 10 px (8 of the 224 names need 10 px) | none of its own names needs 10 px (`G#madd9add4` would be 70 px there) |
| the Scope view's header, the Notes view's title | 15 px bold | 110 px | 105 px |

Standard's longest names are shorter than Orchid's longest (`G#madd4 JAZZ`, which Standard also shows for the both-
sevenths cells), so no layout had to change for Standard. Two things were found on the way, both in Orchid mode too:

- The screen's copy of the superscript (`cr_screen.h cr_name_t.sup`) was 8 bytes against the engine's 12: on the
  views, `Cm add4 JAZZ` showed as `Cm add4 JA`, `add4 7 6` as `add4 7` (the looper's lines were whole). It is 12 now
  (`cr_ui.c` asserts it against `CR_SUP_MAX`): 4 bytes a name, 8 bytes of POOL (the frame's screen holds two names)
  and 8 of RAM (the animation's two).
- With the superscript whole, the longest names ran off Geek Out's left edge (centred on x 70 with 224 px of room:
  `G#m add4 JAZZ` began at x -7). Geek Out's room is 132 px now; a name up to 132 px wide is drawn as before.

## The 6th above a seventh: Options > 6th with 7th

**Options > 6th with 7th: In place / Octave up** (0.20), the entry after Secret Chords. In place is the default.

The user who asked for Standard names: "it would be nice if the 6th was played an octave higher if either 7th is
enabled (effectively making it a 13th chord)". The owner: "Let's add as option you can turn on, defaulted off."

- **In place** (the default): the 6 key adds 9 semitones, always. This is what the Orchid does and what every version
  up to 0.19 did; nothing changes if the option is left alone (bit for bit: below).
- **Octave up**: when a chord has the 6 **and** at least one seventh (the 7 key, M7, or both), the 6th is 21 semitones
  above the root instead of 9: the same note an octave higher, the chord's 13th. Everything else is as before.

Root C4, KNOB 1 (Voicing) at 0. The names are the same under either value (Orchid: `C7 6`; Standard: `C13`).

| Buttons | In place | Octave up |
| --- | --- | --- |
| MAJ + 7 + 6 | C4 E4 G4 **A4** A#4 (0 4 7 9 10) | C4 E4 G4 A#4 **A5** (0 4 7 10 21) |
| MAJ + M7 + 6 | C4 E4 G4 **A4** B4 (0 4 7 9 11) | C4 E4 G4 B4 **A5** (0 4 7 11 21) |
| MIN + 7 + 6 | C4 D#4 G4 **A4** A#4 (0 3 7 9 10) | C4 D#4 G4 A#4 **A5** (0 3 7 10 21) |
| MAJ + 7 + 6 + 9 | C4 E4 G4 **A4** A#4 D5 (0 4 7 9 10 14) | C4 E4 G4 A#4 D5 **A5** (0 4 7 10 14 21) |
| DIM + 7 + 6 | C4 D#4 F#4 **A4** A#4 (0 3 6 9 10) | C4 D#4 F#4 A#4 **A5** (0 3 6 10 21) |
| MAJ + 6, MAJ + 6 + 9 (no seventh) | C4 E4 G4 A4 (D5) | the same |

### What it does and does not touch

- **Which chords.** Every chord type with the 6 and a seventh: 7 types x 6 extension sets (6 + 7, 6 + M7, 6 + 7 + M7,
  each with or without the 9) = 42 of the 112 chords. A chord without a seventh is never touched: `C6` and `C6 9`
  stay close.
- **The 9.** The 9th is at 14, the raised 6th at 21: the 13th sits above the 9th, the conventional stacking, and is
  the chord's top note.
- **DIM.** Raised too. Alone, the 6 on a diminished triad is the diminished seventh (`Cdim7`) and stays where it is;
  with the 7 key the chord is half-diminished and Standard already calls the 9-semitone note its 13th (`Cm13b5`). One
  rule for every type reads and sounds the same everywhere, and it takes the semitone between the 6th and the
  seventh out of this chord as out of the others. DIM + M7 + 6 (`CdimM13`) follows the same rule.
- **The secret chords** (`+`, `5`, `m add4`): raised, by the same rule. The power chord with 7 + 6 is C4 G4 A#4 C5 A5.
- **Both sevenths at once** (`7 M7` with the 6: `JAZZ`, `WTF`): raised ("either seventh").
- **A single note** (no chord type, Key Mode off): extensions never applied to it; nothing changes.
- **Key Mode.** The scale chooses the chord's type; the extensions are added as held, in either setting (they were
  never corrected to the scale), so the raised note is the same pitch class as before.
- **The voicing (KNOB 1).** The 6th is raised before the voicing arranges the chord, so the knob treats it as the
  top note it is, as it treats the 9th: one step down moves the 13th down an octave (which gives In place's notes at
  0), one step up moves the root up under it. A loop step's own voicing works the same way. At every position the
  two settings have the same pitch classes and the same number of notes; away from the ends of the range they
  differ by exactly one note, an octave apart.
- **Split / Octave** (Single Notes): those settings move single notes only, so they do not meet this one.
- **The top of the range.** A note above MIDI 127 does not exist. Where root + 21 would be above it (a root above
  A#7, MIDI 106) the 6th stays in place rather than disappear. The engine's rule for a note out of range is to leave
  it out (the 9th of such a chord already is); the 6th has a place inside the range, so it keeps it. The number of
  notes is the same as In place for every root. KNOB 1's own rule (a step that would leave 0 .. 127 stops the
  rotation) applies to the raised note as to any other.
- **Voices.** The same number of notes: no extra voice.
- **The bass part.** Unaffected: it plays the root.
- **Strum, Slop, Harp, the arp, patterns.** They walk the chord's notes, so the raised 6th is where it sounds: last
  and highest going up. A pattern's step that names "the fourth note" plays the fourth note of the chord as it is now
  (in `C7 6` that was the 6th and is the seventh with Octave up).
- **MIDI out, the Raw Chord stream, the key LEDs, the Notes and Geek Out views' lists.** All are the engine's notes,
  so they agree. The key LEDs light a sounding note's key in both octaves of the keyboard, so the same keys are lit
  under either value.
- **The looper.** A loop stores a chord as root, type and extension bits, never as notes, so a recorded `C7 6` is
  played by the setting **at the moment each step is struck**: a loop recorded In place plays Octave up once the
  option says so, and back. That is how a loop already follows the live voicing, Extension Addition, the Perform mode
  and the sound. Nothing is rewritten in the loop.
- **When a change takes effect.** With the next chord that is built: a new press, a pad, a loop step, or a chord key
  changing a held chord (Advanced / Free). A chord that is sounding keeps its notes, and its note-offs are for the
  notes it started: nothing hangs when the option is turned under a held or latched chord.
- **The names.** Unchanged in both namings: Orchid `C7 6` (it names the keys), Standard `C13`. With In place,
  Standard's `13` names a 6th voiced inside the octave; with Octave up it names a 13th.

### How it is built

`cr_engine.c cr_chord_base_ex(root, type, extensions, sixth_up, out)`: the one place a chord's notes come from. With
`sixth_up` the 6's interval is 21 instead of 9 when a seventh's bit is set and `root + 9 >= 0`, `root + 21 <= 127`.
`cr_chord_base` is the same with `sixth_up` 0 (main's function, unchanged). The three callers (a key, a pad, a loop
voice) pass `cr_t.sixth_up`, set by `cr_set_sixth_up` (the engine's event `CRE_SIXTH` from the Options,
`cr_settings_apply` at power-on, a revert and a reset). The byte sits in what was padding: `cr_t` has the size it
had.

**Cost.** Note selection when a chord is built, never per sample: nothing in the audio path, `.ram_text` unchanged.
Flash +368 B (`.text` 468064), RAM +0, POOL +0.

**In place is main's, bit for bit.** `tests/cr_engine_test.c`: 8 types x 16 extension sets x 28 roots against a copy
of main's builder, each voiced at 13 KNOB 1 positions; 5600 chords through a loop voice, a loop step with its own
voicing, a pad and the keys. Every emulator script's WAV against a build of `git archive e51baa5`: HISTORY 3y30.

**Stored** in bit 4 of the settings block's `pick_roots` byte (`cr_settings.h CRS_SIXTH_UP`; set = Octave up), beside
Output, USB Rate and Chord Names: docs/SETTINGS.md. No record version: the bit is 0 in every older record and 0 is
the default. An Option, so it is saved by itself; Reset Settings puts it back to In place. **Going back to an older
firmware with Octave up set**: that firmware finds the byte out of its range and takes its default for the whole
byte, so Output reads Clean, USB Rate 44.1 kHz and Chord Names Orchid there until they are set again. With In place
set the older firmware reads the byte as before.

## The numeral (the KEY layer's Numerals, 0.22)

GitHub issue 1 asked for "the Roman numeral of the chord being played relative to the selected key ... alongside the
chord name". The KEY layer's page 2 has **Numerals: Off / On** on KNOB 4 (Off by default; a third value, Solfège,
since after 0.22: below). On, with Key Mode on and
a chord sounding, the Chord view shows the numeral in the voicing's blue (`CR_COL_BLUE`), centred above the chord
name (`cr_ui.c cu_numeral`, `cu_numeral_put`; `cr_draw.c cr_p_chord`, `cr_p_arp`; the screen's `numeral`,
`numeral_on`). Until HISTORY 3y34 it was on the top line, beside the key (`Key: C · V`).
The numeral is `cr_engine.c cr_chord_numeral(root_pc, quality, tonic, scale, buf)`, a pure function
(`tests/cr_engine_test.c s_numerals`):

- **The degree** is the root **as played** above the tonic: the name's root (`cr_chord_info`'s `root_pc`: Key Mode
  resolved, transposed) less the Transpose inside it (`cr_chord_info`'s `transpose`; `cu_numeral` subtracts it, the
  function stays pure). Major: 0 2 4 5 7 9 11 semitones = I .. VII; minor (natural): 0 2 3 5 7 8 10. So the tonic
  chord reads `I` at any Transpose: with Transpose +1 in C the name is `C#M` (what sounds) and the numeral `I` (the
  degree played). `transpose` is the Transpose the chord was struck with (the voice's own copy: turning the knob
  under a held chord changes neither its name nor its numeral). A loop's chord is recorded as the root that sounded
  and is not transposed again when it plays, so the Transpose it was recorded with is not known: the Transpose of
  now is taken out (right when the loop was recorded with the Transpose that is still set).
- **A root between two degrees** is the neighbour with an accidental: major `bII bIII #IV bVI bVII` (1 3 6 8 10),
  minor `bII #III #IV #VI #VII` (1 4 6 9 11). In Key Mode a key outside the scale plays a chord on a scale note, so
  with Transpose taken out these are reached only when the key (or, for a loop, Transpose) is changed under a
  sounding chord.
- **The case** is the third: lower for MIN, DIM and MIN + SUS (`m add4`), upper for MAJ, SUS, the augmented chord,
  the power chord and a single note.
- **Marks**: the degree sign after a diminished chord (`vii°`; with the 7 key it is half-diminished and still
  written `°`), `+` after the augmented one. No extensions: `V` for G, G7 and G9.

**The layout** (`cr_draw.c CR_NUM_PX`, `CR_NUM_Y`, `CR_NUM_ARP_DY`; `tests/cr_draw_test.c`): bold 16 px in the face
of the notes line (CRB, which has the degree sign), centred at x 120 on baseline 44; its ink is rows 31 .. 43, 8 px
under the top line (which ends on row 22). The big name (104 px) starts on row 71 or lower (row 62 with a
superscript and the voicing line under it), so on the plain Chord view nothing moves: with Numerals on and nothing
sounding the screen is the one with Numerals off, pixel for pixel. The arp screen's name (44 px on baseline 78) has
its superscript's top on row 42, so while the layout is on (`numeral_on`: Numerals and Key Mode both on, a chord
sounding or not) it is drawn 6 px lower, on baseline 84 (its ink from row 48; the hop's arc starts at row 122):
4 px between the numeral and the tallest name, and the name does not jump as chords are struck and released. The
widest numeral, `#vii°`, is 34 px. Only the Chord view shows it (its plain form, its arp screen, and its voicing
read-out); the Keyboard, Notes, Geek Out and Scope views, the PERF layer's arp band and the loop editor do not.
While a loop records or plays the top left is the looper's (`Loop 1`); the numeral is on the panel and still shows,
Key Mode being on. Stored in bit 1 of the settings block's `bass_on` (docs/SETTINGS.md), saved with SAVE held.

### Solfège (GitHub issue 15)

GitHub issue 15 (shinobi, 2026-10-13, "No support for Fixed Do Solfege") asked for DO RE MI; the owner's scope is the
numeral: "an option where 'Numerals' is ... to be able to do solfège notation for the scale degrees, so it now becomes
3 options: off, on, solfège". So KNOB 4 reads **Off / On / Solfège**, and Solfège shows the numeral's degree as a
**movable-do** syllable in the numeral's place (the same blue, size and position): `cr_engine.c
cr_chord_solfege(root_pc, quality, tonic, scale, buf)`, which shares the numeral's degree table and rules
(`crx_degree`: one function, two spellings; `cu_numeral` picks one; `tests/cr_engine_test.c s_solfege` checks C
major and A minor at all 12 roots, the marks, other tonics, and that all 2304 cells carry their numeral's accidental,
case and mark).

- **The syllables**: `DO RE MI FA SOL LA TI` for I .. VII, Do on the tonic in either scale (do-based minor: A minor's
  C is `MI` because it is `III`; la-based minor, where the minor tonic is La, was not taken: the owner's words are Do =
  the tonic, and it would make one degree two syllables depending on the scale). Ti, not Si: Si is the B of fixed do.
- **The case carries the third**, exactly as the numeral's: the whole syllable in capitals for MAJ, SUS, the augmented
  chord, the power chord and a single note (`SOL`), in small letters for MIN, DIM and MIN + SUS (`sol`). Whole-word
  case rather than `Sol` / `sol`: one letter's case (S / s at 16 px) is too small a difference to read at a glance,
  and SOL / sol is the numeral's V / v.
- **The marks**: `°` after a diminished chord (`ti°`), `+` after the augmented one (`SOL+`), as the numeral's.
- **A root between two degrees** takes the numeral's `b` / `#` before the syllable (`#FA`, `bTI`, A minor's `#MI`).
  The alternative is the chromatic syllables (raised `DI RI FI SI LI`, lowered `RA ME SE LE TE`); not taken because a
  prefix reads instantly at 16 px with nothing new to learn after the numeral, and because the chromatic names count
  from the major scale: in A minor the scale's own C would be `ME`, not the third degree `MI` the owner asked for.
- **The longest** is 4 characters (`#FA°`, `SOL+`; the numeral's longest is 5, `#vii°`), so the screen's `numeral[6]`
  holds both. The widest drawn is 39 px (`#MI+` at bold 16 px), in the numeral's gap with the tallest names
  (`tests/cr_draw_test.c`: 2 px clear of the top line and of the name). The KEY page's cell says `Solfège`: the è is
  an extra glyph of CRB alone (`tools/gen_aa_font.py CRB_EXTRAS`, 249 bytes).
- **Fixed do** (Do = C always: the note names of the Romance-language countries, what the issue's title says) is a
  different thing, a naming of the notes and so a value of Options > Chord Names, not of Numerals. Not built.

Stored in a bit of its own, bit 7 of the settings block's `scale` (`CRS_SOLFEGE`; On stays bit 1 of `bass_on`, and
each is written alone: docs/SETTINGS.md), saved with SAVE held. 0.17 .. 0.22 read a saved Solfège as Numerals Off
with the scale Major.

## Where the option is stored

Bit 3 of the settings block's `pick_roots` byte (`cr_settings.h CRS_NAMES_STD`; set = Standard), beside Output
(bit 1) and USB Rate (bit 2), and 6th with 7th (bit 4): docs/SETTINGS.md. No record version: the bit is 0 in every older record and 0 is the
default, Orchid. An Option, so it is saved by itself. The engine is told through `cr_chord_names_set` (a byte of its
own in `cr_engine.c`, not part of `cr_t`: nothing that plays reads it).

## Open questions (the owner's)

(The 6th an octave higher when a seventh is held was question 1 here: built as an option of its own, for both
namings: [The 6th above a seventh](#the-6th-above-a-seventh-options--6th-with-7th).)

1. **`C` for the major triad in Standard.** Kept as `CM` (above). `C` would need the single note to read
   differently where both are named (its octave, `C4`, as the notes list writes it).
2. **`13` with and without the 9th.** `C13` is both MAJ + 7 + 6 and MAJ + 7 + 6 + 9. `C7add13` for the first would
   tell them apart, at 7 characters of superscript.
3. **The Min + Sus chord's missing fifth.** `Cmadd4` (as Orchid's `Cm add4`); `Cm(add4,no5)` would be exact and
   would not fit.
4. **6th with 7th: a held chord and the option.** A chord that sounds keeps its notes when the option is turned;
   the next chord has the new setting. Re-striking held chords at the turn (as KNOB 1 does) would let the two be
   compared without lifting a finger, at the price of a re-attack; not built.
