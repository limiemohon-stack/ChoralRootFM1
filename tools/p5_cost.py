#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
"""PROPHET's cost on the unit, counted from the device compiler's code (docs/PROPHET.md "Cost on the unit").

    ./build.sh && python3 tools/p5_cost.py            (Docker, as ./build.sh; build/choralroot.dis must be this tree's)

The sample loop (eng_prophet_test.c p5_samples) is one loop of about a thousand instructions with a branch for every
switch of a program: what a sample runs depends on the program. For each of the programs below (CONFIGS) the loop of
build/choralroot.dis is walked with that program's switches decided (switches(): which branches test a switch is read
off the listing itself, from the bytes the code before the loop loads out of the program) and the two branches a
sounding voice never takes closed (ordinary()); every path left from the loop's top to its end is a sample. Counted:

  hit      the shortest path: no oscillator edge in the sample (the BLEP's divide is on the other branch), the filter's
           coefficients as the sample before (cutoff and resonance unchanged: an envelope at rest, no modulation)
  miss     the shortest path through the coefficient set-up (the block that reads P5_TPT_G): the cutoff moved since the
           sample before, as it does all through an envelope's attack and decay, with Poly-Mod or the wheel on the filter
  longest  the longest path (every edge and every clamp in one sample: a bound, not a sample that happens)

The cross-check ("built alone"; --no-unit leaves it out, and Docker with it): a small unit (build/p5cost/unit.c) calls
p5_samples, inlined, with each program as a constant, and the device compiler (the same clang, -Os, as tools/build.py
compiles the audio code) folds the switches away: the loop that program alone would need. It is always shorter than
the firmware's path for the program: by the switches' tests, and by what the compiler cannot drop while a switch may
be on (a pulse width worked out for a pulse that is off). Per block (32 samples) and per part: the listing's functions
as they are (p5_render with two p5_env_tick, p5_block, p5_mod_samples, p5_post). At 205 instructions a microsecond (the
unit's measured rate on straight loops, docs/FX.md "The cost on the unit"); a hardware divide counts as one
instruction, as there. The count has been LOW against the unit by up to 1.7 (CZ-1: docs/HANDOFF.md item 21): the
table prints both.
"""
import os
import re
import sys
from pathlib import Path

os.environ.setdefault("JIELI_TOOLCHAIN", str(Path.home() / ".jieli" / "toolchain"))   # (as build.sh)
sys.path.insert(0, str(Path(__file__).resolve().parent))
import build as B  # noqa: E402  (tools/build.py: the toolchain in Docker)

SRC = Path(__file__).resolve().parent.parent
OUT = SRC / "build" / "p5cost"
RATE = 205.0                      # instructions a microsecond
HALF_US = 2902.0
LOW = 1.7                         # the count against the unit, at worst so far (CZ-1)
MEASURED = 2033 / 1318            # P5 itself, the owner's unit, 2026-10-13: a five-voice chord's pk_part0_us against
                                  # this tool's count for it (cd43720): 1.54

# prophet_patch.c's indices
(FREQ_A, FREQ_B, FINE_B, SAW_A, PULSE_A, SAW_B, TRI_B, PULSE_B, PW_A, PW_B, SYNC, LOW_B, KEY_B, GLIDE, LEVEL_A, LEVEL_B,
 NOISE, CUTOFF, RESONANCE, KEY_FILTER, FILTER_REV, LFO_RATE, LFO_INITIAL, LFO_SAW, LFO_TRI, LFO_PULSE, WHEEL_MIX,
 WHEEL_FREQ_A, WHEEL_FREQ_B, WHEEL_PW_A, WHEEL_PW_B, WHEEL_FILTER, POLY_ENV, POLY_B, POLY_FREQ, POLY_PW, POLY_FILTER) = range(37)
