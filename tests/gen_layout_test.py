#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
"""The layout generator (tools/gen_layout.py, docs/LAYOUT-TOOL.md "Step 4, the firmware: what was built"):

  python3 tests/gen_layout_test.py          (from the repo root; needs nothing built; node for the JS comparison)

- the build: BUILD is p5-rows.json, compiled as a screen plan (11 screens over 17 pages, all 56 P5 parameters once, the
  header's classic tables, CR_LAYOUTS_FREE 0); its bytes are packDesign's of the same file, byte for byte (node);
- the shipped designs: p5-panel.json compiles as free screens (8 screens, 73 elements, 19 lanes, all 56 P5 parameters
  once, the byte cost printed; --design P5=..: CR_LAYOUTS_FREE 1), its free half packFree's byte for byte (node);
  p5-osc-panel.json is refused (flat);
- web/test_layout.mjs 6's broken designs, made the same way from p5-osc-panel.json: each refused with its own reason
  (an error the unbroken file does not have, matching the reason's words);
- the generator's own refusals (the firmware's rules: the grid, the area, the budgets, the lanes, a parameter once, a
  pair apart, a band element, a classic screen beside free ones, the flat style, FX and MIX as today ..) on
  p5-panel.json; the screen plan's (a stack, three lanes, a band the plan has not, a part parameter on an engine page,
  a label not Latin-1) on p5-rows.json;
- a warning, not a refusal: a lane whose slots are not left to right (0.22-beta1's LFO lane) builds and warns, the
  shipped panel does not; the warnings are checkScreen's "order" findings word for word (node);
- the same rules as validateDesign: the generator's checks without the build's (validate(.., built=False)) give
  validateDesign's errors word for word on every design here, and with them validateDesign(.., {build: true})'s
  (node, web/fm1layout.js)."""
import copy
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import gen_layout as G  # noqa: E402

checks = fails = 0


def ok(cond, what):
    global checks, fails
    checks += 1
    if not cond:
        fails += 1
        print("FAIL", what)


def load(name):
    return json.loads((ROOT / "design/layouts/designs" / name).read_text(encoding="utf-8"))


cat = G.load_catalog("P5")
panel, osc = load("p5-panel.json"), load("p5-osc-panel.json")


def refusals(d):
    try:
        G.compile_design(d, cat)
        return []
    except ValueError as e:
        return e.args[0]


# ------------------------------------------------------------------------------------- the shipped designs ---
c = G.compile_design(copy.deepcopy(panel), cat)
n = G.cost(c)
ok((n["screens"], n["elements"], n["lanes"]) == (8, 73, 19), f"p5-panel: 8 screens, 73 elements, 19 lanes ({n})")
addr = [(r[5], r[6]) for s in c["screens"] for r in s["elements"] if r[5] != G.NONE]
ok(len(addr) == 56 and len(set(addr)) == 56 and all(p < 19 and col < 4 for p, col in addr),
   f"p5-panel: {len(addr)} bound elements, all 56 P5 parameters once, each within its 19 pages")
total = n["bytes"] + n["pool"]
ok(n["bytes"] == 73 * 14 + 8 * 38 + 8 and 300 < n["pool"] < 700 and total < 2048,
   f"p5-panel: {total} B (the plan's estimate: about 1.8 KB)")
print(f"gen_layout_test: p5-panel {n['elements']} x 14 + {n['screens']} x 38 + pool {n['pool']} + 8 = {total} B of flash")
ok(c["screens"][3]["name"] == "ENV 1 · amp" and b"ENV 1 \xb7 amp\0" in c["pool"].data, "a title's middle dot as Latin-1 (0xB7)")
ok(c["classic"] is None, "p5-panel: no classic screen in OSC .. MOD, no classic layout (the engine's own pages served)")
osc_base = refusals(copy.deepcopy(osc))
ok(osc_base == ['style "flat": the firmware draws the p5 style only (a built design is "style": "p5")'],
   f"p5-osc-panel: refused for the flat style only (its classic screens in FILT .. MOD: a plan) ({osc_base})")
