#!/usr/bin/env python3
"""One table per Digitone envelope (value -> seconds), run 1 and run 2 merged, plus fitted curves.

  python3 tools/digitone/summary.py [PATH ...]

PATH: an analysis.json written by dt_analyse.py, or a directory holding one (default:
dt_measure's --out). Run dt_analyse.py DIR first; this script only reads. Several paths
are merged; a step seen twice (same capture, parameter, value, tempo) counts once. The
run is the capture name's rN_ prefix (r2_amp_atk: run 2), none = run 1.

Per envelope the quantity is
  attack            the full linear rise, (t90 - t10) / 0.8
  decay / release   T60 (the dB slope, see dt_analyse)
  filt_*, op_*      t90 of the spectral-centroid sweep (a proxy: compare like with like)
  delay             t50 of the centroid step
  fade              t90 of the tremolo depth; also the full (linear) fade
"e" marks a value extrapolated from an unfinished step (dt_analyse's T_from says how).
Merged: the mean of the runs, or the measured one when the other is extrapolated.
Fits over the merged points (value > 0, fade: |display|): T = a v^p, T = a 10^(k v), and
log10 T = a cubic (or quadratic) in v; the best by rms log10 residual gives the fit column.
"""
import json
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dt_analyse  # noqa: E402

ORDER = ["amp_atk", "amp_dec", "amp_rel", "f_atk", "f_dec", "f_rel", "a_atk", "a_dec", "b_atk", "b_dec",
         "a_del", "lfo1_fade"]
QUANTITY = {"attack": "full rise", "decay": "T60", "release": "T60", "filt_attack": "t90 centroid",
            "filt_decay": "t90 centroid", "filt_release": "t90 centroid", "op_attack": "t90 centroid",
            "op_decay": "t90 centroid", "delay": "t50 centroid", "fade": "t90 depth"}


def run_of(capture):
    m = re.match(r"r(\d+)_", capture)
    return int(m.group(1)) if m else 1


def load(paths):
    steps, seen = [], set()
    for p in paths:
        j = os.path.join(p, "analysis.json") if os.path.isdir(p) else p
        if not os.path.exists(j):
            raise SystemExit(f"{j}: not found (run tools/digitone/dt_analyse.py {p} first)")
        for r in json.load(open(j))["steps"]:
            key = (r["capture"], r["param"], r["value"], r.get("tempo"))
            if key not in seen:
                seen.add(key)
                steps.append(r)
    return steps


def seconds(r):
    """(seconds, estimated?) of one step's main quantity"""
    v = r.get("rise_full") if r["kind"] == "attack" else r.get("T")
    how = r.get("T_from") or ""
    est = any(w in how for w in ("ramp fit", "extrapolated", "t50 x", "against the fastest"))
    return v, est


def logpoly(v, y, deg):
    c = np.polyfit(v, y, deg)
    return c, float(np.sqrt(np.mean((np.polyval(c, v) - y) ** 2)))


def fits(pts):
    """{'power'|'exp'|'logpoly': (rms log10, formula text, predict(v))}"""
    pts = [(v, T) for v, T in pts if T and T > 0 and v > 0]
    out = {}
    f = dt_analyse.fit(pts)
    if f:
        p, x = f["power"], f["exp"]
        out["power"] = (p["rms_log10"], f"T = {p['a']:.5g} * v**{p['p']:.4f}",
                        lambda v, a=p["a"], q=p["p"]: a * v ** q)
        out["exp"] = (x["rms_log10"], f"T = {x['a']:.5g} * 10**({x['k']:.6f} * v)",
                      lambda v, a=x["a"], k=x["k"]: a * 10 ** (k * v))
    if len(pts) >= 4:
        deg = 3 if len(pts) >= 6 else 2
        v = np.array([q[0] for q in pts], float)
        y = np.log10([q[1] for q in pts])
        c, res = logpoly(v, y, deg)
        terms = " + ".join(f"{a:.6g}*v^{deg - i}" if deg - i else f"{a:.6g}" for i, a in enumerate(c))
        out["logpoly"] = (res, f"log10 T = {terms}", lambda vv, c=c: 10 ** np.polyval(c, vv))
    return out


