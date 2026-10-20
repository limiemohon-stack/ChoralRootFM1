# Melodee's P5 dual-core scheduling (nightly, 2026-10-16 .. 17): what ChoralRoot should take

A read-only review, 2026-10-17. Nothing here is built or run on a unit. Our side was read at `15ca8d0` (0.25.1); the
counts come from this tree's `build/choralroot.dis` (`./build.sh`: XIP 94.7 %, RAM 81.9 %, POOL 89.7 %). They use 205
instructions a µs. **Every µs figure below is an estimate unless it says "the unit"**. P5's listing count has been
1.54 × low against the unit (docs/PROPHET.md "Measured on the unit").

## In short

Most of Melodee's new scheduling solves a problem we don't have, or have already solved another way. Their idea
that pays most here is one they already had: **every engine's voices go to whichever core has room**. We do that for
P5's chord voices only. The unit's heaviest case is a VA MORPH PAD chord: core 0 renders it alone (2293 µs) while
core 1 waits after the bass (355 µs). So splitting the VA chord across the cores, as we already split P5's, is
worth about ten times everything else here. The P5-only pieces are small, cheap and bit-exact: a flat wheel block
when the wheel amount is zero, and pulse widths worked out once a block.

Ranked:

1. **Split the VA chord's voices across the cores** (then FM6's), with our existing one-job protocol and a weighted
   split that leaves core 0 room for TIMER5. Estimate: −600 .. −850 µs a half on the VA MORPH PAD row (108 % →
   about 80 .. 85 %).
2. **P5: a flat wheel block when the amount is 0**, and no pitch ratios when no voice reads them. About −50 µs a
   half (count), on core 0's critical path, for 41 of the 115 shipped programs at rest. Also helps with one core.
3. **P5: pulse widths once a block** when neither the wheel nor Poly-Mod moves them. About −5 µs a voice a half
   for each oscillator (count). PW-B is fixed in 75 of 115 programs, PW-A in 62.
4. *(Optional, a lab tool)* a RAM-only override of the split over SysEx, so one package can A/B splits on the unit.

Not to port: the 5 + 3 P5 split as such, streaming publication / the 12-slot queue, overlapped per-track reverbs,
user voice weights with SAFETY OFF, and the no-fallback build. Reasons are below.

## The unit's reading before item 1 (2026-10-17)

`python3 tools/perf_unit.py va fm6 --seconds 20` on the owner's unit: 5BD75BE3, ChoralRoot 0.25, Dual Core On,
reverb on core 1 since 3y50. Eight notes + bass; µs of the 2902 µs half; worst since the read before each scenario.

| scenario | block | tick | part0 | part1 | fx | master | wait | t5 | wall | late | stale | shed (since power-on) | avg | p0 avg | fx avg |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| va: MORPH PAD + PUNCH BASS | **3256** (112 %) | 354 | **2397** | 375 | 484 | 159 | 175 | 308 | 3547 | 36 | 0 | 30 | 2079 | 1540 | 341 |
| fm6: TINE EP + DEEP SUB | **2799** (96 %) | 424 | 1764 | 641 | 529 | 162 | 184 | 262 | 3061 | 2 | 0 | 41 (this run: 11) | 1669 | 1072 | 361 |

Against 2026-10-14 (INTEGRATION.md): the fx stage is lower, as 3y50 predicted (va 611 → 484, fx avg 510 → 341).
But part0 is no better (2293 → 2397) and the block is worse (3121 → 3256). The VA chord on core 0 is still the
overload, and FM6 now sheds dry too. **Item 1 stands, with more need than estimated.**

## 1. The source

Cloned fresh into a scratch folder (`git clone https://github.com/keremimo/melodee`). The older copy at
`../melodee` was not touched. **The new dual-core work is on the `nightly` branch** (tip `5fca0a4`). `main`
(`a7553c9`, Melodee 1.4.1 plus an installer fix) has none of it.

### The commits (all authored and committed by Kerem Kilic <git@keremk.be>; no co-author trailers)

The dual-core / P5 ones:

| SHA | date (+0200) | message | what it is |
| --- | --- | --- | --- |
| `6ca918de0fb442700b26b9794179f3619db35c94` | 2026-10-16 23:14 | fix(audio): balance cores and overlap reverbs | **the main one**: CPU0 service allowance (CPU1 now takes 5 of 8 equal jobs), 12 job slots, overlapped adjacent-track reverbs, P5 PW-B once a block |
| `5fca0a4af3eae5570ed0ce87ee99739b9be21abf` | 2026-10-17 06:07 | feat(cpu): make single-core fallback optional | `MELODEE_CORE1_FALLBACK=0`: halt for the watchdog on a CPU1 failure (diagnostic; nightly builds it off) |
| `15a5ffd9bf0d6fa937e1fb52f9978b07956c9eca` | 2026-10-16 18:05 | feat(glo): add per-engine voice weights and overload guard | GLO > WEIGHTS (points a voice per engine), SAFETY (off: never shed) |
| `51ca12127b24c01cc4c8b4f8e32a280114942e6d` | 2026-10-16 18:08 | fix(settings): default overload safety to off | the shed off by default |
| `4b3275f880624e968531f06382f4bfaf5a978ac0` | 2026-10-16 17:52 | perf(prophet): specialize zero wheel preparation | `MELODEE_PROPHET_FAST` (default **0**): a separate wheel kernel when the amount is 0 |
| `eb858af576d4765a80e830f664dad302a8891513` | 2026-10-16 17:46 | feat(perf): measure batch scheduling costs | prepare / merge / CPU0 / CPU1 spans, a host job bench |
| `5f323556edb7c618c54722422af526cfba397be6` | 2026-10-16 18:07 | fix(memory): fit resident audio with cache RAM | SRAM layout for the resident audio graph |
| `63edc53bc8c1074c4a12c59e1f5b851f91eeb2b4` | 2026-10-16 23:45 | fix(build): reach fx_pan from flash in the default build | follow-up to 6ca918d |
| `25a2bef6cd3c438c48a6a319c1478a5f6ab260f8` | 2026-10-15 18:08 | refactor: deduplicate and require dual-core builds | single-core builds retired (CPU1 fault fallback kept) |

The same "CPU optimization" merge (`03d17df`, 2026-10-16 18:09) also has engine and mixer specialisations that are
not about scheduling: centered mono sends `9756a81`, empty chorus rings `30121cb`, centered bend `64e919a`, a
routing cache `9f1caa1`, FM6 zero level `6a5efec`, CZ window four `25b8845`, SID `992b16e`, drums `e705068`. See
"Not looked at" at the end.

### Licence and credit

Melodee is **GPL-3.0-only** (`LICENSE`, `LICENSING.md`). The files in question (`audio_batch.h`, `mix.c`, `fx.c`,
`eng_prophet_test.c`) carry `SPDX-License-Identifier: GPL-3.0-only` and "Copyright (C) 2026 Kerem Kilic (Ellic
Studio)". ChoralRoot is GPL-3.0-only too, so code can be taken. Credit **Kerem Kilic (Ellic Studio)** in the file
header and in LICENSING.md, as for the 3y81 / 3y82 port. None of the ranked items needs Melodee's code: items 2 and
3 restate ideas in our integer P5, and item 1 extends our own `p5x` split. Credit the ideas in the HISTORY entry and
the comments, as 3y50 did ("the ideas, written anew").

## 2. What Melodee did

**Their architecture first.** Melodee plays up to eight tracks. Every voice of P5, FM6, CZ, SID and the drums
becomes a **job**: a descriptor of ≤ 168 bytes with the voice's prepared controls, plus its own CTL-sample PCM
scratch (`audio_batch.h`). CPU0 runs `mix_prepare_batch`: each track's matrix, envelopes and engine controls. It
pushes one job per voice and **publishes each job as soon as it is complete**. CPU1 starts on job 0 while CPU0
still prepares the rest (`audio_batch_parallel` polls `audio_batch_published`). At the flush CPU0 closes the stream,
renders the jobs that are its own, joins, and adds every job's PCM in voice order.