rows = json.loads((ROOT / "design/layouts/designs/p5-rows.json").read_text(encoding="utf-8"))   # (3y94: the p5 cell styles)
ok(G.BUILD == {"P5": "design/layouts/designs/p5-rows.json"}, f"BUILD: P5's rows ({G.BUILD})")
cr = G.compile_design(copy.deepcopy(rows), cat)
cl, nr = cr["classic"], G.cost(cr)
srcs = [c["src"] for p in cl["pages"] for c in p["cols"] if c]
ok(not cr["screens"] and len(cl["pages"]) == 17 and len(cl["screens"]) == 11 and cl["fence"] is None and
   cl["section"] == [0, 4, 7, 9, 13, 255, 255, 255] and len(srcs) == 56 and len(set(srcs)) == 56 and
   [s["band"] for s in cl["screens"]] == [0, 0, 2, 1, 0, 1, 0, 0, 0, 0, 0] and
   all((s["pg"][1] == 255) == (s["band"] != 0 or s["title"] in ("ENV 2 +", "ENV 1 +")) for s in cl["screens"]),
   f"p5-rows: a screen plan of 11 screens over 17 pages, all 56 parameters once, a band's screen one lane ({nr})")
ok(cl["pages"][4]["title"] == "FILTER" and cl["pages"][4]["cols"][0] == {"src": (5, 0), "label": "CUTOFF", "style": 11} and
   cl["pages"][9]["cols"][1] == {"src": (12, 0), "label": "SAW", "style": 13},
   "FILTER's KNOB 1: CUTOFF at FILTER col 0 (p5knob); LFO's KNOB 2: SAW at LFO+ col 0 (p5led-saw)")
with tempfile.TemporaryDirectory() as tmp:
    out = Path(tmp) / "cr_layouts.h"
    r = subprocess.run([sys.executable, str(ROOT / "tools/gen_layout.py"), str(out)], capture_output=True, text=True)
    text = out.read_text(encoding="latin-1") if out.exists() else ""
    ok(r.returncode == 0 and "a screen plan of 11 screens over 17 pages" in r.stdout and "#define CR_LAYOUTS_FREE 0" in text and
       "static const eng_page_t CR_LAYC_PAGES[17]" in text and "static const eng_screen_t CR_LAYC_SCREENS[11]" in text and
       '"ENV 2 \\267 filter"' in text and "#define CR_LAYC_N 1u" in text,
       f"tools/gen_layout.py writes the header (P5's rows, no free layout) and its cost line ({r.stdout.strip() or r.stderr.strip()})")
    out2 = Path(tmp) / "free.h"
    r = subprocess.run([sys.executable, str(ROOT / "tools/gen_layout.py"), str(out2), "--design",
                        "P5=design/layouts/designs/p5-panel.json"], capture_output=True, text=True)
    text = out2.read_text(encoding="latin-1") if out2.exists() else ""
    ok(r.returncode == 0 and "8 free screens, 73 elements, 19 lanes" in r.stdout and f"= {total} B" in r.stdout and
       "static const cr_lay_elem_t CR_LAY_ELEMS[73]" in text and "#define CR_LAYOUTS_FREE 1" in text and "#define CR_LAYC_N 0u" in text,
       f"--design P5=p5-panel.json: its free tables, CR_LAYOUTS_FREE 1, no classic layout ({r.stdout.strip() or r.stderr.strip()})")

# ------------------------------------------------- web/test_layout.mjs 6: the broken designs, the same way ---
def osc_el(d, pred):
    return next(e for e in d["groups"][0]["screens"][0]["elements"] if pred(e))


def two_leds(d):
    osc_el(d, lambda e: e["type"] == "led").update(leds=2, glyph=None, param="OSC A:FREQ")


def set_(path, v):
    def f(d):
        o = d
        for k in path[:-1]:
            o = o[k]
        o[path[-1]] = v
    return f


