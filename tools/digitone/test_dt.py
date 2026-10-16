#!/usr/bin/env python3
"""Hardware-free tests for tools/digitone: CC/NRPN bytes, --plan, and the analysis on
synthetic captures with known times (with simulated ffmpeg start latency and drift).

  python3 tools/digitone/test_dt.py [WORKDIR]
"""
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dt_cc  # noqa: E402
import dt_analyse  # noqa: E402

SR = 48000
F0 = 261.63
LAT = 0.37      # simulated ffmpeg start latency (audio starts later than host_ff_start)
MIDI_LAT = 0.006
DRIFT = 25e-6
fails = []


def check(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails.append(msg)


def test_cc():
    check(dt_cc.encode("amp_atk", 100) == [[0xB0, 104, 100]], "amp_atk -> B0 68 64")
    check(dt_cc.encode("amp_rel", 5, channel=2) == [[0xB1, 107, 5]], "channel 2 status B1")
    check(dt_cc.encode("f_freq", 20) == [[0xB0, 99, 1], [0xB0, 98, 20], [0xB0, 6, 20],
                                         [0xB0, 38, 0]], "f_freq hi-res as NRPN 1/20")
    check(dt_cc.encode("lfo1_dep", 64)[1] == [0xB0, 98, 55], "lfo1 depth NRPN LSB 55")
    check(dt_cc.encode("a_del", 16) == [[0xB0, 83, 16]], "A delay CC 83")
    check(dt_cc.note_on(60, 100) == [0x90, 60, 100], "note on bytes")
    check(dt_cc.all_notes_off()[0] == [0xB0, 123, 0], "all notes off")
    check(dt_cc.encode("harm", dt_cc.HARM_ZERO) == [[0xB0, 99, 1], [0xB0, 98, 76], [0xB0, 6, 63], [0xB0, 38, 0]],
          "HARM 0.00 -> NRPN 1/76 coarse 63, fine 0 (proved on the device 2026-10-14)")


def test_lists():
    """the list encoders against the owner's device test (2026-10-14): value sent -> what the Digitone showed"""
    proved = [("algo", 4, 90, 3), ("algo", 7, 90, 6), ("f_type", "HP", 74, 2), ("ratio_c", 8, 91, 10),
              ("ratio_a", 1.5, 92, 5), ("ratio_a", 4, 92, 15), ("lfo1_mult", 16, 108, 4),
              ("lfo1_wave", "SQR", 111, 2), ("lfo1_mode", "TRG", 113, 1), ("lfo1_dest", "NONE", 110, 0),
              ("lfo2_wave", "SQR", 117, 2)]
    for name, shown, cc, sent in proved:
        got = dt_cc.encode(name, shown)
        check(got == [[0xB0, cc, sent]], f"{name} {shown} -> CC {cc} = {sent} (got {got})")
    check(dt_cc.encode("ratio_c", 1, channel=3) == [[0xB2, 91, 3]], "ratio_c 1.00 -> 3 on channel 3")
    for name, v in (("ratio_c", 1.3), ("ratio_a", 4.4), ("algo", 9), ("f_type", 1), ("lfo1_dest", "VOLUME"),
                    ("lfo1_wave", "SQUARE"), ("ratio_b", 64)):
        try:
            dt_cc.encode(name, v)
            check(False, f"{name}={v!r} refused")
        except ValueError as e:
            check(True, f"{name}={v!r} refused ({e})")


def plan_out(*args):
    p = subprocess.run([sys.executable, os.path.join(HERE, "dt_measure.py"), "--plan", *args],
                       capture_output=True, text=True)
    last = [l for l in p.stdout.splitlines() if "captures," in l]
    check(p.returncode == 0 and last, f"--plan {' '.join(args)} runs: " + (last[0] if last else p.stderr[-200:]))
    mins = float(last[0].split("about ")[1].split(" min")[0]) if last else 99.0
    return p, (last[0] if last else ""), mins


def check_hand(p):
    hand = sorted({l.strip() for l in p.stdout.splitlines() if l.strip().startswith("by hand:")})
    check(hand and all(h == "by hand: nothing" or all(x.startswith(("LFO1 DEST = VOLUME", "project TEMPO"))
                                                      for x in h[9:].split("; ")) for h in hand),
          f"by hand only DEST VOLUME and the tempo: {hand}")


def test_plan():
    p, last, mins = plan_out()
    check(last.startswith("12 captures, 55 steps"), f"run 1's set unchanged: {last}")
    check(mins < 30, f"plan under 30 min ({mins} min)")
    hand = sorted({l.strip() for l in p.stdout.splitlines() if l.strip().startswith("by hand:")})
    check(hand and all(h == "by hand: nothing" or all(x.startswith(("LFO1 DEST = VOLUME", "project TEMPO"))
                                                      for x in h[9:].split("; ")) for h in hand),
          f"by hand only DEST VOLUME and the tempo: {hand}")
    check("algo=6 mix=0 f_type=OFF" in p.stdout and "f_type=LP4" in p.stdout and "lfo1_wave=SQR" in p.stdout,
          "--plan shows the list settings sent over MIDI")
    import dt_measure
    sets = [c["base"] for c in dt_measure.build_plan(1) + dt_measure.build_plan(2)] + [dt_measure.INIT_RESTORE]
    try:
        n = sum(len(dt_cc.encode(k, v)) for b in sets for k, v in b.items())
        check(True, f"every capture base (sets 1 and 2) and the init restore encode ({n} messages)")
    except (ValueError, KeyError) as e:
        check(False, f"a capture base or the init restore does not encode: {e!r}")
    check(all(b["harm"] == 63 for b in sets), "every base and the init restore send HARM 0.00 as 63")
    check(all(not c["name"].startswith("r2_") for c in dt_measure.build_plan(1)), "run 1's capture names unchanged")

    # run 2
    p, last, mins = plan_out("--set", "2")
    check(mins <= 15.0, f"run 2 at most 15 min of recording ({last})")
    check_hand(p)
    caps = dt_measure.build_plan(2)
    check(all(c["name"].startswith("r2_") for c in caps), "run 2's captures all named r2_*")
    vals = {c["name"]: [s["value"] for s in c["steps"]] for c in caps}
    check(vals["r2_amp_atk"] == sorted([100, 104, 108, 112, 114, 116, 118, 120, 122, 124, 125, 126,
                                        8, 16, 24, 48, 80]), "run 2 amp attack values")
    check(vals["r2_amp_dec"] == [4, 8, 16, 24, 48, 80, 104, 120] and vals["r2_amp_rel"] == [8, 24, 48, 80, 120],
          "run 2 amp decay / release values")
    atk = next(c for c in caps if c["name"] == "r2_amp_atk")["steps"]
    check(all(s["gate"] >= 1.29 * dt_measure._logi(dt_measure.R1_AMP_ATK, s["value"]) for s in atk),
          "run 2 attack gates >= 1.3 x run 1's interpolated full rise")
    dec = [s for c in caps if c["name"] in ("r2_amp_dec", "r2_amp_rel") for s in c["steps"]]
    check(max(max(s["gate"], s["tail"]) for s in dec) <= 8.0, "run 2 decay / release windows <= 8 s")
    fade = {c["name"]: c for c in caps if c["group"] == "fade"}
    disp = {n: [s["display"] for s in c["steps"]] for n, c in fade.items()}
    check(disp.get("r2_fade_120") == [0, -4, -8, -16, -24, -32, -40, -48, -56, -64]
          and disp.get("r2_fade_80") == [0, -16, -32, -48], f"run 2 fade values {disp}")
    check(all(s["value"] == 64 + s["display"] for c in fade.values() for s in c["steps"]),
          "fade CC = 64 + display")
    b = fade["r2_fade_120"]["base"]
    check(b["lfo1_wave"] == "SIN" and b["lfo1_mode"] == "TRG" and b["lfo1_mult"] == "F64" and b["lfo1_spd"] == 127
          and abs(dt_measure.lfo_hz(b["lfo1_spd"], b["lfo1_mult"]) - 15.75) < 1e-9
          and abs(dt_measure.lfo_hz(b["lfo1_spd"], b["lfo1_mult"], 80) - 15.75) < 1e-9
          and abs(dt_measure.lfo_hz(96, "1", 120) - 1 / 8.0) < 1e-9 and abs(dt_measure.lfo_hz(96, "1", 80) - 1 / 12.0) < 1e-9,
          "fade LFO: SIN TRG, SPD +63 x F64 = 15.75 Hz at any tempo; run 1's SPD +32 x 1 = 8 s / 12 s at 120 / 80 BPM")


class Cap:
    """Builds one synthetic capture: host timeline -> audio buffer."""

    def __init__(self, name, total, dt_rec=False):
        """dt_rec: written like build/dt_rec (32-bit float WAV, host_rec_start = host time of sample 0)
        instead of ffmpeg (24-bit PCM, launch time only)"""
        self.name = name
        self.h0 = 1000.0
        self.dt_rec = dt_rec
        self.x = np.random.default_rng(1).normal(0, 3e-5, int(total * SR))
        self.meta = {"capture": name, "group": name, "wav": name + ".wav",
                     "sample_rate": SR, "markers": [], "steps": []}
        if dt_rec:
            self.meta.update(recorder="dt_rec", host_rec_start=self.h0 + LAT,
                             rec_stats={"frames": len(self.x), "overruns": 0})
        else:
            self.meta["host_ff_start"] = self.h0
        self.h = self.h0 + 1.5

    def a(self, host):
        return (host - self.h0) * (1 + DRIFT) - LAT + MIDI_LAT

    def put(self, host, sig):
        i = int(round(self.a(host) * SR))
        n = min(len(sig), len(self.x) - i)
        self.x[i:i + n] += sig[:n]

    def marker(self):
        n = int(0.08 * SR)
        self.put(self.h, 0.5 * np.sin(2 * np.pi * F0 * np.arange(n) / SR))
        self.meta["markers"].append(self.h)
        self.h += 1.3

    def step(self, ev, sig_fn, gate, tail):
        self.h += 0.3
        on, off = self.h, self.h + gate
        sig = sig_fn(gate, tail)
        self.put(on, sig)
        ev.update(on=on, off=off, end=off + tail, gate=gate, tail=tail)
        self.meta["steps"].append(ev)
        self.h = off + tail + 0.7

    def write(self, d):
        self.marker()
        y = np.clip(self.x, -1, 1)
        if self.dt_rec:     # stereo float32, fmt chunk of 18 bytes + fact chunk, as dt_rec.c writes
            pcm = np.repeat(y.astype("<f4")[:, None], 2, axis=1).tobytes()
            fmt = struct.pack("<HHIIHHH", 3, 2, SR, SR * 8, 8, 32, 0)
            body = (b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"fact" +
                    struct.pack("<II", 4, len(y)) + b"data" + struct.pack("<I", len(pcm)) + pcm)
            with open(os.path.join(d, self.name + ".wav"), "wb") as fh:
                fh.write(b"RIFF" + struct.pack("<I", len(body)) + body)
            json.dump(self.meta, open(os.path.join(d, self.name + ".json"), "w"))
            return
        q = np.round(y * (2 ** 23 - 1)).astype(np.int32)
        b = np.stack([q & 255, (q >> 8) & 255, (q >> 16) & 255], 1).astype(np.uint8)
        with wave.open(os.path.join(d, self.name + ".wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(3)
            w.setframerate(SR)
            w.writeframes(b.tobytes())
        json.dump(self.meta, open(os.path.join(d, self.name + ".json"), "w"))


def sine(n):
    return 0.5 * np.sin(2 * np.pi * F0 * np.arange(n) / SR)


SIX = [0, 32, 64, 96, 112, 126]
T_ATK = lambda v: 0.005 * 10 ** (v / 35)     # noqa: E731  linear ramp length
T_DEC = lambda v: 0.02 * 10 ** (v / 40)      # noqa: E731  T60 of exponential decay
TAU_F = lambda v: 0.01 * 10 ** (v / 45)      # noqa: E731  log-cutoff time constant


def gen(d):
    c = Cap("amp_atk", 100)
    c.marker()
    for v in SIX + [127]:
        T = T_ATK(v)
        def f(g, tl, T=T):
            n = int((g + tl) * SR)
            t = np.arange(n) / SR
            env = np.minimum(t / T, 1.0) * (t < g)
            return sine(n) * env
        # 127: a gate at 55 % of the rise (the plateau not reached): extrapolated by the ramp fit
        c.step({"param": "amp_atk", "value": v, "kind": "attack"}, f, 0.55 * T if v == 127 else max(1.0, T * 1.3), 0.3)
    c.write(d)

    c = Cap("amp_dec", 90, dt_rec=True)
    c.marker()
    for v in SIX:
        T = T_DEC(v)
        def f(g, tl, T=T):
            n = int((g + tl) * SR)
            t = np.arange(n) / SR
            return sine(n) * 10 ** (-3 * t / T) * (t < g)
        c.step({"param": "amp_dec", "value": v, "kind": "decay"}, f, min(max(1.0, T * 1.2), 30), 0.3)
    c.write(d)

    c = Cap("filt_dec", 80)
    c.marker()
    fmin, fmax = 200.0, 12000.0
    for v in SIX:
        tau = TAU_F(v)
        def f(g, tl, tau=tau):
            n = int((g + tl) * SR)
            t = np.arange(n) / SR
            saw = 0.4 * (2 * ((t * F0) % 1.0) - 1)
            fc = fmin * (fmax / fmin) ** np.exp(-t / tau)
            a = 1 - np.exp(-2 * np.pi * fc / SR)
            y = np.empty(n)
            s1 = s2 = 0.0
            for i in range(n):
                s1 += a[i] * (saw[i] - s1)
                s2 += a[i] * (s1 - s2)
                y[i] = s2
            return y * (t < g)
        c.step({"param": "f_dec", "value": v, "kind": "filt_decay"}, f,
               min(max(1.0, tau * 5), 30), 0.3)
    c.write(d)


R2_T60 = {8: 0.03, 24: 0.5, 80: 9.4, 120: 26.0}     # run 2 style: windows <= 8 s, slope fitted
LATE = {80: 0.075}                                   # a late onset (run 1 saw 74 ms on some notes)
F_LFO = 15.75
FADE_T = {0: 0.0, -16: 0.06, -32: 1.0, -40: 5.0, -64: 250.0}     # full (linear) fade times
FADE_GATE = {0: 1.5, -16: 1.5, -32: 2.5, -40: 12.0, -64: 20.0}


def gen2(d):
    c = Cap("r2_amp_dec", 40, dt_rec=True)
    c.marker()
    for v, T in R2_T60.items():
        def f(g, tl, T=T, lag=LATE.get(v, 0.0)):
            n = int((g + tl) * SR)
            t = np.arange(n) / SR - lag
            return sine(n) * np.where(t >= 0, 10 ** (-3 * np.maximum(t, 0) / T), 0.0) * (t < g - lag)
        c.step({"param": "amp_dec", "value": v, "kind": "decay"}, f, min(8.0, max(1.0, 1.3 * T)), 0.3)
    c.write(d)
    # releases keep sounding after their step (as the Digitone's do): each tail runs on under the next steps
    # and, for the last, under the end marker
    c = Cap("r2_amp_rel", 45, dt_rec=True)
    c.marker()
    for v, T in R2_T60.items():
        def f(g, tl, T=T):
            n = int((g + tl + 20.0) * SR)
            t = np.arange(n) / SR
            return sine(n) * np.where(t < g, 1.0, 10 ** (-3 * (t - g) / T))
        c.step({"param": "amp_rel", "value": v, "kind": "release"}, f, 1.0, min(8.0, max(1.0, 1.3 * T)))
    c.write(d)
    # LFO fade: a 15.75 Hz sine on VOL 64 +- 24 units (amplitude ~ VOL^2), its depth ramped linearly over T
    c = Cap("r2_fade_120", 50, dt_rec=True)
    c.marker()
    for disp, T in FADE_T.items():
        def f(g, tl, T=T):
            n = int((g + tl) * SR)
            t = np.arange(n) / SR
            ramp = np.ones(n) if T == 0 else np.minimum(t / T, 1.0)
            vol = 64 + 24 * ramp * np.sin(2 * np.pi * F_LFO * t)
            return 2 * sine(n) * (vol / 127) ** 2 * (t < g)
        c.step({"param": "lfo1_fade", "value": 64 + disp, "display": disp, "tempo": 120, "kind": "fade"}, f,
               FADE_GATE[disp], 0.3)
    c.write(d)


def test_analysis2(d):
    """run 2's analyses on synthetic captures: slope T60, the tremolo-depth fade"""
    gen2(d)
    out = dt_analyse.analyse_dir(d, plots=False)
    st = {(r["capture"], r["value"]): r for r in out["steps"]}
    for cap in ("r2_amp_dec", "r2_amp_rel"):
        sy = st[(cap, 8)]["sync"]
        # the release capture's end marker sits on the last release's tail (20 dB down): its onset reads
        # ~1 ms late; 1e-4 in the rate is 3 ms on a 26 s T60
        tol = 1e-4 if cap == "r2_amp_rel" else 2e-5
        check(sy["markers"] == 2 and abs(sy["rate"] - (1 + DRIFT)) < tol,
              f"{cap}: both markers found{' (the last over a release tail)' if tol > 2e-5 else ''}, "
              f"rate {sy.get('rate', 0):.6f}")
        for v, T in R2_T60.items():
            r = st[(cap, v)]
            slope = "slope" in (r.get("T_from") or "")
            tol = max(0.004, 0.015 * T) if slope else max(0.006, 0.05 * T)
            check(r["T"] is not None and abs(r["T"] - T) < tol and slope == (T >= 0.5),
                  f"{cap} v{v}: T60 {r['T'] if r['T'] is None else round(r['T'], 4)} vs {T} "
                  f"({r.get('T_from')}{', onset 75 ms late' if cap == 'r2_amp_dec' and v in LATE else ''})")
    fr = {r["display"]: r for r in out["steps"] if r["capture"] == "r2_fade_120"}
    r0 = fr[0]
    exp_db = 40 * math.log10(88 / 40)
    check(r0.get("lfo_hz") and abs(r0["lfo_hz"] - F_LFO) < 0.1 and abs(r0["final_depth_db"] - exp_db) < 0.5,
          f"fade: LFO {r0.get('lfo_hz', 0):.2f} Hz vs {F_LFO}, full depth {r0.get('final_depth_db', 0):.2f} dB "
          f"vs {exp_db:.2f}")
    res = r0.get("resolution_s", 0.032)
    check(r0["T"] is not None and r0["T"] <= res + 0.01, f"fade 0: instant (T {r0['T']} <= resolution {res:.3f} s)")
    for disp in (-16, -32, -40, -64):
        r, exp = fr[disp], 0.9 * FADE_T[disp]
        tol = max(0.035, 0.05 * exp)
        ext = "extrapolated" in (r.get("T_from") or "")
        check(r["T"] is not None and abs(r["T"] - exp) < tol and ext == (disp == -64),
              f"fade {disp}: t90 {r['T'] if r['T'] is None else round(r['T'], 3)} vs {exp:.3f} ({r.get('T_from')}, "
              f"reached {r.get('reached_pct', 0):.0f} %)")
    r = fr[-64]
    check(r.get("t_full") and abs(r["t_full"] - 250.0) < 12.5, f"fade -64: full fade {r.get('t_full')} vs 250 s")


def test_analysis(d):
    gen(d)
    out = dt_analyse.analyse_dir(d, plots=False)
    st = {(r["param"], r["value"]): r for r in out["steps"]}
    sync = out["steps"][0]["sync"]
    check(sync["markers"] == 2 and abs(sync["rate"] - (1 + DRIFT)) < 2e-5,
          f"two markers found, drift rate {sync.get('rate'):.6f}")
    sd = st[("amp_dec", 0)]["sync"]
    check(sd["recorder"] == "dt_rec" and sd["markers"] == 2 and abs(sd["offset"] - MIDI_LAT) < 0.003,
          f"dt_rec float32 capture: markers {sd['markers']}, offset from host_rec_start "
          f"{sd.get('offset', 0) * 1e3:.2f} ms vs MIDI latency {MIDI_LAT * 1e3:.0f} ms")
    r = st[("amp_atk", 127)]
    check("ramp fit" in (r.get("T_from") or "") and abs(r["t90"] - 0.9 * T_ATK(127)) < 0.03 * 0.9 * T_ATK(127)
          and abs(r["rise_full"] - T_ATK(127)) < 0.03 * T_ATK(127),
          f"atk v127 gate at 55 % of the rise: t90 {r['t90']:.3f} vs {0.9 * T_ATK(127):.3f}, full "
          f"{r['rise_full']:.3f} vs {T_ATK(127):.3f} ({r.get('T_from')})")
    for v in SIX:
        r = st[("amp_atk", v)]
        exp = 0.9 * T_ATK(v)
        tol = max(0.002, 0.03 * exp)
        check(r["t90"] is not None and abs(r["t90"] - exp) < tol,
              f"atk v{v}: t90 {r['t90']:.4f} vs {exp:.4f}")
        if T_ATK(v) > 0.05:
            check(abs(r["level_at_half_t98"] - 0.5) < 0.05,
                  f"atk v{v}: linear, level at t98/2 = {r['level_at_half_t98']:.3f}")
    for v in SIX:
        r = st[("amp_dec", v)]
        exp = T_DEC(v)
        check(r["T"] is not None and abs(r["T"] - exp) < max(0.006, 0.03 * exp),
              f"dec v{v}: T60 {r['T']:.4f} vs {exp:.4f} ({r.get('T_from')})")
    ca = out["curves"]["amp_atk"]["fit"]
    check(ca["best"] == "exp" and abs(ca["exp"]["k"] - 1 / 35) < 0.002,
          f"atk fit exp k={ca['exp']['k']:.5f} vs {1/35:.5f}")
    cd = out["curves"]["amp_dec"]["fit"]
    check(cd["best"] == "exp" and abs(cd["exp"]["k"] - 1 / 40) < 0.001,
          f"dec fit exp k={cd['exp']['k']:.5f} vs {1/40:.5f}")
    cf = out["curves"]["f_dec"]["fit"]
    ratios = [st[("f_dec", v)]["T"] / TAU_F(v) for v in SIX if v > 0]
    print("     filt t90/tau ratios:", " ".join(f"{q:.2f}" for q in ratios))
    check(cf["best"] == "exp" and abs(cf["exp"]["k"] - 1 / 45) < 0.0015,
          f"filt fit exp k={cf['exp']['k']:.5f} vs {1/45:.5f}")


if __name__ == "__main__":
    test_cc()
    test_lists()
    test_plan()
    d = sys.argv[1] if len(sys.argv) > 1 else tempfile.mkdtemp(prefix="dt_test_")
    os.makedirs(d, exist_ok=True)
    test_analysis(d)
    d2 = os.path.join(d, "run2")
    os.makedirs(d2, exist_ok=True)
    test_analysis2(d2)
    print(f"\n{len(fails)} failure(s); synthetic files in {d}")
    sys.exit(1 if fails else 0)
