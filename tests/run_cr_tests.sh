#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build and run the ChoralRoot engine host tests (firmware/src/cr_engine.c), no make needed:
#   sh tests/run_cr_tests.sh
# The programs are compiled and run side by side (tests/par.sh; PAR_JOBS=1: one after another) and reported in the
# order written here, as when they ran in a row: the first one that fails (a compile or a test) is the last thing
# printed and its status the script's. A program is compiled again only when its command or one of the files it was
# compiled from has changed (tests/hostcc.sh: hashes, not dates); every test runs every time.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
. tests/par.sh
par_init build/host/jobs_cr
trap 'par_kill' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
t() { par_run "" "$@"; }
logged() {      # logged NAME [ARGS]: the program's output to build/host/NAME.log, shown when it fails
    logged_n=$1; shift
    "./build/host/$logged_n" "$@" > "build/host/$logged_n.log" || { cat "build/host/$logged_n.log"; return 1; }
}
count_ok() { grep -c ' ok$' "build/host/$1.log" | sed "s/^/$1: /; s/\$/ checks ok/"; }
last() { tail -1 "build/host/$1.log"; }
logged_last() { logged "$@" && last "$1"; }
logged_count_last() { logged "$@" && count_ok "$1" && last "$1"; }
logged_count() { logged "$@" && count_ok "$1"; }
align() {
    ./build/host/cr_align_test > build/host/cr_align_test.log 2>&1 || { grep -a 'cr_align' build/host/cr_align_test.log; return 1; }
    grep -ac ' ok$' build/host/cr_align_test.log | sed 's/^/cr_align_test: /; s/$/ checks ok/' &&
        grep -a 'cr_align_test' build/host/cr_align_test.log | tail -1
}