FS = ["groups", 0, "screens", 0]           # OSC's first (free) screen
CL = ["groups", 1, "screens", 0]           # FILT's (classic) screen
BROKEN = {   # name: (how, the reason's words)
    "another format": (set_(["format"], "x"), r"not a choralroot-layout file"),
    "version 1": (set_(["version"], 1), r"format version 1"),
    "not a design": (set_(["kind"], "layout"), r'"kind" must be "design"'),
    "another params_hash": (set_(["params_hash"], "0" * 16), r"made for other parameters"),
    "another engine": (set_(["engine", "index"], 3), r"this catalog is P5"),
    "no title-line header": (set_(["header"], None), r'"header" must be'),
    "an unknown group": (set_(["groups", 1, "name"], "ARP"), r"not a group of P5"),
    "a screen of no type": (set_(CL + ["type"], "panel"), r'"type" must be "classic" or "free"'),
    "a classic lane of three cells": (lambda d: d["groups"][1]["screens"][0]["lanes"][0]["cells"].pop(), r"four cells"),
    "an unknown parameter": (set_(CL + ["lanes", 0, "cells", 0], {"param": "FILTER:NOPE"}), r'no parameter "FILTER:NOPE"'),
    "an unknown band": (set_(CL + ["band"], "rainbow"), r'band "rainbow"'),
    "an unknown element type": (set_(FS + ["elements", 1, "type"], "slider"), r'type "slider"'),
    "a position not a number": (set_(FS + ["elements", 1, "x"], "12"), r"x a whole number"),
    "two elements of one id": (lambda d: set_(FS + ["elements", 2, "id"], d["groups"][0]["screens"][0]["elements"][1]["id"])(d),
                               r"the id twice"),
    "a switch of four positions": (lambda d: osc_el(d, lambda e: e["type"] == "switch").update(positions=4), r"2 or 3 positions"),
    "an led glyph that does not exist": (lambda d: osc_el(d, lambda e: e["type"] == "led").update(glyph="star"), r'led glyph "star"'),
    "a colour that is not a token": (set_(FS + ["elements", 1, "colour"], "#ff0000"), r"not a palette token"),
    "a lane naming no element": (set_(FS + ["lanes", 0, 0], "nothing"), r'no element "nothing"'),
    "an element bound to a key": (set_(FS + ["elements", 1, "key"], 12), r"bound to a key"),
    "an unknown style": (set_(["style"], "x"), r'style "x"'),
    "an unknown cell style": (lambda d: d["groups"][1]["screens"][0]["lanes"][0]["cells"][0].update(style="glow"), r'style "glow"'),
    "two LEDs on a 0..127 parameter": (two_leds, r"two LEDs show a parameter of two values"),
    "an unknown sketch glyph": (lambda d: d["groups"][1]["screens"][0]["lanes"][0]["cells"][0].update(style="sketch:k9"),
                                r'style "sketch:k9" \(the sketch glyphs: k1, '),
}
ok(len(BROKEN) == 23, f"{len(BROKEN)} broken designs, as web/test_layout.mjs 6")
designs = {"p5-panel": panel, "p5-osc-panel": osc, "p5-rows": rows}
sketch = copy.deepcopy(osc)                                  # (3y96: a sketch glyph, the page's: valid, never built)
sketch["groups"][1]["screens"][0]["lanes"][0]["cells"][0]["style"] = "sketch:k6d"
sketch["groups"][1]["screens"][0]["lanes"][1]["cells"][1]["style"] = "sketch:b1a-saw"
designs["sketch: k6d and b1a-saw on FILT"] = sketch
sk_built = G.validate(copy.deepcopy(sketch), cat, built=True)
ok(not G.validate(copy.deepcopy(sketch), cat, built=False) and
   "FILT screen 1 lane 1 knob 1: not buildable yet: a sketch glyph (k6d); port it into the firmware to build" in sk_built and
   "FILT screen 1 lane 2 knob 2: not buildable yet: a sketch glyph (b1a); port it into the firmware to build" in sk_built,
   f"a design with sketch glyphs: valid; the generator refuses to build it, naming each ({[e for e in sk_built if 'sketch' in e]})")
for name, (how, why) in BROKEN.items():
    d = copy.deepcopy(osc)
    how(d)
    designs["broken: " + name] = d
    new = [e for e in refusals(d) if e not in osc_base]
    ok(any(re.search(why, e) for e in new), f"broken ({name}): refused with /{why}/ (new errors: {new})")


# ------------------------------------------------------------------------------- the generator's own rules ---
def scr(d, g, s=0):
    return next(x for x in d["groups"] if x["name"] == g)["screens"][s]


def el(d, g, i, s=0):
    return next(e for e in scr(d, g, s)["elements"] if e["id"] == i)


def classic_filt(d):
    next(x for x in d["groups"] if x["name"] == "FILT")["screens"] = copy.deepcopy(osc["groups"][1]["screens"])


def classic_beside(d):
    next(x for x in d["groups"] if x["name"] == "FILT")["screens"].append(copy.deepcopy(osc["groups"][1]["screens"][0]))