INIT = {FREQ_A: 24, FREQ_B: 24, SAW_A: 1, KEY_B: 1, PW_A: 64, PW_B: 64, LEVEL_A: 127, CUTOFF: 127, FILTER_REV: 1}
B_ON = {LEVEL_B: 127, SAW_B: 1}
CONFIGS = [
    ("none", "no waveform switched on, Curtis (the loop's floor)", {SAW_A: 0}),
    ("init", "INIT: saw A, Curtis", {}),
    ("saws_curtis", "saw A + saw B, Curtis", B_ON),
    ("saws_ssi", "saw A + saw B, SSI", {**B_ON, FILTER_REV: 0}),
    ("pulses_curtis", "saw + pulse A, saw + pulse B, Curtis", {**B_ON, PULSE_A: 1, PULSE_B: 1}),
    ("pulses_ssi", "saw + pulse A, saw + pulse B, SSI", {**B_ON, PULSE_A: 1, PULSE_B: 1, FILTER_REV: 0}),
    ("vibrato_ssi", "saw A + saw B, the wheel on both frequencies, SSI", {**B_ON, WHEEL_FREQ_A: 1, WHEEL_FREQ_B: 1, FILTER_REV: 0}),
    ("polyflt_ssi", "saw A + saw B, Poly-Mod to the filter, SSI", {**B_ON, POLY_FILTER: 1, POLY_B: 64, POLY_ENV: 64, FILTER_REV: 0}),
    ("sync_curtis", "sync: saw A synced to B, Poly-Mod to A's frequency, Curtis",
     {**B_ON, SYNC: 1, POLY_FREQ: 1, POLY_ENV: 96}),
    ("all_ssi", "everything: saw + pulse A, saw + triangle + pulse B, sync, Poly-Mod to frequency, width and filter, "
     "the wheel on all five, SSI",
     {**B_ON, PULSE_A: 1, PULSE_B: 1, TRI_B: 1, SYNC: 1, POLY_FREQ: 1, POLY_PW: 1, POLY_FILTER: 1, POLY_ENV: 127,
      POLY_B: 127, WHEEL_FREQ_A: 1, WHEEL_FREQ_B: 1, WHEEL_PW_A: 1, WHEEL_PW_B: 1, WHEEL_FILTER: 1, FILTER_REV: 0}),
]

INSN = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t?(.*)$")
LABEL = re.compile(r"^(?:[0-9a-f]+ <)?([A-Za-z_][A-Za-z_0-9.]*)>?:$")
GOTO = re.compile(r"goto -?\d+ <[^>]*: ([0-9a-f]+) >")
RELOC = re.compile(r"^\s*([0-9a-f]+):\s+R_\S+\s+(\S+)")


def function(lines, name):
    """[(addr, text)] of a function in an objdump -d listing; a relocation line's symbol is appended to the text of the
    instruction before it"""
    out, on = [], False
    for line in lines:
        m = LABEL.match(line.strip())
        if m and not m.group(1).startswith("."):
            if on:
                break
            on = m.group(1) == name
            continue
        if not on:
            continue
        m = RELOC.match(line)
        if m and out:
            out[-1] = (out[-1][0], out[-1][1] + " <" + m.group(2) + ">")
            continue
        m = INSN.match(line)
        if m and not m.group(3).lstrip().startswith("<"):
            out.append((int(m.group(1), 16), m.group(3).strip()))
    return out


def loops(ins):
    """[(first index, last index)] of the loops: a backward branch closes one (the farthest per head)"""
    idx = {a: i for i, (a, _) in enumerate(ins)}
    heads = {}
    for i, (a, t) in enumerate(ins):
        m = GOTO.search(t)
        if m and int(m.group(1), 16) in idx and idx[int(m.group(1), 16)] <= i:
            h = idx[int(m.group(1), 16)]
            heads[h] = max(heads.get(h, i), i)
    return sorted(heads.items(), key=lambda x: x[0] - x[1])   # the widest first


REG = r"r\d+"
OPS = {"+": lambda x, y: x + y, "-": lambda x, y: x - y, "|": lambda x, y: x | y, "&": lambda x, y: x & y,
       "^": lambda x, y: x ^ y}


