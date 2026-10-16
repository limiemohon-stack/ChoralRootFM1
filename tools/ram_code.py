#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Code in RAM (docs/FX.md "Code in RAM"): what a build's listing says about it, and the proof that a CR_RAM_*
switch moved code without changing it.

  tools/ram_code.py list  DIS                 the functions in RAMTEXT (0x01C00000..): bytes, their direct calls,
                                              the flash (XIP) data they refer to
  tools/ram_code.py reach DIS FN [FN...]      the functions reached from FN by direct calls, with bytes: what a
                                              switch would have to move (calls through a pointer are not followed)
  tools/ram_code.py marks SWITCH [SWITCH...]  the functions firmware/src marks for these switches (CR_RAM_FX ..):
                                              the CR_HOT_x markers of cr_ram.h, as tools/build.py reads them
  tools/ram_code.py place OBJ NAME [NAME...]  rename the object's sections .text.NAME to .rhot.NAME, in place
                                              (firmware/app.ld puts .rhot.* in RAMTEXT), and point the calls across
                                              at the veneers in it (firmware/src/cr_ram.c); fails if one is missing
  tools/ram_code.py cmp   BASE.dis NEW.dis    the proof that a switch moved code and changed none: every function
                                              of both listings, instruction for instruction (below). Exit 1 if
                                              anything differs that is not explained

DIS is tools/build.py's build/choralroot.dis (the pi32v2 objdump listing); cmp also reads build/choralroot.sym beside
it (objdump -t: which labels are functions).

How code gets to RAM (firmware/src/cr_ram.h has the reasons): the compiler is not told. The sources mark the functions
(CR_HOT_x, an empty macro); tools/build.py compiles as always and then, for the switches that are on, renames those
functions' sections in the object (.text.NAME -> .rhot.NAME, which firmware/app.ld links at RAMTEXT) and points the
direct calls that would now cross between XIP and RAMTEXT at veneers (firmware/src/cr_ram.c, compiled in by the same
switch: cr_v_NAME takes NAME's arguments, loads NAME's address into a register and calls it). XIP and RAMTEXT are
4 MiB + 4 KiB apart and pi32v2's direct call reaches 4 MiB (R_PI32V2_LONG_JUMP_23M2: the linker refuses, it does not
truncate).

What "the same" means in cmp. An instruction is compared as its text with every address taken out: `<name : addr >`
becomes `<name>` (a jump-table or local label `<.L..+0x12 : addr >` becomes `<.>`), and the decimal the listing
prints before it (the relative offset of a call or goto, the absolute value of an address load) is dropped, as is the
encoding's bytes column. So a function may differ in where things are and in how far a branch reaches, never in an
operation, a register or a constant. A call of cr_v_NAME is read as a call of NAME. Every function the two listings
share must then be the same, the moved ones and all the others; the veneers are printed whole, and each must load
exactly one address, its target's, on the other side."""
import bisect
import re
import struct
import sys
from pathlib import Path

LABEL = re.compile(r"^([A-Za-z_.$][\w.$]*):$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)$")
REF = re.compile(r"(-?\d+) <([^:>]*?) ?: ([0-9a-f]+) >")
RAM_LO, RAM_HI, XIP_LO, XIP_HI = 0x01C00000, 0x01C06000, 0x02000000, 0x02100000   # RAMTEXT; the flash's window
RAM_TOP = 0x01C80000                    # the end of RAM


def functions(path):
    """{name: {"addr", "size", "insns": [(addr, nbytes, text)]}}; a function runs to the next label that does not
    start with '.' (the compiler's jump tables and local labels are inside functions)"""
    out, cur = {}, None
    for line in open(path, errors="replace"):
        m = LABEL.match(line.strip())
        if m and not line.startswith((" ", "\t")):
            if not m.group(1).startswith("."):
                cur = out.setdefault(m.group(1), {"insns": []})
            continue
        m = INSN.match(line)
        if cur is not None and m:
            cur["insns"].append((int(m.group(1), 16), len(m.group(2).split()), m.group(3).strip()))
    for f in out.values():
        f["addr"] = f["insns"][0][0] if f["insns"] else 0
        f["size"] = (f["insns"][-1][0] + f["insns"][-1][1] - f["addr"]) if f["insns"] else 0
    return {n: f for n, f in out.items() if f["insns"]}


