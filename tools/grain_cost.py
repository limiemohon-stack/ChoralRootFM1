#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
"""GRAIN's cost on the unit, counted from the device compiler's code (docs/GRAIN.md "Budgets").

    python3 tools/grain_cost.py            (Docker, as ./build.sh; build/gen/felucca_tables.h must exist: ./build.sh)

Compiles tools/grain_sketch.c's core alone (GS_UNIT 1: the source generator and the grain player, no main) with the
device compiler at -Os as tools/build.py compiles the audio code, into build/grain-cost/unit.o, and reads its listing:
the functions' sizes (the flash the engine's DSP would take, before pages, presets and the band) and every loop's
body (tools/fx_loops.py's rule: a backward branch; the body's instructions), so that a generator's cost a source
sample and the player's cost a grain-sample can be counted at 205 instructions a microsecond (docs/FX.md "The cost on
the unit"). The count has been low against the unit by 1.2 .. 1.7 (the fx stage, CZ-1): the table says so."""
import os
import re
import sys
from pathlib import Path

os.environ.setdefault("JIELI_TOOLCHAIN", str(Path.home() / ".jieli" / "toolchain"))   # (as build.sh)
sys.path.insert(0, str(Path(__file__).resolve().parent))
import build as B  # noqa: E402

SRC = Path(__file__).resolve().parent.parent
OUT = SRC / "build" / "grain-cost"
RATE = 205.0


def functions(dis):
    """{name: [(addr, text)]} of an objdump -d listing"""
    fns, cur = {}, None
    for l in dis.split("\n"):
        m = re.match(r"^(\w+):$", l)
        if m:
            cur = fns.setdefault(m.group(1), [])
            continue
        m = re.match(r"\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)", l)
        if m and cur is not None:
            cur.append((int(m.group(1), 16), m.group(3)))
    return fns


def loops(ins):
    """[(start offset, end offset, body length)] of a function's backward branches"""
    addr = [a for a, _ in ins]
    out = []
    for k, (ad, tx) in enumerate(ins):
        m = re.search(r"goto (-?\d+) <[^:]*: ([0-9a-f]+) >", tx)   # (tools/fx_loops.py's rule)
        if not m:
            continue
        t = int(m.group(2), 16)
        if t >= ad or t not in addr:
            continue
        out.append((t - ins[0][0], ad - ins[0][0], k - addr.index(t) + 1))
    return out


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    flags = [*B.CFLAGS, "-Ibuild/gen", "-Ifirmware/src", "-Ifirmware/hal", "-DGS_UNIT=1", "-DCR_CPU1=0"]
    B.tc("cc", *flags, "-c", "tools/grain_sketch.c", "-o", "build/grain-cost/unit.o")
    dis = B.tc("common/bin/objdump", "-d", OUT / "unit.o")
    (OUT / "unit.dis").write_text(dis)
    hdr = B.tc("common/bin/objdump", "-h", OUT / "unit.o")
    (OUT / "unit.hdr").write_text(hdr)
    text = rodata = 0
    for l in hdr.split("\n"):
        m = re.match(r"\s*\d+\s+(\.\S+)\s+([0-9a-f]+)", l)
        if m:
            name, size = m.group(1), int(m.group(2), 16)
            if name.startswith(".text"):
                text += size
            elif name.startswith(".rodata"):
                rodata += size
    fns = functions(dis)
    print(f"the core alone, -Os: .text {text} B, .rodata {rodata} B (tables: the window 514, the vowels 30, the ratios 24, "
          f"the norms 34)")
    print()
    print("| function | instructions | bytes | loops: body instructions (passes a sample unless said) |")
    print("| --- | --- | --- | --- |")
    for name in sorted(fns, key=lambda n: (not n.startswith("gs_"), n)):
        ins = fns[name]
        if not ins or not (name.startswith("gs_") or name.startswith("gp_")):
            continue
        lp = loops(ins)
        print(f"| `{name}` | {len(ins)} | {ins[-1][0] - ins[0][0]} | "
              + ", ".join(f"+0x{a:X}..+0x{b:X}: {n}" for a, b, n in lp) + " |")
    print()
    print(f"At {RATE:.0f} instructions a microsecond. A generator's sample loop times {16384} samples is the render "
          f"(a 100-instruction body: {16384 * 100 / RATE / 1000:.1f} ms); a grain's loop times 128 is its cost a half.")


if __name__ == "__main__":
    main()
