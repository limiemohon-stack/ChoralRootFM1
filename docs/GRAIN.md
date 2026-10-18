# GRAIN: a granular engine over material it makes itself (design, 2026-10-15)

**Status: built through step 5 (2026-10-15 / 16, HISTORY 3y133 .. 3y137; MORPH a mix and the render in place since
3y167 .. 3y169: "The third phase", below): the engine is in the firmware as engine 8
(`firmware/src/eng_grain.c` over `grain_src.c`), on the unit since 3y136's build, and since the owner's first listening
(3y137, below: "The second phase") with its own matrix, two synced LFOs, ENV 2, the VA's filter, a bipolar SCAN and
the wave band; its presets not yet by ear (step 6). Merged on main d75523e (v0.24-beta1 with the flash diet) on the owner's word, 2026-10-16: XIP 537 068 B (93.7 %), POOL 97.0 % (the size table). A sound saved on the first build (a 20-byte version-1 record, the owner's U033 DARK HAZE) loads again since 3y139: the pool's reader takes that length beside the 64-byte version 2.** Branch `claude/grain-engine-f724f9` from main da1f8e2 (v0.22-beta3). The owner heard the sketches
(`build/grain-sketch/`, below), kept the granular morph, and said go for steps 1 to 4. The build's size line: XIP
559 652 B of 573 440 (97.6 %), RAM 79 028 (80.4 %), POOL 304 472 (88.5 %) over da1f8e2; merged on main 5e1e8a7
(v0.22-beta4: the master filter and the step preview) XIP 561 540 B (97.9 %), POOL 304 712 (88.6 %); on main a11e0ca
(v0.22 stable, the VA matrix's fine amounts) XIP 562 196 B (98.0 %), POOL 304 716 (88.6 %); on main 1ebaaf2 (Options >
Velocity Curve, the emulator's velocity) XIP 562 772 B (98.1 %). The owner's words: "Could the
grain synth have a way to generate its own samples from a couple knobs to quickly get a variety of interesting samples to
work with?"

Felucca's GRAIN (`firmware/src/eng_grain.c`, engine 8) granulated the SAMPLE engine's ADPCM sets. ChoralRoot dropped
SAMPLE, GRAIN and DRUM on 2026-10-07 (flash, POOL) and kept slot 8 reserved. This GRAIN keeps Felucca's player, the
part it was good at (a grain pool shared by the voices, the Hann window, SIZE DENS POS SPRD RAND, the 1/sqrt(overlap)
level), and replaces its source: no samples, no decoder, no seek index. **Four knobs render the material into a short
RAM buffer: TYPE (a family of sound), TONE (its brightness or colour), MOTION (how it moves over its length) and SEED
(a variant).** The sound stores the four values, never audio; the same values render the same buffer, bit for bit, on
the host and the unit. **And two of them: a sound has sources A and B, each with its own four knobs, and MORPH mixes
them, the grains reading the mix** (3y167; until then each grain read one or the other: the owner, after the first sketches: "What would be really cool is if you could generate
two waves, each with their own settings, and then there's a parameter to morph between them").

## In three sentences

A GRAIN sound holds two 743 ms buffers at 22.05 kHz (16 384 samples, 32 KB of POOL each: sources A and B) that a
small deterministic macro-synth fills in about 12 .. 55 ms each of core 0's main loop whenever a sound loads or a
source knob turns, in slices over two passes, written over the old material while the grains play on (3y168). Up to
16 grains a part, shared by up to 8 voices, read A and B mixed by MORPH (3y167), at the note's rate against the buffer's root (C4), windowed, spread round
POS, reversed and detuned by RAND, through a filter and the platform's envelope: Felucca's player over RAM. The engine
costs about 11 .. 12 KB of flash (the core is 7.5 KB measured), 64 KB of POOL (the two buffers; a second part on GRAIN
shares them), nothing of RAM to speak of, and at its worst about 600 .. 900 us of a 2902 us half for a full part, a
quarter of a VA pad's.

## The second phase (the owner, 2026-10-16, after the first build on the unit)

"Grain engine has been tested, good start": the owner's list, and what was built for it (HISTORY 3y137; the patch
is version 2, 64 bytes; a version-1 record (the first build's 20 bytes) loads with the new values at their defaults):

| the owner | built |
| --- | --- |
| "Mod matrix needs to be able to select between envelope or LFO in each row" | GRAIN's own 8-slot matrix (`MOD 1 .. 8`: SRC DST AMT), SRC `OFF ENV1 ENV2 LFO1 LFO2 VEL KEY RAND MODW` (ENV 1 the platform's amplitude envelope, ENV 2 a modulation envelope of the engine's: ATK DEC SUS REL a voice; MODW CC 1) |
| "Need to be able to target more parameters, right now they are hard locked" | DST `OFF MORPH POS SIZE DENS SPRD RAND PITCH SCAN CUT RES FTYPE DRIVE RATE1 RATE2 DEP1 DEP2 AMP`; at a full source and AMT 63: the 0 .. 127 knobs +-126, CUT +-63 steps, PITCH +-12 st, RATEn +-126 of the rate, AMP as the VA's; the grain knobs are read at each grain's start, the filter's and AMP at the block; SCAN and RATEn are the part's, from the latest note's voice (as the VA's PAN and RATE). The source knobs (TYPE TONE MOTION SEED) are not destinations: they render |
| "Modulation amount should be the same as it is in the VA engine in that the amounts can be adjusted as decimals" | AMT exactly the VA's since 3y110: hundredths on the knob (`F_FIX2`), tenths below 6, whole numbers from 6, a per-slot fine flag in the patch (`GQ_MFINE0 / 1`), the same `va_amt_q` knob rule and `VA_AMX` render scale |
| "The LFO needs to be able to be switched to sync with rates matching the VA engine" | two LFOs of the engine's (`LFO 1`, `LFO 2`: RATE WAVE DEPTH FADE; `LFO SYN`: SYNC1 SYNC2), the VA's waves (SIN TRI SAW SQR S&H), its free rates and, synced, its list (N_VA_SYNC: 8BARD .. 256T, 4 / 3 / 2 ms) with the same rescale when SYNC switches; FADE from the note, a voice; DEPn and RATEn as destinations |
| "Need to be able to change scan between forward and reverse and be able to make it scan very slowly" | SCAN -64 .. +63 (`F_BIPCT`): the sign the direction, a ramp over the whole buffer (POS the start), a round in about 110 s at 1, halving every 8, 0.47 s at 64; 0 holds |
| "The generated A/B waveforms and also the combined morphed one" | the wave band (`ENG_B_WAVE`, `cr_draw.c CR_W_WAVE`, the data from `eng_grain.c grain_wave`): on the SRC screen the active lane's buffer (A on SRC 1, B on SRC 2) in white; on the GRAIN screen both at once, A blue and B orange, each as bright as MORPH gives it (MORPH 0: A alone bright; 127: B); SPRD a dim green span round POS, POS a white line that moves with SCAN (3y167: the GRAIN screen draws the mix: the third phase) |
| "animated grains moving across the waveform" | every grain sounding on the part is a bright vertical line of its source's colour at its place in the buffer, as strong as its window is open: they travel as they read (forward or reversed), appear and fade with DENS and SIZE; the band redraws every third frame while the screen is open (20 a second; a redraw is ~100 lines over the LCD), and holds while a source renders (3y167 .. 3y168: one colour; while a source renders, redrawn as each render lands) |
| "Filter should use the same filter from VA engine" | the VA's SVF a voice (`va_fweights`, `va_svf`: FTYPE 0 .. 127 round LP BP HP NOTCH with the crossfade between, CUT RES, DRIVE's pre-filter tanh 1x .. 4x, KTRK round C4, the same knee), on the `FILTER` (CUT RES FTYPE DRIVE) and `FILTER+` (KTRK) pages under the filter band |

Also: `ENV 1` and `ENV 2` are plan screens (so ENV 2 reads "ENV 2 · mod", not the title rules' "filter"); the window is
sin² from the sine table, not a 514-byte table; `eng_grain.c` is included after `eng_va.c` and needs `FELUCCA_VA`
(its filter, sync list and amount rule are the VA's); GRAIN's pool state is its own POOL array (the union is defined
before). **Flash after it: XIP 569 620 B of 573 440 (99.3 %), 3.8 KB left**; RAM 80 132 (81.5 %), POOL 306 196
(89.0 %). The matrix, the LFOs, ENV 2 and the filter cost about 7.5 KB of code. The size pass (tools/size_fns.py's
minsize) over the whole of GRAIN gives back 80 bytes (measured: 569 540 with it): the audio code is -Os already. What
would give flash back: a family of the generator (DRONE 1.1 KB, VOWEL 0.95, SWEEP 0.85 of code each), the second LFO or
ENV 2 (a few hundred bytes each), or room found elsewhere in the image: the owner's call.

## The third phase: MORPH a mix, the render in place (the owner, 2026-10-16, HISTORY 3y167 .. 3y169)

The owner, after playing GRAIN on the unit: "For morph, it morphs the grains not the waveforms, I think it should
basically mix and blend the waveforms, so they are mixed together and the grains are scanning the mixed waveform. Mix
should be realtime." "For the wave generation, I'd like this to maybe be spaced across frames so you can change the
wave generation params in realtime without slowdown, instead of having it pause and regenerate the whole thing in one
frame." "Mixing the A and B buffer should be free cost-wise. I'd go with the render in place, if that is not great
we can try third buffer."

**MORPH is a mix (3y167).** A grain has one read head over both buffers: a fetch reads A and B at the same place and
blends them, `b = A + (B - A) x MORPH` (Q15, 0 .. 32768 = B alone: (B - A) x 32768 still fits int32), and the
grain's two-sample interpolation, window and level are as before. MORPH is the voice's effective one (the knob, the
track's SHP, the matrix's MORPH from ENV 1 / ENV 2 / the LFOs / VEL / KEY / RAND / MODW), taken once a block as
before but **ramped a fetch** from the last block's value (`voice_t.s[1]`) to this one's, so an LFO or an envelope
on MORPH does not zipper at the block rate; the step a fetch is (m1 - m0) / the fetches the grain makes in the
block (`GP_RCP`, 32768 / n rounded down: no divide, never past m1). Where and when the grains start no longer depends
on MORPH (the old share's Bresenham count is gone), so the test checks the mix exactly: MORPH 64's output is A's and
B's outputs mixed sample for sample. **The root:** one read head, one root, **source A's** (`grs.root16[0]`): MORPH
0 plays A exactly as before; a PLUCK as B (ACID DUST) plays 6 cents sharp of A, a PLUCK as A (STRUM) leaves B 6
cents flat (the average would put both 3 cents off). A linear mix of two unrelated materials sits about 3 dB under
their average at MORPH 64 (CLOUD PAD's sweep: 7 % RMS at A, 3 .. 4 % in the middle, 5 % at B); an equal-power law
would cost a second multiply a fetch: the owner's call after listening.

The loop, from `build/choralroot.dis` (`grain_render`, `gp_run` inlined), per output sample of a grain:

| | before (3y166) | after |
| --- | --- | --- |
| forward: the sample (interpolate, window, accumulate, the phase) | 20 | 18 |
| forward: a fetch (C4 at the root: half a fetch a sample; 1 .. 2 at an octave or two up) | 9 | 14 (a second read, the multiply-add, MORPH's step) |
| reverse: the sample / a fetch | 20 / 9 | 16 / 11 |
| a forward grain at C4 (0.5 fetch) / an octave up (1) / two (2) | 24.5 / 29 / 38 | 25 / 32 / 46 |

The first version (the fetch indexing `grain_buf[0][pos]` and `grain_buf[1][pos]`, MORPH's step a sample) was 22 +
18 a fetch: the compiler rebuilt the second buffer's address on every fetch (the load's offset reaches a few hundred
bytes, not 32 KB) and spilled the step; two read pointers stepped together and the step a fetch gave it back. The
regress tool's host count (8 notes held, instructions a sample): CLOUD PAD 1477 -> 1542, SHIMMER 1546 -> 1685, BREATH
PAD 1471 -> 1543, FROZEN 1417 -> 1495, STRUM 1355 -> 1389, SWARM 1494 -> 1552, ACID DUST 1622 -> 1678, GLITCH 1409 ->
1458 (+2.5 .. 9 %; `tests/cpu_baseline.txt`); `tests/target_budget.py`'s weighted count of `grain_render` 1162 ->
1394 (the mix and the step's ramp below; its budget line moved with it). In the unit's terms a full part's grains (16) at C4
are 16 x 25 x 128 / 205 = 250 us a half (before: 245), an octave up 320 (290).

**The wave band** draws the mix the grains read on the GRAIN screen: A's and B's cached min / max envelopes weighted
by MORPH as the latest note plays it (`gp_part_t.morph`, modulation included; the patch's MORPH with no note), in a
colour from A's blue to B's orange; the grains are one colour (white), as they read both. The SRC screens draw their
source alone, as before. The envelopes are kept column by column as the render writes (`grain_wv_dirty`: the columns
a slice touched are retaken when the band asks), not whole after each render.

**The render in place (3y168).** No lock, no fade: the grains play on while the new material is written over the
old. `gs_finish` normalizes and high-passes over the whole buffer, which a partial write cannot do, so the render is
**two passes of the deterministic generator** (`grain_src.c`: `gs_step_to`, `gs_measure`, `gs_gain`, `gs_apply`):
a measuring pass renders slice by slice into a scratch (`grain_sc`, 2.3 KB of POOL), runs the finish's high-pass
over it and keeps the peak, and writes nothing; then the writing pass renders again from the start, high-passes and
applies the measured gain as it goes, and writes each slice over the old material. The high-pass is the same
recursion over the same raw samples, so **the finished buffer is byte for byte the whole render's**:
`tests/cr_grain_golden.txt` holds untouched, and the test compares every family's in-place render with `gs_finish`'s.
PLUCK's string reads its own last 85 samples: the scratch keeps the last slice's raw tail ahead of the next
(`GS_HIST` 128). Slices by TYPE (`GRAIN_SLICE_N`), the cost at 205 instructions a us (x 1.5 the unit's factor):

| TYPE | before: a buffer, ms | now: two passes, ms | slice | polls a render | a poll, ms |
| --- | --- | --- | --- | --- | --- |
| PLUCK | 6.6 / 10 | 11.9 / 18 | 1024 | 32 | 0.40 / 0.60 |
| DRONE | 28.1 / 42 | 54.9 / 82 | 256 | 128 | 0.44 / 0.65 |
| BELL | 9.9 / 15 | 18.6 / 28 | 512 | 64 | 0.30 / 0.46 |
| BREATH | 12.2 / 18 | 23.3 / 35 | 512 | 64 | 0.38 / 0.57 |
| CHIRP | 10.2 / 15 | 19.1 / 29 | 512 | 64 | 0.31 / 0.47 |
| SWEEP | 21.1 / 32 | 41.0 / 61 | 256 | 128 | 0.33 / 0.49 |
| VOWEL | 25.6 / 38 | 50.0 / 75 | 256 | 128 | 0.40 / 0.60 |
| METAL | 16.9 / 25 | 32.5 / 49 | 512 | 64 | 0.52 / 0.78 |

(The families' loops 58 .. 327 instructions a sample, `tools/grain_cost.py`, unchanged by the slice pointer; the
measuring pass 11 a sample, the writing pass 20 and the copy; before, one slice of 1024 was 2.4 ms of DRONE with the
unit's factor.) A render costs twice the CPU it did, but nothing goes quiet: **a poll stays under 0.8 ms** (the
owner's "without slowdown": the frame that runs it keeps its time), the core-0 tick and the main-loop poll keep their
split, and the host paths (`GRAIN_ASYNC 0`: hostsim, regress) render whole in the tick as before.

**Latest wins at the end of a render, never inside one.** A knob turned during a render lets it finish on the knobs
it started with, and the next render starts on the knobs as they are then (the turns between are skipped). The old
rule (restart on every change) would never finish while a knob is turned steadily (a DRONE render is ~120 ms of
main-loop time; a knob sends a detent every few tens of ms), so the sound would only move when the hand stops; with
the render finishing, it moves with every render, several times a second. Both sources asked: the one not rendered
last goes first, so turning A on and on does not hold B's change back. `tools/grain_listen.c`'s model of the unit
(the polls get half the time, x 1.5), turning TONE 0 .. 127 over 8 s and SEED 0 .. 60 over 8 s of a DRONE under a
held chord: **97 renders landed in the 18 s; 0.00 s under 1 % of the chord's level** (before: 60 renders, **13.1 s
silent**).

**Clicks (3y169).** A grain reading where the head writes sees the old material become the new: the head overtakes it
between two blocks (85 .. 90 % of the cases), or it reads across the head inside one. Measured on the host
(`tools/grain_listen.c`, the turn above, A of each TYPE alone): 1 400 .. 2 200 such splices in 18 s, the step
between the old and the new sample at the grain's place a median 0.5 % of the grain's level, the 99th percentile 11
.. 32 %, at most 38 .. 109 %. Two softeners, both cheap: **a crossfade at the write head** (each slice's last
`GRAIN_RAMP` 64 samples fade from the new material into the old ahead, the next slice writes them whole: a grain
reading across the head hears a 64-sample crossfade; not on the last slice, so the finished buffer stays exact), and
**a grain's step ramped out** (the tick takes the samples rewritten since the last block; a grain whose two held
samples are among them takes the new ones at once and carries the difference as an offset ramped to zero over
`GP_DECLICK` 4 blocks, 2.9 ms; a loop of its own in the blocks that need it). What a grain plays, its peak second
difference a block in % of its level, in the blocks with a splice against the clean blocks' 99.9th percentile:

| A | clean blocks: 99.9th | spliced: 99th, softened (off) | spliced above the clean 99.9th, softened (off) |
| --- | --- | --- | --- |
| PLUCK | 13.5 | 8.5 (13.3) | 0.25 % (0.90 %) |
| DRONE | 35.8 | 27.5 (38.0) | 0.09 % (1.17 %) |
| BELL | 4.0 | 4.8 (11.5) | 1.33 % (4.05 %) |
| BREATH | 100 | 64.8 (69.5) | 0 (0) |
| CHIRP | 10.8 | 40.0 (94.0) | 5.34 % (26.7 %) |
| SWEEP | 33.5 | 42.8 (64.0) | 1.55 % (6.17 %) |
| VOWEL | 12.8 | 23.5 (57.5) | 2.52 % (8.34 %) |
| METAL | 5.0 | 8.0 (23.3) | 2.70 % (18.2 %) |

So the softeners take 3 .. 10 times fewer spliced blocks out of the material's own range; what is left is a change
of slope where the material changes (an offset cannot take that out: the old material is gone), largest on CHIRP,
whose SEED turns its sweep the other way. A third buffer (the render beside, the grains moved over at a block
boundary) would have no splices at all for 32 KB of POOL (89.7 % -> 99.2 %): the owner's fallback if the in-place
render is not great by ear. The listening files: `build/grain-mix/INDEX.md` (`tools/grain_listen.c`).

Sizes: XIP 541 948 -> 542 764 B (94.7 %), RAM 80 368 -> 80 544 B, POOL 306 196 -> 308 508 B (89.7 %, the scratch).

## 1. The source generator

### The buffer

| | rate | samples | ms | bytes a buffer | the sound's A and B | POOL after (now 238 936 B, 69.4 %) |
| --- | --- | --- | --- | --- | --- | --- |
| **proposed** | 22 050 | 16 384 | 743 | 32 768 | 65 536 | 304 472 B, **88.5 %**, 39.6 KB left |
| a second | 22 050 | 22 050 | 1000 | 44 100 | 88 200 | 327 136 B, 95.1 %, 16.9 KB left |
| shorter, with a third buffer as the render's scratch | 22 050 | 12 288 | 557 | 24 576 | 73 728 (3) | 312 664 B, 90.9 %, 30.7 KB left |
| full rate, half a second | 44 100 | 22 050 | 500 | 44 100 | 88 200 | 95.1 % |

**22.05 kHz**: the delay and the reverb already run there (docs/FX.md "The rates"); a grain played two octaves up
reads the buffer at two source samples an output sample (Felucca's cap, kept), so the material's 11 kHz top is the
output's 22 kHz there; granular textures do not miss the top octave, and the render costs half. **743 ms** is a
power of two of samples (the position arithmetic stays shifts), longer than the longest grain (393 ms) by nearly two,
and leaves POOL at 88.5 % with two buffers. **Two buffers: the sound's sources A and B.** They are the chord part's;
a bass part on GRAIN plays the same two buffers with its own grain knobs and its own MORPH (its source pages show the
chord part's values, greyed: "shared"). So a re-render has no spare buffer: **it goes in place, one source at a
time**: until 3y168 with a fade (the grains reading the buffer being re-rendered faded out over one block, the slices
wrote the buffer, the grains came back when it was done: that source quiet while its knobs turned); since 3y168
written over the old material while the grains play on (the third phase, above). The alternative, always seamless, is three
buffers of 557 ms (A, B and a scratch the pointers rotate through, 90.9 %); the sketches render 743 ms (open
question 1).

### The four knobs

| knob | range | what |
| --- | --- | --- |
| **TYPE** | 8 families (a picker, F_ENUM) | PLUCK DRONE BELL BREATH CHIRP SWEEP VOWEL METAL |
| **TONE** | 0 .. 127 | the brightness or colour within the family |
| **MOTION** | 0 .. 127 | how the material moves over its 743 ms: decay, sweep, vibrato, re-plucks, strums; material that is interesting to granulate has movement inside it, and a still buffer sounds like a looped sample |
| **SEED** | 0 .. 127 | a variant: ratios, detunes, phases, noise, the partials, the vowel order; the same SEED is the same material |

The eight families, written from the firmware's own blocks (dsp.c: `osc_saw` with its polyBLEP, `osc_pulse`, `osc_tri`,
`sine_i`, the trapezoidal SVF (`tsvf_lp` and a band-pass tap of the same filter), `noise32`, `softclip`, `soft_knee`;
felucca_tables.h `pitch_inc`, `SVF_G`), all int32, every family one loop over the samples with a control tick every 32:

| TYPE | what it is | TONE | MOTION | SEED | instructions a sample (device listing) | render, ms at 205 / us (x 1.5: the unit's factor so far) |
| --- | --- | --- | --- | --- | --- | --- |
| PLUCK | a Karplus-Strong string, the buffer its own delay line (period 84 samples: C4 + 6 ct) | the burst's and the loop's brightness | the decay; above 64 a strum of re-plucks (two .. five) | the burst | 58 | 4.6 / 6.9 |
| DRONE | three detuned saws (the third a pulse on odd seeds) and a sub sine through the SVF, its cutoff swept by a triangle LFO | the cutoff's centre | the detune (0.1 .. 2.4 %) and the sweep's rate (0.25 .. 4 Hz) | phases, the pulse's width, the resonance | 327 | 26 / 39 |
| BELL | two-operator FM, the modulator at an inharmonic ratio, the index decaying | the index (0.4 .. 8 rad) | the index's decay (600 .. 40 ms); above 64 a second pair beats against the first | the ratio (twelve) | 100 | 8.0 / 12 |
| BREATH | white noise through a resonant band-pass whose centre wanders under a tremolo | the centre (300 Hz .. 6.5 kHz) | the wander's depth, the tremolo's rate and depth | the walk, the resonance | 129 | 10 / 16 |
| CHIRP | a sine sweeping a span of semitones, repeated; a second one a fifth up on some seeds | the span (6 .. 57 semitones) | chirps a buffer (1 .. 16) | up / down / alternating, the second voice | 103 | 8.2 / 12 |
| SWEEP | a saw and a pulse (an octave down on some seeds), driven, through the SVF sweeping | the resonance and the sweep's top | sweeps a buffer (1 .. 8) | the pulse's width, the sweep's shape (down, up, both) | 240 | 19 / 29 |
| VOWEL | a narrow pulse with vibrato through three band-passes at a vowel's formants, travelling round A E I O U | where on the cycle it starts | how far it travels (still .. two rounds) and the vibrato's depth | the vowel order, the vibrato's rate | 296 | 24 / 36 |
| METAL | six sine partials at inharmonic ratios, each decaying at its own rate, a noise strike | the partials' spread (1.5 .. 8) and the strike | the decay (900 .. 80 ms) | the ratios, the levels | ~187 | 15 / 22 |

Every render ends with one pass (`gs_finish`, 24 instructions a sample, 1.9 ms): a one-pole high-pass at 20 Hz takes
the DC out (SWEEP's pulse carries some) and the buffer is normalized to a peak of 28 000 (a gain of at most 8). The
shifts that feed back are rounded to nearest, not floored: a floored shift's bias integrates (the high-pass settled at
-88 raw, which an 8x gain made -700, 2 % of full scale, in every quiet buffer; PLUCK's loop would hold its bias as
long as the string rings), and PLUCK's noise burst goes through a 70 Hz blocker of its own, since the string's loop
holds the burst's mean as DC. Step 1's test found both. So every buffer has the same peak; the RMS differs by family (CHIRP 0.59, PLUCK 0.12: table in
`build/grain-sketch/INDEX.md`), which the part LEVEL and the presets' trims take care of as for every engine.

The code is `firmware/src/grain_src.c` (`gs_*`; step 1, 2026-10-15): `gs_begin(knobs)`, `gs_step(n)` renders the
next n samples from the state it keeps, `gs_finish()`. `tools/grain_sketch.c` includes it, so the sketches heard are
the firmware's generator byte for byte; `tests/cr_grain_test.c` holds the 24 sketch settings' hashes as goldens
(`tests/cr_grain_golden.txt`) and compares the sketch WAVs with its own renders.

### Two sources and MORPH

A sound has two source rows, A and B (TYPE TONE MOTION SEED each: eight values), two buffers, and one knob, **MORPH
0 .. 127**, between them. The blend is granular, not a mix: **each grain reads A or B**, and MORPH is the share of a
voice's grains that read B, spread evenly over its grains (a Bresenham count a voice: MORPH 64 is A B A B .., 32 is
A A A B ..), so MORPH 0 is A alone, 127 B alone, and in between the two materials interleave in the grain cloud, each
grain pitched against its own buffer's root. It costs nothing a sample (a grain already holds its source pointer; the
choice is 28 bytes of code in `gp_spawn`) and nothing of memory beyond the second buffer. The other way, every grain
reading both buffers at its position and mixing them by MORPH, is a plain crossfade (two layers that fade; a second
fetch a sample, +30 % a grain): the sketches render it beside the granular morph for two pairs (`-sweep-xfade.wav`)
so the owner can hear the difference, and it is not the design. **Decided** (the owner, 2026-10-15, after hearing
them: "Keep the granular morph (each grain reads A or B by MORPH's share) as designed"). **Changed** (the owner,
2026-10-16, after playing it on the unit: "it morphs the grains not the waveforms, I think it should basically mix and
blend the waveforms"): since 3y167 every grain reads A and B at its position and mixes them by MORPH, a fetch at a
time, the grains scanning the mix (the third phase, above), one read more and a multiply-add a fetch.

**MORPH is a modulation destination**: the platform's SHP destination of the track envelope, the LFO and the matrix
binds to MORPH (Felucca's GRAIN bound SHP to POS; POS has SCAN now). ENV -> MORPH with a sound whose B is a PLUCK
or METAL and A a DRONE gives B's strike into A's body (the `morph-*-env.wav` sketches: MORPH falling 127 -> 0 in 0.6
s); LFO -> MORPH breathes between two materials; the matrix's VEL -> MORPH plays B harder. A render of A or B while
MORPH sits at the other end is not heard at all. (Since 3y167 the modulation is a voice's, ramped a fetch: an LFO or
an envelope on MORPH moves the mix smoothly.)

### When and where the render runs

- **When**: a sound loads with GRAIN, the part's engine switches to GRAIN, or one of TYPE TONE MOTION SEED changes
  (the editor's set, the macros' P_E, the matrix never: the source knobs are not modulation destinations; a render
  is not a per-tick thing). The engine's `block()` hashes the four values; a change posts a request. **Latest wins**:
  a request during a render restarts it on the new values at its next slice; a knob turned through ten detents
  renders once or twice, not ten times. (Since 3y168 latest wins at the end of a render: it finishes, then the
  latest knobs render: the third phase.)
- **Where: core 0's main loop, in slices.** `main.c`'s loop draws a frame every 15 ms and waits the rest of the
  frame in an idle loop (`while (fm1_ms - m < 15u) { ui_input(); ed_service(); }`). `grain_poll()` renders one slice
  of 1024 samples a call (DRONE, the heaviest: 1.6 ms, 2.4 with the unit's factor; PLUCK 0.25 ms) and returns: once
  right after every frame's draw, whatever the frame cost, and once a turn of the idle loop (3y138: on the unit a
  frame that fills its 15 ms, the wave band's redraw, left the idle loop no turn and the knobs' renders waited for a
  page change). The audio ISR preempts the main loop as it preempts a frame's drawing: **the audio never waits for
  the render**. A whole buffer is 16 slices: DRONE lands in 26 .. 40 ms of main-loop time when the frames are cheap,
  within 16 frames (a quarter of a second) when they are not; PLUCK in one or two. (Since 3y168: two passes, slices
  of 256 .. 1024 samples by TYPE, a poll under 0.8 ms: the third phase's table.)
- **The swap** (the design's; the build went in place, first with the fade, since 3y168 under the playing grains):
  the slices write the scratch (the other part's buffer, or in place with the fade: above). When the
  render is done, `block()` (the audio ISR) sees the ready flag at a block boundary and takes the new pointer; the
  grains in flight hold their own `src` pointer and finish on the old buffer; the next render waits for the pool to
  have no grain on the old buffer (`gp_part_t` counts them). Nothing is copied.
- **Core 1** is the alternative, not the first build: it is nearly idle (the bass's render 300 .. 500 us and the
  reverb's job 200 of each 2902 us half), so a third job "render the next 2048 samples" in each block would land
  DRONE in eight blocks (23 ms) with no main-loop cost at all; but core 1 is optional (Dual Core Off, the single-core
  package, a unit that gave it up), so the main-loop path has to exist anyway. Core 1 can take the slices later
  with the same `gs_step`.
- **Latency on a knob turn**: a detent's render reaches the ear 30 .. 60 ms later (the slices, then the swap at a
  block boundary, then the grains that start next). While turning, the old material plays: no gap, no click.

## 2. The grain player

From `eng_grain.c`, what stays, what goes, what changes:

| | Felucca's GRAIN | this GRAIN |
| --- | --- | --- |
| source | ADPCM zones in flash, a zone per key range, decoded on the fly from a seek index built in the block hook | one int16 buffer in POOL, written by the generator; no decoder, no index, no zones |
| the pool | GR_NG 12 grains a part shared by GR_POLY 3 voices (a voice's share GR_NG / voices, at least 2) | **GP_NG 16 grains, GP_POLY 8** (ChoralRoot's chords are eight notes); the share rule kept: 16 / voices, at least 2 |
| a grain | position, length (SIZE 10 .. 393 ms), rate Q16, direction, Hann window (257-point table, linear interpolation), level 1 / sqrt(overlap); the window times the level ramps linearly over the block | **kept as it is** (`gp_run` is `gr_run` with `src[pos++]` for the decode); a reverse grain reads `src[--pos]`: Felucca's 128-sample reverse windows go |
| the rate | the note against the zone's root, PITCH, a random detune (RAND, quadratic, up to +-1 octave), capped at 2 source samples an output sample | the note against the buffer's root (GS_ROOT16 = C4, PLUCK's 961), PITCH -24 .. 24, the detune as before, times 22 050 / 44 100; the cap kept: **the range is C1 .. C6 from one buffer** (two octaves up from the root at the cap; downwards unlimited) |
| the start | DENS a second (1 .. 98), SPRD jitters the interval; POS (+ the track's SHP modulation) the centre, SPRD the random spread; at most one start a voice a block | the same (until 3y167 plus the grain's source, A or B by MORPH's share; since, every grain reads the mix); POS also moves by **SCAN** (a slow triangle over the buffer, 0 off .. a round in 0.5 s: the frozen-scan and the wandering-pad movements of the sketches); SHP modulates MORPH now, not POS |
| RAND | the detune and the share of reversed grains in one knob | kept in one knob (the row's fourth cell went to MORPH); a REV knob of its own is a later option |
| TONE | a one-pole low-pass a voice (+ the track's FLT modulation) | **CUT, RES**: the SVF low-pass a voice (`tsvf`), the FILT group's band drawn; the track's FLT modulation adds to CUT |
| the cost bound | the part's decode load capped at 8 source samples an output sample | none needed: a grain costs the same at any rate; the pool's size is the bound |
| state | `gr_part_t` in its own pool section (27 KB with the index and the reverse windows) | `gp_part_t`: 16 x 44 B + 24 B = 728 B (a grain's source pointer among them), a member of `eng_state`'s union (FM6's 6.9 KB: no growth); a voice's `gp_voice_t` (next-grain countdown, the MORPH count, the filter) in `voice_t.s[]` as Felucca kept it |
| GR_POLY | 3 | 8 (the budget below) |
| voices.c's view | `sampled 1` (positions, no phases), `keep` the filter | the same |

**Why one root and not two or three.** A sample set needs zones because a recording repitched three octaves is a
different instrument. Here the material is synthetic and the player's repitch is the granular sound (formants and
decays shift with the note: a chord of grains is meant to sound like that), the generator's tuning is exact at the
root, and C1 .. C6 covers the chord part and the bass. A second buffer at another root would double POOL for a
difference the owner may not want; **if a note above C6 is wanted, the generator can render the buffer at the
chord's root instead** (a render on every chord change: 4 .. 40 ms of main loop, acceptable for the PLUCK .. CHIRP
families, slow for DRONE) as a later mode, not now.

## 3. The pages, presets, slot, records

### The knobs: sixteen values, four rows of four, plus the filter

The eight `P_E0 .. P_E7` are **TYPE TONE MOTION SEED (source A) | MORPH SIZE DENS POS** (EDIT 1: the first source;
EDIT 2: the playing knobs; `engine_t.knob` = MORPH SIZE DENS POS). The whole sound is a small patch the engine
owns, `eng_deep_t` as the VA's, the eight P_E macros into it (`block()` picks a macro change up, `set` writes the
macro back, as the VA):

| page (the editor's title rules, docs/EDITOR.md section 4) | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 | group / screen |
| --- | --- | --- | --- | --- | --- |
| `SRC 1`, `SRC 2` | TYPE (picker: PLUCK .. METAL) | TONE | MOTION | SEED | OSC screen 1 (the plan): rows A and B under **the wave band**, the active row's buffer |
| `GRAIN` | SIZE (10 .. 393 ms) | DENS (1 .. 98 /s) | POS (0 .. 100 %) | SPRD (0 .. 100 %) | OSC screen 2 (the plan), row A, the wave band over both buffers, morphed |
| `GRAIN+` | **MORPH** (A .. B) | RAND (detune, reversed) | PITCH (-24 .. +24 st) | SCAN (-64 .. +63: reverse .. forward) | OSC screen 2, row B |
| `FILTER`, `FILTER+` | CUT | RES | FTYPE (the VA's cycle) | DRIVE; row B: KTRK | FILT (the title rules): the VA's filter curve |
| `ENV 1`, `ENV 2` | ATK | DEC | SUS | REL | ENV (the plan): the platform's amplitude envelope, then the engine's modulation envelope, each under the AHDSR ("ENV 1 · amp", "ENV 2 · mod") |
| `LFO 1`, `LFO 2`, `LFO SYN` | RATE | WAVE | DEPTH | FADE; the SYN page SYNC1 SYNC2 | LFO (the title rules): a stack of the two, the SYNC stack |
| `MOD 1 .. 8` | SRC | DST | AMT | – | MOD (the title rules): the matrix's stack of eight, then EXPR |
| FX, MIX | the platform's | | | | |

Four rows of four and a filter row: the owner's grammar (up to four cells a row, the knob colours, the active row
highlighted, SELECT walking the lanes: SRC 1, SRC 2, GRAIN, GRAIN+). No matrix of its own ("not modulatable"); the
platform matrix's SHP reaches MORPH and FLT reaches CUT.

**The wave band** (`ENG_B_WAVE`, new in `cr_draw.c`; the SRC and GRAIN screens): a buffer drawn as its envelope
across the 240 columns (68 samples a column: a min / max scan of the live buffer at frame time, 16 384 reads, ~0.25
ms, no RAM). On the SRC screen the active lane's buffer, redrawn when its render lands: a knob turn visibly reshapes
the material. On the GRAIN screen both buffers, A over B in the band's two halves, POS as a bright mark and SPRD as
a dimmer span through both in KNOB 3's and KNOB 4's colours, and **the live grains as ticks** moving across (each
grain's position read from the pool at frame time, in A's or B's half: MORPH is seen as where the ticks are). One
big thing per screen, animated.

### Presets

A pool for the engine (docs/PRESETS.md: INIT at 00, then the factory presets, then the user's), the engine after
WHEEL in `ENGINE_ORDER` and the picker's keys. INIT: A DRONE 64 / 64 / 0, B BELL 64 / 64 / 0, MORPH 0, SIZE 92 DENS
88 POS 50 SPRD 40, RAND 14 PITCH 0 SCAN 30, CUT 100 RES 20 (the sketches' "pad" grains over `morph-drone-bell`).
Eight factory presets to start, by ear with the owner: CLOUD PAD (DRONE / BELL, MORPH 40), SHIMMER (BELL / BELL
bright, PITCH +12, LFO -> MORPH), BREATH PAD (BREATH / VOWEL, MORPH 64), FROZEN (VOWEL / BREATH, SPRD 0, SCAN slow),
STRUM (PLUCK / METAL, MOTION 100, ENV -> MORPH), SWARM (CHIRP / SWEEP, RAND 60), ACID DUST (SWEEP / PLUCK, small
grains), GLITCH (METAL / DRONE, SIZE 24 DENS 112 SPRD 100 RAND 90). Each with its ADSR and sends (`preset_t`) and its
patch blob (`blob_preset`).

### The slot and the records

- **Engine 8** again: `ENGINES[8] = &ENG_GRAIN`, `FELUCCA_GRAIN 1` in choralroot.c and the emulator, the bit out of
  `ENG_RETIRED`, `NENG_SHOWN` follows. `engines.c`'s `#error "GRAIN and SLICE play the SAMPLE engine's sets"` goes:
  this GRAIN needs no SAMPLE. The source file: `eng_grain.c` replaced (Felucca's player lives on inside it, its
  decoder goes to git history), or the new engine beside it as `eng_grain_syn.c` with Felucca's kept for Felucca's
  renders (`tests/golden.txt` has `preset/GRAIN/00_CLOUD_PAD ..` from the old engine): the orchestrator's call, step 4.
- **The blob**: `'G'`, version 1, then A's TYPE TONE MOTION SEED, B's TYPE TONE MOTION SEED, SIZE DENS POS SPRD,
  MORPH RAND PITCH SCAN, CUT RES (18 bytes, PITCH signed), zeros to 20; `eng_deep_t.blob_size` 20 (`upool.c` UPP_MAX
  188: fine). **The sound stores the knobs, never audio.** A user record, a backup (`cr_backup.c`), a SysEx sound
  export and the web manager (docs/SOUNDS.md: patch kind `grain`) carry the 20 bytes as any engine's blob.
- **Old records of engine 8** (a Felucca unit's, or ChoralRoot's "retired: stand-in" cases): their P_E were SRC POS
  SIZE DENS PTCH SPRD RAND TONE; a record with no blob loads INIT's sources and MORPH 0 with the record's SIZE DENS
  POS SPRD PITCH RAND in the new places and TONE as CUT (`cr_ui.c cu_load_user`, where the stand-ins are), so an old
  GRAIN sound still granulates something of the right shape.

## 4. Budgets (measured where the host can measure)

Counted as docs/FX.md "The cost on the unit" counts: the device compiler's listing (`python3 tools/grain_cost.py`:
`tools/grain_sketch.c`'s core compiled alone with the firmware's `-Os` for pi32v2, `build/grain-cost/unit.dis`), 205
instructions a microsecond, and the unit's factor of 1.2 .. 1.7 over the count seen so far (the fx stage 1.2 .. 1.3,
CZ-1 1.7, P5 1.54) given as x 1.5. The baseline is main da1f8e2's size line: XIP 547 020 B of 573 440 (95.4 %), RAM
78 652 of 98 304 (80.0 %), POOL 238 936 of 344 064 (69.4 %).

### Flash

| piece | bytes | from |
| --- | --- | --- |
| the generator: `gs_begin` (the eight families' set-up, inlined) | 1250 | the listing |
| the eight family loops | 4550 (PLUCK 234, DRONE 1124, BELL 368, BREATH 460, CHIRP 386, SWEEP 846, VOWEL 950, METAL 182) | the listing |
| `gs_step`, `gs_finish` | 264 | the listing |
| the player: `gp_spawn` (MORPH's choice among it), `gp_run`, `gp_voice_block`, `gp_voice_on`, `gp_init`, `pow2_q16` | 1404 | the listing |
| **the core** | **7500 B of .text** (7130 before the per-family split; 7420 before MORPH; 7456 before step 1's rounding and PLUCK's blocker) | `objdump -h` |
| its own tables: the Hann window 514, the norms 34, the ratios 24, the vowels 30 | ~600 B of .rodata (the listing's 4192 includes `SINE`, `TANH_Q15`, `SVF_G`, `PITCH_OCT`, which the firmware has) | |
| the engine's glue: `engine_t`, the pages and `eng_deep_t`, `block()` with the request / swap, the blob get / set, the presets (9 x ~24 B), the old-record mapping | ~2.5 .. 3 KB | estimate from the VA's and QUAD's glue |
| the wave band in `cr_draw.c` | ~1 KB | estimate (the filter band is 0.8 KB) |
| **the engine** | **~11 .. 12 KB: XIP 547 020 -> ~559 000 B, 97.4 .. 97.6 %, 14 .. 15 KB left** (the estimate) | |
| **as built (step 4, 3y136)** | **XIP 559 652 B (97.6 %): +12 632 B, 13 788 B left; RAM 79 028 (+376), POOL 304 472 (+65 536, 88.5 %)** | `./build.sh`'s size line |
| **merged on main d75523e (v0.24-beta1 with the flash diet, 3y126 .. 3y132; the second phase in)** | **XIP 537 068 B (93.7 %), 36 372 B left; RAM 80 176 (81.6 %), POOL 333 900 (97.0 %, 10 164 B left: the diet's HARM cycles, 27.7 KB, live in POOL now)** | `./build.sh`'s size line, the byte the diet session measured |
| **3y139: the first build's 20-byte version-1 records read again (upool.c, cr_backup.c, the installer, the web page)** | **XIP 537 068 B (93.7 %), the same bytes to the slot: a compare in the pool's reader and the restore's check** | `./build.sh`'s size line |

The tightest resource. `FELUCCA_SIZE`'s minsize pass (tools/size_fns.py) can take the generator's set-up code
(`gs_begin`, 1.2 KB, runs once a render) if XIP is wanted back.

### POOL and RAM

| piece | bytes | where |
| --- | --- | --- |
| two buffers (the sound's A and B; a second part on GRAIN shares them) | 65 536 | POOL: 69.4 % -> **88.5 %** |
| `gp_part_t` x 2 (the pool of 16 grains with their source pointers) | 2 x 728 | `eng_state`'s union (no growth: FM6's member is 6.9 KB) |
| the generator's state `gs_t` (one render at a time) | ~220 | .bss |
| the patch x 2 parts, the macros as last seen | 2 x 40 | .bss |
| **RAM** | **~320 B: 80.0 % -> 80.3 %** | |

### CPU

| piece | instructions | per | us a half (x 1 / x 1.5) |
| --- | --- | --- | --- |
| a grain, forward (`gp_run`'s loop body 26, the fetch loop 9 taken 0.5 .. 2 times a sample at rates 0.5 .. 2) | 30 .. 44, 37 typical | an output sample | **23 / 35** |
| a grain, reverse | the same (26 + 9 a fetch) | | 23 / 35 |
| the pool full, 16 grains | | | 370 / 555 |
| a voice's block: its filter and clamp loop 33 a sample, the pool scan and the start ~60 a block | 33 | a sample | 21 / 32 |
| the platform's `voice_amp` on the voice's output | ~10 | a sample | 6 / 9 |
| 8 voices | | | 220 / 330 |
| a grain start (`gp_spawn`: two `pow2_q16`, the fits, MORPH's choice) | ~160 | a start, at most one a voice a block | 8 voices: 6 / 9 |
| MORPH | 0 (3y167: 5 a fetch, ~1.5 .. 8 a grain-sample: the third phase) | a sample (until 3y167 a grain read one buffer whatever MORPH was) | 0 (3y167: ~2 .. 30 a half for 16 grains) |
| **a part at its worst: 8 voices, the pool full** | | | **~600 / ~900 (21 / 31 %)** |
| CLOUD PAD's chord (SIZE 92: 142 ms grains, DENS 88: 24 a second: 3.4 grains a voice; 6 notes fill the pool) | | | ~550 / ~830 |
| GLITCH's chord (SIZE 24: 20 ms, DENS 112: 61 a second: 1.2 a voice; 8 notes: 10 grains) | | | ~450 / ~680 |

Against the unit's readings (docs/INTEGRATION.md "Measured on the unit": the chord part's worst half fm6 1647, va
2293, cz 1439, quad 1509 us with 8 notes): **GRAIN would be the lightest chord engine, at a third of FM6's and a
quarter of the VA's worst.** The pool's size is the bound whatever the knobs do: DENS and SIZE change how many grains
sound, never past 16.

### The render burst

| TYPE | ms a buffer (x 1 / x 1.5) | slices of 1024 (ms a slice, x 1.5) |
| --- | --- | --- |
| PLUCK | 4.6 / 6.9 | 16 x 0.4 |
| BELL | 8.0 / 12 | 16 x 0.7 |
| CHIRP | 8.2 / 12 | 16 x 0.8 |
| BREATH | 10 / 16 | 16 x 1.0 |
| METAL | 15 / 22 | 16 x 1.4 |
| SWEEP | 19 / 29 | 16 x 1.8 |
| VOWEL | 24 / 36 | 16 x 2.2 |
| DRONE | 26 / 39 | 16 x 2.4 |
| + `gs_finish` | 1.9 / 2.9 | one pass |

In the main loop's idle time: no cost to the audio half; a UI frame slips by one slice at most (2.4 ms of a 15 ms
frame). A sound load renders A then B: twice the table's figure, 8 .. 80 ms. On the host (`tools/grain_sketch.c`, -O2, Apple silicon) a buffer renders in 0.1 .. 0.3 ms: the host says
nothing about the unit.

## 5. Risks and open questions for the owner

Risks:

1. **Flash**: the engine takes about half the XIP left (26.4 KB -> 14 .. 15 KB). The core is measured; the glue and
   the band are estimates until step 4's build. If XIP is wanted for other things, the generator can lose a family
   (DRONE 1.1 KB, VOWEL 0.95 KB, SWEEP 0.85 KB are the large ones) or the band can wait.
2. **A re-render goes in place** (the two buffers are the sound's A and B, no scratch): the grains of the source
   being rendered fade for 20 .. 60 ms while the other source keeps sounding; or three buffers of 557 ms (question 1).
3. **The render on a knob turn** is 30 .. 60 ms behind the hand (the slices, the swap, the next grain starts) and the
   old material plays until then; turning SEED fast sounds like stepping through variants a few frames late. If the
   owner wants it tighter, core 1's job lands DRONE in 23 ms and the lighter families in a block or two.
4. **Levels**: every buffer peaks the same, the RMS differs by family (0.12 .. 0.59); the presets' LEVEL and `CB_TRIM`
   even them, as for the other engines; `tests/va_levels.c`-style measurement in step 6.
5. **Aliasing**: a grain two octaves up reads the 22 kHz buffer at two samples an output sample with linear
   interpolation and no filter: the material's top folds (Felucca had the same cap at the same rate ratio for a
   22 kHz zone). Audible on BREATH and METAL high up; CUT tames it; the owner judges on the unit.
6. **Determinism**: the generator is integer code with no float and no libc; the same input renders the same bytes on
   the host and on pi32v2 (step 1's golden test proves the host side; the unit's buffer can be read back through the
   console or SysEx and hashed for the proof).
7. **Felucca's goldens**: `tests/golden.txt` and `tests/cpu_baseline.txt` have the old GRAIN's four presets; replacing
   `eng_grain.c` re-records those four lines (GOLDEN_UPDATE / BUDGET_UPDATE, the diff reviewed), or the new engine
   lives beside the old one under its own flag.

Open questions:

1. **The buffers**: A and B of 743 ms at 22.05 kHz (88.5 % POOL; a re-render in place with a fade of that source's
   grains), or three of 557 ms (90.9 %, always seamless), or two of 1 s (95.1 %)? Proposed: the first.
2. **A second part on GRAIN** shares the chord part's A and B (its own grain knobs and MORPH; its source rows read
   the chord part's): proposed, since its own pair would be another 64 KB. Or the bass part gets one buffer of its
   own (32 KB, 98 % POOL: no).
2b. **MORPH's spread**: evenly over a voice's grains (A B A B at 64: proposed, the sketches) or at random (a
   cloudier middle, pure ends the same)? And SHP -> MORPH (proposed) or SHP -> POS as Felucca? (The granular morph
   itself, against the crossfade, is decided: the owner keeps it.)
3. **The families**: which of the eight to keep, drop or replace after hearing `seeds-*.wav`; the names (PLUCK DRONE
   BELL BREATH CHIRP SWEEP VOWEL METAL are five characters or fewer but BREATH, which the picker can show as BREATH
   in its big type and the cell as BRTH); the knob names TONE MOTION SEED (or COLOR, MOVE, VAR).
4. **SEED's knob**: 128 variants on a knob, or fewer (16, each a detent) with OPT + the knob rolling a new one? The
   sketches use 0 .. 127; eight are in the tours.
5. **A FREEZE / LIVE type later**: the buffer filled from the live output (the chord part's dry mix, or the master)
   instead of the generator, the same player over it: a ninth TYPE and a capture path (`fx.c` mix -> the scratch
   buffer, 743 ms, taken on a button or continuously as a ring the grains read behind the write point). Not in
   this design's steps; the player and the buffer are ready for it.
6. **GP_POLY 8 with 16 grains** (two a voice at eight notes: a thin pad at full chords, and at MORPH 64 one grain
   of each source a voice; 24 grains would cost +185 / +280 us) or 6 voices as the sketches play? Proposed: 8 and 16,
   then listen on the unit.
7. **The render's home**: the main loop (proposed; works on every unit) or core 1 first?

## 6. Steps, each with its test

1. **Done (2026-10-15, HISTORY 3y135). The generator in the firmware's tree**: `firmware/src/grain_src.c` from
   `tools/grain_sketch.c`'s `gs_*` (the sketch tool includes it). `tests/cr_grain_test.c` (in `run_cr_tests.sh`,
   with `-fsanitize=signed-integer-overflow,shift`): 800 renders of a TONE x MOTION x SEED grid a family (the peak
   28 000 within the gain's rounding, or under it when the gain capped at 8: 17 of 800; the mean under 1 % of full
   scale; RMS over 1 %; the same bytes twice; slices of 512 equal one call), slices of one sample, the roots, the
   knobs' masking, eight seeds a family all different, the 24 sketch settings against `tests/cr_grain_golden.txt`
   (GOLDEN_UPDATE=1 rewrites it) and against the sketch WAVs where present, the host time a render. Nothing is in
   the image yet (no file includes it): the size line is da1f8e2's.
2. **Done (3y136). The player as an engine on two static buffers**: `eng_grain.c` (`gp_*`; `engine_t`: `note_on`,
   `render`, `block`, `poly 8`, `sampled`), the two buffers `grain_buf[2]` in POOL, MORPH's choice in `gp_spawn`
   (the voice's count in `s[1]`), SCAN (the part's phase), the SVF a voice in `s[2..3]`, the platform's amplitude
   through `voice_amp`. Test `tests/cr_grain_test.c` section 6 (the driver: the tick, the part's block, each voice
   under an open amplitude): no grain outside its buffer or past the rate's cap at six extreme knob sets (C1 and C7,
   SIZE 0 / 127, POS 0 / 127, SPRD 0 / 127, RAND 0 / 127, PITCH +-24, SCAN full), the pool's shares at one and six
   voices, MORPH 0 / 127 / 64 (A alone, B alone, both, by the knob and by SHP), the rate at C4 / C3 / the cap, the
   window's continuity (a DC buffer under one grain: the jump at a block boundary no more than twice a jump within
   one), 1.4 s of a 6-note chord on each preset (no int32 wrap, 27 .. 69 % FS at LEVEL 92, not silent, the grains
   gone after the voices). `tests/regress.c`: the eight presets' goldens and CPU lines (`tests/golden.txt`,
   `tests/cpu_baseline.txt`: the old GRAIN's four lines replaced).
3. **Done (3y136). The render scheduler**: `grain_tick` once a block on core 0 (`fx.c mix_block`, before any part
   renders: the owner's source knobs against what the buffers hold, a request a source; a locked source's grains
   killed: `gp_run` ramps them to 0 over the block), `grain_poll` in `main.c`'s idle loop and the emulator's
   `emu_fw_idle` (GRAIN_ASYNC 1: lock, wait for the fade's block to end, render 1024 samples a turn, swap the knobs
   in, unlock; latest wins: a change mid-render restarts), `GRAIN_ASYNC 0` for a host without a main loop (hostsim,
   regress: the tick renders whole). Test section 7: a load renders both; a knob change requests, locks, the
   source's grains fade and are gone after one block while the other source's play on, nothing starts on it, the
   poll waits the fade's block out, 16 slices land the generator's bytes, the unlock, the grains come back; a change
   mid-render gives one render on the latest knobs; ten changes in a row at most two renders; a second part on GRAIN
   shares (no render from its knobs) and owns when the chord part leaves GRAIN; same knobs render nothing. Not
   written: a two-thread ThreadSanitizer run (the flags are `volatile` bytes behind `fm1_irq_off`; the emulator's
   audio thread and main thread exercise the two sides in `test_cr.sh`).
4. **Done (3y136). The platform**: `ENGINES[8]` (`FELUCCA_GRAIN 1` in choralroot.c and the emulator, Felucca's
   default; `GRAIN_ASYNC 1` there), `engines.c`'s SAMPLE `#error` for SLICE alone, `eng_state`'s `grain` member,
   `ENGI_GRAIN 8u` (core.h), the deep pages (`SRC 1`, `SRC 2`, `GRAIN`, `GRAIN+`, `FILTER`, and `ENV` / `LFO` as pages
   over the track's P_ATK .. P_REL and P_LRATE P_LWAVE P_LD_PIT P_LD_FLT, since the editor wants the four sections:
   cr_edit.c ce_deep), the 20-byte blob in the pool (`upool.c`: the kind `grain`, `grain_store_read`,
   `grain_user_pending`), `fm6_track_loaded` -> `grain_track_loaded` (a user slot's blob, an old Felucca GRAIN record's
   grain knobs mapped, a preset's patch, INIT + the macros), eight presets in the pool after INIT, the tools' kind
   (`web/fm1sounds.js`, `tools/fm1_install.py`: pool only, 20 bytes; docs/SOUNDS.md), the editor mock
   (`web/editor.html`). Tests: `cr_grain_test` section 5 (the blob's round trip and a bad one, every page column's
   range and default against the patch, get / set and the clamps, the macros both ways, the presets' macros, a part
   above the two, `track_loaded`'s four cases), `cr_fm6_test` (seven engines shown, GRAIN after WHEEL), Felucca's
   `ui_test` (GRAIN's first knob no longer SAMPLE's list), `web/test_web.mjs` (the mock against `desc.json`),
   `test_retired.sh` (its pre-cut build without GRAIN, as 0.17 was). Not done: a settings slot for GRAIN's remembered
   pool place (`cr_settings.c CRS_SLOT_ENG`: eleven slots, none for GRAIN, as none for P5: the part lands on its
   first preset after a power cycle).
5. **Done (3y137). The editor**: the plan's SRC and GRAIN screens under the wave band (`cr_draw.c cr_wave_band`:
   the envelopes, both buffers on the GRAIN screen coloured by MORPH, POS, SPRD, the grains as moving lines), ENV 1 /
   ENV 2 under the AHDSR, the rest by the title rules; `tools/emu/scripts/cr_grain.txt` and its `test_cr.sh` section
   (eleven screenshots); `design/layouts/grain.json` regenerated. Tests: `tools/emu/scripts/cr_grain.txt`
   (each knob, the band's redraw after a render, SELECT through the lanes), `tests/run_cr_draw.sh` golden,
   `tests/cr_layout_test.c` if a layout file is added (`design/layouts/grain.json`: the owner may lay the rows out in
   the layout tool as P5's).
6. **The unit**: flashed by the orchestrator with a backup first; `tools/perf_unit.py` scenario `grain` (8 notes +
   bass, CLOUD PAD and GLITCH: `part0` worst and average against this document's 600 / 900), the render's time read
   from a counter (73 DEBUG: the longest slice, the longest render), the owner's listening; the presets by ear;
   `tests/va_levels.c`-style levels for `CB_TRIM`. docs: HISTORY, HANDOFF's state line, PRESETS.md's pool list,
   EDITOR.md's engine table, INTEGRATION.md's engines, MANUAL.md's engine page.
7. **Later, each its own piece**: the render on core 1; a FREEZE / LIVE type; a key-tracked render (the buffer at
   the chord's root) if the owner wants notes above C6.

## The sketches

```
cc -O2 -w -Ibuild/gen -o build/host/grain_sketch tools/grain_sketch.c -lm && build/host/grain_sketch build/grain-sketch
```

(`build/gen/felucca_tables.h` from `./build.sh` or `python3 tools/gen_tables.py build/gen/felucca_tables.h`.) 130
WAVs and `INDEX.md` in `build/grain-sketch/`, 35 MB, rendered in 0.3 s:

- `src-<family>-<setting>.wav`: the buffer as rendered, 743 ms at 22.05 kHz, for the settings `mid` (TONE 64 MOTION
  64 SEED 0), `dark-slow` (28 / 20 / 3) and `bright-fast` (104 / 104 / 11).
- `seeds-<family>.wav`: TONE 64 MOTION 64 with SEED 0 .. 7 in a row, 100 ms apart: what SEED gives a family.
- `gran-<family>-<setting>-<grains>.wav`: the buffer granulated by the player (`gp_*`), a C3 G3 C4 E4 chord held 2.2
  s of 3 s at 44.1 kHz, no effects, a plain 30 / 400 ms envelope, normalized to -2 dBFS; the grain settings `pad`
  (SIZE 92 DENS 88 POS 50 % wandering +-20 % at 0.3 Hz, SPRD 40 RAND 14 CUT 100 RES 20: Felucca's CLOUD PAD),
  `glitch` (SIZE 24 DENS 112 SPRD 100 RAND 90: half reversed, detuned up to an octave, CUT open: Felucca's GLITCH)
  and `frozen` (SIZE 108 DENS 72 SPRD 0 RAND 10 CUT 92, POS scanning 10 -> 90 % over the three seconds).
- `morph-<A>-<B>-<how>.wav`: two buffers under the pad grains, a 4 s scene (the chord held 3.4 s), eight pairs
  (PLUCK -> VOWEL, DRONE -> BELL, BREATH -> METAL, CHIRP dark -> SWEEP, BELL dark -> BELL bright, VOWEL dark -> BREATH
  dark, METAL dark -> DRONE dark, SWEEP bright -> PLUCK bright); `half`: MORPH 64 held; `sweep`: 0 -> 127 over the hold
  (A becoming B); `env`: 127 -> 0 in the first 0.6 s (what ENV -> MORPH gives: B's attack into A's body); for the
  first two pairs `-sweep-xfade`: the plain crossfade (every grain mixing both buffers) for comparison.

Listening order: the seed tours (is the family worth having), then `mid` against `dark-slow` and `bright-fast` (do
TONE and MOTION move it), then `gran-*-pad` (the engine as a pad), `-frozen`, `-glitch`, then `morph-*-sweep` (one
material becoming another), `-env`, and `-sweep` against `-sweep-xfade` (the granular morph against the crossfade). `python3
tools/grain_cost.py` prints the budgets' table from the device listing (Docker, as `./build.sh`).