def switches(ins, a, b, program):
    """the firmware's own loop for one program: {index of a conditional branch in a..b: True taken / False not} for
    the branches that test a switch of the program. The compiler reads the fifteen switch bytes before the loop (the
    program is in r1 at the function's entry) into registers and stack slots, sometimes two of them OR-ed, and tests
    those in the loop: the code before the loop is evaluated here for what is known from the program alone (a byte
    of it, a constant, a move, | & ^ + - of known values, a stack slot), and a branch in the loop is decided when what
    it tests is a register or slot the loop never writes, or one loaded from such a slot just before"""
    reg, slot = {"r1": ("P", 0)}, {}

    def val(x):
        x = x.strip()
        if re.fullmatch(REG, x):
            return reg.get(x)
        if re.fullmatch(r"-?\d+", x):
            return int(x)
        return None

    def step(t, reg, slot):
        t = t.split("#")[0].split("<")[0].strip()
        m = re.fullmatch(rf"({REG}) = b\[({REG})\+(\d+)\] \(u\)", t)
        if m:
            p = reg.get(m.group(2))
            reg[m.group(1)] = program[p[1] + int(m.group(3))] if isinstance(p, tuple) else None
            return
        m = re.fullmatch(rf"\[sp\+(\d+)\] = ({REG})", t)
        if m:
            slot[int(m.group(1))] = reg.get(m.group(2))
            return
        m = re.fullmatch(rf"({REG}) = \[sp\+(\d+)\]", t)
        if m:
            reg[m.group(1)] = slot.get(int(m.group(2)))
            return
        m = re.fullmatch(rf"({REG}) = ({REG}|-?\d+)", t)
        if m:
            reg[m.group(1)] = val(m.group(2))
            return
        m = re.fullmatch(rf"({REG}) = ({REG}) ([-+|&^]) ({REG}|-?\d+)", t)
        if m:
            x, y = val(m.group(2)), val(m.group(4))
            if isinstance(x, tuple) and isinstance(y, int) and m.group(3) == "+":
                reg[m.group(1)] = ("P", x[1] + y)
            else:
                reg[m.group(1)] = OPS[m.group(3)](x, y) if isinstance(x, int) and isinstance(y, int) else None
            return
        m = re.fullmatch(rf"({REG}) ([-+|&^])= ({REG}|-?\d+)", t)
        if m:
            x, y = reg.get(m.group(1)), val(m.group(3))
            if isinstance(x, tuple) and isinstance(y, int) and m.group(2) == "+":
                reg[m.group(1)] = ("P", x[1] + y)
            else:
                reg[m.group(1)] = OPS[m.group(2)](x, y) if isinstance(x, int) and isinstance(y, int) else None
            return
        for r in re.findall(rf"^({REG})(?:_({REG}))? ", t):          # anything else that assigns: unknown
            for x in r:
                if x:
                    reg[x] = None
        m = re.match(rf"\S*\[sp\+(\d+)\] = ", t)
        if m:
            slot[int(m.group(1))] = None

    # the code before the loop, in line (its one branch: n == 0). The listing prints some instructions as a pair
    # ("r5 = 16383  #" and, indented under it, "[sp+164] = r5"): the two read the registers as they were before
    # the pair (the store above keeps the old r5, the sample loop's "r2 = r1 * r0  #" / "r0 = [sp+172]" multiplies by
    # the old r0), so a pair is evaluated on the state before it and both results kept
    i = 0
    while i < a:
        if re.search(r"\s#(\s|$)", ins[i][1]) and i + 1 < a:
            none = object()
            new = []
            for t in (ins[i][1], ins[i + 1][1]):
                r2, s2 = dict(reg), dict(slot)
                step(t, r2, s2)
                new.append(({k: v for k, v in r2.items() if reg.get(k, none) is not v and reg.get(k, none) != v},
                            {k: v for k, v in s2.items() if slot.get(k, none) is not v and slot.get(k, none) != v}))
            for dr, ds in new:
                reg.update(dr)
                slot.update(ds)
            i += 2
        else:
            step(ins[i][1], reg, slot)
            i += 1
    # what the loop itself writes is not known in it
    wreg, wslot = set(), set()
    for i in range(a, b + 1):
        t = ins[i][1].split("#")[0].strip()
        for m in re.finditer(rf"^({REG})(?:_({REG}))? [-+|&^*/<>]*=", t):
            wreg.update(x for x in m.groups() if x)
        m = re.match(rf"\S*\[sp\+(\d+)\] [-+|&^]*= ", t)
        if m:
            wslot.add(int(m.group(1)))
        for m in re.finditer(rf"({REG})\+\+", t):      # h[r6++=2]
            wreg.add(m.group(1))
    out = {}
    for i in range(a, b + 1):
        t = ins[i][1]
        if not (t.lstrip().startswith("if") and GOTO.search(t)):
            continue
        m = re.match(rf"ifs? \(\(?({REG})(?: & 0xFF\))? (==|!=) 0\) goto", t.strip())
        if not m:
            continue
        r, v = m.group(1), None
        if r not in wreg:
            v = reg.get(r)                             # a register the loop never writes
        else:                                          # loaded from a slot the loop never writes, just before
            for k in range(i - 1, max(a, i - 4) - 1, -1):
                d = ins[k][1].split("#")[0].strip()
                mm = re.fullmatch(rf"{r} = \[sp\+(\d+)\]", d)
                if mm:
                    v = slot.get(int(mm.group(1))) if int(mm.group(1)) not in wslot else None
                    break
                if re.match(rf"{r}\b.*=", d):
                    break
        if isinstance(v, int):
            out[i] = (v & 0xFF == 0) if m.group(2) == "==" else (v & 0xFF != 0)
    return out


