# Digitone envelope-time measurement

Measures the Elektron Digitone's (original, OS 1.4x) envelope time curves so the
FM TONE engine (`eng_quad.c`, `docs/QUAD.md`) can match them. Elektron publishes
no times.

- `dt_cc.py`: the Digitone's track MIDI CC/NRPN map (manual OS 1.44, Appendix B:
  <https://www.elektron.se/wp-content/uploads/2026/10/Digitone-User-Manual_ENG_OS1.44_260930.pdf>).
  14-bit parameters go out as NRPN, 7-bit ones as CC. The list parameters (ALGO,
  FILTER TYPE, RATIO C / A, LFO MULT / WAVE / MODE, LFO DEST NONE) take the value the
  Digitone shows; their CC value is the list position, proved on the owner's Digitone
  on 2026-10-14 (the module docstring has the values). `python3 tools/digitone/dt_cc.py`
  prints the table and example bytes.
- `dt_measure.py`: sets a plain-sine base patch on track 1 over MIDI, plays one C4
  (velocity 100) per measured value, records the Digitone's USB audio with
  `build/dt_rec` (device "Elektron Digitone", 48 kHz). It writes one WAV + JSON per
  capture. Each capture starts and ends with a short sync marker note; the analysis
  maps the logged host times onto the audio with the two markers, which removes
  recorder start latency, MIDI/audio latency and clock drift. The JSON also keeps
  dt_rec's host time of the first sample (`host_rec_start`) and its counts
  (`rec_stats`).
- `dt_rec.c`, `build_rec.sh`: `dt_rec DEVICE_NAME OUT.wav` records a CoreAudio input
  (AUHAL, native rate and channels, 32-bit float WAV) until `q`/EOF on stdin or
  SIGINT/SIGTERM. It prints `start <host s of the first sample>` (the
  `time.monotonic()` clock) and at the end `frames N overruns K ...`. K counts every
  discontinuity and must be 0. dt_measure builds it into `build/dt_rec` when it is
  missing. It exists because ffmpeg's avfoundation capture drops about 10 % of the
  samples on this Mac (a 2.000 s note recorded as 1.79 s), spread through the take,
  which makes every envelope time wrong. dt_rec delivers every sample: 10 s and
  60 s takes match the host span to under 1 ms. ffmpeg is only a fallback when the
  build fails, and dt_measure prints a loud warning when it uses it.
  `--plan` prints every step and the total time and sends nothing. `--run` measures;
  `--only amp_atk,filt_dec,...` picks groups (amp_atk amp_dec amp_rel filt_dec
  filt_atk filt_rel opA opB delay fade; amp_hold is accepted but skipped: the
  original Digitone has no AMP HOLD). `--out DIR` (default
  `/Volumes/Q7Media-2025/Projects/Github/ChoralRootFM1/DigitoneRecordings/times/`).
  Ctrl-C sends all notes off, stops the recorder and writes the JSON for what was captured.
  `--set 2` picks run 2's captures (below); `--set 1`, run 1's, is the default.
- `dt_analyse.py [DIR]`: per step the amplitude envelope (FIR Hilbert, 1 ms) or the
  spectral-centroid track: rise times and the full linear rise; decay/release T60
  from the dB slope between -1 and -50 dB below the start (threshold times when the
  fall is under 30 ms or 10 dB); centroid sweep times; the LFO fade from the tremolo
  depth per cycle. Unfinished steps (a gate shorter than the rise or sweep) are
  extrapolated and say so in `T_from`. Fits T(v) as a power law and as an exponential
  10^(kv), keeps the better; writes `analysis.json` (raw points + fits) and PNG plots
  if matplotlib is installed. It reads every capture in DIR, run 1's and run 2's.
- `summary.py [PATH ...]`: reads `analysis.json` (or directories holding one) and prints
  one table per envelope, value -> seconds, run 1 and run 2 side by side and merged,
  with power / exponential / log-polynomial fits. The FM TONE times are built from it.
- `test_dt.py`: no hardware: CC/NRPN bytes (HARM 0.00 = 63), the list encoders against
  the owner's device test, `--plan` for both sets (run 1's set unchanged, run 2 within
  15 min, its by-hand lines), and the analysis on synthetic captures with known times
  (simulated 0.37 s recorder latency and 25 ppm drift; dt_rec float32 WAVs with
  `host_rec_start`): an unfinished attack, slope T60 on partial decays and on releases
  whose tails run on under the next steps, a late onset, and the tremolo-depth fade
  (finished and extrapolated).

## FM TONE's times from these numbers (docs/QUAD.md "Times")

- `digitone_times.json`: the Digitone's readings, derived numbers only (no audio): per envelope the measure, the
  setup, value -> seconds (runs merged as summary.py merges them), each run's own numbers (t10 / t50 / t90, the T60,
  the fade's), whether extrapolated; the amp attack's grid (the 0.4962 s multiples from 112 up); for the filter and
  the operators each step's centroid track (log2 Hz in time, thinned: `--extract` needs the captures' WAVs in DIR)
  and an attack's reference (the fastest step's end). The filter and operator tables are calibrated on these whole
  tracks; the operator attack's shape (linear in dB from `OP_FLOOR_DB`, -38 dB) was chosen by them (QUAD.md).
