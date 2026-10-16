#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
"""FM TONE's envelope and LFO fade times from the Digitone's (docs/QUAD.md "Times").

  python3 tools/digitone/make_quad_times.py                  # digitone_times.json + quad_calib.json -> quad_times.h
  python3 tools/digitone/make_quad_times.py --extract DIR..  # dt_analyse.py's analysis.json (DIR) -> digitone_times.json
  python3 tools/digitone/make_quad_times.py --calibrate      # FM TONE through the proxies -> quad_calib.json, the header
  python3 tools/digitone/make_quad_times.py --verify         # the header's tables through the proxies: the residuals

digitone_times.json holds the Digitone's numbers (derived from the owner's recordings by dt_analyse.py; no audio):
per envelope, value -> seconds, the measure, the run(s) and whether it is extrapolated. quad_calib.json holds our own
measurement of FM TONE: per table the engine time that makes FM TONE's reading through the same analysis equal the
Digitone's at each measured value, and the residual. The header (firmware/src/quad_times.h) is built from both:
  QUAD_T_AATK  amp attack (a linear rise): the full rise. From 112 up the Digitone's times are multiples of one unit
               (0.4962 s: 112 = 3, 114 = 4, 116 = 6, 118 = 10, 120 = 18, 122 = 30, 124 = 42, 125 = 48, 126 = 54,
               127 = 60); between them the multiples are interpolated. Below 112: monotone cubic (PCHIP) in log time.
  QUAD_T_ADEC  amp decay and release (one exponential, dB-linear): T60. 127 is INF (the engine holds; the entry unused).
  QUAD_T_FATK  filter attack (linear), QUAD_T_FDEC filter decay = release (exponential), QUAD_T_OATK operator attack
  QUAD_T_ODEC  (operators A and B), calibrated through the spectral-centroid proxy the Digitone was measured with.
  QUAD_T_FADE  the LFO fade's full (linear) time by |FADE| (fade-in measured; fade-out assumed the same).
  QUAD_DLY_K   the DELAYs: v / 32 beats (A DELAY measured; B and the filter's ENV DELAY assumed the same).
Each table entry is a Q24 per control tick (CTL = 32 samples at 44.1 kHz = 0.7256 ms): an attack's step (2^24 x tick /
T), a decay's share of the way (2^24 x (1 - 10^(-3 tick / T60))), the fade's 32767 x 2^16 x tick / T.
--calibrate and --verify render with build/host/quad_render (tools/digitone/quad_render.c, built here when missing:
needs build/gen/felucca_tables.h) and analyse with dt_analyse.py's own functions at 48 kHz (the recordings' rate).
"""
import argparse
import json
import math
import os
import re
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
TIMES_JSON = os.path.join(HERE, "digitone_times.json")
CALIB_JSON = os.path.join(HERE, "quad_calib.json")
OUT_H = os.path.join(ROOT, "firmware", "src", "quad_times.h")
RENDER = os.path.join(ROOT, "build", "host", "quad_render")
FS, CTL, SR = 44100, 32, 48000
TICK = CTL / FS
Q24 = 1 << 24

# ------------------------------------------------------------------------------------------------ the measurements
# dt_analyse's parameter -> our envelope name; per kind the step's numbers kept
ENV_OF = {"amp_atk": "amp_attack", "amp_dec": "amp_decay", "amp_rel": "amp_release", "f_atk": "filter_attack",
          "f_dec": "filter_decay", "f_rel": "filter_release", "a_atk": "op_a_attack", "a_dec": "op_a_decay",
          "b_atk": "op_b_attack", "b_dec": "op_b_decay", "a_del": "a_delay", "lfo1_fade": "lfo1_fade"}
STATS = {"attack": ("t10", "t50", "t90", "t98", "level_at_half_t98", "rise_full"),
         "decay": ("t_20db", "t_40db", "t_60db", "T60_slope", "slope_rms_db", "slope_span_db"),
         "release": ("t_20db", "t_40db", "t_60db", "T60_slope", "slope_rms_db", "slope_span_db"),
         "proxy": ("t10", "t50", "t90", "first_oct", "last_oct", "span_oct", "reached_pct"),
         "fade": ("t90_db", "t90_lin", "t_full", "lfo_hz", "final_depth_db", "reached_pct", "resolution_s")}
MEASURE = {"attack": ("rise_full", "the full linear rise (t90 - t10) / 0.8 of the amplitude envelope"),
           "decay": ("T60", "the time to -60 dB from the dB slope (-1 .. -50 dB), else the -20/-40/-60 dB times"),
           "release": ("T60", "as the decay's, from the note-off"),
           "proxy": ("t90", "t90 of the spectral-centroid sweep (log Hz) from the note-on (release: the note-off); t10 / "
                            "t50 kept too; unfinished sweeps: against the fastest step's end, t90 = t50 x the finished "
                            "sweeps' median t90 / t50 (extrapolated)"),
           "delay": ("t50", "t50 of the centroid step: the delay"),
           "fade": ("t_full", "the full fade of the tremolo depth (a linear ramp): the line fitted to the swing, else "
                              "t90 / 0.9 of the dB depth")}
SETUP = {
    "amp": "ALGO 1, MIX -64 (C alone, a plain sine), A LEV 0, B LEV 0, filter OFF, C4 velocity 100; decay: SUS 0",
    "filter": "LP4, FREQ 20, RESO 40, ENV +63, ALGO 1, A LEV 96 (ATK 0, DEC 0, END 127: held), FDBK 70, MIX -64; decay "
              "SUS 0, attack and release SUS 127 (release: AMP REL INF); run 1 also HARM +1.00",
    "op_a": "ALGO 1, A LEV 110, MIX -64, filter OFF; attack: DEC 127 END 127, decay: ATK 0 END 0; run 1 also HARM +1.00",
    "op_b": "ALGO 6 (B2 > C), B LEV 110, MIX -64, filter OFF; attack: DEC 127 END 127, decay: ATK 0 END 0 (run 1)",
    "delay": "as op_a's attack, A ATK 0, A DELAY v, at 120 and 80 BPM (run 1)",
    "fade": "LFO 1 SIN TRG, SPEED +63.00, MULT F64 (15.75 Hz at any tempo), DEPTH +12.00 on VOLUME 64, FADE v (run 2)",
}


def kind_of(k):
    return "proxy" if k.startswith(("filt_", "op_")) else k


def main_quantity(r):
    """(seconds, extrapolated?) of one dt_analyse step: summary.py's quantity"""
    k = r["kind"]
    s = r.get("rise_full") if k == "attack" else r.get("t_full") if k == "fade" else r.get("T")
    how = r.get("T_from") or ""
    est = any(w in how for w in ("ramp fit", "extrapolated", "t50 x", "against the fastest"))
    return s, est