def ordinary(ins, a, b):
    """{index: False} for the two kinds of branch in the loop that the shortest path would take and a sample does not:
    an oscillator's "increment is 0" guard (p5_fast_blep's first test: the next instruction compares the phase with the
    same register) and p5_inc's "no modulation at all" (x == 0: it skips the block that reads P5_EXP2_Q14). Not taken
    in a sounding voice; the shortest path goes on through the phase's two range tests and the exponential"""
    idx = {ad: i for i, (ad, _) in enumerate(ins)}
    out = {}
    for i in range(a, b):
        t = ins[i][1].strip()
        m = re.match(rf"if \(({REG}) == 0\) goto", t)
        g = GOTO.search(t)
        if not m or not g or int(g.group(1), 16) not in idx:
            continue
        j = idx[int(g.group(1), 16)]
        if re.match(rf"if \({REG} >= {m.group(1)}\) goto", ins[i + 1][1].strip()):
            out[i] = False
        elif j > i and any("P5_EXP2_Q14" in ins[k][1] for k in range(i + 1, j)) and \
                not any(GOTO.search(ins[k][1]) and ins[k][1].lstrip().startswith("if (") and "== 0" in ins[k][1]
                        and any("P5_EXP2_Q14" in ins[q][1] for q in range(k + 1, min(j, b)))
                        for k in range(i + 1, j)):
            out[i] = False                             # (the innermost such branch: the switch's own is outside it)
    return out


def paths(ins, a, b, via=None, decided=None):
    """the shortest and the longest path (instructions run) from index a to the loop's closing branch b, forward edges
    only; via: a predicate of an instruction's text the shortest path must run through (None: any); decided: {index:
    taken} for the branches that are not a sample's to choose (switches). A path that leaves the span (a return) does
    not count. Returns (shortest, longest, divides on the shortest)"""
    idx = {ad: i for i, (ad, _) in enumerate(ins)}
    decided = decided or {}

    def succ(i):
        t = ins[i][1]
        m = GOTO.search(t)
        s = []
        if m:
            j = idx.get(int(m.group(1), 16))
            cond = t.lstrip().startswith("if")
            if j is not None and i < j <= b and decided.get(i, True):
                s.append(j)
            if cond and not decided.get(i, False):
                s.append(i + 1)
        else:
            s.append(i + 1)
        return [j for j in s if a <= j <= b]

    INF = 10 ** 9
    lo = [INF] * (b + 2)
    hi = [-INF] * (b + 2)
    dv = [0] * (b + 2)
    lo[b] = hi[b] = 1
    for i in range(b - 1, a - 1, -1):
        for j in succ(i):
            d = 1 if re.search(r"= r\d+ / r\d+", ins[i][1]) else 0
            if lo[j] + 1 < lo[i]:
                lo[i], dv[i] = lo[j] + 1, dv[j] + d
            if hi[j] > -INF and hi[j] + 1 > hi[i]:
                hi[i] = hi[j] + 1
    if via is None:
        return lo[a], hi[a], dv[a]
    # through a marked instruction: the shortest a -> k plus k -> b over the marked k
    fw = [INF] * (b + 2)
    fw[a] = 0
    for i in range(a, b):
        if fw[i] < INF:
            for j in succ(i):
                fw[j] = min(fw[j], fw[i] + 1)
    best = min((fw[k] + lo[k] for k in range(a, b + 1) if via(ins[k][1]) and fw[k] < INF and lo[k] < INF), default=None)
    return best, hi[a], None