**How voices are split, and when** (6ca918d). The split is decided per job, at push time, greedily against a
cost table (`audio_batch_cost`: P5 3100, CZ 2100, SID 2000, 909 1900, FM6 1250, 808 1150). The first job of a group
starts CPU0's tally at a **service allowance**: `max(that job's cost, 2000)`, or 6000 for a P5 group when FM6
voices are still preparing on CPU0. Each later job goes to CPU1 while CPU1's tally ≤ CPU0's, else to CPU0. Eight
equal P5 jobs therefore split **5 on CPU1, 3 on CPU0** (mask `0xab`, asserted in `tests/audio_batch_profile_test.c`;
4 + 4 before). The commit's reason: CPU0 also prepares the descriptors and serves TIMER5 inside the window, and "a
fixed 2000 under-reserved Prophet and CZ work, leaving CPU0 late while CPU1 finished early". A lab-only SysEx
(editor 98, sub 5, guarded) sets the allowance at run time for A/B runs.

**The job protocol and synchronisation.** Single producer, single consumer, word flags (`published`,
`worker_mask`, `closed`, `invalid`) with acquire / release (`fm1_aw_load` / `fm1_aw_store` on the target). Each
descriptor carries a guard word (`run ^ n ^ id ^ 0x913ab267`). CPU1 rejects a bad one and sets `invalid`, and CPU0
then retires the worker. A failed worker's partial PCM is **dropped, not replayed** ("a job partially advanced by a
failed worker must never be replayed"). Twelve slots, up from 8, keep an 8-voice part and its drums in one group;
a 13th push flushes first.

**Voice stealing and budgets.** These are unchanged by the scheduling. A P5 voice costs 2 points with CPU1 online
and 3 in serial fallback; the default budget is 16, which allows 8 P5 voices. 15a5ffd lets the player override each
engine's points (GLO > WEIGHTS) and adds SAFETY: off means `shed_voice` is never called. Their own doc says a
sustained overload then "can starve the main loop until its 8-s watchdog restarts the device", and "no weight here
is calibrated against measured device cost". 51ca121 made SAFETY **off** by default.

**Effects and per-block work.** Melodee has an insert reverb per track. When two adjacent tracks each have exactly
one reverb insert (and no modulation or spread on the second), the second track's reverb runs on CPU1, with a
private copy of its PCM and its own tank, while CPU0 runs the first's (`fx_reverb_private_*`). P5's oscillator B
pulse width is worked out once a block when the wheel does not move it (`fixed_pw_b`). The zero-wheel kernel
(4b3275f) is behind `MELODEE_PROPHET_FAST`, **default 0**, and the package was "prepared, not installed".

**What polyphony they claim, and how they measured it.** No new voice count. The capacities are as before (P5 8, 5
in serial fallback), and their plan's "Stage 7: polyphony calibration" is "not started; allowances unchanged". The
6ca918d message: "Device playback at budget 18 sustained without further misses after the live-note test; startup
and occasional live-note deadline misses remain." Their method is host instruction counts per job (with about 5,700
instructions of query overhead in each, as they say), golden-render and concurrent-PCM equality on the host, and on
the device a profiling build that reads spans over SysEx 98. Their older device figure for P5 is 5 voices at budget
16, a 2141 µs peak with 26 % headroom (`v1.3.1-budget4`). **So the scheduling change is host-verified, with one
short device run that still had misses.**

## 3. Compared with ChoralRoot

Where we stand (docs/PROPHET.md "Two cores, eight voices", docs/DUALCORE.md "What each core runs"):

- Core 0 prepares everything **before** one hand-over a block: the events, allocation, both parts' matrix and LFO,
  the chord part's `p5_block` and every P5 voice's controls (`voice.c track_render_x` → `p5x.it[]`).
- `fx.c p5x_run`: `p5x_assign` picks k, and **one job** (`c1_part1_job`: the bass part's whole render, then core 1's
  k chord voices into `p5x_buf`) goes to core 1. Core 0 renders the rest, waits (`cr_c1_wait`, 8 ms at most), and
  adds `p5x_buf`. The reverb's half-band and lines are job 2, after join 1.
- `p5x_assign`: the k that makes the longer side shortest. A P5 voice counts 440 µs and another engine's budget unit
  75 µs. At most 4 P5 voices a core (the owner's rule). The odd voice goes to core 1.
- **Only P5 chord voices are split.** A VA, FM6, CZ-1, FM TONE or GRAIN chord renders whole on core 0.

### Idea by idea

**(a) A CPU0 service allowance (5 + 3).** *We do it differently.* Our split has no allowance, but core 0 does have
one thing in the window: TIMER5 nests in the audio ISR at 10 kHz. That is 6.6 .. 7.3 µs a tick on the unit, so
about 7 % of core 0 dry and about 11 % while recording (INTEGRATION.md "TIMER5"). Melodee's other reason, CPU0
preparing descriptors inside the window, does not apply: our preparation all happens before the hand-over.

*Gain.* I simulated `p5x_assign` for every chord size 0..8, with no bass, a P5 bass of 1..4, and other-engine
basses, with and without a ×1.07 / ×1.11 core-0 factor:
- with a MONO bass (2 units, 150 µs: ChoralRoot's bass) or a P5 bass, **no decision changes**;
- decisions change only with a 1-unit bass voice (5 or 7 chord voices, 17 .. 120 µs saved), which ChoralRoot's
  MONO bass never is;
- 5 + 3 at eight voices breaks the owner's "4 per core", and with 440 µs voices it makes the longer side about
  2200 µs instead of about 1880 µs (4 × 440 × 1.07).

So **nothing to gain for P5**. The allowance does matter once voices are smaller, which is item 1 (VA voices are
about 280 µs on the unit).

**(b) Streaming publication (CPU1 starts on job 0 while CPU0 prepares the rest).** *We don't, by decision.*
DUALCORE.md: "Publishing job 1 before core 0 has prepared all of the chord's voices (Melodee's per-job publishing)
needs a mailbox deeper than `fm1_c1_post`'s one job: not done."

*Gain.* At most core 0's pre-hand-over work in a block, and only when core 1 is the longer side: a heavy P5 bass,
not the common chord. From the listing, that work is about 4,900 instructions a block (two `mod_begin`, two LFO
ticks, `p5_block`'s compare, the wheel's 32 × ~90, about 150 a voice in `track_render_x`): **about 95 µs a half
(estimate), about 40 µs after item 2**. With TIMER5 core 0 is usually the longer side already, and then it gains
nothing.

*Cost and risk.* A second publish point in each block, a deeper mailbox, and a new failure semantic. Melodee drops
a failed worker's PCM; we re-run the whole job on core 0. All of that touches "Reliability" and "The persistent
fallback". **Not worth it.**

**(c) Overlapped per-track reverbs.** *We already do the useful part.* We have one global reverb, and since 3y50
its half-band and lines run on core 1 as job 2 while core 0 runs the chorus and the delay (bit-exact: integer
`+=`). Melodee's version exists because each of its tracks has its own insert reverb. **Not applicable.**

**(d) P5 PW-B once a block** (6ca918d). *We don't.* In `p5_samples` (`eng_prophet_test.c:738`) the B width is
`p5_pw(pwm_b + (p[P5_WHEEL_PW_B] ? lfo : 0))` every sample. The listing does a select, an add, smin, a shift and a
compare-select: **about 11 instructions a sample, 3 of them stack reloads** (`build/choralroot.dis`, `p5_samples`
+0x39a .. +0x3be). A per-block `fixed_pw_b` leaves about 3. PW-A (`:743` / `:747`) has the same shape when neither
`P5_POLY_PW` nor `P5_WHEEL_PW_A` is on (+0x694 .. +0x6b8).

*Gain.* About 8 instructions × 32 samples × 4 blocks ≈ 1,000 instructions, so **about 5 µs a voice a half for
each oscillator** (count; 1.54 × that on the unit would be about 8). Eight voices with both widths fixed: about
80 µs of work a half, about 40 on the longer core. Shipped list: PW-B fixed in 75 of 115 programs, PW-A in 62,
both in 47. Bit-exact: the same integer expression, moved. *Cost*: a few bytes of RAMTEXT and one more live value
in a loop that already spills. Re-count with `tools/p5_cost.py`. **Port** (item 3).

**(e) P5 zero-wheel block** (4b3275f, which Melodee left off by default). *We don't.* `p5_mod_samples`
(`eng_prophet_test.c:407`) runs every block for a sounding part: noise, LFO wave, three Q15 multiplies and
`p5_ratio_q15`, about 90 instructions a sample (PROPHET.md "Counted": 32 × 90 a block). With `amount == 0`, `w` is
0 for every sample, in integers, so `wheel[] = 0` and `wheel_pitch[] = p5_ratio_q15(0)` are exact. What must still
move is what 3y45's idle skip already moves: 32 steps of `mod_noise` and the LFO phase.

*Gain.* About 2,560 instructions a block, **about 50 µs a half (count; about 75 at 1.54 ×)**. It comes off the half
whole, because it runs on core 0 **before** the hand-over, and the bass part's copy runs on core 1. It also helps
with one core. Applies when LFO INITIAL is 0 and the mod wheel and pressure are at rest: **41 of 115 shipped
programs** (48 ignoring pressure).

*Extension.* With a non-zero amount but neither `WHEEL_FREQ_A` nor `WHEEL_FREQ_B` on (47 of 115 programs), nothing
reads `wheel_pitch[]`: skipping `p5_ratio_q15` is about 20 instructions a sample, about 12 µs a half. No program
has all five wheel switches off.

*Risk.* None to the dual-core work: the change is inside `p5_block` on the core that owns the part. **Port** (item
2).

**(f) Per-engine voice weights, SAFETY OFF.** *We do it differently, by the owner's rules.* There is the pool of 8 /
4, 2 units a P5 voice with two cores and 3 with one, the bass always keeps one voice (3y53), and the shed takes a
voice rather than run late. Melodee's own text says SAFETY OFF can end in a watchdog restart and that its weights
are uncalibrated. **Not to port.**

**(g) `MELODEE_CORE1_FALLBACK=0`.** *We do it differently.* We have the persistent fallback (one core until
power-off), the single-core package, and the `c1_rejected` / `c1_faults` counters in `--debug`, which surface a
core-1 fault without a reboot. A halt-for-watchdog build exists to make faults loud in Melodee's nightlies. **Not to
port** to releases. If the owner ever wants it as a lab switch, it is about 20 lines in `cr_cpu1.h`.

**(h) Batch-cost instrumentation** (eb858af). *We have the equivalent*: 78 PEAK 55..61 (`pk_c0_busy_us`,
`pk_c1_busy_us`, the joins' waits, `pk_fx_map`), GEEK OUT 2's two load meters, and `tools/perf_unit.py`. What Melodee
has that we lack is the **run-time override** of the split (their guarded editor 98/5), which lets one package
compare splits on the unit. That is item 4.

### The other engines

| engine | split today | what Melodee's approach would give | notes |
| --- | --- | --- | --- |
| **VA** | no: the chord renders on core 0 | **the largest gain here**: the unit's MORPH PAD row is part0 2293 µs, block 3121 (108 %), shed +26 in 20 s, the wait 9 .. 41 µs (core 1 idle after its 355 µs bass) | item 1 |
| FM6 | no | TINE EP, 8 notes: part0 1647, block 2580 (88 %), sheds while recording | needs a third `fm6_scr` (384 B) for chord voices on core 1 beside an FM6 bass |
| CZ-1 | no | 70 .. 80 % worst: little need | per-part caches to audit |
| FM TONE | no | 70 .. 80 % worst | `quad_render`'s frame is 556 B: core 1's stack (2816 B) needs re-counting |
| GRAIN | no | not measured on the unit | shared grain source buffers to audit (read-only?) |
| P5 | yes (3y46) | items 2, 3 | |

Items 2 and 3 are P5's own and do not apply to the other engines. Item 1 applies to all of them. Melodee batches
P5, FM6, CZ, SID and drums this way (`voice.c engine_batch_kernel`, since 6da744c of 2026-10-13, which 3y50
surveyed). It has no VA engine.

## 4. Recommendations

### 1. The VA chord's voices on both cores (then FM6)

*Built (the VA) in HISTORY 3y177 (docs/DUALCORE.md "The VA's chord on both cores"); not yet measured on the unit.*

*Brief.* Generalise 3y46's P5 split (`voice.c` `p5x` list, `fx.c p5x_run` / `p5x_assign`) from `P5_IS(t0)` to an
engine flag, `engine_t.split`, set for the VA first. Keep **our** protocol: core 0 prepares every voice before the
one hand-over, `c1_part1_job` renders the bass part and then core 1's share of the list, core 0 renders the rest,
waits, and adds. Do **not** take Melodee's streaming queue. That keeps the fallback (core 0 re-runs the job) and
DUALCORE.md "Reliability" exactly as they are.

What to add:
- A stereo side buffer for core 1's VA voices (`render2` writes `side`): `p5x_side`, 128 B of POOL. It is added on
  core 0 after the wait, like `p5x_buf`.
- `p5x_assign` weighted per engine: a cost a voice (VA about 280 µs from the unit's row; P5 440), the bass as
  today, and **core 0's side × 1.07** for TIMER5. This is Melodee's service allowance; it matters here because VA
  voices are smaller (simulate it as in §3a).

The audit, as PROPHET.md "What a voice's render touches" did for P5:
- `eng_va.c va_pan_off[tr]` / `va_rate_off[tr][]`: written in the render by the latest note's voice only (`v - t->v
  == t->m_vi`), read on core 0 after the join (`mix_part` pan) and in the next block's LFO tick. That is one writer;
  check it holds.
- The VA noise oscillators' generators, and anything else per part that a voice's render writes.
- Core 1's stack with a VA voice frame.
- **Flash contention**: core 1 running VA code from XIP while core 0 does too. Build and measure with `CR_RAM_VA=1`
  (9022 B; RAMTEXT has 17.5 KB free), and without it.

Tests:
- `tests/cr_c1_thread_test.c`: add VA-chord pairs (VA bass, FM6, P5, CZ-1 basses). Inline = thread = unsplit,
  every sample, and TSan clean with the planted word.
- `test_cr.sh` with `EMU_C1=thread` / `nostart` / `hang` on an 8-note MORPH PAD chord.
- The goldens are unchanged: the sums are integers.

*Cost.* POOL +128 B (+384 B for FM6's scratch later), RAM for a list item a voice if the `vmod_t` copy is shared
with P5's list (it is the same list), and flash about 0.5 .. 1 KB; XIP is at 94.7 %. Risk: the job grows, the
protocol does not.

*Unit check.* `tools/perf_unit.py va fm6 --seconds 20`, Dual Core On, dry and recording. Read `block`, `part0`,
`part1`, the wait, `pk_c0_busy_us` / `pk_c1_busy_us`, `late`, `shed`.

*Expected (estimate).* VA 8 notes + bass: the longer core about 1450 µs instead of 2293, the block about 2300 ..
2500 instead of 3121, and no shed in the dry row. FM6 8 notes: part0 about 900 instead of 1647.

### 2. P5: a flat wheel block

*Brief.* In `eng_prophet_test.c p5_block`, after `amount` is known, add `if (amount == 0) {` and run the idle skip's
moves: 32 `p5_fast_noise32(&s->mod_noise)`, `s->lfo += inc * 32`, and `lfo_value`. Then fill `wheel[]` with 0 and
`wheel_pitch[]` with `p5_ratio_q15(0)`. A part flag can skip the fill when the arrays are already flat. Add a
second fast path: when neither `P5_WHEEL_FREQ_A` nor `_B` is on, `p5_mod_samples` skips `wheel_pitch[]` (nothing
reads it). Put both in `.ram_text` beside `p5_mod_samples`. Use a switch `P5_WHEEL_FLAT` (default 1, 0 = the old
code) as `P5_IDLE_SKIP` / `P5_COEF_CACHE` do.

*Tests.* `cr_prophet_test`: an `--idle`-style digest over every shipped program and all 200, built with 0 and 1.
Move the mod wheel 0 → 127 → 0 and pressure on and off under held notes, so both paths and their transitions run.
The goldens unchanged. `tools/p5_cost.py` before and after.

*Unit check.* A 4- and an 8-note chord of a program with LFO INITIAL 0. Read `pk_part0_us` and `pk_block_us`;
expect about 50 .. 75 µs lower. Then the same program with the mod wheel up, which should be unchanged.

*Cost.* About 100 B of RAMTEXT. No RAM or POOL.

### 3. P5: pulse widths once a block

*Brief.* In `p5_samples`, compute `pw_b_fixed = p5_pw(pwm_b)` before the loop and use it when `!p[P5_WHEEL_PW_B]`.
Compute `pw_a_fixed = p5_pw(pwm_a)` for `!p[P5_POLY_PW] && !p[P5_WHEEL_PW_A]`, for both `P5_STAGE36` branches.
Melodee's line in 6ca918d is the model; ours is integer, so it is exact.

*Tests.* The goldens and `cr_prophet_test` digests unchanged. `tools/p5_cost.py`: expect about −8 instructions a
sample a fixed oscillator on the "hit" column. Watch the spills: the loop already reloads from the stack, so keep
it only if the count drops.

*Unit check.* Folded into item 2's readings: an 8-note chord of a saw + pulse program with fixed widths. Expect
`pk_c0_busy_us` and `pk_c1_busy_us` each about 20 .. 30 µs lower.

*Cost.* None measurable.

### 4. (Optional) a run-time split override for the unit

*Brief.* A 78-family debug SysEx that sets, in RAM only, a forced core-1 share (as `CR_C1_TEST_SHARE` does on the
host) and the core-0 factor of item 1. It is cleared at power-on. Guard it as Melodee guards its lab control. Then
one flash can measure 4 + 4 against 3 + 3 for P5 (HANDOFF 33's open question, `-DP5_CORE=3`) and item 1's VA
splits. Test: `tests/` wire validation, and that the override never survives a restart.

### Not to port, and why

- **The 5 + 3 P5 split / a service allowance for P5 alone**: it changes no decision with our MONO or P5 bass (§3a),
  and 5 on a core breaks the owner's "4 per core". The allowance belongs in item 1's weighted split instead.
- **Streaming publication, the 12-slot descriptor queue**: it replaces a protocol whose reliability work
  (DUALCORE.md "Reliability", "The persistent fallback") is what keeps Dual Core On by default. It would gain at
  most about 40 .. 95 µs, only with a heavy bass.
- **Overlapped per-track reverbs**: they need a reverb per track. Our one reverb is on core 1 already.
- **GLO > WEIGHTS, SAFETY OFF**: uncalibrated by Melodee's own account. Turning the shed off trades a dropped voice
  for late halves and a watchdog restart. Our pool and units are the owner's decisions, set from readings.
- **`MELODEE_CORE1_FALLBACK=0`**: a diagnostic for Melodee's nightlies. Our counters and the single-core package
  cover the case.
- **FM6's 6000 allowance**: specific to their mixed P5 / FM6 tracks.

## Not looked at (a possible follow-up)

The same merge's per-engine specialisations may apply directly, because our `cz_native.c` and `fm6_core.c` come
from Melodee: `25b8845` "perf(cz): specialize native window four" and `6a5efec` "perf(fm6): specialize zero level
preparation". Each would need the same check as items 2 and 3: is it bit-exact here, and what does the listing
count? The mixer and FX ones (`9756a81`, `30121cb`, `64e919a`, `9f1caa1`) are for Melodee's own mixer and effects,
which are not ours.