def extract(paths):
    import summary
    steps = summary.load(paths)
    env = {}
    for r in steps:
        name = ENV_OF.get(r["param"])
        if not name:
            continue
        if "tempo" in r:
            name += "_%d" % r["tempo"]
        k = kind_of(r["kind"])
        run = str(summary.run_of(r["capture"]))
        v = r["display"] if r["kind"] == "fade" else r["value"]
        e = env.setdefault(name, {"kind": r["kind"], "pts": {}})
        s, est = main_quantity(r)
        rec = {"s": None if s is None else round(s, 6), "extrapolated": est, "capture": r["capture"]}
        for key in STATS.get(k, ()):
            if r.get(key) is not None:
                rec[key] = round(r[key], 6)
        if r.get("T_from"):
            rec["how"] = r["T_from"]
        if r.get("note"):
            rec["note"] = r["note"]
        e["pts"].setdefault(v, {})[run] = rec
    out = {}
    order = ["amp_attack", "amp_decay", "amp_release", "filter_attack", "filter_decay", "filter_release",
             "op_a_attack", "op_a_decay", "op_b_attack", "op_b_decay", "a_delay_120", "a_delay_80", "lfo1_fade_120",
             "lfo1_fade_80"]
    for name in sorted(env, key=lambda n: order.index(n) if n in order else 99):
        e = env[name]
        k = kind_of(e["kind"])
        pts = []
        for v in sorted(e["pts"], key=lambda v: (abs(v), v)):
            runs = e["pts"][v]
            got = [(r["s"], r["extrapolated"]) for r in runs.values() if r["s"] is not None]
            meas = [s for s, x in got if not x]
            use = meas or [s for s, _ in got]
            p = {"v": v, "s": round(float(np.mean(use)), 6) if use else None,
                 "extrapolated": bool(got) and not meas, "runs": dict(sorted(runs.items()))}
            if not use:
                p["note"] = "not measurable (" + "; ".join(sorted({r.get("note", "no reading").split(":")[0]
                                                                 for r in runs.values()})) + ")"
            pts.append(p)
        meas = MEASURE["delay" if k == "delay" else k]
        setup = SETUP["amp" if name.startswith("amp") else "filter" if name.startswith("filter") else
                      "op_a" if name.startswith("op_a") else "op_b" if name.startswith("op_b") else
                      "delay" if name.startswith("a_delay") else "fade"]
        out[name] = {"measure": meas[0], "about": meas[1], "proxy": k in ("proxy", "delay"), "setup": setup,
                     "points": pts}
        if "tempo" in name or name.endswith(("_120", "_80")):
            out[name]["bpm"] = int(name.rsplit("_", 1)[1])
    if "amp_attack" in out:
        out["amp_attack"]["grid"] = {
            "from": 112, "multiples": {"112": 3, "114": 4, "116": 6, "118": 10, "120": 18, "122": 30, "124": 42,
                                       "125": 48, "126": 54, "127": 60},
            "about": "from 112 up every measured full rise is an integer multiple of one unit (fitted: unit_s); the "
                     "values between are interpolated on the multiples (113 = 3.5, 115 = 5, 117 = 8, 119 = 14, 121 = "
                     "24, 123 = 36)"}
        g = out["amp_attack"]["grid"]
        ms = [(g["multiples"][str(p["v"])], p["s"]) for p in out["amp_attack"]["points"]
              if str(p["v"]) in g["multiples"] and p["s"] and not p["extrapolated"]]
        g["unit_s"] = round(sum(m * s for m, s in ms) / sum(m * m for m, s in ms), 6)
        g["residual_pct_max"] = round(max(abs(s / (m * g["unit_s"]) - 1) for m, s in ms) * 100, 3)
    add_tracks(out, paths)
    doc = {"about": "The Elektron Digitone's envelope and LFO fade times, measured on the owner's unit (OS 1.44, "
                    "2026-10-14, recorded losslessly over USB at 48 kHz: tools/digitone/dt_measure.py runs 1 and 2, "
                    "analysed by dt_analyse.py). Derived numbers only (no audio). Per envelope: the measure, the setup "
                    "(Digitone values), per value the seconds (s: run 1 and 2 merged as summary.py merges: the mean "
                    "of the measured runs, else of the extrapolated), extrapolated, and each run's own numbers. Run 1 "
                    "sent HARM coarse 64 (+1.00), not 0.00: its filter and operator A captures differ from run 2's. "
                    "The proxy envelopes (filter, operators) are readings of the spectral centroid, not envelope "
                    "times: FM TONE is calibrated through the same reading (make_quad_times.py --calibrate).",
           "source": "tools/digitone/make_quad_times.py --extract (dt_analyse.py analysis.json)",
           "envelopes": out}
    txt = json.dumps(doc, indent=1)                      # (a list of numbers on one line)
    txt = re.sub(r"\[\s*(-?[\d.]+(?:,\s*-?[\d.]+)*)\s*\]", lambda m: "[" + ",".join(
        x.strip() for x in m.group(1).split(",")) + "]", txt)
    with open(TIMES_JSON, "w") as fh:
        fh.write(txt + "\n")
    print("make_quad_times: wrote", os.path.relpath(TIMES_JSON, ROOT))


