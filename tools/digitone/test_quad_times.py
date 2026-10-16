#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
"""FM TONE's times against the Digitone's (docs/QUAD.md "Times"): the firmware's eng_quad.c rendered on the host
(tools/digitone/quad_render.c) with the measurement's patches and read by dt_analyse.py, as the Digitone was.

  python3 tools/digitone/test_quad_times.py           (tests/run_cr_tests.sh runs it; needs numpy and build/gen)

Checks: firmware/src/quad_times.h is what make_quad_times.py makes of digitone_times.json and quad_calib.json (not
stale); the amp envelope's times (the amplitude, exact): ATK 64 / 112 / 126 full rise, DEC 32 / 96 and REL 64 T60
within 2 % of digitone_times.json (ATK 112 and 126: the multiples of 0.4962 s); the proxy readings (the spectral
centroid) within the calibration's residuals: filter ATK 32 t90 within 5 %, operator ATK 64 t90 within 5 %, operator
DEC 64 t50 and t90 within 15 % (their shapes differ: the engine time is the geometric mean of the two), filter DEC 64
t50 and t90 within a factor 2.5 each and their geometric mean within 25 %; A DELAY 64 = 2 beats (1.00 s at 120 BPM,
1.50 s at 80) within 1 %; LFO FADE -32 full fade within 5 %."""
import math
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
try:
    import numpy  # noqa: F401
except ImportError:
    print("test_quad_times: skip (python3 has no numpy)")
    sys.exit(0)
import make_quad_times as M  # noqa: E402

fails = 0


def ok(c, what):
    global fails
    print("%-100s %s" % (what, "ok" if c else "FAIL"))
    if not c:
        fails += 1


def pt(env, v, run=None):
    p = next(p for p in E[env]["points"] if p["v"] == v)
    return p["runs"][run] if run else p


def within(a, b, tol):
    return a is not None and b and abs(a / b - 1) <= tol


E = M.load_times()
calib = M.load_calib()
T = M.engine_times(E, calib)
# the header is the JSONs' (written to a temporary file and compared)
with open(M.OUT_H) as fh:
    have = fh.read()
out_h = M.OUT_H
with tempfile.NamedTemporaryFile("r", suffix=".h", delete=False) as fh:
    tmp = fh.name
try:
    M.OUT_H = tmp
    import contextlib
    import io
    with contextlib.redirect_stdout(io.StringIO()):
        M.write_header(T, calib)
    with open(tmp) as fh:
        ok(fh.read() == have, "quad_times.h is make_quad_times.py's of digitone_times.json + quad_calib.json")
finally:
    M.OUT_H = out_h
    os.unlink(tmp)
M.build_render()

# the amp: the full rise, T60 (exact through the amplitude)
for v in (64, 112, 126):
    r = M.measure_stage("AATK", [(v, None)], times={v: T["AATK"][v]})[0][2]
    ok(within(r["rise_full"], pt("amp_attack", v)["s"], 0.02),
       "AMP ATK %d: full rise %.4f s, the Digitone's %.4f s" % (v, r["rise_full"], pt("amp_attack", v)["s"]))
for tab, env, v in (("ADEC", "amp_decay", 32), ("ADEC", "amp_decay", 96), ("ADEC_REL", "amp_release", 64)):
    r = M.measure_stage(tab, [(v, None)], times={v: T["ADEC"][v]})[0][2]
    ok(within(r["T"], pt(env, v)["s"], 0.02), "AMP %s %d: T60 %.4f s, the Digitone's %.4f s" % (
        "REL" if tab.endswith("REL") else "DEC", v, r["T"], pt(env, v)["s"]))
# the proxies (run 2's patches and readings)
r = M.measure_stage("FATK", [(32, None)], times={32: T["FATK"][32]})[0][2]
d = pt("filter_attack", 32, "2")
ok(within(r["t90"], d["t90"], 0.05) and within(r["t50"], d["t50"], 0.25),
   "filter ATK 32: centroid t50 %.3f / t90 %.3f s, the Digitone's %.3f / %.3f s" % (r["t50"], r["t90"], d["t50"], d["t90"]))
for v in (16, 64, 126):           # the shape (linear in dB from -38 dB): t10, t50 and t90 together
    r = M.measure_stage("OATK", [(v, None)], times={v: T["OATK"][v]})[0][2]
    d = pt("op_a_attack", v, "2")
    ok(all(within(r[k], d[k], 0.06) for k in ("t10", "t50", "t90")),
       "operator ATK %d: centroid t10 %.3f / t50 %.3f / t90 %.3f s, the Digitone's %.3f / %.3f / %.3f s" % (
           v, r["t10"], r["t50"], r["t90"], d["t10"], d["t50"], d["t90"]))
r = M.measure_stage("ODEC", [(64, None)], times={64: T["ODEC"][64]})[0][2]
d = pt("op_a_decay", 64, "2")
ok(within(r["t50"], d["t50"], 0.15) and within(r["t90"], d["t90"], 0.15),
   "operator DEC 64: centroid t50 %.3f / t90 %.3f s, the Digitone's %.3f / %.3f s" % (r["t50"], r["t90"], d["t50"],
                                                                                     d["t90"]))
r = M.measure_stage("FDEC", [(64, None)], times={64: T["FDEC"][64]})[0][2]
d = pt("filter_decay", 64, "2")
g = math.sqrt(r["t50"] * r["t90"] / (d["t50"] * d["t90"]))
ok(within(r["t50"], d["t50"], 0.15) and within(r["t90"], d["t90"], 0.15) and abs(g - 1) <= 0.1,
   "filter DEC 64: centroid t50 %.3f / t90 %.3f s, the Digitone's %.3f / %.3f s (geometric mean x %.2f)" % (
       r["t50"], r["t90"], d["t50"], d["t90"], g))
# the delay: v / 32 beats
for bpm in (120, 80):
    st = (dict(M.harm(M.OPA, "1"), AATK=0, ADEC=127, AEND=127, ADLY=64), "delay", 64, 4.0, 0.3, (), bpm, {})
    x, ss = M.capture([st])
    r = M.analyse(x, ss)[0]
    ok(within(r["T"], 2 * 60.0 / bpm, 0.01), "A DELAY 64 at %d BPM: %.4f s, 2 beats %.4f s (the Digitone's %.4f s)" % (
        bpm, r["T"], 2 * 60.0 / bpm, pt("a_delay_%d" % bpm, 64)["s"]))
# the LFO fade: -32 against FADE 0's depth, as dt_analyse measured it
sts = [(dict(M.FADE, L0FADE=fd), "fade", 64 + fd, M.FADE_GATE[-fd], 0.3, (), 120, {"display": fd}) for fd in (0, -32)]
x, ss = M.capture(sts)
r = M.analyse(x, ss)[1]
ok(within(r.get("t_full"), pt("lfo1_fade_120", -32)["s"], 0.05),
   "LFO FADE -32: full fade %.3f s, the Digitone's %.3f s" % (r.get("t_full") or 0, pt("lfo1_fade_120", -32)["s"]))
print("test_quad_times: %s" % ("ALL PASSED" if not fails else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
