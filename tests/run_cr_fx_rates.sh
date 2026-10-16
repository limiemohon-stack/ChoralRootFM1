#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The delay and the reverb at half and at full rate, side by side (docs/FX.md "Half rate or full"): tests/cr_fx_test.c
# built both ways (CR_FX_DELAY_DS, CR_FX_REVERB_DS = 1 full, 2 half), every check run on each, the cost of each, and the
# same material through each into build/fx-review/*_full.wav and *_half.wav (cmp_delay, cmp_reverb: a bright chord stab;
# the impulse responses and the repeats), then their pictures (tools/fx_plots.py).
#   sh tests/run_cr_fx_rates.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host build/fx-review
cc -std=gnu11 -O2 -w -DCR_FX_DELAY_DS=1 -DCR_FX_REVERB_DS=1 -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_fx_test_full tests/cr_fx_test.c -lm
cc -std=gnu11 -O2 -w -DCR_FX_DELAY_DS=2 -DCR_FX_REVERB_DS=2 -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_fx_test_half tests/cr_fx_test.c -lm
./build/host/cr_fx_test_full build/fx-review _full > build/host/cr_fx_test_full.log || true
./build/host/cr_fx_test_half build/fx-review _half > build/host/cr_fx_test_half.log || true
for r in full half; do
    echo "== both effects at $r rate: $(grep -c ' ok$' build/host/cr_fx_test_$r.log) checks ok, $(grep -c 'FAIL$' build/host/cr_fx_test_$r.log) failed"
    grep 'FAIL$' build/host/cr_fx_test_$r.log || true
    grep 'COST\|the delay path\|the delay.s band\|tail from -20\|a line of' build/host/cr_fx_test_$r.log | sed 's/^cr_fx: */   /'
done
python3 tools/fx_plots.py build/fx-review > /dev/null
echo "WAVs and pictures: build/fx-review/cmp_delay_{full,half}.*, cmp_reverb_{full,half}.*, ir_*_{full,half}.*"
# (the checks' limits hold at both rates since 2026-10-14 (the default build: both at half); sh tests/run_cr_tests.sh is
# the one that must pass. A comparison build may miss one by a little: shown above, not an error here)
exit 0