def in_ram(a):
    return RAM_LO <= a < RAM_HI


def calls(f, fns):
    """direct calls and gotos that leave the function: [(target name, target addr)]"""
    lo, hi, out = f["addr"], f["addr"] + f["size"], []
    for _, _, t in f["insns"]:
        if t.lstrip("\t ").startswith("<"):
            continue                                # jump-table data
        for m in REF.finditer(t):
            a = int(m.group(3), 16)
            if ("call" in t or "goto" in t) and not lo <= a < hi and m.group(2) in fns and fns[m.group(2)]["addr"] == a:
                out.append((m.group(2), a))
    return out


def xip_refs(f):
    """names of the flash addresses the function loads (tables, strings; not calls)"""
    out = set()
    for _, _, t in f["insns"]:
        if "call" in t or "goto" in t or t.startswith("<"):
            continue
        for m in REF.finditer(t):
            if XIP_LO <= int(m.group(3), 16) < XIP_HI:
                out.add(m.group(2).split("+")[0])
    return out


# the linker's marks in RAM (firmware/app.ld). An address that is one of them is also the address of whatever lies
# there (_rt_start: the first function in RAMTEXT; _data_start: the first table), so it has two readings: norm's
# `edges` chooses this one, and cmp accepts an instruction that is the same in either
EDGES = ("_rt_start", "_rt_end", "_data_start", "_data_end", "_bss_start", "_bss_end", "_pool_start", "_pool_end")


def listing(dis):
    """a listing ready for cmp: its functions (the labels its .sym calls functions: the listing disassembles .rodata's
    symbols too), each function's start by address, and _etext"""
    sym = Path(dis).with_suffix(".sym")
    if not sym.exists():
        raise SystemExit(f"cmp: no {sym} (tools/build.py writes it): cannot tell functions from data in the listing")
    names, objs, lnk, edge = set(), {}, {}, {}
    for ln in sym.read_text().splitlines():
        m = re.match(r"^([0-9a-f]+) (.{7}) (\S+)\s+([0-9a-f]+) (\S+)$", ln)
        if not m:
            continue
        a, name = int(m.group(1), 16), m.group(5)
        if "F" in m.group(2):
            names.add(name)
        elif (XIP_LO <= a < XIP_HI or RAM_HI <= a < RAM_TOP) and int(m.group(4), 16):   # an object with a name and a
                                                                                    # size: an anchor
            objs[a] = min(objs.get(a, name), name)
        elif name in ("_rt_load", "_c1_load", "_data_load", "_etext"):   # the linker's: where the image's parts are
            lnk[a] = min(lnk.get(a, name), name)
        elif name in EDGES and (a not in edge or EDGES.index(name) < EDGES.index(edge[a])):
            edge[a] = name
    fns = {n: f for n, f in functions(dis).items() if n in names}
    return {"fns": fns, "at": {f["addr"]: n for n, f in fns.items()}, "obj": sorted(objs.items()), "lnk": lnk, "edge": edge,
            "etext": max([a for a, n in lnk.items() if n == "_etext"] + [0]) or XIP_HI}