def many(d):
    scr(d, "OSC")["elements"] += [{"id": f"x{k}", "type": "label", "x": 4 * k, "y": 224, "w": 8, "h": 12, "text": "x"}
                                  for k in range(14)]


def fx_swap(d):
    c = scr(d, "FX")["lanes"][0]["cells"]
    c[0], c[1] = c[1], c[0]


OWN = {
    "off the grid": (lambda d: el(d, "OSC", "a_freq").update(x=9), r"not on the 4 px grid"),
    "over the title line": (lambda d: el(d, "OSC", "a_freq").update(y=24), r"outside the design area"),
    "off the right edge": (lambda d: el(d, "OSC", "fa").update(w=240), r"outside the design area"),
    "a band element": (lambda d: scr(d, "ENV")["elements"].append(
        {"id": "b", "type": "band", "x": 0, "y": 140, "w": 240, "h": 96, "band": "env"}), r"a band element: not built"),
    "a classic screen beside a free one": (classic_beside, r"a classic screen beside free screens in FILT"),
    "the flat style": (set_(["style"], "flat"), r"the p5 style only"),
    "no style (flat)": (lambda d: d.pop("style"), r"the p5 style only"),
    "25 elements": (many, r"25 elements: at most 24"),
    "9 lanes": (lambda d: scr(d, "OSC")["lanes"].extend([[None] * 4] * 6), r"9 lanes: 1 \.\. 8"),
    "a lane of five": (lambda d: scr(d, "OSC")["lanes"][0].append(None), r"four: KNOB 1\.\.4"),
    "an element in two lanes": (lambda d: scr(d, "OSC")["lanes"][1].__setitem__(0, "a_freq"), r"a_freq is in two lanes"),
    "a bound element in no lane": (lambda d: scr(d, "OSC")["lanes"][2].__setitem__(0, None), r"m_a .* is in no lane"),
    "a frame in a lane": (lambda d: scr(d, "OSC")["lanes"][0].__setitem__(3, "fa"), r"fa binds no parameter"),
    "a knob bound to nothing": (lambda d: el(d, "OSC", "a_freq").pop("param"), r"a knob bound to nothing"),
    "a parameter twice": (lambda d: el(d, "OSC", "b_fine", 1).update(param="OSC A:FREQ"), r"OSC A:FREQ is bound twice"),
    "a pair apart": (lambda d: (el(d, "LFO", "l_saw").update(pair="left"), el(d, "LFO", "l_tri").update(pair="right")),
                     r"no right half touching"),
    "labelAt in the middle": (lambda d: el(d, "OSC", "a_sync").update(labelAt="middle"), r'"labelAt" is "above" or "below"'),
    "a frame of r 13": (lambda d: el(d, "OSC", "fa").update(r=13), r'corner radius "r" is 0 \.\. 12'),
    "three LEDs": (lambda d: el(d, "FILT", "f_rev").update(leds=3), r'"leds" is 1 or 2'),
    "two LEDs and a glyph": (lambda d: el(d, "FILT", "f_rev").update(glyph="saw"), r"two LEDs have their names above them"),
    "a dial of 48": (lambda d: el(d, "OSC", "a_freq").update(size=48), r"a knob's dial is 28 \.\. 44"),
    "a title not Latin-1": (lambda d: scr(d, "OSC").update(title="OSC Ā"), r"not Latin-1"),
    "FX changed": (fx_swap, r"today's platform screen"),
    "a free screen in MIX": (lambda d: next(x for x in d["groups"] if x["name"] == "MIX")["screens"].append(
        {"type": "free", "title": "MORE", "elements": [], "lanes": [[None] * 4]}), r"a free screen in MIX"),
    "no ENV": (lambda d: d["groups"].pop(2), r"ENV: no screens"),
}
for name, (how, why) in OWN.items():
    d = copy.deepcopy(panel)
    how(d)
    designs["own: " + name] = d
    got = refusals(d)
    ok(any(re.search(why, e) for e in got), f"the generator refuses {name}: /{why}/ (errors: {got})")