def straight(ins):
    """a function without its loops' repeats: the instructions of its shortest and its longest forward path from its
    first instruction to a return (a block's work: its loops, the rare ones of a fade, are not followed back)"""
    idx = {ad: i for i, (ad, _) in enumerate(ins)}
    n = len(ins)
    INF = 10 ** 9
    lo, hi = [INF] * (n + 1), [-INF] * (n + 1)
    for i in range(n - 1, -1, -1):
        t = ins[i][1]
        m = GOTO.search(t)
        s = []
        if "{pc," in t or t.startswith("rts") or t.startswith("rets"):
            lo[i] = hi[i] = 1
            continue
        if m:
            j = idx.get(int(m.group(1), 16))
            if j is not None and j > i:
                s.append(j)
            if t.lstrip().startswith("if") and i + 1 < n:
                s.append(i + 1)
        elif i + 1 < n:
            s.append(i + 1)
        for j in s:
            if lo[j] < INF:
                lo[i] = min(lo[i], lo[j] + 1)
                hi[i] = max(hi[i], hi[j] + 1)
        if not s:
            lo[i] = hi[i] = 1
    return lo[0], hi[0]


def unit_source():
    L = ["/* generated by tools/p5_cost.py: p5_samples built for one program at a time */",
         "#include <stdint.h>",
         "void *memset(void *d, int c, unsigned long n);",
         "void *memcpy(void *d, const void *s, unsigned long n);",
         "#define FELUCCA_PROPHET 1",
         "#define FELUCCA_P5_FACTORY 0",
         "#define P5_HOT static inline __attribute__((always_inline))",
         "#define P5_HOT_IN static inline __attribute__((always_inline))",
         '#include "felucca_tables.h"',
         '#include "core.h"',
         '#include "dsp.c"',
         '#include "eng_prophet_test.c"',
         "static p5_part_t p5_cost_part;",
         "static p5_part_t *p5_part(uint32_t part) { (void)part; return &p5_cost_part; }"]
    for name, _, over in CONFIGS:
        L += [f"static const uint8_t P5C_{name}[133] = {{{','.join(map(str, program(over)))}}};",
              f"void p5c_{name}(p5_voice_t *s, int32_t *out, const vmod_t *m, const p5_render_params_t *c)",
              f"{{ p5_samples(s, P5C_{name}, out, CTL, m, c); }}"]
    return "\n".join(L) + "\n"


def program(over):
    raw = [0] * 133
    for k, v in {**INIT, **over}.items():
        raw[k] = v
    return raw


def tables_extern(text):
    """the generated tables with PROPHET's as externs, so that the listing's relocations name them"""
    def ext(m):
        return f"extern const {m.group(1)} {m.group(2)}[{m.group(3)}];"
    return re.sub(r"static const (\w+) (P5_\w+)\[(\d+)\] = \{.*?\};", ext, text, flags=re.S)


def us(n):
    return n / RATE


