# ChoralRoot FM-1 — P5

**P5** is **engine 16** (`ENGI_PROPHET`): **Melodee's** Prophet-5 engine (Kerem Kilic's fork of Felucca,
<https://github.com/keremimo/melodee>, GPL-3.0-only; its code name `p5_`). Native Sequential programs (the 133 raw
bytes of a program dump), two band-limited oscillators with hard sync and audio-rate Poly-Mod into a four-pole ladder
with a tanh knee in two characters (Rev 1/2: SSI; Rev 3: Curtis), two ADSRs, the wheel's modulation source, **four
voices a core** (Melodee: five a part; [the unit's reading](#measured-on-the-unit-2026-10-13)): **eight on the chord
part with two cores, four with one** ([Voices](#voices), 3y46). It came here by way of a remix of
ChoralRoot 0.14-dev that Rosen sent ("the Prophet 5 Engine from Melodee that was added sounds really nice and is
perfect for chords").

**The name.** A player sees **P5** everywhere (the engine pickers, the pool's read-out, the editor, the save dialog,
the emulator's traces): the owner's name for it, 2026-10-13 ("we can name in P5"). The code, the switches and
this file keep their names (`FELUCCA_PROPHET`, `ENGI_PROPHET`, `eng_prophet_test.c`, `p5_`); "Prophet" below means the
instrument and Sequential's data.

**Where it stands (0.22-beta1, 2026-10-14).** P5 ships in the release with the **115 programs of
`assets/prophet5-factory/shipped.txt`** (the owner's favourites of Sequential's 200, 3y51; the other 85 are neither in
a release nor on the site: [Open decisions](#open-decisions-the-owners) 2). Its sounds **save** like every engine's
since the sound pool (3y57, [Saving](#saving)); eight voices on two cores, the bass always keeping one (3y46, 3y53),
four and a one-voice bass on one core; the owner has played the eight-voice build on his unit (3y53: six- to
eight-note chords with the effects up, 0 late, 0 stale, 0 shed). Open: the crackle of 3y44 (not reproduced on the
host, not explained), and Sequential program SysEx import.

**Where it stood, 3y46** (2026-10-13): the chord part's voices are rendered by BOTH cores (eight voices, four a
core), a P5 bass shares that pool with the chord first, one core plays four and a P5 bass one, the engine is 6 dB
lower, and Options > Dual Core is On on a new unit. All of it on the host and in the emulators; **nothing of 3y46 has
run on a unit**. [Voices](#voices), [Two cores, eight voices](#two-cores-eight-voices-3y46), [Level](#level-3y46).

**Where it stood** (0.22 in development, 2026-10-13; HISTORY 3y38, 3y39): the engine plays on the host and in the
emulators, its sounds browse like any engine's, its editor is complete. The owner has played the first build
(cd43720, five voices) on the FM-1 **once**, and that reading set the cap to four; nothing of the tree as it is now
(four voices, the shipped list, UNISON on a load) has run on a unit. Not in this phase: a store for user programs
(SAVE is refused), backup ids, the web manager, loading the other programs of the 200 from the device manager (the
owner's plan; it needs that store), a release note. [Open decisions](#open-decisions-the-owners) lists what waits.

| file | what | from |
| --- | --- | --- |
| `firmware/src/eng_prophet_test.c` | the engine: voices, envelopes, the sample loop, the wheel's source, the part's DC blocker; ChoralRoot's load hook, blob, deep pages | Melodee 8a3074b (+ 7cd66fb's DC blocker, 1bc4555's unpacking), the remix's three optimisations, ChoralRoot |
| `firmware/src/prophet_patch.c` | the program: the 133 raw bytes and their indices, INIT, SysEx decode / encode (model 0x32 / 0x31, 133 / 128 bytes) | Melodee, as it is (8a3074b = 1.2.1, byte for byte below the header) |
| `tools/gen_tables.py` `p5_tables` | the engine's nine tables and `P5_KEYTRACK_HALF_Q7` | Melodee 8a3074b's expressions |
| `tools/gen_prophet_factory.py`, `assets/prophet5-factory/` | Sequential's 200 factory programs v1.03, of which `shipped.txt` names the ones in the build -> `build/gen/melodee_prophet_factory.h` (`tools/build.py` generate()) | Melodee 1.2.1 (the packed form), + the PRESETS names, the shipped list |
| `tools/p5_goldens.py` | P5's golden renders and CPU entries alone, after the list changed | ChoralRoot |
| `tests/cr_prophet_test.c`, `tests/run_prophet_test.sh` | the host test | ChoralRoot |
| `tools/p5_cost.py` | the cost on the unit, counted from the device compiler's code | ChoralRoot |
| `tools/prophet_ab.c`, `tools/prophet_listen.sh` | one engine file against another; the listening files | ChoralRoot |

## Where it comes from

Melodee's history of the engine: c7f1bea (prototype), 1fbeccd (playable), b351a14 (calibrated to a Rev-4 demo, the 200
factory programs), **8a3074b** ("Lo Freq B seven octaves, free-running phase, smoother saturation": the last INTEGER
version), f12b310 (experimental dual core: only scaffolding around the same sample code), fa64ef0 (float on the
hardware FPU), 7cd66fb (the part's DC blocker), 1bc4555 (the bank packed), 1.2.1. ChoralRoot's build has no FPU flags
and keeps none, so **the DSP is 8a3074b's**, statement for statement, with these differences:

| difference | why | the samples |
| --- | --- | --- |
| the remix's stage 36, three optimisations (`P5_STAGE36` bits): (1) an oscillator with no waveform on computes nothing; (2) the envelope's terms of the cutoff, the Poly-Mod frequency and width once a sample instead of once an oversampling pass; (4) hard sync's two phase products as 32-bit halves instead of a 64-bit multiply | fewer instructions | **bit-identical**, each alone and all three: `tests/run_prophet_test.sh` renders INIT, the shipped programs and 683 stress programs with `P5_STAGE36` 0, 1, 2, 4, 7 and compares every sample's digest |
| the part's DC blocker (`p5_post`, Melodee 7cd66fb: integer code written after the float change, about 5 Hz) | without it 59 of the 201 presets (INIT and all 200: cd43720's count) hold a DC of more than a quarter of their RMS in a held chord (the worst 47 878 on a scale where a voice is 24 000): it would ride into the sends and the limiter | changed on purpose (`P5_DC_BLOCK=0` gives 8a3074b's output for a measurement) |
| no `.dsp_text` / `.dsp_tables` sections: the code runs from flash, the tables are `const` | ChoralRoot's map has no such sections, RAM is the tight region, and RAM changed nothing for the effects' loops on the unit (docs/FX.md `CR_FX_RAM`). **Since 3y41 a device build has `P5_RAM=1`**: the two per-sample functions (`p5_samples` with `p5_inc` inlined, `p5_mod_samples`: 3792 B) are in `.ram_text`, because the unit said so ("Cost on the unit": the chord part 2084 -> 1533 us). The four per-sample tables stay in flash unless `CR_RAM_TABLES_P5=1` (docs/FX.md "Code in RAM") | the same |
| the pitch bend: Melodee's `p5_update_bend` (voice.c: the program's own range byte) is not here | the part bends as ChoralRoot's other engines do, through `vmod_t` (voice.c: the channel's range, RPN 0, ±2 semitones). The program's `P5_BEND` byte is kept and unused | tested: +2 / −2 semitones measured |
| the matrix's ENV source is 0 (Melodee's voice.c asks `p5_env_source`) | as FM6's and CZ-1's here; voice.c is not touched | none on a factory program |
| four voices a part (`P5_POLY`; Melodee: five), three budget units each as Melodee's (c176dd4: four units) | the unit's reading of five voices, [below](#measured-on-the-unit-2026-10-13); three units so that a bass does not cost the chord a voice: a measurement build, [Voices](#voices-which-keys-of-a-big-chord-sound) | a chord of more than four keys sounds four of them |
| UNISON on a load | as Melodee's (`V_UNISON` whenever the program says so); cd43720 had left it out, the owner asked for it | [UNISON](#unison) |
| only `P5_OVERSAMPLE` 1 | Melodee's 2x never passed its budget; the generator makes the 1x coefficient table only | the same |

The remix's engine file is Melodee **b351a14** with stage 36 (not 8a3074b: its Lo Freq B is ten octaves down and
starts every note at the bottom of the ramp, its SSI output is the uninterpolated knee). What this tree does not take
from the remix: the 192-frame half buffer, the second core on by default, the voice caps and the chord-aware
shedding (`voice.c`, `cr_out.c`), six voices with dual core, the external bank switch, the factory bank and the
tables as committed generated headers (here both are generated at build time; the tables were compared once: the
remix's `choralroot_prophet_tables.h` and `build/gen/felucca_tables.h` hold the same ten tables, value for value:
`tools/prophet_listen.sh` prints it), its 9 pages of 36 untyped 0..127 cells with a screen plan and a special case
in `cr_edit.c` (here 19 pages, 56 named and ranged cells, the editor's title rules, `cr_edit.c` untouched), and its
user-slot scheme ([Saving](#saving)).

## The engine

`FELUCCA_PROPHET` (core.h, default 0: Felucca's unit; 1 in `choralroot.c`, the emulator, `tests/regress.c`,
`tests/va_levels.c`). **`FELUCCA_PROPHET=0 ./build.sh` gives the image of before the engine, byte for byte**
(`build/choralroot.bin` sha256 501a9d09.., aa5875a's: checked again for 3y39; since the favourites, 3y40, which are
every engine's, the image without P5 is 57371526.., and the rule is the same: nothing of P5 outside the switch). `ENGINES[]` stays append-only (16
after FM TONE's 15; Melodee numbers it 19); `ENGINE_ORDER` shows it **after CZ-1 and before WHEEL** (FM6, FM TONE, VA,
CZ-1, P5, WHEEL: six engines). The display name is `ENG_P5_TEST.name`, "P5" ("Prophet" is Sequential's mark,
LICENSING.md).

- **State**: `p5_part_t` (672 B: four voices of 116 B, the wheel's 32 samples and their pitch ratios, the DC blocker) is
  a member of `engines.c`'s `eng_state` union (6884 B, FM6's size): **the union does not grow**. The program of each part
  (`p5_patch[NPART]`, 4 × 138 B) is in the pool: **POOL +552 B**.
- **Voices, since 3y46**: [Voices](#voices) (eight on the chord part with two cores, four with one; the state is
  eight voices', 1172 B, still inside the union). What follows is 3y40's:
- **Voices**: four (`P5_POLY`, the cap), each **three of the 16 budget units** (`p5_units`) **since 2026-10-13, a
  measurement build** (HISTORY 3y40). One core shares the 16 units between the parts: a chord alone is four voices
  (12 units) and **with a VA or FM6 bass sounding still four** (4 × 3 + 2 = 14 of 16). c176dd4 had four units a
  voice, which left the chord THREE voices whenever a bass sounded (3 × 4 + 2 = 14). The price of three units: **a
  P5 bass under a P5 chord is a fifth P5 voice** (4 × 3 + 3 = 15), and five voices are the reading that did not fit
  with a margin. **To be measured on the unit next**: four P5 chord voices + a VA bass + all three effects
  (`--debug`: `pk_part0_us`, `pk_block_us`, `pk_late`, `stale_halves`), then the same with a P5 bass; **it may go
  back to 4 units**. Where this document says "three voices with a bass" further down, that is c176dd4's four units.
  With Options > Dual Core On each part has its own 16: four and the bass.
- **EDIT 1 / 2** are Melodee's eight offsets on the program (CUT RES TUNA TUNB PWA PWB MIXA MIXB), 0 in every preset;
  the editor shows the deep pages instead.
- `FELUCCA_P5_FACTORY` (default 1): Sequential's programs in the build, the ones `assets/prophet5-factory/shipped.txt`
  names ([The shipped list](#the-shipped-list)). **0: INIT alone** (no factory data in the image;
  `tests/run_prophet_test.sh` builds and plays it). Whether a release carries Sequential's data at all is the owner's
  decision: the data are Sequential's, no licence granted (LICENSING.md).

### Voices

The owner, 2026-10-13: "lets split main P5 across both cores so we can get up to 8 voices of P5, 4 per core. If the
bass is using P5, then it can use up to 4 [voices], but if main chord needs 6 voices, and bass has 4, then priority
goes to the main chord engine. For Single Core, limit P5 bass to using max of 1 voice."

**Who gets how many voices** (`eng_prophet_test.c` "THE VOICES", `p5_cap`, `p5_units`; `voice.c p5_pool_*`; the
split: `fx.c p5x_run`; all measured by `tests/cr_prophet_test.c`, through `mix_block`):

| Options > Dual Core | the bass part's engine | the chord part (P5) | the bass part | which core renders |
| --- | --- | --- | --- | --- |
| **On**, core 1 running | none sounding | **up to 8** | | shared out every block: core 1 takes the odd one (1: 0 + 1; 4: 2 + 2; 5: 2 + 3; 8: 4 + 4), never more than 4 a core |
| On | **P5** | up to 8, **first**: chord + bass together are at most 8; **at most 7 while a bass note sounds** (3y53) | **up to 4** (a UNISON key's stack; a MONO program one), as far as the chord leaves them: 4 under a chord of up to 4, 3 under 5, 2 under 6, **1 under 7 and under 8: the bass always keeps one voice** (3y53) | the bass's voices: core 1, always. The chord's: core 1 up to four with the bass's, the rest core 0 (6 + 2: core 0 four, core 1 two and the bass's two) |
| On | FM6, VA, CZ-1, FM TONE, WHEEL: one voice (MONO, as ChoralRoot's bass is) | up to 8 | its own budget of 16 units, as before | the bass part: core 1. The chord: 4 + 4 at eight, and below that core 1 takes the smaller share when the bass sounds (4: 2 + 2; 5: 3 + 2; 7: 4 + 3) |
| On | another engine with many voices (a UNISON VA bass: 8) | 8 less one for every 6 budget units of the bass's sounding voices (16 units: 6 chord voices) | its own 16 units | the bass part and up to 4 - that many chord voices: core 1 |
| **Off**, or core 1 not started, failed or given up | none sounding, or another engine | **up to 4** (three budget units each: 4 x 3 + a VA or FM6 bass's 2 = 14 of the one core's 16) | the shared 16 units, as before | core 0 |
| Off | **P5** | **3 while the bass sounds**, 4 when it does not | **1** (a UNISON program too: one voice, at its stack's level, [Level](#level-3y46)) | core 0 |

- **The chord first, but the bass always has one voice** (two cores; 3y53, the owner: "I would like to enforce
  that the Bass always has at least 1 voice reserved for it."). A chord note that finds the pool of eight full
  takes, in this order: a released voice of either part (the oldest); a voice of the bass's stack, from the top (the
  stack thins towards one, never below it); only then one of the chord's own (the steal below: the oldest held voice
  that is not the lowest note). A bass key takes a released voice, else the chord's oldest held voice that is not
  its lowest note: eight chord keys and a bass key sound seven chord voices and one bass voice, and the chord note
  that goes is the oldest held one that is not the lowest (keys 48 .. 62 struck in order: 50). A stack's extra voices only start into free room,
  and come back with the next bass key once the chord has let go. The voices of a thinned stack are raised to the
  stack's level, so the bass does not drop in level as the chord grows ([Level](#level-3y46)).
  (`-DP5_BASS_KEEP` is gone: 3y53 made its rule the only one.) A bass of another engine is not in this pool's
  reckoning for its own voice: with two cores it runs on core 1 in its own 16 budget units; with one core it shares
  the 16 units and the budget never takes a part's last voice (a POLY part's lowest held note, a stack's lead). The
  overload shed (`audio.c shed_voice`) never takes it either: a released voice first, then a P5 bass stack's extra
  voices (not its lead), then a held voice that is not a part's lowest note or lead.
- **One core: four P5 voices in all** (`P5_ONE_POOL` 4). The owner's rule is only "max of 1 voice" for the bass.
  Built beyond it: with a P5 bass sounding the chord part has three. Why: five P5 voices on one core are the
  readings that ran late (2814 us of 2902 from flash, 17 late halves; P5 on both parts with three effects about
  3050 us by the second reading, 105 %), and a chord that is four voices for 6 ms and then loses one to the overload
  shed at every strike is worse than one that is three from the start. The bass's one voice comes before the
  chord's fourth (a bass under every chord is how ChoralRoot plays). `-DP5_ONE_POOL=5`: four and one, the owner's
  words alone (built, `tests/run_prophet_test.sh` builds and runs it).
- **A chord struck over its own release** (a progression, the bass's key in the chord's block): every chord note
  sounds and the bass has what is left (six-note chords: two of its four; seven: one; eight: seven chord notes
  and the bass's one, 3y53). Found by the listening
  files, where with every slot of the part still sounding or fading the eighth note took one of its own chord's
  new notes while the bass kept a voice: a note now waits for a slot that is fading before it takes a held voice
  of its own part (`voice.c voice_alloc`, P5 alone; `-DP5_WAIT_SLOT=0` is without it and the test's progression
  then lost 3 chord notes in 8 chords; since 3y53, the bass keeping its one voice, it loses none there either).
- **More keys than voices** (the rule is voice.c's, as for every engine): the new note takes **the oldest held
  voice that is not the lowest held note**. That voice fades as a whole voice over `P5_STEAL_BLOCKS` (2) blocks and
  the note starts after it ([A stolen voice](#a-stolen-voice)); while it fades it still counts in the pool, so there
  are never more than eight whole voices (four with one core). The fade's state is the voice's own
  (`p5_voice_t.steal`), so it does not matter which core renders the fading voice: tested (an eleventh key on eight
  held: its victim's two fade blocks rendered by core 1, the note in block 3, never more than four voices on a core).
  Measured: of ten keys from the bottom up the bottom note and the top seven sound; with one core, of six keys the
  bottom and the top three (as before).
- **A UNISON key is four voices at most** on either part with one core or two (`P5_STACK`; the owner asked for
  eight voices of chords, not for a stack of eight). The editor's VOICES cell is 1..4 as before.
- **Core 1 lost while notes sound** (a job that does not answer in 8 ms, a fault about core 1, a second misread:
  docs/DUALCORE.md "The fail-safes"): the block in which it is given up is rendered whole by core 0 (the same job
  function, the samples of one core; late, once), and the next block has the one core's counts: the chord's voices
  above four are given up (released ones first, then the ones in the slots a four-voice part does not use, then the
  oldest that is not the lowest note), a bass stack keeps one, the chord three with it. `voice.c p5_pool_enforce`
  does that once a block, before the block's notes, and so also covers a cap that changed under held notes. Tested
  for all three ways of losing the core: no voice stuck, the chord's lowest note kept, a key after it steals as one
  core does, everything free after the release.
- **Options > Dual Core turned while notes sound** changes nothing (it is read at power-on); **the bass part's engine
  changed under held chord keys** (to the VA and back to P5): no voice stuck, the pool's counts hold at every step.
  Both tested.
- The looper's loop voices play on the chord part and count as its voices.

**The budget's units.** One core: three of the sixteen a voice, as since 3y40 (so that a VA or FM6 bass does not
cost the chord a voice). Two cores: each part has its own sixteen (docs/DUALCORE.md), and the chord part's are eight
voices: **two units**. How many P5 voices the two parts may have together is not the units' to say: that is the pool.

### UNISON

**A load takes the program's UNISON switch**, on the chord part and on the bass part (the owner, 2026-10-13: "loading
a preset should load the Unison"; 40 of Sequential's 200 have it on, 5 of the provisional 48). INIT is poly.

**What a 4-note chord does on a UNISON program** (`tests/cr_prophet_test.c`, measured on the host): **one note
sounds**, on the program's stacked voices.

- *Which note*: the program's key priority (the VOICE page's Priority). **LOW** (LOW, LOW R: most of them): the
  chord's **lowest note**, D4 of D4 F#4 A4 C#5. **LAST** (LAST, LAS R): the **last note the chord engine sent**,
  which is the top one, C#5, in its plain bottom-up order.
- *How many voices*: the program's VOICES count, **four at most** here (Sequential's count goes to five, and its 0
  means all: both play as four). Of the provisional list: LAGOON BASS, UNISAWYER, UNISON GLIDE four, FEATURE BASS
  four (stored 0), BASSMENT one (a mono bass). They are spread by the program's DETUNE (0 on most) and its Vintage.
  **Since 3y46 a stack's voices start together** (both oscillators at phase 0) and the voices that sound are raised
  to the whole stack's level when the budget gives fewer: [Level](#level-3y46).
- *The cost*: a voice is a voice. Four stacked voices are **the four voices of a four-note chord: all 16 budget
  units, and the same processor time** (about 1630 us of the half by the unit's reading, below). A UNISON program
  costs no more than a full poly chord, and never more than the cap. With a VA bass sounding on one core the one note
  keeps its four voices (measured on the host; three units a voice: 12 + 2. With four units: three).
- **3y46: on the bass part a UNISON program is ONE voice with one core, and up to four with two, as far as the
  chord leaves them ([Voices](#voices)).** What follows was 3y45's (three units a voice, no pool):
- On the **bass part** a UNISON program stacks up to four voices on the bass note while the chord part is silent
  (all 16 units). **Under a chord on one core the stack gives way**: measured with LAGOON BASS as the bass and a
  4-note chord, in either order of arrival: **the bass note keeps one voice and the chord has all four of its notes
  (five P5 voices, 15 units: three units a voice; with c176dd4's four units the chord had three)**,
  which is what a mono bass leaves the chord too (a stack's extra voices are the first the budget takes, and are
  only started when there is room: voice.c). So a UNISON bass is thick alone and a plain mono bass under a chord; it
  does not starve the chord (the reason cd43720 gave for leaving UNISON off a load does not hold at four units a
  voice). With Dual Core On the parts do not share: the bass keeps its four.

How the part's voice mode is kept straight (`eng_prophet_test.c p5_track_loaded`, `p5_own`, `p5_voice_mode`,
`p5_leave`, `p5_voice_restore`):

- every sound load sets the part's mode first (POLY on the chord part, MONO on the bass: `cr_ui.c cu_load`), then
  the engine's load hook makes it UNISON when the program says so. So **the next program without the switch is POLY
  again** (MONO on the bass), and **another engine's sound finds the part as it expects it**.
- the editor's **UNISON cell** switches between UNISON and the part's own mode, as before.
- the editor's engine switch (EDIT held: it keeps the part's mix, and so its voice mode) takes a UNISON that a P5
  program gave off again; a UNISON the player set on the MIX page of a program without the switch stays, as between
  any two engines.
- **the session** keeps the mode as it was left (a UNISON program: UNISON; one with UNISON turned off in the editor:
  POLY) and brings it back after a power cycle (`cr_settings.c css_part_put`; `tools/emu/test_persist.sh` powers on
  with a UNISON program). The engine picker's cancel does the same with its snapshot.
- **held keys do not hang**: every load releases the part's notes before the next block (`panic_req`), in both
  directions (tested: a chord held over UNISON -> POLY and POLY -> UNISON; no voice left, no mono stack left).

### Size (`./build.sh`'s size line)

| build | XIP (of 573 440) | RAM (of 98 304) | POOL (of 344 064) |
| --- | --- | --- | --- |
| before (aa5875a) and `FELUCCA_PROPHET=0` | 483 396 (84.3 %) | 81 332 (82.7 %) | 315 500 (91.7 %) |
| the engine, INIT only (`FELUCCA_P5_FACTORY=0`) | 500 868 (87.3 %) | 81 348 (82.8 %) | 316 052 (91.9 %) |
| the engine and the provisional 48 | 505 588 (88.2 %) | 81 348 (82.8 %) | 316 052 (91.9 %) |
| the engine and all 200 (`P5_SHIPPED=all ./build.sh`) | 519 508 (90.6 %) | 81 348 (82.8 %) | 316 052 (91.9 %) |
| 8472e70 (3y45), the 48 | 511 412 (89.2 %) | 81 732 (83.1 %) | 316 064 (91.9 %) |
| **3y46, the 48: eight voices on two cores, the pool, the level** | **515 076 (89.8 %)** | **81 748 (83.2 %)** | **316 900 (92.1 %)** |

The engine is 17 472 B of flash (code about 8 KB of which the sample loop 3350; the nine tables 7430, the largest
`P5_TANH_Q15` 4098; the pages). A shipped program costs **93 to 98 B** (its packed bytes, 53 a program with these 48
and 55 with all 200; its `preset_t` row and 12-character name; its number): 48 programs 4720 B, 200 programs
18 640 B. RAM +16 B (the parts' flags; the pool read-out's name field, below). POOL +552 B (the four programs); the
four-voice state is smaller than the five-voice one but lives in a union of FM6's size, so POOL is the same.
`P5_RAM` (1 in a device build since 3y41): RAMTEXT 3016 -> 6808 of 24 576, XIP the same (the code is in the image
either way). 3y46: RAMTEXT the same 6808 (the sample loop is 8a3074b's but for the level); XIP +3664 (the pool, the
split, the stack's level, the counters); RAM +16; POOL +836 (the list of a block's voices 708 B and core 1's buffer of
the chord's voices 128 B: in the pool, RAM being the tight region); `p5_part_t` is 1172 B (eight voices' state) in
the union of FM6's 6884. `CR_RAM_TABLES_P5=1`: RAM +5376 B (86 820).

## Sounds

`PRESETS` on P5 turns its pool as on any engine (docs/PRESETS.md): **00 INIT** (one saw, the filter open), then the
**shipped factory programs** in the list's order (the owner's 115 favourites since 3y51: 01 ITS PROPHET5 .. 115, by Sequential's number). The names
are the programs' in upper case, shortened to PRESETS' 12 characters where needed (88 of the 200:
`tools/gen_prophet_factory.py SHORT`; the program keeps its 20-character name in its bytes). The pool read-out cut a
12-character name's last letter (`cu.pop.sub[12]`: "ENSEMBLE ST", "FOREVER KEY"); with P5 in the build the field is
13 (with `FELUCCA_PROPHET=0` it is as it was, for the byte-identical image).

### The shipped list

The owner, 2026-10-13: "I'd like to trim down the presets it ships with, maybe in the device manager you can have
option to load up all 200". Which of Sequential's 200 are compiled in is **data**:
**`assets/prophet5-factory/shipped.txt`**, one program number (1..200, the program's place in Sequential's file) a
line, comments after `#`, in the order of the pool. No file, or the word `all`: all 200.
`tools/gen_prophet_factory.py` reads it at every build; everything else is sized from what it generates
(`P5_FACTORY_N`: the engine's preset count, the pool, the tests, `va_levels p5`, the regression's entries).

**A program's identity is its Sequential number**, not its place in the list. Every factory program carries that
number in its own 138 bytes (a program dump's group and program bytes; `p5_patch_id`), an edit does not touch it, and
the generated `P5_FACTORY_ID[]` maps place to number. What refers to a factory program, and how:

| record | what it holds | when the list changes in a later build |
| --- | --- | --- |
| **the session** (`OBJ_SESSION`, a part's sound at power-off: `css_part_t`) | the whole program, edited or not (the 138-byte blob, with its number in it), its name, the part's parameters; and the place it had in the pool (`preset`, `fk`) | **the same sound, always**: the program is the record's own bytes. Its place is looked up again from the number (`css_part_put`): the program's place in this build's pool, or, when this build does not ship it, no place: it plays from the record, shown at the pool's INIT position and marked edited, and PRESETS steps on from 00. (cd43720 trusted the stored place: a list changed would have shown the wrong position, and a place past the pool's end would have dropped the record.) |
| the settings block's `chord_sound` / `bass_sound` | the engine and a **pool position** (a byte), as for every engine | used only when no session record is there (the session is written with it by the global save; a unit that has only the block is a case not seen here): then the part comes up on whatever stands at that position now. Not changed: the block's format is every engine's, and for the others the position is not stable either (a user preset saved or deleted moves it). |
| the settings block's per-engine pool positions | nothing for P5 (it has no slot, [Open decisions](#open-decisions-the-owners)) | nothing to go wrong |
| user slots, backups | nothing for P5 (no store yet) | phase B: the store must hold the 138 bytes (they carry the number) |

Tested: `tests/run_prophet_test.sh` builds the test with three lists (the tree's, all 200, seven programs in an order
of their own), writes the session's record of every program with one and reads it with the others: the same program
comes back each time, at that build's place for it or from the record alone; and a program renders the same samples
in every build that ships it.

**To change the list** (a one-file change):

1. edit `assets/prophet5-factory/shipped.txt` (`python3 tools/gen_prophet_factory.py --list all` prints the 200 with
   their numbers and a `U` for UNISON; `--list` prints the list as it stands, with the places);
2. `./build.sh` (any build generates the header again; the generator prints the count and any two programs of the
   list within 8 of the 55 panel bytes of each other);
3. `python3 tools/p5_goldens.py`: P5's lines of `tests/golden.txt` and `tests/cpu_baseline.txt` made again, every
   other line left byte for byte (it refuses to write if a render of another engine moved). The entries are named
   by the program's number (`preset/P5/044_CARS_STRINGS`), so a program that stays keeps its line and its hash;
   and (since 3y80, the device manager's P5 tab) `./build/host/sound_templates`' `"factory"`, `"factory_first"` and
   `"factory_ids"` lines pasted into `web/fm1sounds.js` and `tools/fm1_install.py` (`FACTORY_PRESETS`,
   `FACTORY_FIRST`, `FACTORY_IDS`: its `--check` in `tests/run_cr_tests.sh` fails until they hold the new list);
4. `CR_VERSION=0.22 sh tests/run_all.sh`; then this file's table below and the size table
   (`python3 tools/gen_prophet_factory.py --list`).

The emulator's tests read the names of the places they visit from the generator (`test_cr.sh`: places 01, 03, 04;
`test_persist.sh`: the list's first UNISON program), so they follow the list; they need at least 15 programs.
What does not follow by itself: `tools/emu/scripts/perf_p.txt` (the step to the bass, BASSMENT since 3y51: its place + 1)
and the examples in this file.

**The shipped list: the owner's 115** (2026-10-13). He played all 200 of Sequential's programs on the P5 engine (a build with `P5_SHIPPED=all`) and marked his favourites on the unit; the marks were read off it (read-only, `fm1-backups/p5-favorites-2026-10-13-all200.txt`) and written with `p5_shipped_from_favorites.py --by-number` ([below](#the-shipped-list-from-the-units-favourites)). His decision: "We should only ship with the presets I favorited. The other presets can be optionally loaded from the website." (2026-10-14 the owner decided they will NOT be hosted on the site: closed, HISTORY 3y55). The list is by Sequential's number, so place = row; "his type" is the favourite type he gave the mark (Both: 83, Chord: 21, Bass: 11). 18 of the 115 have UNISON on. It replaces the provisional 48 (chosen from parameters, not by ear; 4 FUNK BASS II, the old perf bass, is not in it).

| Sequential's number | PRESETS name | his type |
| --- | --- | --- |
| 1 | ITS PROPHET5 | Chord |
| 2 | AFTER RINGER | Chord |
| 3 | FOREVER KEYS | Both |
| 6 | STABBY BRASS | Both |
| 7 | NO WHINING | Both |
| 8 | FANTASTIC 5 | Both |
| 10 | PDG ORGAN | Both |
| 11 | CLAVICLE | Bass |
| 12 | BLOW BY BLOW | Both |
| 13 | TAARICHOIR | Chord |
| 15 | REEDY STRPAD | Chord |
| 16 | CANNED HAPPY | Both |
| 17 | SYNCHROTRILL | Both |
| 18 | BORN IN 80S | Both |
| 19 | STICKY PLUCK | Both |
| 20 | WATERY PLUCK | Chord |
| 21 | BASS FACE | Both |
| 23 | WHINY OPENER | Both |
| 24 | SYNCTINK | Both |
| 25 | TREMBL ORGAN | Both |
| 26 | SCI-FI ECHO | Bass |
| 27 | UNICORN DRMS | Both |
| 28 | COCKTAIL HR | Both |
| 29 | NICE SMOOTH | Chord |
| 30 | TUMESC BASS | Both |
| 31 | INTERNALIZED | Bass |
| 32 | BLINKR BELLS | Both |
| 34 | UWATER TRILL | Both |
| 41 | POLY MCFAT | Both |
| 43 | REED EP | Both |
| 45 | PIPE ORGANIS | Both |
| 46 | FAT POLYBASS | Both |
| 47 | CHORAL VOICE | Both |
| 51 | HUM-ALONG | Bass |
| 53 | AFTRBLW HORN | Both |
| 54 | MIDNIGHT PWM | Both |
| 56 | RIPPLES | Both |
| 59 | RADIO TOWERS | Chord |
| 60 | HARPSI-VIBE | Chord |
| 61 | INSTABIL PAD | Chord |
| 62 | VINTAGE WRLY | Both |
| 65 | PRESSURE PTS | Chord |
| 66 | 80S HORROR | Both |
| 67 | NURSERYCOMP | Chord |
| 68 | HOUSE MOVER | Bass |
| 70 | VOCAL PAD | Chord |
| 72 | SQUARECLIFF | Both |
| 77 | UNDER ICE | Chord |
| 79 | MARIMBA | Chord |
| 82 | UNISAWYER | Both |
| 83 | RECORDER | Both |
| 85 | GHOSTLY GLOW | Chord |
| 86 | UPTIGHT BASS | Both |
| 87 | RAVECAVE | Bass |
| 89 | CELTIC MOON | Both |
| 92 | MOUSE BLASTR | Both |
| 93 | KYOTO | Both |
| 95 | ORGAN PERC | Both |
| 97 | DRM ORGONON | Both |
| 100 | PLUCK HOLLOW | Both |
| 101 | VELO RES LD2 | Both |
| 103 | REGAL MOVMNT | Both |
| 104 | LOOSE SQUARE | Both |
| 107 | RUBBR BUMPER | Both |
| 108 | GALV BUCKET | Both |
| 109 | SOFT SWELL | Both |
| 110 | REVERB STRGS | Both |
| 111 | LAGOON BASS | Both |
| 112 | PLUCKTY DUCK | Both |
| 113 | BELL TREE | Bass |
| 114 | RISER CHORD | Both |
| 115 | TIMPANALLY | Both |
| 117 | TUNED PERC | Both |
| 120 | FUNKY BELLS | Both |
| 122 | FEDBACK | Both |
| 123 | NYLON FINGRS | Both |
| 127 | FAINT MEMORY | Bass |
| 128 | FULL HEXAGON | Both |
| 131 | ICONIC BRASS | Both |
| 132 | SYNC SWELLPD | Both |
| 133 | PRETTY PLUCK | Chord |
| 135 | FEATURE BASS | Bass |
| 136 | CHORUSLAYER | Chord |
| 137 | BETTER WAYS | Chord |
| 139 | BASSMENT | Bass |
| 140 | DENOUEMENT | Chord |
| 142 | SYNCWAVE | Both |
| 143 | ORGANIZER | Both |
| 146 | BULL BY HORN | Bass |
| 147 | NAIVE MELODY | Both |
| 148 | POLY MORGUE | Chord |
| 150 | SWITCHBACK | Both |
| 152 | PLUCKYNESS | Both |
| 156 | CYBERIAN WND | Chord |
| 159 | METAL DRONE | Both |
| 162 | LOW STRINGS | Both |
| 164 | PERC E PIANO | Both |
| 165 | FLUTES | Both |
| 168 | PERC ORGAN | Both |
| 169 | UNISON GLIDE | Both |
| 171 | ORGAN W RES | Both |
| 172 | TOY PIANO | Both |
| 173 | TRUMPT FLUTE | Both |
| 179 | ELEC PIANO I | Both |
| 181 | OCTAVE SAWS | Both |
| 183 | DELAYED HARM | Both |
| 184 | ECHO REPEAT | Both |
| 185 | PULSE W MOD | Both |
| 187 | 4THS W RES | Both |
| 188 | SWEEP HARMS | Both |
| 189 | SLOW SYNC | Both |
| 194 | NOISE SWEEP | Both |
| 198 | RES BELLS | Both |
| 199 | HOLLOW SOUND | Both |
| 200 | CAT | Both |

**Sends.** The factory programs are dry, as the instrument. Nothing was added for that: since 0.18 the sends are the
PART's, not the sound's (`cr_bank.c param_kept`), so a P5 program plays with the part's reverb, chorus and delay
as they stand (a new unit: 35 / 45 / 25), exactly as every other engine's sound does.

**Level.** `build/host/va_levels p5` (tests/va_levels.c): a 4-note chord (D4 F#4 A4 C#5) alone, every send 0, the
RMS of 0.3..5.9 s after the master, at the parts' default level, every program played POLY (the tool sets the part's
mode: the five UNISON programs are measured as chords here, not as they load):

| | median | quartiles | examples |
| --- | --- | --- | --- |
| P5, INIT + the provisional 48 | −22.9 dB | −26.5 .. −20.2 | ITS PROPHET5 −20.1, FOREVER KEYS −19.2, REEDY STRPAD −20.7, CARS STRINGS −20.7, STABBY BRASS −25.5, SYNCHROTRILL −18.7, BASSMENT −15.8 (the loudest), UNISON GLIDE −33.9 |
| (P5, INIT + all 200, five voices: cd43720) | −25.2 dB | −31.4 .. −21.2 | |
| VA, 21 chord presets | −23.5 dB | −25.0 .. −22.7 | LUSH PAD −23.3, SLOW STRINGS −21.8, SYNTH BRASS −21.4 |
| FM6, 19 presets | −36.9 dB | −57.8 .. −28.7 | ORGAN −25.3, STRINGS −26.6, PAD −29.0, TINE EP −32.0 (most of FM6's decay within the window) |

The 48's median is 0.6 dB over the VA's (the list has fewer percussive programs than the bank); none touches the
limiter with four notes. **No trim was applied** (the rule: only beyond about 4 dB of the median). Not measured: a
UNISON program as it loads (one note on four voices), six keys with a bass against the limiter (the VA's own test).

### The shipped list from the unit's favourites

The owner's way to choose the list by ear (2026-10-13): flash a build with all 200, mark the programs worth keeping
(SAVE, KNOB 4; or EDIT, PRESETS to browse, KNOB 4 on each), then read the marks off the unit.

```
P5_SHIPPED=all ./build.sh                                   # the build with all 200 (flashed by the orchestrator)
# .. on the unit: OPT + PRESETS to P5; SAVE, KNOB 4 right on a program; or EDIT, PRESETS, KNOB 4 while browsing ..
python3 tools/fm1_install.py --favorites > build/scratch/favorites.txt     # read-only; a moment after the last mark
python3 tools/p5_shipped_from_favorites.py build/scratch/favorites.txt             # the new list, printed
python3 tools/p5_shipped_from_favorites.py build/scratch/favorites.txt --write     # .. into assets/prophet5-factory/shipped.txt
./build.sh && python3 tools/p5_goldens.py && CR_VERSION=0.22 sh tests/run_all.sh
```

`--favorites` prints P5's marked programs by Sequential's number, one a line with the name as a comment (`44     #
CARS STRINGS`); the other engines' marks are comment lines, so the file is a valid list as it is.
`p5_shipped_from_favorites.py` orders it: **a marked program that is in the present list keeps its place, its comment
and its group heading** (the list is grouped by hand: pads, brass, keys ..); **the marked programs that were not in
it follow under one heading, by Sequential number**; `--by-number` drops the groups. The unit knows no categories, so
new programs have to be moved into a group by hand. The marks stay on the unit and name the same programs in the
build with the shorter list (`tests/run_prophet_test.sh`: marked with one list, read with another).

## The editor

EDIT on a P5 part shows **the owner's rows** (2026-10-15: "This layout now checks out, I updated the label text in
editor. We are good to build and get into the firmware."): his design `design/layouts/designs/p5-rows.json`, compiled
into the firmware from the design file (`tools/gen_layout.py`; docs/LAYOUT-TOOL.md "Step 4"; the screens and their
lanes: docs/EDITOR.md §4 "P5"). Eleven screens of up to four cells a row, the editor's own rows (the row bars, the
labels on top, the hot block), each cell drawn as the Prophet-5's control: the ticked knob, the LED button with its
wave's icon, the two-LED REV button, the KEY TRK lever. OSC (OSC A with the mixer, OSC B), FILT (FILTER under the
filter's curve, ENV 2 under its envelope, ENV 2 +), ENV (ENV 1 under its envelope, ENV 1 +), LFO (the LFO with its
wave buttons on one row, WHEEL), MOD (POLYMOD, VOICE, then the EXPR screen every engine has: docs/EDITOR.md); FX and
MIX the platform's. A cell is one raw byte of the program, named and ranged as Melodee's panel (`prophet_edit.h
P5_PANEL`); turning it writes that byte only. The engine's 19 deep pages are still its tables (`get` / `set`, the blob,
SysEx and the layout file's hash address them); the rows are pages of their own whose columns go through to them
(`cr_pages.c` `cp_deep`), and they bind every one of the 56 controls that change the sound once:

| Group | Screen | KNOB 1–4, a lane a row |
| --- | --- | --- |
| OSC | `OSC A` | FREQ · SAW · PULSE · P WIDTH / SYNC · MIX A · MIX B · NOISE (the mixer) |
| OSC | `OSC B` | FREQ · SAW · TRI · PULSE / FINE · P WIDTH · LO FREQ · KEYB |
| FILT | `FILTER` | CUTOFF · RES · ENV AMT · REV (1/2: the SSI model, 3: the Curtis; two LEDs), under the filter's curve |
| FILT | `ENV 2 · filter` | ATTACK · DECAY · SUSTAIN · RELEASE, under the envelope |
| FILT | `ENV 2 +` | VELOC (ENV 2) · – · KEY TRK (Off, Half, Full: a lever) · AFTER (the filter's aftertouch) |
| ENV | `ENV 1 · amp` | ATTACK · DECAY · SUSTAIN · RELEASE, under the envelope |
| ENV | `ENV 1 +` | VELOC · REL SW (the RELEASE switch: both envelopes') · – · – |
| LFO | `LFO` | FREQ · SAW · TRI · SQUARE / INIT AMT (the wheel's initial amount) · AFTER |
| LFO | `WHEEL` | SRC MIX (the LFO's noise amount: the wheel-mod's source mix) · FREQ A · FREQ B · PW A / PW B · FILTER |
| MOD | `POLYMOD` | FILT ENV · OSC B (the two amounts) · FREQ A · PW A / FILTER (the three destinations) |
| MOD | `VOICE` | UNISON (it switches the part: [UNISON](#unison)) · VOICES (1..4) · DETUNE (0..7) · GLIDE / PRIORITY (Low, Low R, Last, Las R: R retriggers) · VINTAGE |

From 0.22-beta1 until this, the editor was the owner's earlier panel (`p5-panel.json`: free screens framed as the
Prophet-5's front panel); the owner then asked for "a up to 4 parameter per row philosophy" with "the same row
highlighting behaviour we use in the other engine's UI" (docs/LAYOUT-TOOL.md "Cell styles"), and laid the rows out
himself. Not tested on a unit yet.

The 57th panel byte, BEND (the program's own bend range), has no cell: the part bends by the channel's range. A switch
byte other than 0 reads "on", VOICES 0 (as most factory programs have it) reads 4, and so does a stored 5: what plays. Value names are at most
five characters (a cell's width: Melodee's "CURTIS" and "LOW RET" would be cut). No matrix destination of its own
("not modulatable"); the platform's routes still act on the pitch, the cutoff, the widths and the level. An edit marks
the sound edited (`REEDY STRPAD*`) and is kept by the session over a power cycle (the 138-byte blob,
`eng_deep_t.blob_get`: `cr_settings.c css`), like any engine's.

## Saving

**Since 0.22 (the sound pool, docs/PRESETS.md "The pool") a P5 sound saves as every engine's does.** SAVE opens the
dialog with its five entries (Overwrite, Save as new, Favorite, Favorites filter, Favorite type); Overwrite or Save as
new (with the name editor) writes the record and **the whole 138-byte program** (`p5_patch_t`, edited or not: the
part's program as it is) into the slot, one slot of the 128. Loading that user sound restores the program byte for
byte and, as any load, its UNISON switch (`p5_track_loaded`: `p5_user_pending`, `p5_store_read`). P5's user sounds
follow the factory programs in P5's pool as other engines' do; a program change, the session, the favourites (a P5
user sound's key is its slot; an Overwrite of a factory program keeps that program's Sequential number) and the
filter treat them as any user sound. The Sound Manager exports and imports a P5 user sound as a sound file (patch
kind `p5`); the device manager has a P5 tab since 3y80 (the programs, P5 user sounds over them or added, the
favourites' stars: docs/DEVICE-MANAGER.md "The P5 tab"). Sequential's program SysEx is not imported (not done: `prophet_patch.c p5_patch_decode` decodes it, the
manager's path for it is not written). A 0.21 FM-1 has no P5 store: the tools refuse a P5 sound for it.

What follows is the reasoning of before the pool (0.22's first builds), kept for the record.

**Nothing of a P5 part is saved to a slot.** Since 0.22's favourites the SAVE tap opens the menu with **the three
favourites' entries alone** (Favorite, Favorites filter, Favorite type; `cr_ui.c cu_save_favonly`): KNOB 4 marks the
program by its Sequential number, KNOB 3 types it, KNOB 2 is the filter (docs/PRESETS.md "Favourites"); SELECT
cannot reach the two saves; **OCT+ is refused**, "P5: no save yet", and nothing is written (until c176dd4 the tap itself
was refused). Why not the
remix's scheme (57 seven-bit values in the 32-slot record's tail, bytes 91..143, marker 'P','5'):

- the bytes are free today (`up_rec_t.packed[144]` holds `P_COUNT` = 91 values; `up_valid` reads no further; `cb_store`
  zeroes the rest), but with no headroom at all: the scheme's first byte is 91, the next common parameter would land
  on it;
- it keeps 57 of the 133 bytes (the name and every byte Sequential's data has beyond the panel are lost; a value over
  127 is refused);
- **a record of engine 16 is not a record to anything older or to the web tools**: `up_valid` wants `engine <
  NENGINES`, so 0.21 and before show the slot empty, and so does today's Sound Manager (`web/fm1sounds.js recordValid`:
  `engine >= ENGINE_NAMES.length`), which then offers the slot for the next sound it places. That is a way to lose a
  saved sound, and the web manager is not part of this phase.

So the simplest thing that cannot lose data is to write none. The proper store is phase B (and the owner's plan to
load the rest of the 200 from the device manager needs it: a program that is not compiled in has to live somewhere): flash objects in the free
region as `va_store.c` (0xB6000..0xC7FFF; 32 × 138 B), backup ids, the web manager's engine list and patch kind, and
the settings record ([Open decisions](#open-decisions-the-owners)). A record of engine 16 that arrives anyway (a
restored bank) loads as P5's INIT with its parameters.

## Cost on the unit

### Measured on the unit (2026-10-13)

The owner played **cd43720** (five voices of three units, all 200 programs) on the FM-1, a five-voice chord held, and
read the counters:

| counter | reading | |
| --- | --- | --- |
| `pk_part0_us` | **2033** | the chord part's peak; the count below said 1318 for five voices of ITS PROPHET5's kind: **the count is 1.54 × low** for this engine (CZ-1's was 1.7 ×) |
| `pk_block_us` | **2814** of 2902 | the whole half: 97 % |
| `pk_late` | **17** | halves that finished late |
| `stale_halves` | 0 | none repeated: nothing was heard |

Five voices do not fit with a margin. **So the cap is four voices a part**: `P5_POLY` 4, `p5_units` 4 of the 16
(HISTORY 3y39). Four voices are **about 1630 us** by proportion (2033 × 4 / 5 = 1626; by the count's own shape, a
voice 249 us and the part 74: 2033 × 1068 / 1318 = 1648), which would put the half near 2410 us, 83 % (2814 less the
fifth voice: an inference, **the four-voice build has not been measured**). With the bass sounding on one core the
chord part had three voices at four units (about 1260 us by the same proportion); **at three units (3y40) it keeps
four: about 1630 us + the bass, by the same proportion about 2410 + 150 = 2560 us of 2902 (88 %) with a VA bass: an
inference, the thing to measure**; with a P5 bass it is five P5 voices, the 2814 us reading's class.

### Flash against RAM (2026-10-13, 40acb01)

The owner played the same thing on two builds of 40acb01 with all 200 programs: P5 on the chord part, four voices,
and P5 on the bass part, reverb, delay and chorus up.

| build | `pk_block_us` | `pk_part0_us` | `pk_part1_us` | `pk_fx_us` | `pk_master_us` | `pk_late` | `stale_halves` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| all code in flash | **3136** of 2902 | 2084 | 1414 | 717 | 187 | 83 | 22 |
| `P5_RAM=1` (the sample loop in RAM) | **2623** | 1533 | 1317 | 716 | 159 | 1 | 0 |

Four P5 voices and a P5 bass with three effects did not fit from flash (108 % at the worst half, 83 late) and fit
with the loop in RAM (90 %). The chord part is 26 % cheaper with the same instructions; the bass part 7 % (by
inference: one voice, less of the loop's time against the part's fixed work). **`P5_RAM` is 1 in every device build
since 3y41**; `P5_RAM=0 ./build.sh` is the flash build. The first row's four voices (2084) are well above the 1630
"by proportion" of the section above: that estimate was low. What the rest of the half would gain from RAM is the
experiment of docs/FX.md "Code in RAM" (packages B, C, D); P5's own per-block code (`p5_render`, `p5_env_tick`,
`p5_block`, `p5_post`: 1926 B) and its tables are still in flash.

### Counted

`python3 tools/p5_cost.py` after `./build.sh`. The sample loop (`p5_samples`) is one loop of
1012 instructions with 86 conditional branches; 17 of them test a switch of the program, the rest are a sample's own
(an oscillator's edge, a clamp, the filter's coefficients kept or set up). The tool finds the 17 from the listing (the
compiler reads the switch bytes before the loop: the code before the loop is evaluated for what the program alone
decides), fixes them for a program and counts the paths through the loop of `build/choralroot.dis`:

- **hit**: no oscillator edge in the sample, the filter's coefficients as the sample before (an envelope at rest, no
  modulation of the cutoff);
- **miss**: the same through the coefficient set-up: the cutoff moved since the sample before, as it does all through
  a filter envelope's attack and decay, and with Poly-Mod or the wheel on the filter. A pad's ordinary case: the table
  below uses it;
- **longest**: every edge and clamp in one sample (a bound, not a sample that happens).

A voice's block (32 samples): `p5_render` and two `p5_env_tick`, 512 instructions at most. A part's block: `p5_block`
445, the wheel's source 32 × 90, the DC blocker 32 × 12: 3803 (74 us a half). At 205 instructions a microsecond
(docs/FX.md), a half of 128 frames = 2902 us:

| program | a sample: hit | miss | longest | a voice a half | us | 3 voices | 4 | 4 at 1.54 × |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| no waveform switched on, Curtis (the loop's floor) | 294 | 315 | 335 | 42368 | 207 | 694 | 901 | 1390 |
| INIT: saw A, Curtis | 308 | 329 | 367 | 44160 | 215 | 720 | 936 | 1444 |
| saw A + saw B, Curtis | 325 | 346 | 402 | 46336 | 226 | 752 | 978 | 1509 |
| saw A + saw B, SSI | 361 | 382 | 447 | 50944 | 249 | 820 | 1068 | 1648 |
| saw + pulse A, saw + pulse B, Curtis | 349 | 370 | 462 | 49408 | 241 | 797 | 1038 | 1602 |
| saw + pulse A, saw + pulse B, SSI | 385 | 406 | 507 | 54016 | 263 | 865 | 1128 | 1740 |
| saw A + saw B, the wheel on both frequencies, SSI | 378 | 399 | 464 | 53120 | 259 | 852 | 1111 | 1713 |
| saw A + saw B, Poly-Mod to the filter, SSI | 375 | 396 | 461 | 52736 | 257 | 846 | 1103 | 1702 |
| sync: saw A synced to B, Poly-Mod to A's frequency, Curtis | 365 | 386 | 531 | 51456 | 251 | 827 | 1078 | 1663 |
| everything: saw + pulse A, saw + triangle + pulse B, sync, Poly-Mod to frequency, width and filter, the wheel on all five, SSI | 490 | 511 | 797 | 67456 | 329 | 1061 | 1390 | 2145 |

(The voices' columns include the part's 74 us. "4 at 1.54 ×": the count scaled by what the unit read for this
engine.) The cheapest and the dearest program are a factor 1.5 apart; the SSI filter costs 36 instructions a sample
more than the Curtis (seven table look-ups for three); sync and Poly-Mod cost little until an edge falls in the sample
(a divide and two more waveforms, once a cycle of oscillator B). Not in the table: an oscillator's own edges. A saw's
wrap costs two samples a divide and about 15 instructions each (the BLEP), a pulse twice that: at A4 some twenty
divides a voice a half, about 2 % on top if a divide is worth 35 instructions; three octaves up eight times that (an
estimate: the divide's time on this core is not known here; it may be part of the 1.54). As a check of the walk: 382
a sample plus the block's and the part's share is 422 instructions a voice and sample, and the host's count for ITS
PROPHET5 (another compiler, another machine) is 422 too.

**The three engines on one scale.**

| | a voice a half, counted from the listing | a chord | measured on the unit |
| --- | --- | --- | --- |
| CZ-1 (a two-line tone; docs/CZ1.md) | 16 240 instructions, 79 us | 8 voices: 634 us | **1066 us** (1.7 × the count) |
| P5 | 44 160 .. 67 456, 215 .. 329 us (2.7 .. 4.2 CZ-1 voices) | 4 voices: 936 .. 1390 us (5: 1152 .. 1720) | **2033 us for five voices** (1.54 × the count); four not measured |
| VA | no listing count exists (not made here) | | not measured against a count |

For the VA only the host's instruction counts compare (`tests/cpu_baseline.txt`, eight notes asked, the idle block
taken off, per sounding voice and sample; cd43720's figures): CZ-1 BRASS 1 94, VA LUSH PAD 182, P5 INIT 355, ITS
PROPHET5 422. For the two engines with a listing count the unit's count is 1.35 (CZ-1) and 0.94 (P5) of the host's,
so a VA voice is *estimated* at 22 .. 31 thousand instructions a half (107 .. 154 us): **a P5 voice is about two VA
voices and three to four CZ-1 voices; four of them are an eight-voice VA chord.** (An inference from two ratios, not
a count.)

**Melodee's own measurements** (its docs/PROPHET5_PROTOTYPE.md, on an FM-1, 2026-10-08): five voices of a harsh stress
program (sync, audio-rate Poly-Mod, a 500 Hz wheel on every destination) with distortion, chorus, delay, reverb and
USB audio running peaked at **2158 us (SSI) / 2009 us (Curtis)** of the half's 2902, no late block. Those figures are
of an older DSP (before b351a14's exponential Poly-Mod, which its author puts at +8 % on the host, and before 8a3074b),
with the sample loop and four tables in RAM, and with Melodee's effects, not ChoralRoot's. ChoralRoot's five voices
of a plain two-saw program from flash read 2033 us for the part alone: the same order, and late where Melodee's were
not. **RAM for the hot code does buy time** (3y41, below: four voices 2084 -> 1533 us); whether it buys the fifth
voice back is not measured.

**The emulator's estimates** (`sh tools/emu/perf.sh`, scenarios (p) and (q); host instructions / 259: ESTIMATES, low by
1.2 to 2.2 for other stages, docs/FX.md), a 6-key ITS PROPHET5 chord with the default sends, us of 2902, avg / max.
**These are cd43720's, with five voices of three units; not run again for four** (the chord part's column would be
about a fifth lower, and "chord voices" 3 and 4):

| | block | chord part | bass part | fx | master | chord voices |
| --- | --- | --- | --- | --- | --- | --- |
| (p) + the P5 bass (FUNK BASS II), one core | 1070 / 1700 | 616 / 990 | 161 / 321 | 215 / 460 | 23 / 95 | 4 (the budget) |
| (p), Dual Core On | 1222 / 1905 | 759 / 1194 | 159 / 431 | 212 / 427 | 25 / 100 | 5 |
| (q) + the VA bass (PUNCH BASS), one core | 1154 / 1672 | 745 / 1052 | 65 / 135 | 258 / 431 | 24 / 80 | 4 |
| (q), Dual Core On | 1338 / 1777 | 918 / 1196 | 63 / 173 | 258 / 550 | 27 / 62 | 5 |

### Does it fit?

Five voices did not, by the unit: 2814 of 2902 us with 17 late halves. With four (all figures from the one reading,
by proportion: inferences until the four-voice build is read):

| on one core | the chord part | the half, about |
| --- | --- | --- |
| 5 chord voices, no bass (cd43720, **measured**) | 2033 us | 2814 us, 97 %, late |
| **4 chord voices, no bass** (the cap) | about 1630 us | 2410 us, 83 % |
| **3 chord voices + a VA or FM6 bass** (what the budget gives) | about 1260 us | 2040 us + the bass (a VA bass about 150 by the count): 2200 us, 76 % |
| 3 chord voices + a P5 bass (one voice, 4 units) | about 1260 us | 2040 + about 500 (a P5 voice and its part, × 1.54): 2540 us, 88 % |
| 3 chord voices + a UNISON P5 bass (it keeps one voice under a chord: [UNISON](#unison)) | about 1260 us | as the row above |

- **One core (the default)**: four voices; three whenever the bass sounds.
- **Dual Core On**: the bass part's render moves to core 1 (docs/DUALCORE.md: `track_render` of `trk[1]` is the one job
  there is), so a P5 bass costs core 0 nothing and the chord part has its own 16 units: four voices. **P5's chord
  voices cannot run on core 1 today.** They could: `p5_samples` writes its own voice state and its output only, and
  reads the program, the wheel's block and its parameters (Melodee f12b310 hands every second voice to its worker
  that way); it needs a second kind of job and a private output buffer, and is not built. That, or the loop built
  per program, is the way back to five.
- **The bass part on P5** is not restricted in code. It is the dearest bass there is and takes four budget units a
  voice (sixteen for a four-voice UNISON program).
- If the unit wants less still: the loop built per program (as CZ-1's 43 loops) saves 20 to 28 % (the tool's "built
  alone" column: 276 for 361 on the two-saw SSI row: the switches' tests and what the compiler cannot drop while a
  switch may be on); the SSI filter's seven look-ups; the coefficient set-up once a block instead of once a sample
  when only the envelope moves it.

**To read next on the unit** (this tree): `pk_part0_us`, `pk_block_us`, `pk_late` with a four-key ITS PROPHET5 chord
held (expected about 1630 / 2410 / 0); the same with the bass on; a UNISON program (LAGOON BASS) on the chord part,
and on the bass part under a chord.

## The crackle (2026-10-13, 3y44: not reproduced, not explained)

**What 3y44 did fix is another fault, found on the way: [the click at a release's end](#the-click-at-a-releases-end-3y44-found-and-fixed). It is not this crackle.**

The owner, on the unit, with 74eae04 built with all 200 programs, Options > Dual Core On: **"On P5 though, preset 16 I
was able to create some kind of glitch by using minor + 6 + 9 then playing chord again as minor + 9 which had a long
crackling effect like the voice got stuck in a washing machine or something. All dry, no bass on."** The other
engines were clean in the same session.

**The program.** Place 16 of all 200 is Sequential's program 16, **Canned Happiness** (`CANNED HAPPY`): saw + pulse
on oscillator A, pulse on B (fine 26), noise 13; the SSI filter (Rev 1/2), cutoff 50, **resonance 0**, full keyboard
tracking, envelope amount 58 with velocity on the filter; filter ADSR 20 / 59 / 25 / 86, amplifier 0 / 79 / 80 / 58,
release on (silent within 3 s of a release); Poly-Mod: the filter envelope to A's frequency (5); the wheel's source a
triangle at rate 87, initial amount 8, on A's frequency and B's pulse width; **Vintage 123** (the loose end: each
note-on draws a voice's pitch, cutoff, level and envelope-time offsets anew). **No UNISON, no glide, no sync**, no
Lo Freq B; its retrigger byte is 0, which is LOW key priority and matters to a mono part only. Nothing of the kind
that self-oscillates.

**The gesture in voice.c's terms.** Minor + 6 + 9 on D4 is 62 65 69 71 76: five notes for four voices, all in one
block. The fifth takes the oldest held voice that is not the lowest note (the 65's), which has not sounded yet and
so is restarted in place. Minor + 9 is 62 65 69 76, struck while the first chord's four voices release: 62, 69 and
76 are struck again on their own sounding voices (in place, the attack from the level they are at); the 65 has no
voice, so the oldest released voice is faded over a block and the 65 waits for it (`vsq`). That voice is the 69's,
which the 69's own note-on takes back in the same block; the 65 tries again two blocks later, fades the 71's voice
and starts on it two blocks after that: **2.9 ms late, four voices, nothing else**. A 6 let go under a held root
changes nothing (a sounding chord keeps its notes).

**It does not happen on the host.** Tried, with the firmware's own sound side (`cr_out.c`'s queue, `cr_engine.c`,
`voice.c` with the fade-and-defer steal, P5, `fx.c`'s mix):

| what | runs | result |
| --- | --- | --- |
| `cr_prophet_test --gesture-sweep 16` (the host test's whole sweep, dry): four roots, play style Simple / Advanced / Free, Ext Addition both ways, Key Mode off and on, Perform off and its five modes, a unit that keeps up and two that do not (below), five ways from the one chord to the other (the 6 let go and the root again; everything let go; the 6 let go under the held root; a second root key before the first is up; the 6 and the 9 pressed into a held minor chord), the first chord held 1 block to 1.5 s, the second struck 0 to 1378 blocks (0 to 1 s; every block from 0 to 12: 32 samples is the finest timing a note has) after | 756 000 | **none broke a rule**: never more than four voices; voices faded stay bounded by the notes struck (no circle); the second chord's sustain at most 1.84 × as rough as the same chord struck out of silence (the mean square of the output's third difference; the two differ by the Vintage draws and the envelopes' phase); no step beyond the reference's; every voice free after the release and the output at rest |
| the emulator, the real keys (`G#3` MIN, `F3` 6, `C4` 9, a root): 5 roots, hold 20 ms to 1.5 s, the second strike 1 to 40 ms and to 3 s after; with the default sends (1 ms steps), and dry (2 ms steps) | 4160 + 1620 | no tail, no saturation, the sustain within 1.9 × of the reference's |
| the same with Options > Dual Core On (a flash file; the emulator's second core runs the bass part's job inline: **real concurrency is not modelled**), and with Key Mode on | 312 + 168 | the same; Dual Core On gives the same samples as Off |
| the same gesture on every one of the 200 programs | 1200 | the only programs that differ from their reference by more than 2.5 × are those with a slow filter attack (FANTASTIC 5, ANOTHER OCEAN: the second strike finds the filter already open), as it should be |
| the emulator, dry (`tools/emu/scripts/cr_prophet_crackle.txt` in `test_cr.sh`) | 1 | `wavclicks.py`: no jump, no hole, no burst inside the sound |

So nothing in ChoralRoot's wiring of the engine, in the four-voice cap or in Melodee's sample path shows a fault for
this sequence **on the host**. Melodee has no later integer fix to take: after 8a3074b its engine is float (fa64ef0);
the two integer pieces since (the DC blocker, the packed bank) are here.

**What the host cannot show, most likely first (all inference).**

1. **The deadline**: halves that end after their boundary until the DAC plays an old one (`stale_halves`: 2.9 ms of
   the sound of two halves before, spliced in). It is the one known mechanism on this unit that sounds like a "long"
   crackle or a voice "stuck" (`6_stale_every_other.wav` and `7_stale_one_in_eight.wav` in the listening folder are
   that, made by editing samples). For it: P5 is the dearest part there is (1533 us measured for four voices of
   another program), this program takes the sample loop's long way (three saws' worth of edges, noise, the SSI
   filter's seven look-ups, Poly-Mod and the wheel on A's frequency: an exponential a sample), a strike moves all four
   filter envelopes at once (the coefficient set-up every sample), and Dual Core On adds core 0's wait for core 1 to
   every block. Against it: dry and without a bass is the cheapest way to play it; the host's count ranks the program
   44th of the 200 (about 1510 us for the whole half by the emulator's estimate, which has been low by up to 1.5);
   and two halves above 85 % shed a voice, which should end an overload in 6 ms: it would have to be late without
   being over, or over for another reason than the voices.
2. **The interrupts masked by the main loop**: a flash write. The owner marks favourites in this build, and item 30
   of docs/HANDOFF.md has a 17.8 ms gap read in such a session. By the code a write waits for a second of silence and
   an erase silences the buffer first; a page program does not silence it, and a DMA left alone loops what it has
   (`storage_hw.c`: "the DMA would loop stale audio (buzz)"). `irq_gap_us`, `flash_erases`, `flash_holes` say.
3. **The shed itself**: a voice faded in 0.7 ms each time two halves are over. Modelled (below): it thins the chord,
   it does not crackle, and nothing strikes a shed voice again.
4. **A circle in the allocator** (a voice faded and taken again every other block): not found by reading, not seen
   in any run (the test bounds the fades by the notes). `voices_faded` / `voices_stolen` would count without end.
5. **The unit's samples are not the host's**: the device compiler's code for the sample loop (in RAM since 3y41) has
   never been compared with the host's, sample for sample; the stage-36 identity is a host test. A recording over USB
   decides (below).
6. **The second core**: a slow or failing core 1 (`pk_wait_us`, the `c1` state in `--debug`).

**The overload, modelled.** `EMU_SHED_OVER=N` / `EMU_SHED_AT=A:B` (tools/emu/README.md "Overload, modelled") give
`audio.c`'s `shed_check` a half of 90 % whenever N or more voices sound (or between A and B ms); the host test has
the same model (`over`). With four voices "not fitting", each chord of the gesture loses one held voice 6 ms after
its strike (three sheds, six halves over, four voices faded in all); with three, two. With every half over for
200 ms around the second strike the chord thins to its lowest note (7 sheds). **No crackle in any of them, and no
voice shed and struck again**: a shed voice is only ever started by a new key. What is not modelled: a late or stale
half (the files 6 and 7 are an edit of the output, not a model of the ISR).

**What `--debug` says since 3y44** (78 PEAK's values 35..41, cleared by the read as the others): `shed_voices`,
`shed_last_voice` (part, voice, note), `shed_last_why` (a released voice / a held one / none to give),
`shed_over_halves`, `shed_last_us`, `voices_faded`, `voices_stolen`, and a line "overload: ..". Until now
`voices_shed` was on the console alone.

**To do on the unit** (`build/scratch/p5-fav/choralroot-p5-crackle-fix-all200.fwsc`: this tree with all 200; for
the crackle it fixes nothing, it counts):

1. `python3 tools/fm1_install.py --debug` once (clears the counters); P5, place 16, dry, no bass; make it crackle;
   `--debug` again at once. Read `stale_halves`, `pk_late`, `pk_block_us`, `pk_part0_us`, `pk_wait_us`, `pk_lag_us`,
   `irq_gap_us`, `flash_erases`, `flash_holes`, the overload line.
   - `stale_halves` > 0: the deadline (1), or with a large `irq_gap_us` the masked interrupts (2); the `pk_*` stages
     say whose time it was.
   - `shed_voices` > 0 and `stale_halves` 0: the shed (3) was heard.
   - `voices_faded` in the hundreds for a handful of chords: the circle (4).
   - all quiet: the unit rendered it that way (5).
2. **Record it over USB** (ChoralRoot In) while listening to the unit's own output: the recording is taken from the
   render, before the DAC. In the recording: rendered (4, 5). Heard but not in the recording: the DAC replayed (1, 2).
3. Options > Dual Core Off, restart, the same.
4. Is the five-note chord needed? Minor + 9 twice; the same on place 15 and 17; does it stop when the keys are let
   go or with the release; is the next chord clean?
5. Which of the listening files is it like (`../audio-0.22/prophet/crackle/`, `tools/p5_crackle_listen.py`): 3 / 4
   (a note dropping out), 6 / 7 (a buzz or a rattle over the chord)?

Another branch has the automatic flash writes in hand (guess 2). **Not tested here**: anything on a unit; two real
cores; a late half; MIDI in, the looper, a bass, the effects' tails under the gesture; velocities other than 100
(host) and the emulator's fixed one.

## The click at a release's end (3y44: found and fixed)

Found by the sweep's "silent after the release" rule, on other programs than the owner's. **A fault in ChoralRoot's
wiring of the part's DC blocker** (`p5_post`: Melodee 7cd66fb's output capacitor, 5 Hz, put into integers here; "with
no voice it starts afresh"). Afresh was the block after the part's last voice ended, whatever the capacitor still
held. The oscillators are unipolar, so a chord carries the mixer's DC up to the VCA; when the VCA closes faster than
the capacitor's 32 ms, the capacitor holds most of that DC with its sign turned at the moment the last voice is
freed, and it went to 0 in one sample:

- **on 34 of the 200 programs a step above 100** (of a chord's 40 000 to 90 000), on about 20 above 1000 (a four-note chord, velocity 100), the largest
  **27 197 on 88 DIRTY LEAD, 8 dB under the chord's peak** (50 PEDAL SWEEP 11 494, 174 FILTER MOD 9016, 168 PERC
  ORGAN 7467), 10 to 130 ms after every release, into the silence: the programs with a short release (organs, leads,
  basses, clavs). In the emulator's output `wavclicks.py` counts it after each of four stabs
  (`8_click_168_perc_organ_before.wav`: 4 clicks; `_after`: 0).
- **CANNED HAPPY does not have it** (a release of 0.44 s: the capacitor holds 3 units when its last voice ends), so
  **this is not the owner's crackle**; and it comes once a release, not "long".

**The fix** (`eng_prophet_test.c`, `voice.c`, `core.h`): the capacitor discharges into the silent blocks after the
last voice and starts afresh once it is at rest (`P5_DC_REST`: under 16 output units, 70 dB under a chord: at most
0.25 s). The part stays in the mix meanwhile through a new engine hook, `engine_t.tail` (asked after `post` when no
voice was rendered: `voice.c track_render_x`; P5 alone has one); the voices are free as before, so nothing changes
for the allocator. Where the capacitor held less than 16 at the last voice's end nothing changes, sample for
sample. `-DP5_DC_REST=0x7FFFFFFF` is the old cut (74eae04's output: `tests/regress.c` built with it gives the 246
committed goldens of 74eae04, every one). FM6's `fm6_post` has the same "afresh" and is left alone: its voices carry
no DC.

**Goldens: 18 of the 49 P5 renders change, the other 31 and the 197 of the other engines do not.** A golden changes
exactly when the capacitor held 16 or more at a moment the part ran out of voices in the regression's phrase
(measured by building `regress.c` with `P5_DC_REST` at each power of two: the golden differs from 74eae04's while
the threshold is at or under what was held):

| held at a last voice's end (output units) | programs |
| --- | --- |
| 4096 .. 8191 | 168 PERC ORGAN |
| 2048 .. 4095 | 10 PDG ORGAN, 62 VINTAGE WRLY, 143 ORGANIZER, 167 SYNC I |
| 1024 .. 2047 | 4 FUNK BASS II |
| 512 .. 1023 | 6 STABBY BRASS, 11 CLAVICLE, 55 HYPER POLY |
| 256 .. 511 | 111 LAGOON BASS, 163 MUTED CLAV |
| 16 .. 31 | 32 BLINKR BELLS, 46 FAT POLYBASS, 91 FURRY TINES, 94 PINK NEON BR, 133 PRETTY PLUCK, 135 FEATURE BASS, 179 ELEC PIANO I |
| under 16: the same samples | INIT and the other 30 |

The CPU baseline (`tests/cpu_baseline.txt`) is not rewritten. Tests: `cr_prophet_test`'s "no step at the last
voice's end beyond the voice's own last ramp" over every shipped program (with the old cut: 11 of the tree's 48
programs fail it, 40 of the 200, the largest step 27 197; now the largest is 20) and "exact silence after the
capacitor's discharge" (the longest 190 ms in the tree's list, 243 of all 200);
`tools/emu/scripts/cr_prophet_relclick.txt` in `test_cr.sh` (PERC ORGAN, four stabs: no click after a release). Not
done: by ear on a unit; the capacitor's own swing after a fast release is still there, as on the instrument (it
is what a 5 Hz coupling does to a gated DC): only its cut is gone.

## The unit's second reading (2026-10-13, 3y45): the bass part, two cores, the level, the steal

The owner, a773d30 with all 200, **Dual Core On, P5 on the chord part and selected on the bass part** ("Bass is on P5
but I'm not playing a bass"; a loop may have been playing): `pk_part0_us` 1760, `pk_part1_us` 1312, `pk_wait_us`
1201, `pk_fx_us` 682, `pk_block_us` 2632 of 2902, `shed_over_halves` 377, `shed_voices` 7, `voices_faded` 375,
`voices_stolen` 393, `pk_late` 7, `stale_halves` 32 with `flash_erases` 6. Then: "voice stealing is likely an issue.
We previously did a bunch of improvements for audio quality, and some of the engines were running too hot causing
internal clipping, this could be happening here and the engine seems very loud. ... Crackle not in the recording. ...
I've switched to single core. The engine also sounds quite different on single core." Four questions came of it; the
tool for all four is `tools/p5_probe.c` (`sh tools/p5_probe.sh [cost|levels|steal] [DIR]`: the firmware's sound side
on the host with all 200 programs; not a pass / fail suite).

### An unplayed part

**Why part 1 cost 1.3 ms: it was playing.** With Bass on, ChoralRoot's bass follows every chord (cr_engine.c: a
bass note under each chord, whatever the hands do; "not playing a bass" still sounds one), and a loop's recorded
bass notes play the part too. What it then costs, counted on the host (`p5_probe cost`: fx.c's own stage marks around
part 1's matrix and render; us / half is emu.c's estimate, instructions / 259, which for this engine reads **1.6 to
1.8 x low** against the unit: the chord part's four voices are 970 here and 1533 .. 1760 there):

| the bass part on P5, the chord part on ITS PROPHET5 | voices | instructions / block | us / half (estimate) |
| --- | --- | --- | --- |
| Bass off, or Bass on and nothing played, or after the releases | 0 | 640 .. 750 (**a773d30: 2500 .. 2610**) | 10 .. 12 (a773d30: 39 .. 40) |
| Bass on, a chord held, FUNK BASS II (no UNISON) | 1 | 20 600 | 320 |
| Bass on, a chord held, LAGOON BASS (UNISON), **Dual Core Off** | 1 (the stack gives way to the chord) | 17 500 | 270 |
| the same, **Dual Core On** (the part's own 16 units) | **4** | **59 600** | **920** |
| every program as the bass, one note: the 160 without UNISON | 1 | | mean 304; three above 400 |
| .. the 40 with UNISON, Dual Core On | 4 | | mean 950, the dearest 1133 (92 MOUSE BLASTR) |

So **1312 us is a stack**: by the chord part's own ratio one P5 voice is about 500 us on the unit and four about
1500. An inference from the count (the unit does not report the part's voices), and the same shape as the reading
of 40acb01 (`pk_part1_us` 1317 and 1414). **Forty of the 200 programs carry the UNISON switch**, and a load takes it
on the bass part too ([UNISON](#unison)): with Dual Core On the bass is then four voices under every chord, a second
P5 part as dear as the chord's. Not a fault of the render; the decision is 4 of [Open decisions](#open-decisions-the-owners).

**The fault that was there, fixed: a part with no voice did the wheel's whole block.** `p5_block` computed the
wheel's 32 samples (the LFO's wave, the noise, the pitch ratio's table look-up) every block, played or not: 2500 ..
2610 instructions a block where FM6's idle part takes 670 .. 780, on the bass part of everyone who has P5 selected
there with Bass off, and on the chord part between phrases. Now a part with no voice only moves what the block
leaves behind: the LFO's phase (32 increments, mod 2^32) and the wheel's noise generator (32 steps); `value[]` is
written again before the first voice renders. **640 .. 750 instructions (FM6's own figure), and the same samples, bit
for bit**: `cr_prophet_test --idle` (every shipped program, the wheel up and on a pitch, played, silent for a number
of blocks of its own, played again, three times) gives the same digests built with `P5_IDLE_SKIP=0` (a773d30's code)
and 1 (`tests/run_prophet_test.sh`), and no golden moves. The capacitor's tail (`p5_tail`, 3y44) costs nothing once
it is at rest (`p5_post` returns before its loop).

### Two cores

**No word is shared between the two parts' renders, for P5 or for the engines that ran on core 1 before it.** What
part 1's job (`fx.c c1_part1_job` -> `voice.c track_render_x(trk[1])`) touches while core 0 renders part 0, read
through:

| state | where | whose |
| --- | --- | --- |
| P5's voices, the wheel's block, the LFO's phase, the wheel's noise, the capacitor, `value[]`, `last_pitch` | `eng_prophet_test.c` `p5_part_t`, in `engines.c eng_state[part]` | the part's |
| the program (`p5_patch[NPART]`, `p5_ready[]`, `p5_own[]`) | `eng_prophet_test.c` | the part's; written by the main loop (a load, the editor), which does not run during the audio ISR |
| a voice's noise and its Vintage offsets (`s->noise`, `drift`, ..) | `p5_voice_t`; seeded at note-on from the voice's age and index | the voice's; note-ons are core 0's, before the hand-over |
| the factory bank's unpacking (`p5_factory`) | const tables into the caller's stack copy, the main loop | no cache |
| the filter's coefficients kept (`coeff_*`), the oscillators' phases, `sync_tail`, `last_pcm`, `steal` | `p5_voice_t` | the voice's |
| the tables (`P5_TANH_Q15`, `P5_TPT_G`, `P5_INV_DEN`, `P5_EXP2_Q14`, ..) | const | read only |
| `track_render_x`: `midi_bend_q8[part]`, the part's `track_t` (voices, P_E0..7 in an engine fade), `song.g[G_TUNE]` (read) | `voice.c` | the part's |
| the matrix (`mod_tab[part]`), FM6's scratch (`fm6_scr[CR_C1_IDX]`) | `mod.c`, `eng_fm6.c` | per part since 0.15 |
| the output (`c1_buf`, `c1_side`, `c1_side_on`, `c1_nr`), the stage time `cr_dbg.cur[CRP_PART1]` | `fx.c`, `cr_cpu1.h` | core 1's alone |
| the shared generator (`libc.c rng_state`), `voice_was`, `vage`, the unison seed in `mono_play`, `vsq`, the budget's counts | note-ons, the LFOs' ticks | core 0's, before the hand-over (`mix_parts_split`) |
| `part_buf`, the sends, the chorus line, the mix, the master | `fx.c`, `cr_fx.c` | core 0's, after the wait |

**Evidence, not only reading** (`tests/run_c1_thread_test.sh`, in `run_cr_tests.sh`; docs/DUALCORE.md "Two threads
on the host"): the host's stub core can now run the job on a real second thread while the first renders part 0.
Twelve pairs of engines (P5 on both parts with a UNISON bass, P5 with FM6 and the VA, FM6, VA, CZ-1 and FM TONE as
they ran before) play a phrase with steals, the wheel, pressure, a bend and a sound change under held notes: **the
output is the inline job's, every sample, and a build with `-fsanitize=thread` reports nothing**; with a word
planted that both parts' P5 renders write it reports that word (so the run can fail). On the unit's side: core 1's
stack for a P5 voice is about 820 bytes of its 2816 by the listing's frames (`c1_part1_job` 36, `track_render_x`
208, `p5_render` 256, `p5_samples` 320; FM TONE's `quad_render` alone has a frame of 556), the build has no
multiply-accumulate register in use, and an interrupt's entry saves r0..r3, psr, rets and reti (the rest is the
callee's): nothing there that P5 alone would lose.

**What the host cannot show**: a core that reads a word wrong (Melodee's finding about this chip above its rated
clock, docs/DUALCORE.md "Reliability": `--debug`'s `c1_rejected`, `c1_faults`), the caches, the flash, the timing.
One thing in P5 would make a single wrong read LAST, and it fits "a long crackling .. like the voice got stuck":
a voice keeps its filter's coefficients for as long as its cutoff and resonance do not change
(`p5_filter_core`, `coeff_valid`), which on a held note with a resting envelope is the whole note. **An inference,
with no evidence for it**; on the host no filter state ever reaches its clamp (below: 2.1 dB under it at the worst
of 200 programs).

**Why it "sounds quite different on single core": the budget and the shed, not the render** (an inference from the
code and the counters; the emulator gives the same samples for the same voices).
- **The bass.** With Dual Core On the bass part has its own 16 units: a UNISON program is four voices there, in
  phase, 12 dB over one voice ([Headroom](#headroom)). With one core the chord's four voices take 12 of the shared
  16 and the stack gives way: **one voice**. If the bass's program has the switch, that alone is a bass 12 dB
  quieter and thinner.
- **The shed.** One core carries the chord part, the bass part and the effects in one half: by this reading 1760 +
  about 450 (one bass voice) + 682 + the master's 160: about 3050 us of 2902, where two cores had 2632 (91 %). Above
  85 % twice in a row `audio.c` fades a voice, and one more each half while it lasts: on one core a full P5 chord
  with a P5 bass and three effects **loses a held note a few milliseconds after every strike** (never the lowest),
  and halves finish late.
- **The same on two cores, less often**: 377 halves above 85 % and 7 voices shed in this reading.

### Headroom

`p5_probe levels`: all 200 programs at velocity 127, one voice (C4, C2), four voices (close at C4, wide C2 .. D5, low
at C2, high at C6) and UNISON x 4 on one note (C3, C2; the editor's switch on every program), the part's LEVEL 92
and the bank's sends (chorus 45, delay 25, reverb 35), MASTER at half, Output Clean. The largest value of the 200 at
each stage, the worst of the eight cases:

| stage | peak | its limit, and what happens there | margin | |
| --- | --- | --- | --- | --- |
| oscillator mixer, into the mixer's tanh | 84 031 | 65 536: the table's end (`P5_TANH_Q15`, tanh(2) = 0.964); beyond it the index is **clamped**: flat, no wrap | **2.2 dB beyond** | 10 programs (63 PHAZY SYNTH the most): the model's own overdrive, soft; the top 2.4 % of the curve is cut flat |
| ladder drive (input - k x feedback) | 93 777 | 262 143: a hard clamp, there so that `drive x reciprocal` fits 32 bits | +8.9 dB | never reached |
| ladder input, into the first tanh | 93 777 | 65 536: the table's end, clamped as above | **3.1 dB beyond** | 4 programs in the high chord (36 THE HATS the most), 9745 samples: soft |
| ladder state (each of the four poles) | 47 042 | 60 000: a hard clamp ("protection, not the normal knee") | **+2.1 dB** | never reached (93 KYOTO the closest) |
| filter out, after the last tanh | 22 986 | 32 767 | +3.1 dB | |
| voice out, after the VCA (x VOICE_FS x 2) | 33 534 | 65 534 = the tanh's full scale through the VCA | +5.8 dB | one voice is at most the level of another engine's loud voice |
| the part's sum, before the capacitor | 120 296 | 884 000: `mix_part_loop`'s clamp before LEVEL | **+17.3 dB** | |
| the part after the capacitor | 131 699 | 8 388 607: the blocker's state is the output x 256 in 32 bits (it would wrap) | +36.1 dB | |
| the part's bus after LEVEL 92 | 41 639 | 524 287: a hard clamp | +22.0 dB | |
| the chorus send (send 45) | 14 348 | 65 535: the chorus line stores `clamp(x >> 1)` in 16 bits, hard | +13.2 dB | at send 127: 40 500, +4.2 dB. **Both parts at 127 on their hottest sounds would reach it** (by arithmetic, not played; FM TONE's and the VA's parts are as hot) |
| the delay and reverb sends | 7 971, 11 159 | the effects shift and clamp their own input (cr_fx.c) | | not looked into further |
| the mix into the master | 62 647 | 32 bits; Clean's limiter works above 34 666 | | the limiter turns it down 5.2 dB |
| the master's out | 13 000 at MASTER half | 32 767 | | the limiter's ceiling |

**Nothing clips hard and nothing wraps, in the engine or on the part's bus, on any of the 200.** The two stages that
run beyond their limit are the model's two tanh tables, which hold their last value there: the saturation the model
intends, cut flat a little early on 10 programs. **So no trim is applied** (the rule set for this work: only a
proven hard clip or wrap), and P5's goldens do not move for the level.

**Why it "seems very loud": a UNISON program.** The engines side by side through the same harness (`p5_probe
engines`: every sound of each pool; the part's peak before LEVEL, dB re 32768):

| | one key, median / max | four notes close, median / max | UNISON on one key, median / max | the limiter at the worst |
| --- | --- | --- | --- | --- |
| P5 (200) | -4.1 / **+10.9** | +4.8 / +11.6 | **+4.9 / +11.9** | -5.2 dB |
| VA (25) | -3.9 / +3.0 | +5.5 / +10.4 (wide: +12.7) | -1.1 / +5.8 | -3.4 dB |
| FM6 (25) | -6.6 / -1.5 | +2.9 / +6.9 | +3.1 / +8.0 | not touched |
| CZ-1 (64) | -4.9 / +1.0 | +4.9 / +8.6 | +3.6 / +12.0 | -2.7 dB |
| FM TONE (16) | -4.9 / +1.1 | +5.5 / +11.6 | +3.3 / +11.5 | -5.0 dB |

A P5 voice and a four-note P5 chord sit with the others (the port's RMS reading said the same). **What stands out is
one key on a UNISON program: as loud as a four-note chord, 11 to 12 dB over the same program's single voice.** The
platform plays a stack's voices at a lower velocity (voice.c `mono_play`: half for four voices) and starts them at
random phases; P5 hears neither: its VCA follows the program's own envelope (the velocity only with the program's
VEL switch) and its oscillators take their phases at the note-on, before `mono_play` moves them. With Vintage 0 the
four voices are one voice four times. On the bass part with Dual Core On that is every bass note
([An unplayed part](#an-unplayed-part)).

**The proposal, built and OFF** (`P5_UNISON_TRIM`, 0): a stack's voices at half level, after the filter and the VCA
(a shift of the voice's output: the filter's saturation is the single voice's). With it a UNISON key peaks +5.8 dB
(one key of a stack, +4.9 .. +5.4), the limiter is not touched, and nothing else changes. Whether a Prophet's UNISON
should be that much louder than its chord is a matter of taste: Melodee's is as loud, the instrument's stack is
louder than one voice too (but drifts apart). **The owner's**, by ear: `../audio-0.22/prophet/headroom/` has the
three hottest cases as they are (`now-*`) and with the proposal (`trim-*`).

### A stolen voice

Four voices: a fifth note takes one at every change of a full chord (375 fades and 393 steals in the owner's few
minutes). voice.c takes the oldest held voice that is not the lowest note, and the note waits for it (`vsq`). **To
a773d30 that voice did not fade: P5 put a straight line from its last sample to 0 in its place, one block (0.7 ms)**
(`p5_render`: Melodee's way for a victim whose replacement sounds in the same block, so that no fifth voice is
paid for). The measure (`p5_probe steal`: the 160 programs without UNISON, a four-note chord held, a fifth key at six
moments, 931 steals; the new note let go at once so that the window holds the stolen voice's end alone; the largest
third difference of the dry mix in the 7 ms from the steal against the largest in the 150 ms of held chord before
it; 0 dB = nothing the chord does not do itself):

| the stolen voice ends | median | 90 % of the steals under | above +6 dB | above +12 dB | the new note after its key | voices at the most |
| --- | --- | --- | --- | --- | --- | --- |
| the last sample's line, 1 block (a773d30; `P5_STEAL_BLOCKS` 0) | -2.9 dB | +4.0 dB | 75 of 931 (**8.1 %**) | 17 | 0.7 ms | 4 |
| the whole voice fading, 1 block (1) | -3.1 | +1.2 | 5 % | | 0.7 ms | 4 |
| **the whole voice fading, 2 blocks (2: the firmware's)** | -3.3 | -0.3 | 10 (**1.1 %**) | 2 | **1.45 ms** | 4 |
| .. 4 blocks (4) | -3.3 | -0.8 | 1 (0.1 %) | 1 | 2.9 ms | 4 |
| .. 8 blocks (8) | -3.3 | -0.8 | 0 | | 5.8 ms | 4 |
| the VA (8 voices, 9th key): the whole voice, 1 block | -2.5 | +0.9 | 5 of 144 (3.5 %) | 3 | 0.7 ms | |
| CZ-1 (8 voices): the same | -3.2 | -1.0 | 0 of 233 | | 0.7 ms | |
| FM6 (8 here): restarts the voice in place, as Dexed | +24 | | 58 % | | 0 | (its window holds the new note's attack: not comparable) |

So on most programs the tick is under the chord's own highs whichever way; on about one steal in twelve (the darker
programs: 161 BRASS the steadiest) it was above them, more than twice as often as the VA's fade, and P5 steals far
more often than an engine of eight voices.

**What is built** (`P5_STEAL_BLOCKS` 2; `engine_t.steal` / `fade`, voice.c `VOICE_STEAL_FADE`, behind
`FELUCCA_PROPHET`: no other engine has the hooks and their code and goldens are as they were): the part's own voice
taken for a waiting note goes on sounding as a whole voice under a straight ramp of its VCA over two blocks, and
the note starts when the fade has ended. **Never a fifth voice** (P5 has four voices' state; the fading voice and
the note share one), **the note 0.7 ms later than before** (1.45 ms after its key). The alternatives, and why not:
- **4 or 8 blocks**: the cleanest by the measure, at 2.9 / 5.8 ms of latency on every stolen note (the note must
  wait: there is no fifth state to overlap with, and a fifth voice for those blocks is what the half cannot pay).
  `P5_STEAL_BLOCKS` is the switch; `four-*` in the listening files.
- **a fast release by the voice's own envelope**: the same wait, an exponential that needs about 7 time constants to
  -60 dB, so longer for the same smoothness; and the program's own release is still running underneath.
- **starting the note late on the same voice**: that is what this is.
A voice the budget takes for ANOTHER part's note, and the overload shed's victim, keep the one-block line: there the
replacement or the overload is in the same block, and a whole voice more is the cost that was to be avoided
(`cr_prophet_test`: both ways are checked).

Listening: `../audio-0.22/prophet/steal/` (`before-*` a773d30, `after-*` this, `four-*`; a five-note chord with ten
steals and an arpeggio over a held chord with 47, on 1 ITS PROPHET5, 16 CANNED HAPPY and 161 BRASS). **By ear it is
the owner's**: `tools/emu/wavclicks.py` counts the same clicks in all three (it does not see the difference).

### What remains of the crackle

Not reproduced and not explained, as at 3y44. What this round rules out on the host: a word shared by the two parts'
renders (the sanitizer), a clip or a wrap inside P5 on any program, a filter state at its clamp. What it found that
is heard: sheds (7 in the reading: a held note drops out), steals as ticks on the darker programs (now a fade),
late halves (7) with the half at 91 %. What only the unit can say: `c1_rejected` / `c1_faults` after a crackle
(a core reading wrong), whether it ever happens with Dual Core Off and the bass off, `stale_halves` without a flash
erase in the same reading, and the bass part's voices (not in `--debug` yet).

## Two cores, eight voices (3y46)

Built on the host and in the emulators on 2026-10-13 from the owner's decisions of that day
([Voices](#voices) has his words and the table). **Not run on a unit.**

### How a block is rendered

Everything that decides anything stays on core 0, before the hand-over, as it was: the events, both parts' note
handling, the allocation, the pool, the steals (`events_block`), then both parts' matrix and LFO tick
(`fx.c mix_parts_split`). Then, with P5 on the chord part:

1. core 0 runs `voice.c track_render_x` for the chord part as ever, but for every voice it does all of the voice's
   block except its samples (the envelope's stage and the fade's level, the glide, TUNE and the bend, the matrix:
   the voice's `vmod_t`) and puts the voice on a list (`p5x.it[]`). Before the voices: `p5_block` (the wheel's 32
   samples, `value[]`, the stack's gain);
2. `fx.c p5x_run`: the list is shared out (below), and **one job** goes to core 1, as before: the bass part's whole
   render (`track_render_x(trk[1])` into `c1_buf`), then the chord voices marked for core 1 (`p5_render` into
   `p5x_buf`, cleared by core 1);
3. core 0 renders the other chord voices into the part's buffer, then waits for core 1 (`cr_c1_wait`, 8 ms at
   most);
4. core 0 adds `p5x_buf` to the part's buffer, runs the part's DC blocker (`p5_post`) and mixes the two parts, in
   the order of one core.

**Which core renders which voice** (`p5x_assign`, every block). Core 0 waits for core 1 before the effects and the
master, so a half is **the tick + the LONGER of the two cores' renders + the effects + the master**. What counts is
the longer side, and the even split makes it shortest: of n whole voices core 1 takes the number that makes the
longer side shortest, with at most four P5 voices a core (the owner's "4 per core"), counting the bass part's
voices on core 1 (a P5 voice 440 us, a budget unit of another engine 75 us: the unit's readings), and an equal
split's odd voice goes to core 1 (core 0 has the rest of the half to do). A voice that was given up and is in its
last block (the line of its last sample to 0) is no work and stays on core 0.

**This is not the fill order first planned** (core 1 takes the first four voices, core 0 only from the fifth on).
That order is right if core 0's other work ran beside core 1's, and it does not: core 0 sits in the wait
(`pk_wait_us` 1201 on the unit). With it a four-note chord, the common case, is four voices on core 1 and core 0
waiting for them: the same half as one core, 2632 us by the reading. Shared out evenly it is two and two. The first
plan is `-DP5_FILL_C1_FIRST=1` (built; `fx.c`).

### What a voice's render touches

A voice's render (`p5_render` -> `p5_samples`) on either core writes its own `p5_voice_t` and the buffer it is given.
Everything else it reads, and nothing it reads is written while the two cores render:

| state | written by | when | how it is kept from two cores |
| --- | --- | --- | --- |
| the voice (`p5_voice_t`: phases, the filter's state and kept coefficients, both envelopes, the glide, `sync_tail`, `last_pcm`, its noise, `steal`) | the voice's own render | the block | a voice is on exactly one core's share of the list (`p5x.it[i].c1`); note-ons (`p5_note_on`) are core 0's, before the hand-over |
| the part's sum (`out[]`) | every voice | the block | **two buffers**: core 0's voices into `part_buf`, core 1's into `p5x_buf`; added on core 0 after the wait |
| the DC blocker (`dc_x1`, `dc_y1`), the part's tail | `p5_post`, `p5_tail` | after the voices | core 0 alone, after the wait, on the sum |
| the wheel's block (`wheel[]`, `wheel_pitch[]`), the LFO's phase and value, the wheel's noise, `value[]`, the stack's gain (`sg0`, `sg1`) | `p5_block` | before the voices | core 0, before the hand-over; read only after |
| the program (`p5_patch[part]`, `p5_ready`) | a load, the editor | the main loop | not during the audio ISR; `p5_block` has made it ready before the voices |
| `mod_tab[0]` (the matrix), `t->p[]` (P_E0..7 swapped during an engine fade), the LFO's value, TUNE, the bend | `mod_begin`, `mod_voice`, `track_render_x`'s head | before the list is handed over | core 0; the voice's share of the matrix is in its own `vmod_t`, copied onto the list |
| `voice_t` (stage, env, `env_out`, `pitch_cur`, `active`) | `env_tick`, the glide (`track_render_x`'s voice loop) | before the list is handed over | core 0; the render only reads `gate`, `active`, `pitch16`, `pitch_cur`, `mvel` |
| a stack voice's scratch (`tmp[CTL]`, the level's ramp) | `p5_render` | the block | on the rendering core's stack |
| `last_pitch` (the glide's start) | `p5_note_on` | a note-on | core 0, before the hand-over |
| the tables | | | const, or RAM tables written at power-on |
| the list itself (`p5x`), `cr_c1_split`, the counters (`cr_dbg.p5_*`) | `p5x_run` | before the hand-over, after the wait | core 0; core 1 reads the list |
| core 1's stage time (`cr_dbg.cur[CRP_PART1]`), `c1_buf`, `c1_side`, `c1_nr`, `p5x_buf` | the job | the block | core 1's alone until the wait is over |
| the bass part (`trk[1]`, `eng_state[1]`: its own `p5_part_t`, its own block) | the job | the block | the part's, as since 0.15 (3y45's table, [Two cores](#two-cores)) |

**Core 1's stack**: the job calls `p5_render` directly (no `track_render_x` frame): 36 + 256 + 128 (a stack voice's
scratch) + 320 = about 740 B of its 2816, less than the bass part's own way (about 820). **The code it runs**:
`p5_samples` and `p5_mod_samples` are in `.ram_text` (RAMTEXT 6808 B, unchanged), one copy, reached from both cores
through `p5_render`'s function pointer, as the bass part's P5 voices have been since 3y41.

### The same samples, and no shared word: evidence

- **The split's sum is the unsplit render's, bit for bit.** The part's sum is 32-bit integer addition, so the order
  and the grouping of the voices cannot change it; and the test shows that nothing else differs.
  `tests/run_c1_thread_test.sh` (`tests/cr_c1_thread_test.c`, all 200 programs): fifteen pairs of engines, a phrase
  of 3000 blocks with chords of up to eight notes (steals, fades, deferred notes), bass notes, the wheel, pressure,
  a bend and a sound change under held notes. For the **seven pairs with P5 on the chord part** (P5 bass, a UNISON
  P5 bass, FM6, VA, CZ-1, FM TONE, WHEEL as the bass) six runs give **one digest**: the job inline; the job on a
  real second thread (twice); **the UNSPLIT render** (every chord voice in `track_render_x`, in voice order, on one
  thread: `cr_c1_test_nosplit`, the same allocation); and two splits of the test's own on the thread (every chord
  voice to core 1; none). The pool is never above eight, and no core renders more than four P5 voices in a block.
- **ThreadSanitizer: 0 reports over the 15 pairs** (900 blocks each with the job on the thread), and the planted
  word that both sides write is reported (the run can fail).
- The emulator: `tools/emu/test_cr.sh` plays `cr_prophet_eight.txt` (eight MIDI notes, a ninth, a UNISON bass under
  six and under eight chord notes) on a fresh unit: 8, 8 voices; the bass 4, 2, 0; **`EMU_C1=thread` gives the
  inline job's WAV, bit for bit**; `EMU_C1=nostart`: four voices, the bass one, the chord three under it;
  `EMU_C1=hang`: one core's WAV.

What this does not show, as in 3y45: the unit's timing, a core that reads a word wrong, the caches.

### Will it fit? Predicted from the unit's readings

**Not measured: nothing of this has run on a unit.** From the readings of 2026-10-13: a P5 voice 385 .. 440 us at
the peak (four: 1533 .. 1760), the tick 140 .. 360, the effects with three sends up 570 .. 720, the master 110 ..
165; a half is 2902 us and the overload shed acts above 2466 (85 %). The half is the tick + the longer core + the
effects + the master. "Sum" adds every stage's worst case, which do not fall in one half: the one reading there is
of this shape (four voices on each core, three effects) was **2632 us** where the sum says 3005, so "by the reading"
is the sum x 0.876. Without effects the fx stage is the parts' own mix: about 200 us, an inference (no reading).

| chord voices (cores 0 + 1) | bass | the longer core | three effects: sum of the worst | by the reading | no effects: sum |
| --- | --- | --- | --- | --- | --- |
| 4 (2 + 2) | none | 770 .. 880 | 1590 .. 2125 (55 .. 73 %) | about 1860 (64 %) | 1220 .. 1605 (42 .. 55 %) |
| 6 (3 + 3) | none | 1155 .. 1320 | 1975 .. 2565 (68 .. 88 %) | about 2250 (77 %) | 1605 .. 2045 (55 .. 70 %) |
| 8 (4 + 4) | none | 1540 .. 1760 | 2360 .. **3005 (81 .. 104 %)** | **2632 (91 %)**, the reading | 1990 .. 2485 (69 .. 86 %) |
| 4 (0 + 4) | P5 stack of 4 (core 1) | 1540 .. 1760 | as 8 | 2632 (91 %): this IS the reading's case | as 8 |
| 6 (4 + 2) | P5, 2 of its stack | 1540 .. 1760 | as 8 | 2632 | as 8 |
| 4 (2 + 2) | P5, one voice | 1155 .. 1320 (core 1: three) | 1975 .. 2565 | about 2250 (77 %) | 1605 .. 2045 |
| 8 (4 + 4) | another engine, one voice (about 150) | 1690 .. 1910 | 2510 .. 3155 (86 .. 109 %) | about 2760 (95 %) | 2140 .. 2635 (74 .. 91 %) |
| the first plan's fill: 4 (0 + 4) | none | 1540 .. 1760 | 2360 .. 3005 | 2632 (91 %) | 1990 .. 2485 |
| one core: 4, no bass | | 1540 .. 1760 | as 8 on two | 2632 | as 8 |
| one core: 3 + a P5 bass voice | | 1540 .. 1760 | as 8 on two | 2632 | as 8 |

**Plainly: 4 + 4 with three effects up does not stay under the 85 % line** (2466 us), by the one reading there is
(2632 us, 377 halves above 85 % and 7 voices shed in a few minutes), and by the sum of the worst cases it can pass
the deadline itself (3005 of 2902). Eight voices fit with a margin only without the effects (about 2180 by the
reading's ratio, 75 %). **No other split of eight does better**: core 0 waits for core 1, so 5 + 3 makes the longer
side five voices (2200 us) and the half longer, whichever core has the five. What fits with three effects is
**three voices a core, six in all** (about 2250 us, 77 %): that is the one-define change, **`-DP5_CORE=3`** (the
pool six, a UNISON stack three; it builds and its test runs, `tests/run_prophet_test.sh`), not the default: the
owner asked for 4 per core. What would make eight fit is core 0 not waiting idle (the effects of a block on core 0
while core 1 still renders, the mix one block behind): not built. A four-note chord, the common case, is what gains
now: two and two, about 1860 us where one core's was 2632.

### The overload shed (`audio.c shed_check`, `shed_voice`)

The half the shed times is core 0's, and core 0 waits in it for core 1's job: it is over 85 % when EITHER core's
render is too long, so the shed sees the right core without asking which. And because the chord part's voices are
shared out between the cores again every block, a voice given up shortens the longer side whichever part it was
(the one exception: a bass of another engine with many voices on core 1). The order, two halves in a row above
85 %, one voice, and one more each half while it lasts:

1. a released voice, the quietest, of any part (as before);
2. **with P5 on the bass part: a voice of the bass's stack, from the top** (never its last: the lead stays), before
   any chord voice (`shed_last_why` 4 in `--debug`);
3. as before: the oldest held voice that is not its part's lowest note or lead: a chord voice.

Without P5 on the bass part it is (1) and (3): today's rule and today's code path. The 16-unit budget is as it was
for every other engine; P5's two-unit voices with two cores and the pool are above.

### Counters for the unit (78 PEAK, `tools/fm1_install.py --debug`, docs/FX.md)

| value | |
| --- | --- |
| `p5_core0_voices`, `p5_core1_voices` | the most P5 voices core 0 and core 1 rendered in one block since the last read. The rule: four each. Core 1's are the bass part's P5 voices and its share of the chord's; with one core all are core 0's |
| `p5_bass_voices` | the bass part's most P5 voices (four with two cores, one with one) |
| `p5_bass_given_up` | held bass voices given up to the chord part |
| `pk_part1_us` (was there) | **core 1's whole job of a half**: the bass part and its share of the chord's voices. `pk_part0_us`: core 0's own render |
| `pk_wait_us` (was there) | core 0's longest wait for core 1 |
| `shed_last_why` 4 | the latest shed voice was a voice of the bass's P5 stack |

and a line "P5: at most .. voices rendered by core 0 and .. by core 1 ..". **To read next**: with Dual Core On, a
four-note, a six-note and an eight-note chord of ITS PROPHET5 held, dry and with the three sends up, then the same
with a UNISON P5 bass: `pk_block_us`, `pk_part0_us`, `pk_part1_us`, `pk_wait_us`, `shed_over_halves`,
`shed_voices`, `stale_halves`, the four `p5_*` values, `c1_rejected`, `c1_faults`.

## The effects off core 0's path, the part's coefficients, the edges (3y50)

After a survey of Melodee 1.4 (6da744c "perf(audio): optimize DSP and share voice budget", cb118c9 "shared sends
and lean reverb": the ideas, written anew; LICENSING.md). Everything here leaves the samples as they were: the 246
goldens unchanged, the threaded identity test unchanged and extended.

### The reverb on core 1

docs/DUALCORE.md "What each core runs": after join 1 core 1 runs the reverb (its half-band, lines, up-sampler) while
core 0 runs the chorus and the delay; join 2; the reverb's buffers added. The effects' stage on core 0 becomes the
longer of the two sides instead of their sum. From the unit's readings (reverb avg 202 / max 273, its down- and
up-sampler 22..98 each, so the reverb's side about 240 avg / 470 worst; the three effects with the parts' sends 570
.. 720, so core 0's side about 250 at the worst), **by inference** the effects' part of a half shrinks by about 200
.. 250 us when all three run. With the delay at the half rate (the default since 0.22, HISTORY 3y74 / 3y76) the
up-sampler, which the delay's wet and the reverb's share, stays on core 0 after join 2 (about 35 us a half): core 1
runs the reverb's half-band and lines (DUALCORE.md "What each core runs").

### The part's coefficients (`p5_coef_t`, `p5_coef_update`)

Melodee's `p5_cache_update` pattern: what `p5_render` worked out for every voice of every block from the part's
values alone (coarse and fine tuning, B's keyboard-off pitch and its `/127`, the glide's step, the cutoff's
part-wide terms and the key tracking's factor, resonance -> k (`/127`) and the filter's gain, the envelope amount,
the six Poly-Mod scales, the pulse widths, the three levels, the macros E0..E7 in all of these, the pressure on the
cutoff) is worked out in `p5_block` **once a block for the part, and only when the values, a macro or the pressure
changed**; each voice adds its own terms (m, the drifts, the glide, the velocity, the width's modulation). The same
expressions; regrouped only as sums of integers (the cutoff) and one product (`d x kf x HALF`, either order: no
overflow at 2047 x 2392), so the same integers. **How a change is seen**: `p5_block` still reads the 55 bytes every
block and compares them with the last block's as it clamps them (any writer is seen in the next block: the editor,
a load, MIDI, a test that writes the bytes itself), then the eight macros and the pressure. A version counter would
skip the 55-byte loop, but every writer would have to count, and the tests and `tools/p5_probe.c` write the bytes
directly: not taken. `-DP5_COEF_CACHE=0` is the old code (a measurement).

**Counted from the listing** (`build/choralroot.dis`, the device compiler): `p5_render` 497 -> 366 instructions, 10
-> 8 divides (each voice, each block: about 131 instructions and two divides fewer, about 0.6 us a voice a block,
2.6 us a voice a half at 205 instructions a us; eight voices about 20 us a half, 0.7 % of it); `p5_block` 210 -> 245
(the compare: +35 a part a block) and `p5_coef_update` 161 when a value moves. tests/target_budget.txt: `p5_block` 29
-> 40 (the compare's two loops).

### The idle part and the word-wide memset / memcpy

An idle P5 part (no voice) already does only what leaves the part's state (3y45: the wheel's noise generator's 32
steps and the LFO's phase); checked again: nothing else of `p5_block` runs, `track_render_x` clears the part's
buffer and `p5_post` returns at rest. The 32 noise steps cannot be skipped bit-exactly (a xorshift has no cheaper
jump than its 32 steps). Nothing more was skipped. `firmware/src/libc.c` (the device's only libc: the compiler's
struct copies and clears, `va_render`'s 48-byte clear every voice and block, the engines' `memset` of a voice at a
note-on) is a word at a time when both pointers are aligned, bytes for the rest: memset 20 instructions,
memcpy 23, memcmp 25 in all; for `va_render`'s 48 bytes about 12 word stores where there were 48
byte stores (about 100 instructions less a VA voice a block, an estimate from the loop's shape).
`tests/cr_libc_test.c`: every alignment of both ends, lengths 0..80 and seven long ones, the host's bytes and signs.

### The oscillators' edges, a study (tools/p5_edges.sh)

The owner: "Is their optimization we could do to the oscillator edges? How much would that change the sound?"

**Where the divide is**: `p5_fast_blep` (each saw a waveform needs: saw A, pulse A's two, B's saw / pulse / the
triangle's square) tests whether the phase is within one increment of its wrap; only then `p5_blep_fraction`
divides (`distance / inc`, normalised: 17..32-bit quotients) and the two-sample polynomial BLEP corrects the sample
before and after the edge. Hard sync adds one divide (`p5_sync_fraction`) per sync edge of B. **Measured on the host**
(all 200 programs, four-note chords root / third / fifth / octave, 1 s held + 0.125 s; per voice and second of sound):

| register | BLEP tests | BLEP divides | sync divides | (the programs with sync alone, 110 voice-seconds a register: BLEP, sync) |
| --- | --- | --- | --- | --- |
| C3 | 166 409 | 3 079 | 32 | 4 664, 250 |
| C4 | 166 532 | 6 109 | 63 | 9 388, 499 |
| C5 | 166 776 | 12 173 | 126 | 18 915, 998 |
| C6 | 167 266 | 23 783 | 252 | 37 444, 1 995 |

So about 3.8 edge tests a sample (each a compare or two), and a divide in 7 % (C3) .. 54 % (C6) of the samples (in a
sync program 0.11 .. 0.85). **Their share of a voice** (by inference: the unit's voice is about 440 us a half = 128
samples x ~700 instructions; a divide with its fraction's code taken as about 25 instructions): C3 about 0.3 %, C4
0.5 %, C5 1 %, C6 about 2 % (a sync program at C6 about 3 %); the 3.8 tests a sample about 2 % at every pitch.

**The options, measured** (the master's output of all 200 programs at the four registers, 95.4 M samples, against
today's):

| option | what changes | peak difference | RMS difference |
| --- | --- | --- | --- |
| (a) the fraction from a 256-entry reciprocal of the increment's top bits, no correction step | the BLEP's position by up to 1 part in 512 | -13.7 dBFS | -49.2 dBFS |
| (c) one correction tap (the sample after the edge) instead of two | the BLEP's shape: half of it gone, more alias | -10.7 dBFS | -40.6 dBFS |
| (b) one reciprocal a block per oscillator | exact only while the increment is constant over the block; Poly-Mod A, the wheel's pitch, glide and the bend change it every sample | not built: an exact reciprocal of a varying divisor needs a divide or a correction step per edge, which is what (a) would add back | |

The peaks are large because the filter, the drive and self-oscillation carry a small change at an edge into a
different waveform later (the sample-level difference does not stay small); the RMS says how much on average.
**None is under -90 dBFS: nothing was built.** An exact table reciprocal (Granlund-Montgomery: a table, one multiply,
one correction) gives the same quotients, but costs about the divide it replaces when the unit's divide is counted
as one instruction (`tools/p5_cost.py`'s convention): no saving worth its code. What could save the ~2 % of the edge
TESTS bit-exactly: one test of the phase against the increment per oscillator (the saws of pulse and triangle share
it) instead of one per saw: not built (a study for the owner).

### Predicted, before and after (not measured; docs/PROPHET.md "Will it fit?"'s method)

"By the reading" scales the sum of the worst cases by 0.876 (the one reading of its shape: 4 + 4, three effects:
2632 us). After: the effects' stage less 200 .. 250 us when all three run (the reverb on core 1), the longer core
less about 2.6 us a voice (the coefficients). Deadline 2902, the shed's line 2466.

| chord voices (cores 0 + 1) | bass | three effects: before | after | no effects: before | after |
| --- | --- | --- | --- | --- | --- |
| 4 (2 + 2) | none | about 1860 (64 %) | about 1640 .. 1680 (57 %) | 1220 .. 1605 | -5 |
| 6 (3 + 3) | none | about 2250 (77 %) | about 2020 .. 2060 (70 %) | 1605 .. 2045 | -8 |
| 8 (4 + 4) | none | 2632 (91 %), the reading | **about 2400 .. 2440 (83 .. 84 %)** | 1990 .. 2485 | -10 |
| 4 (0 + 4) or 6 (4 + 2) | P5 stack 4 / 2 (core 1) | 2632 | about 2400 .. 2440 | as 8 | -10 |
| 4 (2 + 2) | P5, one voice | about 2250 | about 2020 .. 2060 | 1605 .. 2045 | -8 |
| 8 (4 + 4) | another engine, one voice | about 2760 (95 %) | about 2530 .. 2570 (88 %) | 2140 .. 2635 | -10 |

**Plainly**: by inference eight P5 voices with the three effects come under the 85 % line by a few tens of us
(2400 .. 2440 against 2466), where they were over it; with another engine's bass under eight they stay over it. To
read on the unit: 78 PEAK's `pk_block_us`, `pk_c0_busy_us`, `pk_c1_busy_us`, `pk_fx_join_us` (how long core 0
waits for the reverb: if it is long, the reverb's side is the longer one), `pk_fx_map` 15, `shed_over_halves`.

## Level (3y46)

The owner, 2026-10-13: "Since Unison on is going to bring us 5 dB over the limiter, I think we should bring engine
down, since the limiter is going to cause chopping possibly."

**(a) The engine is 6 dB lower: every sample of every voice is exactly half of 3y45's.** Melodee's "x 2" after the
VCA is gone (`P5_OUT_SHIFT` 0; 1 is 3y45's level). After the filter and the VCA: the mixer's and the ladder's
saturation, the filter's states and its character are what they were ([Headroom](#headroom)'s first five stages are
unchanged). As CZ-1's in 0.19 (docs/AUDIO-QUALITY.md). Proved two ways:
- `tests/run_prophet_test.sh`: every shipped program, a four-note chord held and released, the part's capacitor off
  (its 32-bit state rounds): this tree's samples x 2 are `-DP5_OUT_SHIFT=1`'s, bit for bit, and no sample of the old
  level is odd;
- `python3 tools/p5_goldens.py --level 8472e70`: the regression built with `-DP5_OUT_SHIFT=1 -DP5_STACK_PHASE=0`
  gives **all 49 of 8472e70's P5 goldens**: the level and the stack's start are all that changed in a program's
  render on one core.

The capacitor's "at rest" (`P5_DC_REST`) is 8 output units now, the same -70 dB under a chord.

**(b) A UNISON stack: its start, its level, and no trim.**
- **A stack is four voices at most**, on either part ([Voices](#voices)).
- **Its voices start together** (`P5_STACK_PHASE` 1: both oscillators at phase 0 at a fresh note-on of a part in
  UNISON). To 3y45 only a stack's first key after power-on did (the +12 dB of [Headroom](#headroom)); every later key
  had oscillator A at whatever phases voice.c had left in the slots and oscillator B together, so a key's level lay
  somewhere between twice and four times a voice and changed from key to key. Now a stack with DETUNE 0 and
  Vintage 0 is one voice N times, every key, and with them it spreads as the program spreads it. **A change of the
  stack's sound that the owner did not ask for**: it is what makes its level a thing one can match (below), and it
  is the first key's sound he has heard; `-DP5_STACK_PHASE=0` is 3y45's.
- **Its level does not depend on how many voices the budget gave** (the owner: "the engine also sounds quite
  different on single core"). The voices that sound are raised to the whole stack's level, after the filter and the
  VCA, on a straight line over a block when a voice is given up or comes back (`p5_stack_gain`, `P5_STACK_G`): n
  voices of a stack of N play at **N / n** each (one voice of four: x 4), and at sqrt(N / n) where the program
  takes its voices apart at once (DETUNE above 0, or no oscillator in the mixer). So a UNISON bass is as loud
  with one core (one voice) as with two (four), and does not drop while the chord takes its voices.
  **Measured** (`cr_prophet_test --stack`, the 40 UNISON programs of the 200 on the bass part, one key struck four
  times, the held note's RMS, one voice against the whole stack): **within 1 dB on 26, within 2 dB on 32**; the
  seven that are one voice N times: 0.000 dB. Not within 2 dB (the one voice LOUDER, +2.2 .. +7.1 dB: programs whose
  voices drift apart within the note, by Vintage, Lo Freq B or Poly-Mod): 14 DIGI-RECORDR, 36 THE HATS, 75 MORSE
  LEAD, 88 DIRTY LEAD, 153 SCI FI DROP, 197 HELICOPTER; the one voice QUIETER (-5.2, -5.6: a DETUNE of 1 or 2 that
  has not opened yet in a short low note): 80 DUOTONE, 151 TIGHT BASS. The peaks are within 1 dB on 29. Of the
  shipped list's five: LAGOON BASS +0.5, FEATURE BASS +0.7, UNISAWYER +1.4, UNISON GLIDE +1.6, BASSMENT (a stack of
  one) 0. **By ear it is the owner's** (`../audio-0.22/prophet/eight/`).
- **The trim (`P5_UNISON_TRIM`) stays OFF.** The rule set: on, if after (a) a UNISON key still peaks more than
  about 3 dB above a four-note chord of the same program. Measured (`cr_prophet_test --unichord`, all 200 with the
  editor's switch, a stack of four on C3 against C4 E4 G4 B4, two cores): the key is **+1.2 dB over the chord at
  the median** (quartiles +0.5 .. +2.0, the largest +5.1; more than 3 dB on 18 of the 201, more than 6 dB on
  none). Built as before (`-DP5_UNISON_TRIM=1`: a stack 6 dB lower still).

**The new peak table** (`sh tools/p5_probe.sh levels`: every sound of each pool at velocity 127, the part's peak
before LEVEL, dB re 32768, median / max; Clean's limiter works above about +0.5 dB in the mix):

| | one key | four notes close | UNISON on one key | the limiter at the worst |
| --- | --- | --- | --- | --- |
| P5 (200), 3y45 | -4.1 / +10.9 | +4.8 / +11.6 | +4.9 / +11.9 | -5.2 dB |
| **P5 (200), 3y46** | **-10.1 / +5.6** (C2: +6.4) | **-1.1 / +5.8** (wide: +6.5) | **+0.8 / +6.4** (C2: +1.0 / +6.8) | **-0.8 dB** (a UNISON key; a four-note chord does not touch it) |
| P5 with `P5_UNISON_TRIM=1` (not built in) | | | -5.2 / +0.4 | not touched |
| VA (25) | -3.9 / +3.0 | +5.5 / +10.4 (wide: +12.7) | -1.1 / +5.8 | -3.4 dB |
| FM6 (25) | -6.6 / -1.5 | +2.9 / +6.9 (high: +9.9) | +3.1 / +8.0 | not touched |
| CZ-1 (64) | -4.9 / +1.0 | +4.9 / +8.6 | +3.6 / +12.0 | -2.7 dB |
| FM TONE (16) | -4.9 / +1.1 | +5.5 / +11.6 | +3.3 / +11.5 | -4.8 dB |

(P5's "one key" maxima are UNISON programs as they load: a key is their stack.) **Where P5 now sits: under the
others.** A four-note P5 chord was level with the VA's (+4.8 against +5.5 at the median); it is now 6.6 dB under the
VA's and CZ-1's and 4 dB under FM6's, and P5's loudest chord (+5.8) is under FM6's loudest (+6.9). The limiter,
which the hottest UNISON key drove 5.2 dB down, is now touched by 0.8 dB at the worst, and by no four-note chord.
An eight-note chord, which is new, is about 3 dB over a four-note one. **Whether P5 should sit that far under the
other engines is the owner's by ear**: the halving is what was asked; half of it back (3 dB) would need a
multiply in the voice's output, and `-DP5_OUT_SHIFT=1 -DP5_UNISON_TRIM=1` is the other way (the chords as they
were, a stack 6 dB down).

**Stored sounds need no migration**: the level is the engine's. A session or a settings block that holds a P5
program holds its 138 bytes and the part's LEVEL, neither of which changed; it plays 6 dB lower, as every P5 sound
does. **Goldens: all 49 P5 renders change** (the level), the 197 of the other engines do not
(`python3 tools/p5_goldens.py`).

## Tests

- `tests/cr_prophet_test.c` (in `sh tests/run_cr_tests.sh`), on the firmware as the emulator builds it: the engine's
  place and its name (P5); the bank (Sequential's 200 from the `.syx`: 40 with UNISON, 80 on the SSI filter, 25 with
  sync) and **the shipped list** (each shipped program unpacks to the `.syx`'s frame of its own number, encodes back
  to its bytes, and is found by that number; decode / encode round trip; bad frames refused); the names; the blob;
  **every shipped program through voice.c** (a four-note chord: none silent, no sample beyond the voices' sum, no DC,
  every voice free after its release, exact silence then); 410 stress programs; note-off; a note struck again (no
  step beyond the wave's own); **the four-voice cap, the units, which keys of a five- and a six-key chord sound,
  four voices kept with the bass sounding (three units a voice)**; the SAVE dialog (Favorite alone, KNOB 4 marks by
  the Sequential number, OCT+ refused, no record) and the browser's KNOB 4; the favourites across three lists
  (`--fav-out` / `--fav-in`); the editor's UNISON cell and GLIDE; **UNISON on a load** (every shipped
  program on both parts; a 4-note chord on a LOW and on a LAST program: four voices on one note, which note, 16
  units; three with a bass; held keys over a load both ways; the editor's cell after a load; another engine by a
  pool load and by the editor's engine switch; the session's record); the bend through the part's MIDI; the 19 pages
  (every cell sets its byte and no other, clamps, reads back; each of the 56 bytes on exactly one cell); the pool;
  SAVE refused.
- **3y46** ([Two cores, eight voices](#two-cores-eight-voices-3y46), [Level](#level-3y46)): `cr_prophet_test`
  `t_cores` (through `mix_block`: eight voices of ten keys, four a core, which notes; the eleventh key's fade on core
  1; a UNISON bass and a chord of 1 .. 8 keys in both orders; a bass key under eight; a progression struck over
  its own release with the bass's key in the chord's block; one core: 4, 3 + 1, a stack's
  one voice and its gain; core 1 given up three ways under a chord of eight and under 4 + 4; Options > Dual Core
  turned under held notes; the bass part to the VA and back under held chord keys; a UNISON key with one core
  against two); `--half`, `--stack`, `--unichord`; `tests/run_prophet_test.sh` (the level's two builds; all 200:
  the stack's and the key-over-chord figures; `-DP5_ONE_POOL=5`, `-DP5_CORE=3` build and run;
  `-DP5_WAIT_SLOT=0` builds and passes);
  `tests/run_c1_thread_test.sh` (15 pairs; the seven with a P5 chord against the unsplit render; the sanitizer);
  `tests/cr_cpu1_test.c` (the start: SAFE MODE, the boot before failed, a clean boot); `tests/cr_settings_test.c`
  (Dual Core On by default; a record's Off kept); `tests/cr_backup_test.c` (78 PEAK: 45 values);
  `tests/install_test.py` (`--debug`'s four values and its line); `tools/emu/test_cr.sh` (`cr_prophet_eight.txt`
  inline, on a thread, with one core, with a core that hangs; the owner's gesture with two cores, five voices, and
  with one, four; the Dual Core block for a fresh unit that is On); `tools/emu/test_persist.sh` (Reset Settings:
  On). `tests/target_budget.txt`: `p5_render` 50 -> 63 (a stack voice's two short loops), `p5_block` 18 -> 29
  (the stack's count), `fm1_alnk0_irq` 16415 -> 12522 (the shed's cold path is a function of its own);
  `p5_samples` as it was. **Not tested: anything on a unit**; two real cores' timing; the level by ear; a loop's
  voices in the pool; MIDI program changes under held notes.
- **3y45** ([the second reading](#the-units-second-reading-2026-10-13-3y45-the-bass-part-two-cores-the-level-the-steal)):
  `cr_prophet_test`'s "steal" checks (the fifth key's voice fades over `P5_STEAL_BLOCKS` blocks as a whole voice, the
  VCA at half after the first and 0 after the last, its oscillators running; never more than four voices; the new
  note when the fade has ended; a budget's or a shed's victim still ends in one block on its last sample's line) and
  `--idle` (above); `run_prophet_test.sh` builds `P5_IDLE_SKIP=0` (the same digests) and `P5_STEAL_BLOCKS=0`
  (a773d30's steal: the test passes on it); `tests/run_c1_thread_test.sh` (the job on a second thread: the inline
  job's samples, no sanitizer report, a planted word reported); `tools/emu/test_cr.sh`: `EMU_C1=thread` on the VA
  pair and on P5 with a P5 bass (`perf_p.txt`): the inline job's WAV. `tests/target_budget.txt`: `p5_block` 7 -> 18
  (two short loops where the idle part had none: the weight counts loops, the instructions fell). The goldens: none
  changed (the regression plays with Felucca's steal, a voice restarted in place, `voice_fade_steal` 0: the new
  fade belongs to ChoralRoot's fade-and-defer steal and is not reached there; and the idle part renders the same
  samples).
- `tests/run_prophet_test.sh`: the stage-36 identity (above), `FELUCCA_P5_FACTORY=0`, and **the list**: the test
  built with all 200 and with a list of seven passes; a program's render is the same samples in each build; a
  session record written with one list is read with another ([The shipped list](#the-shipped-list)).
- **The owner's gesture** ([The crackle](#the-crackle-2026-10-13-3y44-not-reproduced-not-explained)):
  `cr_prophet_test --gesture [ID]` (600 runs on Sequential's program 16, in `run_prophet_test.sh` on the build with
  all 200; `--gesture-sweep` is the whole sweep, minutes) and `tools/emu/scripts/cr_prophet_crackle.txt` in
  `test_cr.sh` (program 16 where the build ships it, else place 15), once as it is and once with `EMU_SHED_OVER=4`.
  They pass before and after 3y44: **the crackle has no fix, so no test that failed before one**. The click at a
  release's end has both ([there](#the-click-at-a-releases-end-3y44-found-and-fixed)).
- `tests/regress.c`: P5's golden renders and CPU entries are the generated list's, named by the program's number
  (`preset/P5/044_CARS_STRINGS`, `cpu/P5/..`): **246 goldens** (197 of the other engines, untouched lines, + INIT and
  the 48). All 49 P5 hashes differ from cd43720's for the same programs: the regression's phrase ends on a five-note
  chord, which is four voices now, and the five UNISON programs load in UNISON. `tests/target_budget.txt`:
  `p5_samples`, `p5_render`, `p5_mod_samples`, `p5_post`, `p5_block`.
- `tools/emu/scripts/cr_prophet.txt` in `tools/emu/test_cr.sh`: OPT + PRESETS to P5, three places of the list (the
  names read from the generator), a chord each, the ten editor screens, an edit that is heard, SAVE refused ("P5: no
  save yet"), the two engine pickers with six engines. The screens with the shorter name were looked at (the two
  pickers, the pool's read-out, the save message, the editor's titles: centred, nothing cut).
- `tools/emu/test_persist.sh`: the list's first UNISON program, edited, over a power cycle (SAVE refused, the global
  save, the same flash file again: the part powers on with the program as edited and in UNISON).
- `tests/run_cr_tests.sh` builds `tests/sound_templates.c`'s check of the web tools' tables without the engine: the
  Sound Manager and `fm1_install.py` do not know engine 16 (phase B).
- Not tested: the tree as it is on a unit (cd43720 was played once); a UNISON program by ear; the bass part on a
  UNISON program under a chord; the browser emulator beyond its own suite; MIDI program changes into the pool; the
  wheel (CC 1) and aftertouch by ear; `tools/prophet_listen.sh` and `tools/emu/perf.sh` after their changes for the
  list (the listening tool's A/B program was compiled and run for one program, not the whole script).

## Listening

`sh tools/prophet_listen.sh` -> `../audio-0.22/prophet/` (README.txt there): D, Bm, G, A, D on eight programs (ITS
PROPHET5, FOREVER KEYS, STABBY BRASS, REEDY STRPAD, CARS STRINGS, VINTAGE WRLY, SYNCHROTRILL, FUNK BASS II: all in
the provisional list; the tool finds their places and skips one the list does not ship), each dry and with the
default sends, through the emulator; and three programs rendered by the remix's engine file and by this tree's
(`tools/prophet_ab.c`, the engine alone, this tree's side built with all 200): 01 and 15 differ by the DC blocker and
the interpolated knee only, 33 PICKLE PINCH is the program 8a3074b repaired (its Lo Freq B). **The files there are
cd43720's (five voices); not rendered again.**

## Open decisions (the owner's)

Decided on 2026-10-13 (HISTORY 3y39): the name (**P5**), **UNISON on a load**, **a trimmed list** (the mechanism;
the 48 are provisional), and, by the unit's reading, **four voices**. **Decided on 2026-10-13, later (HISTORY
3y46)**: eight voices on two cores, four a core; a P5 bass up to four with the chord first; one core: a P5 bass one
voice; the engine down; Dual Core On by default. That settles 4, 7, 8 and 10 below as they were asked; what 3y46
leaves to the owner:

- **The unit**: nothing of 3y46 has run there ([Will it fit?](#will-it-fit-predicted-from-the-units-readings):
  eight voices with three effects are predicted at 91 %, above the shed's line).
- **P5 is now the quietest engine** ([Level](#level-3y46)): 6.6 dB under the VA's chords at the median.
- **One core: three chord voices under a P5 bass** (built beyond his rule; `-DP5_ONE_POOL=5` is four and one).
- ~~Two cores: a bass key under eight chord keys is silent~~ decided 3y53: the bass always keeps one voice.
- **A stack's voices start together** now, and its level law ([Level](#level-3y46)), by ear.
- **`-DP5_CORE=3`** if eight do not fit on the unit.

1. **The list itself**: the 48 were chosen from the parameters, not by ear. Edit `shipped.txt` after playing
   ([the steps](#the-shipped-list)).
2. ~~**Sequential's data in a release at all** (`FELUCCA_P5_FACTORY`): the data are Sequential's ("no licence
   granted", as Melodee words it and as ChoralRoot words Casio's CZ-1 tones). 48 programs are 4.7 KB of flash, 200 are
   18.6.~~ **Decided (the owner, 2026-10-13, HISTORY 3y51):** a release ships exactly the 115 programs that
   `assets/prophet5-factory/shipped.txt` lists (his favourites), not the other 85; and those 85 are not offered on the
   site either (2026-10-14, 3y55). The data stay Sequential's (LICENSING.md).
3. **The unit again**: the four-voice build's counters ([To read next](#does-it-fit)), and UNISON by ear, on the
   chord part and on the bass.
4. **A UNISON bass** is four voices thick alone and one voice under a chord on one core ([UNISON](#unison)): the
   bass's weight changes when the chord comes in. **With Dual Core On it is four voices under every chord: a second
   P5 part's cost (about 1.3 ms a half on the unit, 3y45) and 12 dB over one voice.** The alternatives: no UNISON
   on the bass part's loads (the bass is MONO for every other engine), a capped stack there (two voices), or as it
   is. To hear on the unit.
8. **A UNISON stack's level** (3y45, [Headroom](#headroom)): one key of a stack is as loud as a four-note chord.
   `P5_UNISON_TRIM` (built, off) halves a stack's voices; `../audio-0.22/prophet/headroom/` has both.
9. **The stolen voice's fade** (3y45, [A stolen voice](#a-stolen-voice)): two blocks are built (the note 0.7 ms
   later than before); four are cleaner by the measure and 2.2 ms later. `../audio-0.22/prophet/steal/`.
10. **P5 on both parts with one core does not fit with three effects** (3y45: about 3050 us of 2902 by the
   reading): the shed thins the chord at every strike. Options: P5's units back to 4 (a P5 bass then leaves the chord
   three voices: 3 x 4 + 4), or as it is with the shed.
5. **Phase B, the store** and **the device manager's "load all 200"**: where (the free region 0xB6000..0xC7FFF;
   200 programs of 138 B are 27.6 KB unpacked), the backup ids, the web manager's engine
   list, and the **settings record**: P5 has no place among its eleven `pool_pos` slots (`cr_settings.c crs_slot`: −1,
   handled), and its `chord_sound` / `bass_sound` hold a pool position, not a program's number
   ([The shipped list](#the-shipped-list)). The sound a part plays at power-off comes back, edited or not, whatever
   the list (the session); what is not remembered over a power cycle is the place a part had in P5's pool when it
   is on another engine (OPT + PRESETS back to P5 lands on 01).
6. **Flash**: XIP is at 88.2 % with the 48 (87.3 % with INIT alone, 90.6 % with all 200).
7. **A fifth voice**: chord voices on core 1, the loop built per program, or more of the half in RAM. The sample
   loop is in RAM since 3y41 (`P5_RAM`, measured: above); the four per-sample tables (`CR_RAM_TABLES_P5`, 5.4 KB of
   RAM) and the rest of the audio path (`CR_RAM_FX` ..: docs/FX.md "Code in RAM") are built as switches and not
   measured. Five voices are not built.