def norm(addr, nbytes, t, f, L, edges=False):
    """an instruction's text with its addresses made relative to what they point into, so that two links of the same
    code read the same: a place in the function itself <+0xNN> (a branch, a jump table), another function <name>,
    a constant in XIP or an object in RAM <OBJECT+0xNN> by the nearest object symbol before it (.rodata, .data and
    .bss keep their order when code moves out of XIP or a table moves into RAM). The listing's decimal in front of
    an annotation (a branch's reach, an address's value) is dropped. A value that is in neither XIP nor RAM is a constant, whatever the listing found
    near it: the number stays, the annotation goes"""
    def where(a, given):
        if f["addr"] <= a < f["addr"] + f["size"]:
            return f"<+{a - f['addr']:#x}>"
        if edges == "abs":
            return f"<{a:#x}>"
        if edges and a in L["edge"]:
            return f"<{L['edge'][a]}>"
        if a in L["at"]:
            return f"<{L['at'][a]}>"
        if a in L["lnk"]:
            return f"<{L['lnk'][a]}>"
        if XIP_LO <= a < XIP_HI and a >= L["etext"]:          # past the code and its constants: a flash address
            return f"<xip {a:#x}>"                              # (a store's sector), the same wherever the code is
        if XIP_LO <= a < XIP_HI or RAM_HI <= a < RAM_TOP:
            i = bisect.bisect_right(L["obj"], (a, "\x7f")) - 1
            return f"<{L['obj'][i][1]}+{a - L['obj'][i][0]:#x}>" if i >= 0 else f"<{a:#x}>"
        return f"<{given}>" if given else f"<{a:#x}>"

    def ref(m):
        a = int(m.group(3), 16)
        if not (XIP_LO <= a < XIP_HI or RAM_LO <= a < RAM_TOP):
            return m.group(1)
        return where(a, m.group(2))
    t = re.sub(r"\s*#\s*<[^>]*>", "", t)                        # (the listing's note on a negative constant)
    t = re.sub(r"\s+", " ", REF.sub(ref, t)).strip()
    m = re.match(r"^(.*\b(?:call|goto)) (-?\d+)$", t)           # a branch the listing found no symbol for
    if m:
        return m.group(1) + " " + where(addr + nbytes + int(m.group(2)), "")
    return re.sub(r"(?<![\w<+x-])(\d{8})\b", lambda m: where(int(m.group(1)), "")   # an address the listing left bare
                  if XIP_LO <= int(m.group(1)) < XIP_HI or RAM_LO <= int(m.group(1)) < RAM_TOP else m.group(1), t)


def body(f, L, edges=False):
    out = [norm(a, n, t, f, L, edges) for a, n, t in f["insns"]]
    while out and out[-1] == "nop":                 # the padding to the next function's alignment is not its code
        out.pop()
    return out


def cmd_list(dis):
    fns = functions(dis)
    tot = 0
    for n, f in sorted(fns.items(), key=lambda kv: kv[1]["addr"]):
        if not in_ram(f["addr"]):
            continue
        tot += f["size"]
        cs = calls(f, fns)
        far = sorted({c for c, a in cs if not in_ram(a)})
        near = sorted({c for c, a in cs if in_ram(a)})
        ind = sum(1 for _, _, t in f["insns"] if re.match(r"call r\d+", t))
        print(f"{f['addr']:08x} {f['size']:6d} {n}" + (f"  calls {' '.join(near)}" if near else "")
              + (f"  through a pointer x{ind}" if ind else "") + (f"  FLASH CALLS {' '.join(far)}" if far else "")
              + (f"  flash data: {' '.join(sorted(xip_refs(f)))}" if xip_refs(f) else ""))
    print(f"total {tot} B of code in RAMTEXT")
    return 0


def cmd_reach(dis, roots):
    fns = functions(dis)
    seen, todo = [], list(roots)
    while todo:
        n = todo.pop(0)
        if n in seen or n not in fns:
            if n not in fns:
                print(f"(no function {n} in the listing: inlined?)")
            continue
        seen.append(n)
        todo += [c for c, _ in calls(fns[n], fns)]
    tot = 0
    for n in seen:
        f = fns[n]
        tot += f["size"]
        ind = sum(1 for _, _, t in f["insns"] if re.match(r"call r\d+", t))
        print(f"{f['addr']:08x} {f['size']:6d} {n}  calls {' '.join(sorted({c for c, _ in calls(f, fns)})) or '-'}"
              + (f"  through a pointer x{ind}" if ind else "")
              + (f"  flash data: {' '.join(sorted(xip_refs(f)))}" if xip_refs(f) else ""))
    print(f"total {tot} B in {len(seen)} functions")
    return 0