def main():
    dis_path = SRC / "build" / "choralroot.dis"
    if not dis_path.exists():
        sys.exit("p5_cost.py: no build/choralroot.dis (./build.sh first)")
    D = dis_path.read_text().split("\n")
    real = function(D, "p5_samples")
    if not real:
        sys.exit("p5_cost.py: no p5_samples in build/choralroot.dis (FELUCCA_PROPHET=0?)")
    (ra, rb) = loops(real)[0]
    is_miss = lambda t: "P5_TPT_G" in t               # the coefficient set-up reads the table
    if not any(is_miss(t) for _, t in real[ra:rb + 1]):
        sys.exit("p5_cost.py: the loop does not name P5_TPT_G: the listing's form changed")

    # the same loop built for one program (the cross-check: the switches folded by the compiler)
    U = []
    if "--no-unit" not in sys.argv:
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / "felucca_tables.h").write_text(tables_extern((SRC / "build" / "gen" / "felucca_tables.h").read_text()))
        (OUT / "unit.c").write_text(unit_source())
        flags = [*B.CFLAGS, "-Ibuild/p5cost", "-Ifirmware/hal", "-Ifirmware/src", "-Ibuild/gen"]
        B.tc("cc", *flags, "-c", OUT / "unit.c", "-o", OUT / "unit.o")
        # linked alone, the tables at made-up addresses, so that the listing names them (P5_TPT_G: the set-up's)
        tabs = re.findall(r"extern const \w+ (P5_\w+)\[", (OUT / "felucca_tables.h").read_text())
        B.tc("pi32v2/bin/ld", "--unresolved-symbols=ignore-all", "-e", "p5c_init", "-Ttext=0x2000000",
             *[f"--defsym={n}={0x3000000 + 0x10000 * k:#x}" for k, n in enumerate(tabs)], OUT / "unit.o", "-o", OUT / "unit.elf")
        listing = B.tc("common/bin/objdump", "-d", OUT / "unit.elf")
        (OUT / "unit.dis").write_text(listing)
        U = listing.split("\n")

    nsw = 0
    rows = []
    usual = ordinary(real, ra, rb)
    for name, text, over in CONFIGS:
        dec = switches(real, ra, rb, program(over))
        nsw = max(nsw, len(dec))
        dec = {**usual, **dec}
        lo, hi, dv = paths(real, ra, rb, decided=dec)
        miss, _, _ = paths(real, ra, rb, via=is_miss, decided=dec)
        row = [name, text, lo, miss, hi, len(dec), None, None]
        f = function(U, "p5c_" + name) if U else []
        if f:
            (a, b) = loops(f)[0]
            du = ordinary(f, a, b)
            row[6] = paths(f, a, b, decided=du)[0]
            row[7] = paths(f, a, b, via=is_miss, decided=du)[0]
        rows.append(row)
    und = sum(1 for i in range(ra, rb + 1) if real[i][1].lstrip().startswith("if") and GOTO.search(real[i][1]))
    print(f"p5_samples in build/choralroot.dis: {len(real)} instructions, its loop {rb - ra + 1} of them with {und} "
          f"conditional branches, {nsw} of them a program's switches (decided per program below), the rest a sample's "
          "(an oscillator's edge, a clamp, the sign at the knee, the filter's coefficients kept or set up)")

    # a block's and a part's work, from the firmware's listing
    fn = {n: function(D, n) for n in ("p5_render", "p5_env_tick", "p5_block", "p5_mod_samples", "p5_post")}
    rs_lo, rs_hi = straight(fn["p5_render"])
    et_lo, et_hi = straight(fn["p5_env_tick"])
    (ma, mb) = loops(fn["p5_mod_samples"])[0]
    mod_lo, mod_hi, _ = paths(fn["p5_mod_samples"], ma, mb)
    (pa, pb) = loops(fn["p5_post"])[0]
    post = pb - pa + 1
    blk_loop = loops(fn["p5_block"])[0]
    blk = len(fn["p5_block"]) + 54 * (blk_loop[1] - blk_loop[0] + 1)
    voice_block = rs_hi + 2 * et_hi            # p5_render's set-up (its longest forward path) and two envelopes
    part_block = blk + 32 * mod_hi + len(fn["p5_mod_samples"]) - (mb - ma + 1) + 32 * post + len(fn["p5_post"]) - post
    print(f"a voice a block (32 samples): p5_render {rs_lo}..{rs_hi} instructions + 2 x p5_env_tick {et_lo}..{et_hi}: "
          f"{voice_block} at most")
    print(f"a part a block: p5_block {blk} (its 55-byte copy), p5_mod_samples 32 x {mod_lo}..{mod_hi}, p5_post 32 x {post}: "
          f"{part_block} at most ({us(4 * part_block):.0f} us a half)\n")

    hdr = ("program", "a sample: hit", "miss", "longest", "built alone: hit", "a voice a half (miss)", "us",
           "2 voices", "3", "4", "4 at 1.54 x")
    print("| " + " | ".join(hdr) + " |")
    print("|" + " --- |" * len(hdr))
    for name, text, lo, miss, hi, nd, ulo, umiss in rows:
        voice = 128 * miss + 4 * voice_block
        part = 4 * part_block
        v = [us(k * voice + part) for k in (2, 3, 4)]
        alone = f"{ulo}" if ulo is not None else "-"
        print(f"| {text} | {lo} | {miss} | {hi} | {alone} | {voice} | {us(voice):.0f} | {v[0]:.0f} | {v[1]:.0f} | "
              f"{v[2]:.0f} | {v[2] * MEASURED:.0f} |")
    print(f"\n(us of a {HALF_US:.0f} us half at {RATE:.0f} instructions a microsecond; the part's own "
          f"{us(4 * part_block):.0f} us is in the voices' columns; \"4 at 1.54 x\": four voices (the cap) with the count as low "
          "against the unit as it was for P5's own five-voice chord (2033 us measured, 1318 counted). \"built alone\": the loop the compiler makes for that program only, without the "
          "switches' tests: the cross-check, always less)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