paired = copy.deepcopy(panel)
el(paired, "LFO", "l_saw").update(pair="left")
el(paired, "LFO", "l_tri").update(pair="right", x=148)
ok(not refusals(paired), "a pair whose halves touch: built")
mixed = copy.deepcopy(panel)
classic_filt(mixed)
designs["own: classic FILT, free elsewhere"] = mixed
ok(not refusals(mixed) and G.compile_design(mixed, cat)["classic"]["fence"] is not None,
   "classic screens in FILT, free ones elsewhere: built (the plan, the built-in pages fenced off after it for the free ones)")


def rscr(d, g, s=0):
    return next(x for x in d["groups"] if x["name"] == g)["screens"][s]


PLAN = {   # the screen plan's rules on p5-rows.json
    "a stack in OSC": (lambda d: rscr(d, "OSC").update(kind="stack"), r"a stack of 2 lanes: the screen plan has edit8 screens of one or two lanes"),
    "three lanes": (lambda d: rscr(d, "LFO").__setitem__("lanes", rscr(d, "LFO")["lanes"] + [{"title": "X", "cells": [None] * 4}]),
                    r"an? edit8 of 3 lanes: the screen plan"),
    "a dx band": (lambda d: rscr(d, "ENV").update(band="dx"), r'band "dx": not a band of the screen plan \(none, env, filter, algo, ade2, wave\)'),
    "a part parameter on an engine page": (lambda d: (rscr(d, "OSC")["lanes"][1]["cells"].__setitem__(3, {"param": "part:33"}),
                                                      rscr(d, "FX")["lanes"][0]["cells"].__setitem__(0, None)),
                                           r"part:33: a part parameter on an engine page"),
    "a label not Latin-1": (lambda d: rscr(d, "OSC")["lanes"][0]["cells"][0].update(label="FRĒQ"), r"its label is a Latin-1 text"),
    "a lane title not Latin-1": (lambda d: rscr(d, "OSC")["lanes"][0].update(title="OSĆ"), r"lane 1: its title is a Latin-1 text"),
    "a sketch glyph": (lambda d: rscr(d, "FILT")["lanes"][0]["cells"][0].update(style="sketch:k6d"), r"a sketch glyph \(k6d\)"),
}
for name, (how, why) in PLAN.items():
    d = copy.deepcopy(rows)
    how(d)
    designs["plan: " + name] = d
    got = refusals(d)
    ok(any(re.search(why, e) for e in got), f"the generator refuses the rows with {name}: /{why}/ (errors: {got})")

# ----------------------------------------------------------- a warning, not a refusal: KNOB 1..4 left to right ---
beta1 = copy.deepcopy(panel)           # 0.22-beta1's LFO lane 1: FREQ (x 64) on KNOB 1, INIT AMT (x 8) on KNOB 2
scr(beta1, "LFO")["lanes"][0] = ["l_rate", "l_amt", None, "l_at"]
designs["warn: the LFO lane of 0.22-beta1"] = beta1
ok(scr(panel, "LFO")["lanes"][0] == ["l_amt", "l_rate", None, "l_at"] and not G.warnings(panel),
   f"p5-panel: every lane left to right, no warning ({G.warnings(panel)})")
ok(not refusals(beta1) and G.warnings(beta1) == ["LFO screen 1: lane 1: its slots are not left to right "
                                                 "(KNOB 1..4 should follow the screen)"],
   f"0.22-beta1's LFO lane: built, with the order warning ({G.warnings(beta1)})")

# -------------------------------------------------------------------- the same rules as validateDesign (JS) ---
node = shutil.which("node")
if not node:
    print("gen_layout_test: skip: no node (the comparison with web/fm1layout.js validateDesign)")