def cmd_cmp(base, new):
    """see the module's text. What may differ between the two builds, and nothing else:
      - where a function is (a moved one: XIP in BASE, RAMTEXT in NEW);
      - the target of a direct call: NAME in BASE, its veneer cr_v_NAME in NEW (the callers on the other side);
      - the veneers themselves: functions only NEW has, printed whole (they are all the new code there is)."""
    import difflib
    LB, LN = listing(base), listing(new)
    fb, fn = LB["fns"], LN["fns"]

    def nbody(f, L, edges=False):                   # a call of a veneer reads as a call of its target
        return [re.sub(r"<cr_v_(\w+)>", r"<\1>", ln) for ln in body(f, L, edges)]

    def same_fn(n):                                 # every instruction the same, in one reading or another (EDGES)
        a, b = nbody(fb[n], LB), nbody(fn[n], LN)
        if a == b:
            return True
        if len(a) != len(b):
            return False
        ae, be = nbody(fb[n], LB, True), nbody(fn[n], LN, True)
        aa, ba = nbody(fb[n], LB, "abs"), nbody(fn[n], LN, "abs")    # (.. or the very same address: a constant
        return all(x == y or xe == ye or xa == ya                    # that is one, the update loader's 0x01C0A800)
                   for x, y, xe, ye, xa, ya in zip(a, b, ae, be, aa, ba))
    moved = sorted(n for n, f in fn.items() if in_ram(f["addr"]) and n in fb and not in_ram(fb[n]["addr"]))
    veneers = sorted(n for n in fn if n not in fb and n.startswith("cr_v_"))
    born = sorted(n for n in fn if n not in fb and n not in veneers)
    gone = sorted(n for n in fb if n not in fn)
    bad = 0
    print(f"cmp {base} -> {new}: {len(fb)} / {len(fn)} functions")
    print(f"moved to RAMTEXT: {len(moved)} functions, {sum(fn[n]['size'] for n in moved)} B")
    for n in moved:
        same = same_fn(n)
        bad += not same
        via = sorted({m for ln in body(fn[n], LN) for m in re.findall(r"<cr_v_(\w+)>", ln)})
        print(f"  {'same' if same else 'DIFF'} {n}: {len(fn[n]['insns'])} instructions, {fn[n]['size']} B at "
              f"{fn[n]['addr']:08x} (base: {len(fb[n]['insns'])}, {fb[n]['size']} B at {fb[n]['addr']:08x})"
              + (f"; calls through a veneer: {' '.join(via)}" if via else ""))
    differ, redirected = [], {}
    for n in sorted(fn):
        if n not in fb:
            continue
        for ln in body(fn[n], LN):
            for m in re.findall(r"<cr_v_(\w+)>", ln):
                redirected.setdefault(m, []).append(n)
        if n not in moved and not same_fn(n):
            differ.append(n)
    for n in differ:
        bad += 1
        d = [ln for ln in difflib.unified_diff(nbody(fb[n], LB), nbody(fn[n], LN), lineterm="", n=0)
             if ln[:1] in "+-" and ln[:3] not in ("+++", "---")]
        print(f"  DIFFERS {n}: {len(fb[n]['insns'])} -> {len(fn[n]['insns'])} instructions")
        for ln in d[:30]:
            print("      " + ln)
    print(f"every other function ({len([n for n in fn if n in fb]) - len(moved)}): "
          f"{'instruction for instruction the same' if not differ else 'SEE ABOVE'}")
    print(f"calls redirected to a veneer: " + ("; ".join(f"{t} from {', '.join(sorted(set(c)))}"
                                                       for t, c in sorted(redirected.items())) or "none"))
    print(f"veneers (the only new code): {len(veneers)} functions, {sum(fn[n]['size'] for n in veneers)} B")
    for n in veneers:
        f = fn[n]
        where = "RAMTEXT" if in_ram(f["addr"]) else "XIP"
        far = [m.group(2) for _, _, tx in f["insns"] for m in REF.finditer(tx) if "call" not in tx and "goto" not in tx]
        ok = far == [n[5:]] and (n[5:] in fn) and in_ram(fn[n[5:]]["addr"]) != in_ram(f["addr"])
        bad += not ok
        print(f"  {n} ({where}, {f['size']} B, target {'ok' if ok else 'WRONG: ' + ' '.join(far)}): "
              + "; ".join(body(f, LN)))
    if gone or born:
        bad += 1
        print(f"functions only in the base: {' '.join(gone) or '-'}; only in the new: {' '.join(born) or '-'}")
    print(f"cmp: {'FAIL' if bad else 'ok'}: {len(moved)} moved, {len(differ)} functions differ, {len(veneers)} veneers")
    return 1 if bad else 0