def add_tracks(out, paths):
    """the proxy captures' centroid tracks (dt_analyse.proxy_track, log2 Hz from the note-on / -off: derived numbers,
    no audio) into each run's point as {"t", "y"} (every 2nd frame of the first 0.1 s, then about 100 more), an attack's
    reference (the fastest step's end) as the envelope's track_ref; needs the captures in the directories"""
    import glob
    import summary
    import dt_analyse as A
    for d in paths:
        for jp in sorted(glob.glob(os.path.join(d, "*.json"))):
            if os.path.basename(jp) == "analysis.json":
                continue
            meta = json.load(open(jp))
            steps = [s for s in meta.get("steps", []) if s["kind"] in ("filt_attack", "filt_decay", "filt_release",
                                                                        "op_attack", "op_decay")]
            if not steps or not os.path.exists(os.path.join(d, meta["wav"])):
                continue
            x, sr = A.load_wav(os.path.join(d, meta["wav"]))
            t, e = A.rms_env(x, sr)
            f, _ = A.host_map(meta, t, A.db(e))
            ct, cc, cl = A.centroid_track(x, sr)
            run = str(summary.run_of(meta["capture"]))
            for s in steps:
                name = ENV_OF[s["param"]]
                ts, ys = A.proxy_track(s, f, ct, cc, cl)
                if not len(ts) or name not in out:
                    continue
                if s["kind"] in ("filt_attack", "op_attack") and s["value"] == 0:
                    out[name].setdefault("track_ref", {})[run] = round(float(np.median(ys[-max(3, len(ys) // 20):])), 4)
                    continue
                p = next((p for p in out[name]["points"] if p["v"] == s["value"]), None)
                if not p or run not in p["runs"]:
                    continue
                n0 = int(np.searchsorted(ts, 0.1))
                idx = list(range(0, n0, 2)) + list(range(n0, len(ts), max(1, (len(ts) - n0) // 100)))
                p["runs"][run]["track"] = {"t": [round(float(ts[i]), 4) for i in idx],
                                           "y": [round(float(ys[i]), 3) for i in idx]}


# ------------------------------------------------------------------------------------------ curves and the header
def pchip(xs, ys, x):
    """the monotone cubic (Fritsch-Carlson) through (xs, ys) at x (inside [xs[0], xs[-1]])"""
    xs, ys = np.asarray(xs, float), np.asarray(ys, float)
    n = len(xs)
    if n == 1:
        return float(ys[0])
    h, d = np.diff(xs), np.diff(ys) / np.diff(xs)
    m = np.zeros(n)
    m[0], m[-1] = d[0], d[-1]
    for k in range(1, n - 1):
        if d[k - 1] * d[k] > 0:
            w1, w2 = 2 * h[k] + h[k - 1], h[k] + 2 * h[k - 1]
            m[k] = (w1 + w2) / (w1 / d[k - 1] + w2 / d[k])
    i = int(np.clip(np.searchsorted(xs, x) - 1, 0, n - 2))
    t = (x - xs[i]) / h[i]
    h00, h10, h01, h11 = 2 * t ** 3 - 3 * t ** 2 + 1, t ** 3 - 2 * t ** 2 + t, -2 * t ** 3 + 3 * t ** 2, t ** 3 - t ** 2
    return float(h00 * ys[i] + h10 * h[i] * m[i] + h01 * ys[i + 1] + h11 * h[i] * m[i + 1])


def curve(anchors, lo=0, hi=127, extend=None):
    """{v: seconds} for v = lo..hi: PCHIP in log time through the anchors; beyond the last anchor extend(v) (else the
    last segment's slope in log time)"""
    vs = sorted(anchors)
    ys = [math.log(anchors[v]) for v in vs]
    out = {}
    for v in range(lo, hi + 1):
        if vs[0] <= v <= vs[-1]:
            out[v] = math.exp(pchip(vs, ys, v))
        elif v > vs[-1]:
            out[v] = extend(v) if extend else math.exp(ys[-1] + (ys[-1] - ys[-2]) / (vs[-1] - vs[-2]) * (v - vs[-1]))
        else:
            out[v] = math.exp(ys[0])
    return out


def k_atk(T):
    """an attack's step a tick (Q24) for a full rise of T s (0 or <= a tick: one tick)"""
    return Q24 if T <= TICK else max(1, min(Q24, int(round(Q24 * TICK / T))))


def k_dec(T):
    """a decay's share of the way a tick (Q24) for a T60 of T s (0: at once)"""
    return Q24 if T <= 0 else max(1, min(Q24, int(round(Q24 * (1 - 10 ** (-3 * TICK / T))))))


OP_FLOOR_DB = -38.0      # the operator attack: linear in dB from here to the top (the Digitone's tracks: -37..-39 best)
OP_FLOOR = int(round(Q24 * 10 ** (OP_FLOOR_DB / 20)))


def k_oatk(T):
    """an operator attack's growth a tick (Q24) for a rise from OP_FLOOR to the top in T s (linear in dB)"""
    return max(1, min(0xFFFFFFFF, int(round(Q24 * ((Q24 / OP_FLOOR) ** (TICK / max(T, TICK)) - 1)))))


def t_oatk(k):
    return TICK * math.log(Q24 / OP_FLOOR) / math.log(1 + k / Q24)


def k_fade(T):
    """the fade's 32767 x 2^16 per tick for a full fade of T s"""
    return 32767 * 65536 if T <= TICK else max(1, int(round(32767 * 65536 * TICK / T)))


def t_atk(k):
    return Q24 * TICK / k


def t_dec(k):
    return math.inf if k <= 0 else -3 * TICK / math.log10(1 - k / Q24) if k < Q24 else 0.0


def load_times():
    with open(TIMES_JSON) as fh:
        return json.load(fh)["envelopes"]


def load_calib():
    if not os.path.exists(CALIB_JSON):
        return None
    with open(CALIB_JSON) as fh:
        return json.load(fh)


def run_pts(env, run=None, stat=None, est_ok=False):
    """{v: seconds} of an envelope: run's numbers (stat: that statistic, else its main quantity, extrapolated ones only
    with est_ok) or the merged s; the not measurable left out"""
    out = {}
    for p in env["points"]:
        if run is None:
            if p["s"] is not None:
                out[p["v"]] = p["s"]
            continue
        r = p["runs"].get(run)
        if not r or r.get("note", "").startswith("no sweep"):     # (value 0's crossings are noise)
            continue
        s = r.get(stat) if stat else (None if r["extrapolated"] and not est_ok else r["s"])
        if s is not None:
            out[p["v"]] = s
    return out


def merged_dec(E):
    """the amp decay and release T60s, one curve: the mean (in log) where both were measured"""
    d, r = run_pts(E["amp_decay"]), run_pts(E["amp_release"])
    out = {}
    for v in sorted(set(d) | set(r)):
        got = [x for x in (d.get(v), r.get(v)) if x]
        out[v] = math.exp(sum(math.log(x) for x in got) / len(got))
    return out


# the tables' Digitone targets: the statistics (several: the engine time is the geometric mean of what each asks for),
# the envelope, the run (None: runs 1 and 2 merged). Attacks: t90, the sweep's end (near where the envelope reaches the
# top: the attack's time); the filter attack's of run 2 measured at 16 and 32, at 48..112 dt_analyse's estimate (its
# t50 x the finished sweeps' t90 / t50; "s": the main quantity, estimates included). Decays: t50 and t90 together: FM
# TONE's exponential and the Digitone's decay differ in shape (matched on one, the other is off by up to ~3x for the
# filter; on both, each is off by the same factor, in turn). Run 2: run 1 sent HARM +1.00, and FM TONE's HARM +1.00 does
# not sound as the Digitone's did (its filter capture opened at 1.1 kHz, ours at 3.2 kHz): run 1 is a cross-check only
TARGETS = {"AATK": (("rise_full",), "amp_attack", None), "ADEC": (("T",), "amp_decay+amp_release", None),
           "FATK": (("s",), "filter_attack", "2"), "FDEC": (("t50", "t90"), "filter_decay", "2"),
           "OATK": (("t90",), "op_a_attack", "2"), "ODEC": (("t50", "t90"), "op_a_decay", "2")}
TABS = ("AATK", "ADEC", "FATK", "FDEC", "OATK", "ODEC")
ATTACKS = ("AATK", "FATK", "OATK")


def digitone_targets(E, tab):
    """{statistic: {v: seconds}} the table is calibrated to"""
    stats, src, run = TARGETS[tab]
    if tab == "AATK":
        return {"rise_full": {v: s for v, s in run_pts(E["amp_attack"]).items() if v < 112}}
    if tab == "ADEC":
        return {"T": merged_dec(E)}
    return {st: run_pts(E[src], run, None if st == "s" else st, est_ok=st == "s") for st in stats}


def engine_times(E, calib):
    """{table: {v: engine seconds}} for every v 0..127 (FADE: |FADE| 0..64) from the measurements and the calibration"""
    cal = (calib or {}).get("tables", {})

    def anchors(tab):
        if tab in cal:                                  # (an instant 0 as half a tick: log time)
            return {int(p["v"]): max(p["engine_s"], TICK / 2) for p in cal[tab]["points"]}
        return digitone_targets(E, tab)[TARGETS[tab][0][0]]   # (before a calibration: the readings as the times)
    T = {}
    # amp attack: the calibrated points below 112, the grid from 112
    g = E["amp_attack"]["grid"]
    mult = {int(k): v for k, v in g["multiples"].items()}
    gv = sorted(mult)
    grid = {v: g["unit_s"] * float(np.interp(v, gv, [mult[x] for x in gv])) for v in range(112, 128)}
    a = anchors("AATK")
    a = {v: s for v, s in a.items() if v < 112}
    a[112] = grid[112]
    T["AATK"] = curve(a, 0, 111)
    T["AATK"].update(grid)
    # amp decay / release: the measured points; 127 (INF) the last segment's slope
    T["ADEC"] = curve(anchors("ADEC"))
    # filter attack: 0 instant; above the last measured value the amp attack's curve, scaled (the Digitone's filter
    # attack follows it: docs/QUAD.md "Times"); filter decay: 127 the last slope
    fa = anchors("FATK")
    fa[0] = TICK
    top = max(fa)
    T["FATK"] = curve(fa, extend=lambda v, top=top, s=fa[top]: s * T["AATK"][v] / T["AATK"][top])
    T["FDEC"] = curve(anchors("FDEC"))
    oa = anchors("OATK")
    oa[0] = TICK
    T["OATK"] = curve(oa)
    T["ODEC"] = curve(anchors("ODEC"))
    T["FADE"] = fade_times(E)
    return T


def fade_times(E):
    """|FADE| -> the full fade (s): the measured (120 and 80 BPM merged: the fade does not follow the tempo) from 16 up,
    the extrapolated 56 and 64; below 16 (at the measurement's floor: 0, 4 and 8 read as FADE 0 does) the law the
    measured ones follow, 2^(|FADE| / 4) x c (x4 per 8 steps), c fitted to 24..48"""
    pts = {}
    for name in ("lfo1_fade_120", "lfo1_fade_80"):
        for p in E.get(name, {}).get("points", []):
            if p["s"] and abs(p["v"]) >= 16:
                pts.setdefault(abs(p["v"]), []).append(p["s"])
    m = {d: math.exp(np.mean(np.log(s))) for d, s in pts.items()}
    fit = [d for d in m if 24 <= d <= 48]
    c = math.exp(np.mean([math.log(m[d]) - d / 4 * math.log(2) for d in fit]))
    a = {1: c * 2 ** 0.25, 4: c * 2, 8: c * 4}
    a.update(m)
    out = curve(a, 1, 64)
    out[0] = 0.0
    return out


def write_header(T, calib):
    tabs = [("AATK", "amp attack: the step of the level a tick (a linear rise over the full time)", k_atk),
            ("ADEC", "amp decay and release (one curve, dB-linear): the share of the way a tick (T60); 127 is INF", k_dec),
            ("FATK", "filter attack (linear): the step a tick", k_atk),
            ("FDEC", "filter decay and release (exponential): the share a tick", k_dec),
            ("OATK", "operator A and B attack (linear in dB from QUAD_OP_FLOOR to the top): the growth a tick", k_oatk),
            ("ODEC", "operator A and B decay to END (exponential): the share a tick", k_dec)]
    L = ["/* generated by tools/digitone/make_quad_times.py from tools/digitone/digitone_times.json and quad_calib.json:",
         " * do not edit (python3 tools/digitone/make_quad_times.py) */",
         "/* FM TONE's times, the Digitone's (docs/QUAD.md \"Times\"): per envelope type a table of 128, Q24 per control",
         " * tick (CTL = 32 samples at 44.1 kHz, %.4f ms): an attack's step 2^24 x tick / T (T the full rise), a decay's" % (
             TICK * 1000),
         " * share of the way 2^24 x (1 - 10^(-3 tick / T60)); the fade 32767 x 2^16 x tick / T (T the full fade). In",
         " * flash (one look-up a tick and envelope). The seconds of each value: the comment after each row of 8 */",
         "#pragma once",
         "#include <stdint.h>",
         "#ifndef QUAD_TIMES_CONST",
         "#define QUAD_TIMES_CONST const      /* (tools/digitone/quad_render.c: writable, for the calibration) */",
         "#endif",
         "/* DELAY v (A, B, the filter's): v / 32 beats at the part's tempo (A's measured: 120 BPM 16 -> 0.25 s, 64 -> 1.0 s;",
         " * B's and the filter's assumed the same): 1/32 beats in Q24 a tick = BPM x QUAD_DLY_K */",
         "#define QUAD_DLY_K %du" % int(round(Q24 * 32 * CTL / (60 * FS))),
         "/* the operators' attack: linear in dB from this level (Q24: %.0f dB) to the top, x += x x QUAD_T_OATK >> 24 a tick */"
         % OP_FLOOR_DB,
         "#define QUAD_OP_FLOOR %d" % OP_FLOOR]
    for name, about, kf in tabs:
        vals = [kf(T[name][v]) for v in range(128)]
        L.append("/* %s */" % about)
        L.append("static QUAD_TIMES_CONST uint32_t QUAD_T_%s[128] = {" % name)
        for i in range(0, 128, 8):
            secs = " ".join(fmt_s((t_oatk if kf is k_oatk else t_atk if kf is k_atk else t_dec)(k)) for k in vals[i:i + 8])
            L.append("    " + ", ".join(str(x) for x in vals[i:i + 8]) + ",   /* %d: %s */" % (i, secs))
        L.append("};")
    fv = [0] + [k_fade(T["FADE"][d]) for d in range(1, 65)]
    L.append("/* the LFO FADE by |FADE| (0 none): 32767 x 2^16 / the ticks of the full fade (in and out) */")
    L.append("static QUAD_TIMES_CONST uint32_t QUAD_T_FADE[65] = {")
    for i in range(0, 65, 8):
        secs = " ".join(fmt_s(32767 * 65536 * TICK / k if k else 0) for k in fv[i:i + 8])
        L.append("    " + ", ".join(str(x) for x in fv[i:i + 8]) + ",   /* %d: %s */" % (i, secs))
    L.append("};")
    with open(OUT_H, "w") as fh:
        fh.write("\n".join(L) + "\n")
    print("make_quad_times: wrote", os.path.relpath(OUT_H, ROOT))


def fmt_s(s):
    if s == 0:
        return "0"
    if s < 1:
        return "%.3gms" % (s * 1000) if s < 0.1 else "%.0fms" % (s * 1000)
    return "%.3gs" % s if s < 100 else "%.0fs" % s


# ------------------------------------------------------------------------------------- rendering and the analysis
def build_render():
    src = os.path.join(HERE, "quad_render.c")
    deps = [src, OUT_H, os.path.join(ROOT, "firmware", "src", "eng_quad.c"), os.path.join(ROOT, "firmware", "src",
                                                                                          "quad_tables.h")]
    if os.path.exists(RENDER) and all(os.path.getmtime(RENDER) >= os.path.getmtime(d) for d in deps):
        return
    gen = os.path.join(ROOT, "build", "gen")
    if not os.path.exists(os.path.join(gen, "felucca_tables.h")):
        os.makedirs(gen, exist_ok=True)
        subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "gen_tables.py"),
                               os.path.join(gen, "felucca_tables.h")], stdout=subprocess.DEVNULL)
    os.makedirs(os.path.dirname(RENDER), exist_ok=True)
    subprocess.check_call(["cc", "-std=c99", "-O2", "-w", "-DFELUCCA_QUAD=1", "-I" + gen, "-o", RENDER, src, "-lm"])


def tick_t(s):
    """a time as the harness rounds it: the nearest control tick"""
    return int(s * FS / CTL + 0.5) * CTL / FS


def render(patch, on, off, total, tables=(), bpm=None):
    """one C4 note at on .. off (s), total s: float32 at 44.1 kHz -> float64 at 48 kHz (FFT resampled, the end
    tapered over 5 ms)"""
    with tempfile.NamedTemporaryFile(suffix=".f32", delete=False) as fh:
        fn = fh.name
    try:
        args = [RENDER, fn, "%.6f" % total, "%.6f" % on, "%.6f" % off]
        if bpm:
            args.append("bpm=%d" % bpm)
        args += ["%s=%d" % kv for kv in patch.items()]
        args += ["T:%s:%d=%d" % t for t in tables]
        subprocess.check_call(args)
        x = np.fromfile(fn, "<f4").astype(np.float64)
    finally:
        os.unlink(fn)
    nt = int(0.005 * FS)
    x[-nt:] *= np.linspace(1, 0, nt)
    x = np.concatenate([x, np.zeros(int(0.1 * FS))])
    n = len(x)
    m = int(round(n * SR / FS))
    X = np.fft.rfft(x)
    Y = np.zeros(m // 2 + 1, complex)
    k = min(len(X), len(Y))
    Y[:k] = X[:k]
    return np.fft.irfft(Y, m) * (m / n)


LEAD = 0.5


def capture(steps):
    """steps: [(patch, kind, value, gate, tail, tables, bpm, extra)] -> (x at 48 kHz, dt_analyse's step dicts: one
    render each, LEAD s of silence first (the lead-in is the noise floor), extra into the step dict)"""
    xs, ss, off = [], [], 0.0
    for patch, kind, value, gate, tail, tables, bpm, extra in steps:
        on = tick_t(LEAD)
        g = tick_t(LEAD + gate)
        x = render(patch, LEAD, LEAD + gate, LEAD + gate + tail, tables, bpm)
        ss.append(dict({"param": kind, "value": value, "kind": kind, "on": off + on, "off": off + g,
                        "end": off + g + tail}, **extra))
        xs.append(x)
        off += len(x) / SR
    return np.concatenate(xs), ss


def analyse(x, steps):
    """dt_analyse.analyse_dir's per-capture analysis, its host map the identity (our times are exact)"""
    import dt_analyse as A
    t, e = A.rms_env(x, SR)
    edb = A.db(e)
    noise = A.noise_floor(t, edb, steps[0]["on"])
    need_c = any(s["kind"] in A.PROXY for s in steps)
    ct, cc, cl = A.centroid_track(x, SR) if need_c else (None, None, None)

    def f(h):
        return h
    rs = [A.analyse_step(s, f, t, e, edb, ct, cc, x, SR, cl, noise) for s in steps]
    A.attack_capture(steps, rs, f, t, e)
    if need_c:
        A.proxy_capture(steps, rs, f, ct, cc, cl)
    A.fade_capture(steps, rs, f, t, e, edb)
    return rs


# the captures' patches (version 5 units; tests/quad_ref.py's names), as dt_measure.py set the Digitone
BASE = {"ALGO": 1, "MIX": -64, "FTYPE": 0, "RC": 3, "RA": 3, "HARM": 0, "DTUN": 0, "FDBK": 0, "ALEV": 0, "BLEV": 0,
        "AATK": 0, "ADEC": 0, "AEND": 0, "ADLY": 0, "BATK": 0, "BDEC": 0, "BEND": 0, "BDLY": 0, "FREQ": 12700,
        "RESO": 0, "FDEPTH": 0, "FATK": 0, "FDEC": 0, "FSUS": 127, "FREL": 0, "FDLY": 0, "BASE": 0, "WIDTH": 127,
        "EATK": 0, "EDEC": 0, "ESUS": 127, "EREL": 0, "LEVEL": 100}
# the filter captures' FREQ 20 behaves as FM TONE's FREQ 0.00 (about 30 Hz): with it both filter tracks, attack and decay,
# match the Digitone's whole (docs/QUAD.md "Times"; FREQ 20.00 here, 80 Hz, closes 1.4 octaves above the Digitone's)
FILT = dict(BASE, FTYPE=3, FREQ=0, RESO=4000, FDEPTH=6300, FDBK=70, ALEV=96, AATK=0, ADEC=0, AEND=127)
OPA = dict(BASE, ALGO=1, ALEV=110)
OPB = dict(BASE, ALGO=6, BLEV=110)
FADE = dict(BASE, LEVEL=64, L0DEPTH=1200, L0SPEED=6300, L0WAVE=1, L0TRIG=1, L0MULT=18, L0DEST=38)   # SIN TRG F64 VOL
FADE_GATE = {0: 1.5, 4: 1.5, 8: 1.5, 16: 1.5, 24: 1.5, 32: 2.5, 40: 12.0, 48: 30.0, 56: 20.0, 64: 20.0}   # dt_measure


def harm(p, run):
    return dict(p, HARM=100) if run == "1" else p        # run 1 sent HARM coarse 64: +1.00


def stage(tab, run="2"):
    """a stage's dt_analyse kind, its patch (value v), its gate and tail for a time T. The amp attack holds its top
    (DEC INF): the Digitone's SUS 127 is its top, FM TONE's 127 / 128"""
    if tab == "AATK":
        return "attack", lambda v: dict(BASE, EATK=v, EDEC=127), lambda T: (1.3 * T + 0.3, 0.2)
    if tab == "ADEC":
        return "decay", lambda v: dict(BASE, ESUS=0, EDEC=v), lambda T: (min(1.2 * T + 0.3, 10.0), 0.2)
    if tab == "ADEC_REL":
        return "release", lambda v: dict(BASE, EREL=v), lambda T: (1.0, min(1.2 * T + 0.3, 10.0))
    if tab == "FATK":
        return "filt_attack", lambda v: dict(harm(FILT, run), FATK=v), lambda T: (1.3 * T + 0.5, 0.2)
    if tab == "FDEC":
        return "filt_decay", lambda v: dict(harm(FILT, run), FSUS=0, FDEC=v), lambda T: (1.2 * T + 0.5, 0.2)
    if tab == "FDEC_REL":
        return "filt_release", lambda v: dict(harm(FILT, run), FREL=v, EREL=127), lambda T: (1.0, 1.2 * T + 0.5)
    if tab == "OATK":
        return "op_attack", lambda v: dict(harm(OPA, run), AATK=v, ADEC=127, AEND=127), lambda T: (1.3 * T + 0.5, 0.2)
    if tab == "ODEC":
        return "op_decay", lambda v: dict(harm(OPA, run), AATK=0, ADEC=v, AEND=0), lambda T: (1.2 * T + 0.5, 0.2)
    if tab == "OBATK":
        return "op_attack", lambda v: dict(OPB, BATK=v, BDEC=127, BEND=127), lambda T: (1.3 * T + 0.5, 0.2)
    if tab == "OBDEC":
        return "op_decay", lambda v: dict(OPB, BATK=0, BDEC=v, BEND=0), lambda T: (1.2 * T + 0.5, 0.2)
    raise KeyError(tab)


TABLE_OF = {"ADEC_REL": "ADEC", "FDEC_REL": "FDEC", "OBATK": "OATK", "OBDEC": "ODEC"}
KFN = {"AATK": k_atk, "ADEC": k_dec, "FATK": k_atk, "FDEC": k_dec, "OATK": k_oatk, "ODEC": k_dec}


def reading(r, stat):
    """a dt_analyse step's statistic ("s": its main quantity, as digitone_times.json's)"""
    return main_quantity(r)[0] if stat == "s" else r.get(stat)


def measure_stage(tab, items, run="2", times=None):
    """FM TONE's dt_analyse readings of stage tab: items [(value, T)]: T None plays value's own table entry (times
    {value: s} sizes the gate), else table entry 64 holds the candidate time T. The attacks are analysed with the
    instant step (value 0) in their capture, as dt_analyse measures them against the fastest step's end.
    Returns [(value, T, the step's dict)]"""
    kind, patch, gt = stage(tab, run)
    base = TABLE_OF.get(tab, tab)
    attack = kind in ("filt_attack", "op_attack")
    ref = (patch(0), kind, 0, 1.5, 0.2, (), None, {})
    out = []
    for n, (v, T) in enumerate(items):
        if T is None:
            gate, tail = gt(times[v])
            st = (patch(v), kind, v, gate, tail, (), None, {})
        else:
            gate, tail = gt(T)
            st = (patch(64), kind, 1 + n, gate, tail, ((base, 64, KFN[base](T)),), None, {})
        x, ss = capture(([ref] if attack else []) + [st])
        out.append((v, T, analyse(x, ss)[-1]))
    return out


T_LIN = 0.25           # from here up the readings scale with T (an envelope's shape times T; the analysis smears ~10 ms)


def reading_model(cpts):
    """a sweep [(T, R)] -> (c, T_of(R)): below T_LIN the readings (a running median of 3 in log, made monotone), from
    T_LIN up R = c T, c the geometric mean of R / T over the sweep's points there (their scatter averaged: the filter's
    chaotic FDBK 70 makes single readings jump by ~20 %). T_of: None below the fastest's reading (the floor)"""
    pts = sorted((T, R) for T, R in cpts if R)
    c = math.exp(float(np.mean([math.log(R / T) for T, R in pts if T >= T_LIN])))
    lo = [(T, R) for T, R in pts if T < T_LIN]
    lr = [math.log(R) for _, R in lo]
    xs = [math.log(T) for T, _ in lo] + [math.log(T_LIN)]
    sm = [float(np.median(lr[i - 1: i + 2])) if 0 < i < len(lr) - 1 else lr[i] for i in range(len(lr))]
    ys = list(np.maximum.accumulate(sm + [math.log(c * T_LIN)]))

    def T_of(R):
        y = math.log(R)
        if y >= ys[-1]:
            return R / c
        if y < ys[0]:
            return None
        i = max(k for k in range(len(ys)) if ys[k] <= y)
        if ys[i + 1] <= ys[i]:
            return math.exp(xs[i])
        return math.exp(xs[i] + (y - ys[i]) / (ys[i + 1] - ys[i]) * (xs[i + 1] - xs[i]))
    return c, T_of


TRACK_TABS = {"FATK": "filter_attack", "FDEC": "filter_decay", "OATK": "op_a_attack", "ODEC": "op_a_decay"}
RISING_K = ("filt_attack", "op_attack", "delay")


def track_norm(ys, kind, ref=None):
    """a centroid track as the share of its sweep (dt_analyse's first: the first 20 ms' median, a decay's maximum;
    last: the end's median, an attack's the fastest step's end)"""
    ys = np.asarray(ys, float)
    n20 = max(3, int(0.02 * SR / 48))
    first = float(np.median(ys[:n20])) if kind in RISING_K else float(ys[:n20].max())
    if kind == "filt_release":
        first = float(ys[0])
    last = ref if ref is not None else float(np.median(ys[-max(3, len(ys) // 20):]))
    return (ys - first) / (last - first)


def fm_track(tab, T):
    """FM TONE's centroid track of stage tab with the candidate time T (table entry 64): (t, share of the sweep)"""
    import dt_analyse as A
    kind, patch, gt = stage(tab)
    base = TABLE_OF.get(tab, tab)
    gate, tail = gt(T)
    st = [(patch(64), kind, 64, gate, tail, ((base, 64, KFN[base](T)),), None, {})]
    attack = kind in ("filt_attack", "op_attack")
    if attack:
        st = [(patch(0), kind, 0, 1.5, 0.2, (), None, {})] + st
    x, ss = capture(st)
    ct, cc, cl = A.centroid_track(x, SR)
    trs = [A.proxy_track(s, lambda h: h, ct, cc, cl) for s in ss]
    ref = float(np.median(trs[0][1][-max(3, len(trs[0][1]) // 20):])) if attack else None
    return trs[-1][0], track_norm(trs[-1][1], kind, ref)


def track_fit(tab, td, fd, T0, span=8.0, n=801):
    """the time whose FM TONE track (a render at T0, scaled in time: an envelope's shape times T) is nearest the
    Digitone's over its window: (T, the rms of the two shares of the sweep)"""
    ts0, f0 = fm_track(tab, T0)
    best = (T0, math.inf)
    for lT in np.linspace(math.log(T0) - span / 2, math.log(T0) + span / 2, n):
        T = math.exp(lT)
        r = float(np.sqrt(np.mean((np.interp(td * T0 / T, ts0, f0, right=f0[-1]) - fd) ** 2)))
        if r < best[1]:
            best = (T, r)
    return best


def calibrate_track(E, tab):
    """a proxy table from the whole tracks: per measured value (run 2, a sweep that got past half way) the time whose
    track fits the Digitone's best (fitted on a 4 s render, then again on a render at that time: the smear of the
    short ones); FM TONE's t10 / t50 / t90 there against the Digitone's"""
    env = E[TRACK_TABS[tab]]
    kind = stage(tab)[0]
    ref = env.get("track_ref", {}).get("2")
    pts = []
    for p in env["points"]:
        r = p["runs"].get("2")
        if not r or "track" not in r or (r.get("reached_pct") is not None and r["reached_pct"] < 50):
            continue
        td = np.array(r["track"]["t"])
        fd = track_norm(r["track"]["y"], kind, ref if kind in ("filt_attack", "op_attack") else None)
        T1, _ = track_fit(tab, td, fd, 4.0, span=13.0, n=1100)
        T, rms = track_fit(tab, td, fd, T1, span=2.0, n=401)
        pts.append({"v": p["v"], "engine_s": round(T, 6), "track_rms": round(rms, 4),
                    "digitone": {k: r[k] for k in ("t10", "t50", "t90") if r.get(k) is not None and (k != "t10" or r[k] >= 0.005) and
                                 not (k == "t90" and r["extrapolated"])}})
    got = measure_stage(tab, [(q["v"], q["engine_s"]) for q in pts])
    for q, (_, _, r) in zip(pts, got):
        q["fmtone"] = _st(r)
        q["residual_pct"] = {k: round(100 * (q["fmtone"][k] / q["digitone"][k] - 1), 1) for k in q["digitone"]
                             if q["fmtone"].get(k)}
    return pts


def calibrate(E):
    build_render()
    res = {"about": "Our own measurement of FM TONE (tools/digitone/make_quad_times.py --calibrate), the firmware's "
                    "code rendered by tools/digitone/quad_render.c with the captures' patches (version 5 units) and "
                    "read by dt_analyse.py's own functions at 48 kHz. Per table: the statistics matched (stats), the "
                    "Digitone's envelope and run, c = FM TONE's reading / engine time above 0.25 s (per statistic), and "
                    "per measured value the Digitone's readings, the engine time chosen (engine_s: an attack's full "
                    "rise, a decay's T60; the geometric mean of what each statistic asks for), FM TONE's readings at "
                    "it and the residuals (FM TONE / Digitone - 1, %). floor: shorter than FM TONE's fastest reads "
                    "(instant taken). sweep: the candidate times and their readings. verify: the generated header's "
                    "tables, every measured value (make_quad_times.py --verify).",
           "tick_s": TICK, "tables": {}}
    grid = [float(T) for T in np.geomspace(TICK, 12.0, 25)]
    for tab in TRACK_TABS:
        pts = calibrate_track(E, tab)
        res["tables"][tab] = {"method": "track", "envelope": TRACK_TABS[tab], "run": "2", "points": pts}
        print("calibrate %s (whole track): %s" % (tab, " ".join("%d:%s r%.3f" % (
            q["v"], "/".join("%+.0f" % x for x in q["residual_pct"].values()), q["track_rms"]) for q in pts)))
    for tab in ("AATK", "ADEC"):
        stats = TARGETS[tab][0]
        tg = digitone_targets(E, tab)
        # (AMP DEC 0 is not measurable, the Digitone's sounds nothing there: the shared entry 0 is the release's,
        # calibrated through a release capture)
        rel = {v for v in tg[stats[0]] if v not in run_pts(E["amp_decay"])} if tab == "ADEC" else set()
        sweeps = {tab: measure_stage(tab, [(i, T) for i, T in enumerate(grid)])}
        if rel:
            sweeps["ADEC_REL"] = measure_stage("ADEC_REL", [(i, T) for i, T in enumerate(grid)])
        models = {(sg, st): reading_model([(T, reading(r, st)) for _, T, r in sw]) for sg, sw in sweeps.items()
                  for st in stats}
        sweep = sweeps[tab]
        pts = []
        for v in sorted(set().union(*[set(tg[st]) for st in stats])):
            sg = "ADEC_REL" if v in rel else tab
            Ts = [models[sg, st][1](tg[st][v]) for st in stats if v in tg[st]]
            Ts = [T for T in Ts if T]
            T = math.exp(float(np.mean(np.log(Ts)))) if Ts else (TICK if tab in ATTACKS else 0.0)
            pts.append({"v": v, "engine_s": round(T, 6), "floor": not Ts,
                        "digitone": {st: round(tg[st][v], 6) for st in stats if v in tg[st]}})
            if v in rel:
                pts[-1]["capture"] = "release"
        for it in range(2 if tab in ("AATK", "ADEC") else 1):   # (the amp's readings are exact: one refinement
            got = []                                            # where the tick's grain shows)
            for sg in sweeps:
                got += [(sg, x) for x in measure_stage(sg, [(p["v"], p["engine_s"]) for p in pts
                                                            if (sg == "ADEC_REL") == (p["v"] in rel)])]
            got = [x for _, x in sorted(got, key=lambda g: g[1][0])]
            for p, (_, _, r) in zip(pts, got):
                p["fmtone"] = {st: None if reading(r, st) is None else round(reading(r, st), 6) for st in stats}
                p["residual_pct"] = {st: round(100 * (p["fmtone"][st] / p["digitone"][st] - 1), 1)
                                     for st in p["digitone"] if p["fmtone"].get(st)}
                if it == 0 and tab in ("AATK", "ADEC") and not p["floor"] and abs(p["residual_pct"][stats[0]]) > 1:
                    p["engine_s"] = round(p["engine_s"] * p["digitone"][stats[0]] / p["fmtone"][stats[0]], 6)
        res["tables"][tab] = {"stats": list(stats), "envelope": TARGETS[tab][1], "run": TARGETS[tab][2] or "merged",
                              "c": {st: round(models[tab, st][0], 5) for st in stats}, "points": pts,
                              "sweep": [[round(T, 6)] + [None if reading(r, st) is None else round(reading(r, st), 6)
                                                         for st in stats] for _, T, r in sweep]}
        print("calibrate %s (%s): %s" % (tab, "+".join(stats), " ".join(
            "%d:%s%s" % (p["v"], "/".join("%+.0f%%" % x for x in p["residual_pct"].values()), "f" if p["floor"] else "")
            for p in pts)))
    old = load_calib() or {}
    if "verify" in old:
        res["verify"] = old["verify"]
    with open(CALIB_JSON, "w") as fh:
        json.dump(res, fh, indent=1)
        fh.write("\n")
    print("make_quad_times: wrote", os.path.relpath(CALIB_JSON, ROOT))
    return res


# ------------------------------------------------------------------------------------------- the verification
def _st(r, keys=("t10", "t50", "t90")):
    return {k: round(r[k], 5) for k in keys if r.get(k) is not None}


def verify(E, T):
    """every measured value through the generated header's tables (rebuilt into the harness): FM TONE's readings
    against the Digitone's; written into quad_calib.json's "verify" and printed"""
    build_render()
    out = {}

    def row(name, v, dt, fm, extra=None):
        res = None if not (dt and fm) else round(100 * (fm / dt - 1), 1)
        out.setdefault(name, []).append(dict({"v": v, "digitone": dt, "fmtone": None if fm is None else round(fm, 5),
                                              "residual_pct": res}, **(extra or {})))
    checks = [("AATK", "amp_attack", None, "rise_full"), ("ADEC", "amp_decay", None, "s"),
              ("ADEC_REL", "amp_release", None, "s"), ("FATK", "filter_attack", "2", "s"),
              ("FDEC", "filter_decay", "2", "t90"), ("FDEC_REL", "filter_release", "1", "t90"),
              ("OATK", "op_a_attack", "2", "t90"), ("ODEC", "op_a_decay", "2", "t90"),
              ("OBATK", "op_b_attack", "1", "s"), ("OBDEC", "op_b_decay", "1", "t90")]
    for tab, env, run, stat in checks:
        base = TABLE_OF.get(tab, tab)
        dts = {p["v"]: p for p in E[env]["points"]}
        vals = [v for v in sorted(dts) if v <= 127 and (run is None or run in dts[v]["runs"])]
        got = measure_stage(tab, [(v, None) for v in vals], run or "2", {v: T[base][v] for v in vals})
        for v, _, r in got:
            p = dts[v]
            d = p["runs"][run] if run else p
            if d.get("note", "").startswith("no sweep") or p.get("note"):
                continue
            dt = d.get("rise_full" if stat == "rise_full" and not run else stat) if stat != "s" else d["s"]
            if stat == "rise_full":
                dt = p["s"]
            ex = {"stat": stat, "digitone_t": {k: d.get(k) for k in ("t10", "t50", "t90") if d.get(k) is not None},
                  "fmtone_t": _st(r)}
            if tab == "AATK":                             # the shape: the level at half the rise (0.5 = linear)
                rr = (p["runs"].get("2") or p["runs"].get("1"))
                ex = {"stat": stat, "level_at_half": [rr.get("level_at_half_t98"), r.get("level_at_half_t98")]}
            elif tab in ("ADEC", "ADEC_REL"):            # dB-linear: the slope fit's rms (dB)
                rr = (p["runs"].get("2") or p["runs"].get("1"))
                ex = {"stat": stat, "slope_rms_db": [rr.get("slope_rms_db"), r.get("slope_rms_db")]}
            row(tab + ("" if run in (None, "2") else " run " + run), v, dt, reading(r, stat), ex)
    for bpm in (120, 80):                                 # A DELAY: v / 32 beats
        env = E.get("a_delay_%d" % bpm)
        if not env:
            continue
        for p in env["points"]:
            st = (dict(harm(OPA, "1"), AATK=0, ADEC=127, AEND=127, ADLY=p["v"]), "delay", p["v"], 8.0, 0.3, (), bpm,
                  {})
            x, ss = capture([st])
            row("A DELAY @%d" % bpm, p["v"], p["s"], analyse(x, ss)[0].get("T"), {"stat": "t50"})
    for bpm in (120, 80):                                 # LFO FADE: in one capture with FADE 0 (the full depth)
        env = E.get("lfo1_fade_%d" % bpm)
        if not env:
            continue
        ds = [p["v"] for p in env["points"]]
        sts = [(dict(FADE, L0FADE=d), "fade", 64 + d, FADE_GATE[-d], 0.3, (), bpm, {"display": d}) for d in ds]
        x, ss = capture(sts)
        rs = analyse(x, ss)
        for p, r in zip(env["points"], rs):
            if p["v"]:
                row("FADE @%d" % bpm, p["v"], p["s"], r.get("t_full"),
                    {"stat": "t_full", "how": r.get("T_from"), "digitone_extrapolated": p["extrapolated"]})
    calib = load_calib() or {}
    calib["verify"] = out
    with open(CALIB_JSON, "w") as fh:
        json.dump(calib, fh, indent=1)
        fh.write("\n")
    for name, rows in out.items():
        print("verify %-14s %s" % (name, " ".join("%d:%s" % (r["v"], "-" if r["residual_pct"] is None else
                                                              "%+.0f%%" % r["residual_pct"]) for r in rows)))
    print("make_quad_times: wrote", os.path.relpath(CALIB_JSON, ROOT), "(verify)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--extract", nargs="+", metavar="DIR")
    ap.add_argument("--calibrate", action="store_true")
    ap.add_argument("--verify", action="store_true")
    a = ap.parse_args()
    if a.extract:
        extract(a.extract)
        return
    E = load_times()
    calib = load_calib()
    if a.calibrate:
        calib = calibrate(E)                                # (only table entry 64, overwritten: any header will do)
    T = engine_times(E, calib)
    write_header(T, calib)
    if a.verify:
        verify(E, T)


if __name__ == "__main__":
    main()