# the boot guard's decision (firmware/src/cr_bootguard.h, main.c fm1_cstart)
par_build cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_bootguard_test tests/cr_bootguard_test.c
t ./build/host/cr_bootguard_test
# the boot stub (docs/BOOT-SAFETY.md "As built"): its decision (firmware/stub/stub_core.h: marker, rescue key, staged
# loader, rescue count; the sanitizers watch its reads), and the package builder's side (the marker, the compensation
# bytes, sector 0x4000 byte-identical for two bodies). The loader's interruption model: tests/ldr_test.c (run_tests.sh)
par_build cc -std=c99 -Wall -Wextra -Wno-unused-function -Werror -pedantic -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -o build/host/cr_stub_test tests/cr_stub_test.c
t ./build/host/cr_stub_test
t python3 tests/stub_pkg_test.py
par_build cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_engine_test tests/cr_engine_test.c firmware/src/cr_engine.c
t ./build/host/cr_engine_test
par_build cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_loop_test tests/cr_loop_test.c firmware/src/cr_loop.c firmware/src/cr_engine.c
t ./build/host/cr_loop_test
# Harmony Preview's pure suggestion engine (firmware/src/cr_harmony.c, the Harmony Preview Edge Case Spec):
# key inference and its hysteresis, the REST/MOVE/PULL candidate tables, the two-tier duplicate-collapse
# fallback, the voice-leading cost function, and the played-chord listener (what counts as a chord played) --
# the pure module alone: no UI, no audio (the emulator's cr_harm_*.txt scripts cover the wiring)
par_build cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_harmony_test tests/cr_harmony_test.c firmware/src/cr_harmony.c firmware/src/cr_engine.c
t ./build/host/cr_harmony_test
par_build cc -std=c11 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_settings_test tests/cr_settings_test.c firmware/src/cr_settings.c firmware/src/cr_engine.c
t ./build/host/cr_settings_test
par_build cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -Ibuild/gen -o build/host/cr_va_test tests/cr_va_test.c -lm
t ./build/host/cr_va_test
# QUAD (docs/QUAD.md): blob, pages, ratios, the routings, against tests/quad_ref.py's goldens, envelopes, filters, LFOs,
# presets; an int32 wrap aborts (the sanitizer). The CPU figure: tests/run_quad_test.sh
par_build cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -DFELUCCA_QUAD=1 -Ibuild/gen -o build/host/cr_quad_test tests/cr_quad_test.c -lm
t ./build/host/cr_quad_test
par_build cc -std=c99 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_trans_test tests/cr_trans_test.c -lm
t ./build/host/cr_trans_test
# the bass part on the second core (firmware/src/cr_cpu1.h, docs/DUALCORE.md): the split bit for bit, the budgets, the fail-safes
par_build cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_cpu1_test tests/cr_cpu1_test.c -lm
t ./build/host/cr_cpu1_test
# ChoralRoot's delay and reverb (firmware/src/cr_fx.c, docs/FX.md): the coefficients, tempo sync, the half rate's response
# and noise, every corner stable and ending in exact silence, smooth parameter changes, the level against Felucca's
# effects, mono, the cost; impulse responses for tools/fx_plots.py into build/fx-review
mkdir -p build/fx-review
par_build cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_output_test tests/cr_output_test.c -lm
t logged_count_last cr_output_test
par_build cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_fx_test tests/cr_fx_test.c -lm
t logged_count_last cr_fx_test build/fx-review
# 0.19: what does not pass the master lined up with it (the click, the USB recording's part pairs), the chorus's test
# switch, MASTER at power-on (docs/AUDIO-QUALITY.md "Round 3"), on the emulator's firmware build
par_build cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_align_test tests/cr_align_test.c -lm
t align
# backup / restore SysEx (cr_backup.c) on the emulator's firmware build (tools/emu/emu_firmware.h; macOS: os_unfair_lock)
par_build cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_backup_test tests/cr_backup_test.c -lm
t ./build/host/cr_backup_test
# a restart asked over SysEx (72 RESTART, 77 STUB) leaves once the host has the reply: the device's ed_service and usb.c's
# EP1 against a model of the SIE, the host and TIMER5 (the same firmware build, FELUCCA_OTA 1)
par_build cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_restart_test tests/cr_restart_test.c -lm
t ./build/host/cr_restart_test
# FM6 (Melodee's engine, docs/FM6.md): patches, the blob, DX7 SysEx, the bank, the deep pages, the patch store, voices
par_build cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_fm6_test tests/cr_fm6_test.c -lm
t logged_last cr_fm6_test
# CZ-1 (Melodee's engine, docs/CZ1.md): Casio's tones, the blob, Casio SysEx, the banks, the deep pages, the tone store
par_build cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_cz_test tests/cr_cz_test.c -lm
t logged_last cr_cz_test
# Felucca #61 (1.0.3.1): DIGITAL -> FM6 conversion without a divide by zero (the JieLi compiler hoists a guarded
# divide; a 0.9 ORGAN preset trapped at boot -> UBOOT). Felucca's own test, built with the divide-by-zero sanitizer.
par_build cc -O1 -w -fsanitize=integer-divide-by-zero -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o build/host/fm4_div0_test tests/fm4_div0_test.c -lm
t logged_count fm4_div0_test
# USB audio recording (Melodee's, docs/USB-AUDIO.md): usb.c's descriptors in each presentation (USB Record on, the
# console), the capture routing, the PCM packing, the ring against clock drift and the host stopping (usb_audio_stream.c)
par_build cc -std=gnu11 -O2 -w -o build/host/cr_usbaudio_test tests/cr_usbaudio_test.c
t logged_last cr_usbaudio_test
# ChoralRoot In at 48 kHz (0.19, tests/cr_usbaudio48_test.c): the two-rate descriptors against SLOOP 2.5's, the
# sampling-frequency requests, the resampler (160 / 147 exactly, bit for bit against a reference, its spectrum), the
# 48 kHz stream and a rate switch mid-stream. (44.1 kHz bit for bit: the digest in cr_usbaudio_test above)
par_build cc -std=gnu11 -O2 -w -o build/host/cr_usbaudio48_test tests/cr_usbaudio48_test.c -lm
t logged_last cr_usbaudio48_test
# Options > USB Rate (0.19, 2026-10-12: 48 kHz is opt-in; tests/cr_usbrate_test.c): at 44.1 kHz the descriptors are
# 0.18's byte for byte (tests/cr_usb018_desc.h) and SET_CUR 48000 changes nothing; the 44.1 kHz digest in both
# states; a change of the option leaves the bus once, and never under a SysEx transfer
par_build cc -std=gnu11 -O2 -w -o build/host/cr_usbrate_test tests/cr_usbrate_test.c
t logged_last cr_usbrate_test
# does the USB recording change what the instrument plays? (tests/cr_usbtap_test.c, docs/USB-AUDIO.md "Does USB
# recording affect the sound?"): the DAC's 24-bit words in five USB states (Record Off, closed, open and waiting,
# 44.1 kHz, 48 kHz) for Output Clean / Classic, USB Level, MASTER, one core and two: bit-identical
par_build cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_usbtap_test tests/cr_usbtap_test.c -lm
t logged_last cr_usbtap_test
par_build cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_midi_test tests/cr_midi_test.c
t ./build/host/cr_midi_test
# the sound templates the clients embed for .syx imports are the firmware's defaults (docs/SOUNDS.md)
par_build cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/sound_templates tests/sound_templates.c -lm
t ./build/host/sound_templates --check web/fm1sounds.js tools/fm1_install.py

for id in $par_order; do
    rc=0
    par_wait "$id" || rc=$?
    cat "$PAR_DIR/$id.log"
    [ "$rc" -eq 0 ] || exit "$rc"
done
par_pids=