MARK = re.compile(r"\bCR_HOT_([A-Z0-9]+)\b([^;{]*?)\b([A-Za-z_]\w*)\s*\(\s*(\w+)?")


def marks(switches, src=None):
    """{switch: [function names]} from the CR_HOT_x markers in firmware/src/*.c (cr_ram.h): the name is the first
    identifier before a '(' after the marker, attributes left out; MIX_LOOP_FN(name, ..): the name it defines"""
    src = Path(src) if src else Path(__file__).resolve().parents[1] / "firmware" / "src"
    out = {s: [] for s in switches}
    for f in sorted(src.glob("*.c")):
        text = re.sub(r"/\*.*?\*/|//[^\n]*", " ", f.read_text(), flags=re.S)
        text = re.sub(r"__attribute__\s*\(\((?:[^()]|\([^()]*\))*\)\)", " ", text)
        for m in MARK.finditer(text):
            name = m.group(4) if m.group(3) == "MIX_LOOP_FN" else m.group(3)
            sw = "CR_RAM_" + m.group(1)
            if sw in out and name not in out[sw]:
                out[sw].append(name)
    return out


R_CALL = 2                              # R_PI32V2_LONG_JUMP_23M2: a call or goto to another section (objdump -r)


def place(obj, names):
    """The compiled object (ELF32, little endian, relocatable), in place:
      1 the section .text.NAME becomes .rhot.NAME for each NAME: six bytes of the section-name string table (a name
        sharing that tail, .rela.text.NAME, changes with it: its relocation section, whose name nothing reads);
      2 the veneers (firmware/src/cr_ram.c: a function cr_v_T for a target T): cr_v_T's reference to the undefined
        cr_t_T is pointed at T; cr_v_T goes to RAM too when T stays in flash (it serves callers in RAM); and every
        direct call (R_PI32V2_LONG_JUMP_23M2) of T from a section on the other side is pointed at cr_v_T.
    No byte of code changes: section names and the symbol index of relocation entries. Returns ({NAME: bytes moved},
    {T: calls redirected})"""
    p = Path(obj)
    b = bytearray(p.read_bytes())
    if b[:6] != b"\x7fELF\x01\x01":
        raise SystemExit(f"{obj}: not a little-endian ELF32 file")
    shoff, = struct.unpack_from("<I", b, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", b, 0x2E)
    sh = [struct.unpack_from("<10I", b, shoff + i * shentsize) for i in range(shnum)]   # name type flags addr off
    str_off, str_size = sh[shstrndx][4], sh[shstrndx][5]                               # size link info align entsize

    def cstr(base, o):
        return bytes(b[base + o:b.index(0, base + o)]).decode()
    sname = [cstr(str_off, s[0]) for s in sh]
    symtabs = [i for i, s in enumerate(sh) if s[1] == 2]
    if len(symtabs) != 1:
        raise SystemExit(f"{obj}: {len(symtabs)} symbol tables")
    st = sh[symtabs[0]]
    syms = []                                       # (name, value, info, shndx)
    for o in range(st[4], st[4] + st[5], 16):
        nm, val, _, info, _, shndx = struct.unpack_from("<IIIBBH", b, o)
        syms.append((cstr(sh[st[6]][4], nm), val, info, shndx))

    def func(name):                                 # the one defined function symbol of that name
        idx = [i for i, s in enumerate(syms) if s[0] == name and s[2] & 15 == 2 and s[3]]
        if len(idx) != 1:
            raise SystemExit(f"{obj}: {len(idx)} functions named {name} (a veneer's target, firmware/src/cr_ram.c)")
        return idx[0]
    veneers = {s[0][5:]: i for i, s in enumerate(syms) if s[0].startswith("cr_v_") and s[2] & 15 == 2 and s[3]}
    hot = list(dict.fromkeys(names))
    hot += ["cr_v_" + t for t in veneers if t not in hot]    # a flash target's veneer: with its callers, in RAM
    sizes, missing, ram = {}, [], set()
    for n in hot:
        idx = [i for i, s in enumerate(sname) if s == ".text." + n]
        if len(idx) != 1:
            missing.append(n)
            continue
        o = str_off + sh[idx[0]][0]
        if bytes(b[o:o + 7 + len(n)]) != (".text." + n).encode() + b"\0" or not str_off <= o < str_off + str_size:
            raise SystemExit(f"{obj}: section name of {n} not where expected")
        for other in sname:                         # nothing but the section and its relocations may share the bytes
            if other.endswith(".text." + n) and other not in (".text." + n, ".rel.text." + n, ".rela.text." + n):
                raise SystemExit(f"{obj}: section {other} shares the name of .text.{n}")
        b[o:o + 6] = b".rhot."
        sizes[n] = sh[idx[0]][5]
        ram.add(idx[0])
    if missing:
        raise SystemExit(f"{obj}: no section .text.NAME for {' '.join(missing)}: marked CR_HOT_x (cr_ram.h) but not "
                         "emitted as a function of its own (inlined into its caller? not in this build?)")
    target = {t: func(t) for t in veneers}          # T -> its symbol
    tsect = {syms[i][3]: t for t, i in target.items()}
    calls = {t: 0 for t in veneers}
    for s in sh:
        if s[1] == 9:
            raise SystemExit(f"{obj}: a REL relocation section (RELA expected)")
        if s[1] != 4:
            continue
        x = s[7]                                    # the section these relocations patch
        for o in range(s[4], s[4] + s[5], 12):
            _, info, addend = struct.unpack_from("<IIi", b, o)
            si, ty = info >> 8, info & 0xFF
            name, val, _, shndx = syms[si]
            if name.startswith("cr_t_") and not shndx:           # the veneer's own reference: to the real function
                if name[5:] not in target:
                    raise SystemExit(f"{obj}: {name} referenced, no veneer cr_v_{name[5:]}")
                struct.pack_into("<I", b, o + 4, target[name[5:]] << 8 | ty)
                continue
            t = tsect.get(shndx)
            if t is None or ty != R_CALL or val + addend != syms[target[t]][1]:
                continue
            if x == syms[veneers[t]][3] or (x in ram) == (shndx in ram):
                continue                            # (the same side: the direct call stays)
            struct.pack_into("<Ii", b, o + 4, veneers[t] << 8 | ty, 0)
            calls[t] += 1
    p.write_bytes(b)
    return sizes, calls


def main(argv):
    if len(argv) >= 3 and argv[1] == "marks":
        for s, names in marks(argv[2:]).items():
            print(f"{s}: {' '.join(names)}")
        return 0
    if len(argv) >= 4 and argv[1] == "place":
        sizes, calls = place(argv[2], argv[3:])
        for n, s in sizes.items():
            print(f"{s:6d} {n}")
        for n, c in calls.items():
            print(f"{c:6d} calls of {n} through cr_v_{n}")
        return 0
    if len(argv) >= 3 and argv[1] == "list":
        return cmd_list(argv[2])
    if len(argv) >= 4 and argv[1] == "reach":
        return cmd_reach(argv[2], argv[3:])
    if len(argv) == 4 and argv[1] == "cmp":
        return cmd_cmp(argv[2], argv[3])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