def _s(x, est=False):
    if x is None:
        return "      -  "
    return (f"{x:9.4f}" if x < 10 else f"{x:9.3f}") + ("e" if est else " ")


def summarise(steps):
    groups = {}
    for r in steps:
        key = r["param"] + (f"@{r['tempo']}" if "tempo" in r else "")
        groups.setdefault(key, []).append(r)
    keys = sorted(groups, key=lambda k: (ORDER.index(k.split("@")[0]) if k.split("@")[0] in ORDER else 99,
                                         -int(k.split("@")[1]) if "@" in k else 0))
    out = {}
    for key in keys:
        rs = groups[key]
        kind = rs[0]["kind"]
        fade = kind == "fade"
        rows = {}
        for r in rs:
            v = r["display"] if fade and "display" in r else r["value"]
            sec, est = seconds(r)
            row = rows.setdefault(v, {"runs": {}, "notes": [], "full": {}})
            row["runs"][run_of(r["capture"])] = (sec, est)
            if fade and r.get("t_full") is not None:
                row["full"][run_of(r["capture"])] = r["t_full"]
            if r.get("note"):
                row["notes"].append(r["note"].split(":")[0])
        runs = sorted({n for row in rows.values() for n in row["runs"]})
        pts = []
        for v, row in rows.items():
            got = [(s, e) for s, e in row["runs"].values() if s is not None]
            meas = [s for s, e in got if not e]
            use = meas or [s for s, _ in got]
            row["merged"] = (float(np.mean(use)) if use else None, not meas and bool(got))
            if row["merged"][0] is not None:
                pts.append((abs(v), row["merged"][0]))
        fs = fits(pts)
        best = min(fs, key=lambda n: fs[n][0]) if fs else None
        unit = QUANTITY.get(kind, "T") + " (s)" + (", proxy" if "centroid" in QUANTITY.get(kind, "") else "")
        print(f"\n== {key}  [{kind}: {unit}]")
        hdr = f"  {'value':>6s}" + "".join(f"  {'run ' + str(n):>10s}" for n in runs) + f"  {'merged':>10s}"
        if fade:
            hdr += f"  {'full fade':>10s}"
        if best:
            hdr += f"  {best + ' fit':>10s}  {'err %':>6s}"
        print(hdr)
        for v in sorted(rows, key=lambda v: (abs(v), v)):
            row = rows[v]
            line = f"  {v:6d}" + "".join("  " + _s(*row["runs"].get(n, (None, False))) for n in runs)
            line += "  " + _s(*row["merged"])
            if fade:
                fl = [x for x in row["full"].values() if x is not None]
                line += "  " + _s(float(np.mean(fl)) if fl else None)
            if best:
                m = row["merged"][0]
                if m and abs(v) > 0:
                    pv = float(fs[best][2](abs(v)))
                    line += "  " + _s(pv) + f"  {100 * (pv / m - 1):+6.1f}"
            if row["notes"]:
                line += "  (" + "; ".join(sorted(set(row["notes"]))) + ")"
            print(line)
        for n, (res, txt, _) in sorted(fs.items(), key=lambda kv: kv[1][0]):
            print(f"  fit {n:8s} rms {res:.3f} log10 (x{10 ** res:.3f}):  {txt}")
        if not fs:
            print("  fit: not enough points")
        out[key] = {"kind": kind, "rows": {str(v): row["merged"][0] for v, row in rows.items()},
                    "fits": {n: {"rms_log10": res, "formula": txt} for n, (res, txt, _) in fs.items()},
                    "best": best}
    return out


def main():
    paths = sys.argv[1:]
    if not paths:
        from dt_measure import DEFAULT_OUT
        paths = [DEFAULT_OUT]
    summarise(load(paths))


if __name__ == "__main__":
    main()
