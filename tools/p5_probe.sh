#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# P5's measurements (tools/p5_probe.c; docs/PROPHET.md "An unplayed part", "Headroom", "A stolen voice"; HISTORY 3y45):
#   sh tools/p5_probe.sh [cost|levels|steal|all] [DIR]
#   sh tools/p5_probe.sh eight DIR      3y46's listening files (docs/PROPHET.md "Voices", "Level"): two cores, D Bm G A
#                                       in six- and eight-note voicings on four programs, the same with a P5 bass, a
#                                       UNISON lead and a UNISON bass key with one core and with two: "after-*" this
#                                       tree, "before-*" 3y45's level and stack (-DP5_OUT_SHIFT=1 -DP5_STACK_PHASE=0)
# builds build/host/p5_probe with ALL 200 programs (and the variants a table compares) and prints the tables.
# DIR: where the listening files go (levels: DIR/headroom, steal: DIR/steal). Needs build/gen (./build.sh or
# tools/emu/build.sh). Not a pass / fail suite; minutes.
#   cost     the bass part on P5 in each state (host instructions), now and with P5_IDLE_SKIP=0 (a773d30)
#   levels   every stage's peak against its limit over all 200 programs; the engines side by side; DIR/headroom: the
#            three hottest cases as they are ("now") and with the proposal P5_UNISON_TRIM=1 ("trim": a stack at half
#            level; NOT applied in the firmware)
#   steal    the click of a stolen voice with P5_STEAL_BLOCKS 0 (a773d30), 2 (the firmware's) and 4; DIR/steal: a
#            five-note chord and an arpeggio over a held chord, "before" (0), "after" (2) and "four" (4)
set -e
cd "$(dirname "$0")/.."
W=${1:-all}
D=${2:-}
mkdir -p build/host/p5_all
python3 tools/gen_prophet_factory.py build/host/p5_all/melodee_prophet_factory.h all > /dev/null
CC="cc -std=gnu11 -O2 -w -Ibuild/host/p5_all -Ibuild/gen -Ifirmware/src -Itests -Itools"
b() {   # b NAME [flags]
    n=$1
    shift
    $CC "$@" -o "build/host/$n" tools/p5_probe.c -lm
}
case $W in cost|all)
    b p5_probe
    b p5_probe_idle0 -DP5_IDLE_SKIP=0
    ./build/host/p5_probe cost
    echo "--- as a773d30 (P5_IDLE_SKIP=0): the idle lines"
    ./build/host/p5_probe_idle0 cost | grep "nothing played\|6 s later"
    ;;
esac
case $W in levels|all)
    b p5_probe
    b p5_probe_utrim -DP5_UNISON_TRIM=1
    ./build/host/p5_probe levels
    ./build/host/p5_probe engines
    echo "--- with the proposal P5_UNISON_TRIM=1 (a UNISON stack at half level; not applied): P5's lines"
    ./build/host/p5_probe_utrim engines | grep "^   P5"
    if [ -n "$D" ]; then
        mkdir -p "$D/headroom"
        ./build/host/p5_probe hot "$D/headroom" now
        ./build/host/p5_probe_utrim hot "$D/headroom" trim
    fi
    ;;
esac
case $W in eight)
    [ -n "$D" ] || { echo "p5_probe.sh eight DIR"; exit 2; }
    b p5_probe
    b p5_probe_before -DP5_OUT_SHIFT=1 -DP5_STACK_PHASE=0
    mkdir -p "$D"
    ./build/host/p5_probe eight "$D" after
    ./build/host/p5_probe_before eight "$D" before
    ;;
esac
case $W in steal|all)
    b p5_probe_s0 -DP5_STEAL_BLOCKS=0
    b p5_probe
    b p5_probe_s4 -DP5_STEAL_BLOCKS=4
    if [ -n "$D" ]; then
        mkdir -p "$D/steal"
        ./build/host/p5_probe_s0 steal "$D/steal" before | tee build/host/p5_steal_before.txt
        worst=$(sed -n 's/.*clicks most: \([0-9]*\) .*/\1/p' build/host/p5_steal_before.txt)
        P5_STEAL_WORST=$worst ./build/host/p5_probe steal "$D/steal" after
        P5_STEAL_WORST=$worst ./build/host/p5_probe_s4 steal "$D/steal" four | grep -v "^  VA\|^  CZ\|^  FM6\|FM6 restarts"
    else
        ./build/host/p5_probe_s0 steal
        ./build/host/p5_probe steal | grep -v "^  VA\|^  CZ\|^  FM6\|FM6 restarts"
        ./build/host/p5_probe_s4 steal | grep -v "^  VA\|^  CZ\|^  FM6\|FM6 restarts"
    fi
    ;;
esac