else:
    with tempfile.TemporaryDirectory() as tmp:
        (Path(tmp) / "designs.json").write_text(json.dumps(designs))
        js = (f'import * as L from {json.dumps((ROOT / "web/fm1layout.js").as_uri())};\n'
              'import fs from "fs";\n'
              f'import {{ SKETCH_NAMES }} from {json.dumps((ROOT / "web/fm1sketch.js").as_uri())};\n'
              f'const cat = JSON.parse(fs.readFileSync({json.dumps(str(ROOT / "design/layouts/p5.json"))}, "utf8"));\n'
              f'const ds = JSON.parse(fs.readFileSync({json.dumps(str(Path(tmp) / "designs.json"))}, "utf8"));\n'
              'const out = {};\n'
              'const pidx = L.paramIndex(L.paramList(cat));\n'
              'const order = (d) => d.groups.flatMap((g) => g.screens.flatMap((s, si) => s.type === "free" ? '
              'L.checkScreen(s, pidx).filter((c) => c.kind === "order").map((c) => `${g.name} screen ${si + 1}: ${c.text}`) : []));\n'
              'for (const [k, d] of Object.entries(ds)) out[k] = [L.validateDesign(d, cat).errors, L.validateDesign(d, cat, { build: true }).errors,\n'
              '  L.validateDesign(d, cat).errors.length ? null : order(d)];\n'
              'out["~sketch names"] = SKETCH_NAMES;\n'
              'out["~rows bytes"] = Array.from(L.packDesign(ds["p5-rows"], cat).bytes);\n'
              'out["~panel free"] = Array.from(L.packDesign(ds["p5-panel"], cat).free || []);\n'
              'out["~mixed bytes"] = Array.from(L.packDesign(ds["own: classic FILT, free elsewhere"], cat).bytes);\n'
              'console.log(JSON.stringify(out));\n')
        (Path(tmp) / "v.mjs").write_text(js)
        r = subprocess.run([node, str(Path(tmp) / "v.mjs")], capture_output=True, text=True)
    ok(r.returncode == 0, f"node ran validateDesign ({r.stderr.strip()[:200]})")
    jsv = json.loads(r.stdout) if r.returncode == 0 else {}
    for built in (False, True):
        differ = [k for k, d in designs.items() if k in jsv and G.validate(copy.deepcopy(d), cat, built=built) != jsv[k][built]]
        ok(not differ, f"the generator's checks {'with' if built else 'without'} the build's = validateDesign's"
           f"{' {build: true}' if built else ''} errors, word for word, on all {len(designs)} designs" +
           "".join(f"\n    {k}: py {G.validate(copy.deepcopy(designs[k]), cat, built=built)} js {jsv[k][built]}" for k in differ[:3]))
    wdiff = [k for k, d in designs.items() if k in jsv and jsv[k][2] is not None and G.warnings(copy.deepcopy(d)) != jsv[k][2]]
    nw = sum(1 for k in designs if k in jsv and jsv[k][2])
    ok(not wdiff and nw >= 2, f"the generator's warnings = checkScreen's \"order\" findings, word for word, on every valid design "
       f"({nw} with one)" + "".join(f"\n    {k}: py {G.warnings(designs[k])} js {jsv[k][2]}" for k in wdiff[:3]))
    ok(not jsv.get("p5-panel", [["?"]])[0] and not jsv.get("p5-panel", [0, ["?"]])[1] and all(jsv.get("broken: " + k, [0])[0] for k in BROKEN),
       "validateDesign: p5-panel valid (and buildable), each broken design refused")
    ok(jsv.get("~sketch names") == G.SKETCH_NAMES, f"the sketch glyphs' names: tools/gen_layout.py's = web/fm1sketch.js's ({len(G.SKETCH_NAMES)})")
    ok(not jsv.get("p5-rows", [["?"]])[0] and not jsv.get("p5-rows", [0, ["?"]])[1] and not G.validate(copy.deepcopy(rows), cat),
       "p5-rows (the owner's rows): valid and buildable, in both")
    rb = G.classic_bytes(cl, cat["engine"]["index"])
    ok(list(rb) == jsv.get("~rows bytes"), f"p5-rows: the generator's tables are packDesign's bytes ('L' 3, the plan), byte for byte "
       f"({len(rb)} B; node {len(jsv.get('~rows bytes') or [])} B)")
    cm = G.compile_design(copy.deepcopy(mixed), cat)
    mb = G.classic_bytes(cm["classic"], cat["engine"]["index"]) + G.free_bytes(cm)
    ok(list(mb) == jsv.get("~mixed bytes"), f"classic FILT and free screens elsewhere: the plan, the fenced built-in pages and the "
       f"free records with their offset are packDesign's bytes ({len(mb)} B; node {len(jsv.get('~mixed bytes') or [])} B)")
    fb = G.free_bytes(c)
    ok(list(fb) == jsv.get("~panel free"), f"p5-panel: the generator's free records are packDesign's free half, byte for byte "
       f"({len(fb)} B; node {len(jsv.get('~panel free') or [])} B)")

print(f"gen_layout_test: {checks} checks, {fails} failed")
sys.exit(1 if fails else 0)
