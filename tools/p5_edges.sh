#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The oscillators' edges, a study (tools/p5_edges.c; docs/PROPHET.md "The oscillators' edges"; HISTORY 3y50):
#   sh tools/p5_edges.sh        the counts per register, then the two alternatives' difference against the firmware
# Needs build/gen (./build.sh or tools/emu/build.sh). Not a pass / fail suite; a few minutes.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host/p5_all
python3 tools/gen_prophet_factory.py build/host/p5_all/melodee_prophet_factory.h all > /dev/null
CC="cc -std=gnu11 -O2 -w -Ibuild/host/p5_all -Ibuild/gen -Ifirmware/src -Itests -Itools"
$CC -o build/host/p5_edges tools/p5_edges.c -lm
$CC -DEDGE_A -o build/host/p5_edges_a tools/p5_edges.c -lm
$CC -DEDGE_C -o build/host/p5_edges_c tools/p5_edges.c -lm
./build/host/p5_edges count
./build/host/p5_edges pcm build/host/p5_edges.pcm
./build/host/p5_edges_a pcm build/host/p5_edges_a.pcm
./build/host/p5_edges_c pcm build/host/p5_edges_c.pcm
echo "(a) the fraction from a 256-entry reciprocal, no correction step:"
./build/host/p5_edges cmp build/host/p5_edges.pcm build/host/p5_edges_a.pcm
echo "(c) one correction tap (the sample after the edge) instead of two:"
./build/host/p5_edges cmp build/host/p5_edges.pcm build/host/p5_edges_c.pcm