- `quad_render.c`: one FM TONE note through the firmware's own eng_quad.c on the host, to raw float32 at 44.1 kHz
  (`build/host/quad_render`, built by make_quad_times.py and tests/run_cr_tests.sh); it can overwrite one entry of
  quad_times.h's tables (the calibration's candidate times).
- `make_quad_times.py`: makes `firmware/src/quad_times.h` (a table per envelope type, the fade, the delay constant)
  of digitone_times.json and `quad_calib.json`. `--calibrate` measures FM TONE through dt_analyse.py with the
  captures' patches (a sweep of engine times per table, inverted at the Digitone's readings) and writes quad_calib.json;
  `--verify` plays every measured value with the header's tables and stores the residuals in quad_calib.json
  ("verify"). Both take about a minute.
- `test_quad_times.py` (in tests/run_cr_tests.sh): the header not stale, the amp's times, a few proxy readings, the
  delay, the fade against digitone_times.json.

After another measurement:

1. `python3 tools/digitone/dt_analyse.py DIR` (DIR: the captures, outside the repository), then
   `python3 tools/digitone/make_quad_times.py --extract DIR` (several DIRs are merged; check the diff of
   digitone_times.json: the measures, the "extrapolated" flags, the grid's unit).
2. `python3 tools/digitone/make_quad_times.py --calibrate` (quad_calib.json, quad_times.h), then `--verify` (the
   residuals: amp within 1 %; read the proxies' against QUAD.md's).
3. `python3 tests/quad_ref.py` (FM TONE's goldens), `sh tests/run_cr_tests.sh`, `GOLDEN_UPDATE=1 sh
   tests/run_tests.sh` for tests/golden.txt's preset/FM_TONE renders only.
4. The factory presets' times were re-expressed once (2026-10-14) for their old times on the new tables: after a
   change of the tables, check they still sound as designed.

## Run 2 (`--set 2`, files `r2_*`, about 13 min)

Run 1 left gaps and a failed fade design. Run 2 records next to run 1 in the same
`--out` (names prefixed `r2_`, run 1's files untouched):

- amp attack 8 16 24 48 80 100 104 108 112 114 116 118 120 122 124 125 126 (gates
  >= 1.3 x run 1's interpolated full rise; 112 -> 126 is where the curve jumps);
- amp decay 4 8 16 24 48 80 104 120 (sustain 0) and release 8 24 48 80 120: windows
  <= 8 s, the T60 from the slope (the decay is exponential: run 1 fitted to 0.02 dB);
- filter decay and attack 0 16 32 48 64 80 96 104 112 120 126 (run 1's LP4 setup).
  Run 1's filter attack never reached the open filter (its t90s were noise); run 2's
  gates go to 30 s;
- op A attack and decay 0 16 32 48 64 80 96 112 126 (op B behaved as A in run 1);
- LFO fade, redesigned: LFO1 SIN, MODE TRG, SPD +63, MULT F64 (the fixed-120-BPM 64),
  a 15.75 Hz tremolo at any tempo, DEP +12 on VOL 64 (run 1 showed DEP +63 swings VOL
  by >= 100 units, clipping both ends); FADE 0 -4 -8 -16 -24 -32 -40 -48 -56 -64 at
  120 BPM and 0 -16 -32 -48 at 80 BPM. FADE 0 gives the full depth. -56 and -64 take
  longer than any gate (run 1: >= 170 s at -64); their ramp is extrapolated.

By hand for the fade captures: LFO1 DEST = VOLUME and the tempo (120, then 80).

## Before a run (owner)

1. Digitone SETTINGS > MIDI CONFIG > PORT CONFIG: RECEIVE NOTES and RECEIVE CC/NRPN
   on, INPUT FROM includes USB; CHANNELS: TRACK 1 CHANNEL = 1 (or pass `--channel`).
   Audio over USB on.
2. Track 1 holds the init sound. Each capture sends its whole base first, the list
   parameters included: ALGO 1 (X = operator C alone; opB uses ALGO 6, where B2
   modulates C), MIX at -64 (assumed the X end, so only X is heard), FILTER TYPE
   OFF (LP4 for the filter captures), RATIO C and A 1.00, both LFO DESTs NONE (the
   fade captures: LFO1 WAVE SQR, MODE TRG, MULT 1). Then it prints what is still by
   hand and a line to check on the screen, and waits for Enter:
   - fade captures: LFO1 DEST = VOLUME (DEST over CC is not solved) and the tempo;
   - delay captures: the tempo;
   - every capture: check ALGO, MIX at the X end, FILTER TYPE, HARM 0.00 (opB also
     RATIO B 1.00/1.00, the init value: RATIO B is not sent). HARM 0.00 is sent as 63,
     proved on the device 2026-10-14; run 1 sent 64, a slightly positive HARM (it
     shapes operators A and B1, not C).
3. At the end it sends back the init sound's values for every parameter it changed
   (the list parameters and, after a fade capture, LFO1 DEST NONE too), taken from
   the owner's init-sound export (`DigitonePresets/PresetFormat/BASE.dnsnd`, outside
   the repository). The tempo is not restored. Reload the sound/project without
   saving for an exact restore.
4. Permission: allow the script in `.claude/settings.local.json` like the existing
   `fm1_install.py` rule, e.g. `"Bash(python3 tools/digitone/dt_measure.py:*)"`.

Recordings stay outside the repository (the default `--out` is outside it); never
commit audio.
