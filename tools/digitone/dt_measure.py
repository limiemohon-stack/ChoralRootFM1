#!/usr/bin/env python3
"""Measure Elektron Digitone envelope times: drive track 1 over MIDI, record USB audio.

  python3 tools/digitone/dt_measure.py --plan [--set 2]  # print the plan, send nothing
  python3 tools/digitone/dt_measure.py --run [--set 2] [--only amp_atk,amp_dec] [--out DIR]

--set 1 (default) is run 1's capture set; --set 2 is run 2 (denser values, the redesigned
LFO fade), its files named r2_<capture> so run 1's stay untouched in the same --out.

Design: one WAV per capture (a group, or a group at one tempo) recorded by build/dt_rec
(tools/digitone/dt_rec.c, CoreAudio, device found by name, every sample; built on
first use). ffmpeg avfoundation is only a fallback, with a warning: it drops about
10 % of the samples on this Mac. Every capture starts and ends with a sync marker
note (amp ATK 0, short gate). Note events are logged in host time (time.monotonic);
dt_analyse.py finds the two marker onsets in the audio and maps host time -> audio
time linearly, which removes recorder start latency, MIDI and audio latency and
clock drift. dt_rec also reports the host time of the first sample (host_rec_start
in the JSON). The JSON next to the WAV holds every event.

Every capture sets its list parameters itself (ALGO, FILTER TYPE, RATIO C / A, the
LFO WAVE / MODE / MULT and DEST NONE: dt_cc.LISTS, proved on the device 2026-10-14)
and MIX at the X end. What MIDI cannot set (LFO1 DEST = VOLUME for the fade
captures, the tempo) is asked for by hand after the base is sent, with a line of
values to check on the screen; press Enter.
"""
import argparse
import json
import math
import os
import re
import signal
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dt_cc  # noqa: E402

DEFAULT_OUT = "/Volumes/Q7Media-2025/Projects/Github/ChoralRootFM1/DigitoneRecordings/times/"
DEVICE = "Elektron Digitone"
DT_REC = os.path.join(os.path.dirname(os.path.dirname(HERE)), "build", "dt_rec")
NOTE, VEL = 60, 100
SETTLE = 0.3          # after parameter changes, before the note
GAP = 0.7             # silence after each step's tail
MARK_GATE = 0.08
LEAD_IN = 1.5         # after the recorder starts, before the first marker

# Plain sine: operator C only, filter off, no mod, no sends, LFOs at depth 0 and DEST NONE.
# ALGO 1 (firmware/src/eng_quad.c QUAD_ALGO, docs/QUAD.md): A>C, B2>B1, B1>C, X = C alone,
# Y = B1 direct (at full level whatever B LEV): MIX at the X end mutes it. With A LEV and
# B LEV 0 nothing modulates C. MIX 0 = -64, assumed the X end (the init sound's MIX: its
# plain sine on ALGO 1 says so); the prompt asks the owner to check it.
BASE = {
    "algo": 1, "mix": 0, "f_type": "OFF", "ratio_c": 1, "ratio_a": 1,
    "lfo1_dest": "NONE", "lfo2_dest": "NONE",
    "harm": dt_cc.HARM_ZERO, "dtun": 0, "fdbk": 0,
    "a_lev": 0, "b_lev": 0, "a_atk": 0, "a_dec": 0, "a_end": 0, "a_del": 0,
    "b_atk": 0, "b_dec": 0, "b_end": 0, "b_del": 0,
    "f_freq": 127, "f_reso": 0, "f_env": 64, "f_atk": 0, "f_dec": 0, "f_sus": 127,
    "f_rel": 0, "f_del": 0, "f_base": 0, "f_width": 127,
    "amp_atk": 0, "amp_dec": 0, "amp_sus": 127, "amp_rel": 0,
    "drive": 0, "pan": 64, "vol": 100, "chr_send": 0, "del_send": 0, "rev_send": 0,
    "lfo1_dep": 64, "lfo1_fade": 64, "lfo2_dep": 64, "lfo2_fade": 64,
}
MARKER = {"amp_atk": 0, "amp_dec": 0, "amp_sus": 127, "amp_rel": 0}

# The init sound's values, sent back at the end for every parameter the tool touched.
# The Digitone cannot report its parameters over CC; these are the owner's init-sound
# export (DigitonePresets/PresetFormat/BASE.dnsnd, OS 1.44, 2026-10-14, outside the
# repository; decoded with web/fm1dnsnd.js), HARM 0.00 = 63 (dt_cc.HARM_ZERO, proved).
# Reload the sound/project without saving to be exact (README says so).
INIT_RESTORE = dict(BASE, **{
    "algo": 1, "mix": 0, "f_type": "LP2", "ratio_c": 1, "ratio_a": 1,
    "a_atk": 0, "a_dec": 32, "a_end": 127, "a_lev": 0,
    "b_atk": 0, "b_dec": 32, "b_end": 127, "b_lev": 0,
    "f_atk": 0, "f_dec": 64, "f_sus": 0, "f_rel": 64,
    "amp_atk": 8, "amp_dec": 32, "amp_sus": 96, "amp_rel": 24, "vol": 110,
    "chr_send": 0, "del_send": 0, "rev_send": 0,
    "lfo1_spd": 96, "lfo1_mult": 32, "lfo1_wave": "TRI", "lfo1_mode": "FRE", "lfo1_dest": "NONE",
    "lfo2_spd": 96, "lfo2_mult": 32, "lfo2_wave": "TRI", "lfo2_mode": "FRE", "lfo2_dest": "NONE",
})

# shown with the list settings: what dt_cc cannot set and the script does not change
NOT_SENT = {"opB": "RATIO B 1.00/1.00 (the init sound's; not sent)"}


def check_line(c):
    """the values the owner confirms on the screen after the base is sent"""
    b = c["base"]
    s = [f"ALGO {b['algo']}", "MIX -64 (assumed the X end: only C heard)",
         f"FILTER TYPE {b['f_type']}", "HARM 0.00"]
    if "lfo1_wave" in b:
        m = str(b["lfo1_mult"])
        s.append(f"LFO1 WAVE {b['lfo1_wave']} MODE {b['lfo1_mode']} MULT {m} " +
                 ("(the fixed-120-BPM one, list entry {}, after 2K: assumed)".format(
                     dt_cc.LFO_MULTS.index(m.upper()) + 1) if m.upper().startswith("F")
                  else "(tempo-synced, proved by run 1)"))
    s += c.get("check_extra", [])
    if c["group"] in NOT_SENT:
        s.append(NOT_SENT[c["group"]])
    return ", ".join(s)


def _hold_attack(v):
    return 2.0 if v <= 64 else 3.0 if v <= 96 else 6.0 if v <= 112 else 40.0


def _hold_decay(v):
    return 2.0 if v <= 64 else 4.0 if v <= 96 else 10.0 if v <= 112 else 30.0


def _hold_filter(v):
    return 2.5 if v <= 64 else 6.0 if v <= 96 else 14.0 if v <= 112 else 30.0


SIX = [0, 32, 64, 96, 112, 126]
THREE = [0, 64, 112]


def build_plan_1():
    """run 1's captures; each one's "base" is complete (BASE with its own changes), "hand" what
    the owner sets by hand, "hand_keys" the parameters those touch (restored at the end)"""
    caps = []

    def cap(name, group, base, steps, hand=(), hand_keys=()):
        full = dict(BASE, **base)
        for k in hand_keys:          # set by hand: the script must not overwrite it
            full.pop(k, None)
        caps.append({"name": name, "group": group, "hand": list(hand), "hand_keys": list(hand_keys),
                     "base": full, "steps": steps})

    # amp: ALGO 1 (BASE): C alone on X, a plain sine
    cap("amp_atk", "amp_atk", {}, [
        {"param": "amp_atk", "value": v, "set": {"amp_atk": v}, "gate": _hold_attack(v),
         "tail": 0.3, "kind": "attack"} for v in SIX + [127]])
    cap("amp_dec", "amp_dec", {"amp_sus": 0}, [
        {"param": "amp_dec", "value": v, "set": {"amp_dec": v}, "gate": _hold_decay(v),
         "tail": 0.3, "kind": "decay"} for v in SIX])
    cap("amp_rel", "amp_rel", {}, [
        {"param": "amp_rel", "value": v, "set": {"amp_rel": v}, "gate": 1.0,
         "tail": _hold_decay(v), "kind": "release"} for v in SIX])
    # filter: LP4 on ALGO 1's C, made bright by A>C at a held level (A LEV 96, END 127; the
    # feedback operator of ALGO 1 is A, so FDBK 70 only sounds through A: with A LEV 0 the
    # filter would sweep a bare sine)
    filt = {"f_type": "LP4", "f_freq": 20, "f_reso": 40, "f_env": 127, "fdbk": 70, "f_sus": 0,
            "a_lev": 96, "a_atk": 0, "a_dec": 0, "a_end": 127}
    cap("filt_dec", "filt_dec", filt, [
        {"param": "f_dec", "value": v, "set": {"f_dec": v}, "gate": _hold_filter(v),
         "tail": 0.3, "kind": "filt_decay"} for v in SIX])
    cap("filt_atk", "filt_atk", dict(filt, f_sus=127), [
        {"param": "f_atk", "value": v, "set": {"f_atk": v}, "gate": _hold_filter(v),
         "tail": 0.3, "kind": "filt_attack"} for v in THREE])
    cap("filt_rel", "filt_rel", dict(filt, f_sus=127, amp_rel=127), [
        {"param": "f_rel", "value": v, "set": {"f_rel": v}, "gate": 1.0,
         "tail": min(_hold_filter(v), 14.0), "kind": "filt_release"} for v in THREE])
    # opA: ALGO 1 (A>C, X = C; B LEV 0 keeps B1 out of C). opB: ALGO 6 (A>C, A>B1, B2>C,
    # B2>B1, X = C; A LEV 0): B2 alone modulates C, one envelope stage as A's in opA
    # (ALGO 1's B is the chain B2>B1>C, both under ENV B)
    for op, algo in (("a", 1), ("b", 6)):
        g = "op" + op.upper()
        base = {"algo": algo, f"{op}_lev": 110}
        steps = [{"param": f"{op}_atk", "value": v,
                  "set": {f"{op}_atk": v, f"{op}_dec": 127, f"{op}_end": 127},
                  "gate": _hold_filter(v), "tail": 0.3, "kind": "op_attack"} for v in THREE]
        steps += [{"param": f"{op}_dec", "value": v,
                   "set": {f"{op}_atk": 0, f"{op}_dec": v, f"{op}_end": 0},
                   "gate": _hold_filter(v), "tail": 0.3, "kind": "op_decay"} for v in THREE]
        cap(g, g, base, steps)
    # A's DELAY (tempo-synced): as opA
    for tempo in (120, 80):
        cap(f"delay_{tempo}", "delay", {"a_lev": 110, "a_atk": 0, "a_dec": 127, "a_end": 127}, [
                {"param": "a_del", "value": v, "tempo": tempo, "set": {"a_del": v},
                 "gate": 8.0, "tail": 0.3, "kind": "delay"} for v in (16, 64, 112)],
            hand=[f"project TEMPO = {tempo} BPM"])
    # LFO fade-in on the plain sine: bipolar, CC 64 = no fade, below 64 = fade in. Display -8,
    # -32, -64. LFO1 DEST = VOLUME is set by hand (dt_cc knows only NONE), LFO2's NONE.
    for tempo in (120, 80):
        cap(f"fade_{tempo}", "fade",
            {"lfo1_dep": 127, "lfo1_spd": 96, "lfo1_wave": "SQR", "lfo1_mode": "TRG", "lfo1_mult": 1}, [
                {"param": "lfo1_fade", "value": v, "display": v - 64, "tempo": tempo,
                 "set": {"lfo1_fade": v}, "gate": 12.0, "tail": 0.3, "kind": "fade"}
                for v in (56, 32, 0)],
            hand=["LFO1 DEST = VOLUME (AMP VOL)", f"project TEMPO = {tempo} BPM"], hand_keys=["lfo1_dest"])
    return caps


# ---- run 2 ------------------------------------------------------------------------------------
# Expected times (s) the run-2 gates are sized from, interpolated in log(T) between the points.
R1_AMP_ATK = {0: 0.002, 32: 0.05, 64: 0.26, 96: 0.71, 112: 1.49, 126: 26.8, 127: 29.8}   # run 1 full rise
R1_AMP_T60 = {0: 0.005, 32: 1.01, 64: 5.5, 96: 15.07, 112: 22.0, 126: 29.5}             # run 1 T60
R1_FILT_DEC_T90 = {0: 0.013, 32: 0.15, 64: 0.54, 96: 2.17, 112: 4.3, 126: 7.9}           # run 1 centroid t90
# Run 1's filter attacks never reached the open filter (value 0's end level): both sweeps stopped at 65 %
# (64 in 2.5 s, 112 in 14 s; the filter passed the fundamental only at ~1.5 s / ~9 s), so run 1's filter
# attack t90s were noise crossings. Op A's attack at 64 was at 52 % when its 2.5 s gate ended (112 just
# finished in 14 s). These are estimates of the end of the sweep.
EST_FILT_ATK_END = {0: 0.02, 16: 0.6, 32: 1.1, 48: 2.0, 64: 3.5, 80: 6.3, 96: 11.5, 104: 15.5, 112: 21.0,
                    120: 28.0, 126: 35.0}
EST_OP_ATK_END = {0: 0.01, 16: 0.5, 32: 1.0, 48: 2.0, 64: 4.0, 80: 6.3, 96: 10.0, 112: 16.0, 126: 24.0}
R1_OP_DEC_T90 = {0: 0.005, 64: 1.0, 112: 3.98, 126: 5.9}                                 # 126 extrapolated
LONG_CAP = 30.0         # longest filter / op attack gate: dt_analyse extrapolates an unfinished sweep
DEC_CAP = 8.0           # amp decay / release: the slope over >= ~20 dB of the exponential is enough

# LFO FADE (run 2): a fast sine tremolo on VOLUME whose per-cycle depth shows the fade ramp. Run 1's
# raw levels (SQR, DEP +63, VOL 100): the high half sat at VOL 127 (+4.1 dB, clipped) and the low half
# was silent, so DEP +63 swings VOL by >= 100 units: a sine at that depth clips at both ends. Hence VOL
# 64 and DEP +12: about +-24 units if DEP +63 is +-127 (13.7 dB peak/trough), +-48 if it is +-254
# (34 dB), unclipped either way. Run 1 also showed the fade is a linear ramp in absolute time (equal at
# 80 and 120 BPM while that LFO's rate changed 1.5x): VOL ramped 160 units/s at -32 and 0.59 units/s
# at -64, so the full fade takes >= 0.6 s (-32) and >= 170 s (-64): -56 and -64 cannot finish within
# 30 s; their gates measure the ramp's slope and dt_analyse extrapolates.
FADE_ZERO = 64          # LFO FADE (CC 109, 7-bit, -64..63): 64 = 0 assumed, not proved
FADE2_SPD, FADE2_MULT, FADE2_DEP = 127, "F64", 64 + 12
FADE2_GATE = {0: 1.5, 4: 1.5, 8: 1.5, 16: 1.5, 24: 1.5, 32: 2.5, 40: 12.0, 48: 30.0, 56: 20.0, 64: 20.0}


def lfo_hz(spd_cc, mult, bpm=120.0):
    """the manual's LFO speed table: one cycle = 128 / (SPD x MULT) whole notes; F mults at 120 BPM"""
    spd = spd_cc - 64                     # coarse CC 127 = +63.00 (64 = 0, proved by run 1)
    m = str(mult).upper()
    fixed = m.startswith("F")
    m = m[1:] if fixed else m
    mv = int(m[:-1]) * 1024 if m.endswith("K") else int(m)
    return abs(spd) * mv / 128 / (4 * 60 / (120.0 if fixed else bpm))


def _logi(table, v):
    """table {value: seconds}, interpolated (and extrapolated by the end segments) in log(T)"""
    ks = sorted(table)
    i = max(1, min(len(ks) - 1, next((j for j, k in enumerate(ks) if k >= v), len(ks) - 1)))
    k0, k1 = ks[i - 1], ks[i]
    a, b = math.log(table[k0]), math.log(table[k1])
    return math.exp(a + (b - a) * (v - k0) / (k1 - k0))


def build_plan_2():
    """run 2: dense values where run 1 was coarse, long enough gates, and the redesigned fade; capture
    files r2_<name>"""
    caps = []

    def cap(name, group, base, steps, hand=(), hand_keys=(), check_extra=()):
        full = dict(BASE, **base)
        for k in hand_keys:
            full.pop(k, None)
        caps.append({"name": "r2_" + name, "group": group, "hand": list(hand), "hand_keys": list(hand_keys),
                     "base": full, "steps": steps, "check_extra": list(check_extra)})

    def r(x):       # gates up to the next 0.1 s (never below the 1.3 x)
        return math.ceil(x * 10 - 1e-9) / 10

    atk = sorted([100, 104, 108, 112, 114, 116, 118, 120, 122, 124, 125, 126, 8, 16, 24, 48, 80])
    cap("amp_atk", "amp_atk", {}, [
        {"param": "amp_atk", "value": v, "set": {"amp_atk": v}, "gate": r(max(0.8, 1.3 * _logi(R1_AMP_ATK, v))),
         "tail": 0.3, "kind": "attack"} for v in atk])
    cap("amp_dec", "amp_dec", {"amp_sus": 0}, [
        {"param": "amp_dec", "value": v, "set": {"amp_dec": v},
         "gate": r(min(DEC_CAP, max(1.0, 1.3 * _logi(R1_AMP_T60, v)))), "tail": 0.3, "kind": "decay"}
        for v in (4, 8, 16, 24, 48, 80, 104, 120)])
    # ascending: a release tail outlives its step (run 1: it went on after AMP REL was set back to 0) and
    # sounds under the next step, but a shorter one is >= 60 dB down before the next, longer, one is fitted
    cap("amp_rel", "amp_rel", {}, [
        {"param": "amp_rel", "value": v, "set": {"amp_rel": v}, "gate": 1.0,
         "tail": r(min(DEC_CAP, max(1.0, 1.3 * _logi(R1_AMP_T60, v)))), "kind": "release"}
        for v in (8, 24, 48, 80, 120)])
    filt = {"f_type": "LP4", "f_freq": 20, "f_reso": 40, "f_env": 127, "fdbk": 70, "f_sus": 0,
            "a_lev": 96, "a_atk": 0, "a_dec": 0, "a_end": 127}
    eleven = [0, 16, 32, 48, 64, 80, 96, 104, 112, 120, 126]
    cap("filt_dec", "filt_dec", filt, [
        {"param": "f_dec", "value": v, "set": {"f_dec": v}, "gate": r(max(1.5, 2.0 * _logi(R1_FILT_DEC_T90, v))),
         "tail": 0.3, "kind": "filt_decay"} for v in eleven])
    cap("filt_atk", "filt_atk", dict(filt, f_sus=127), [
        {"param": "f_atk", "value": v, "set": {"f_atk": v},
         "gate": r(min(LONG_CAP, max(1.5, 1.3 * EST_FILT_ATK_END[v]))), "tail": 0.3, "kind": "filt_attack"}
        for v in eleven])
    nine = [0, 16, 32, 48, 64, 80, 96, 112, 126]
    steps = [{"param": "a_atk", "value": v, "set": {"a_atk": v, "a_dec": 127, "a_end": 127},
              "gate": r(min(LONG_CAP, max(1.5, 1.3 * EST_OP_ATK_END[v]))), "tail": 0.3, "kind": "op_attack"}
             for v in nine]
    steps += [{"param": "a_dec", "value": v, "set": {"a_atk": 0, "a_dec": v, "a_end": 0},
               "gate": r(max(1.5, 2.5 * _logi(R1_OP_DEC_T90, v))), "tail": 0.3, "kind": "op_decay"}
              for v in nine]
    cap("opA", "opA", {"algo": 1, "a_lev": 110}, steps)
    fade = {"vol": 64, "lfo1_dep": FADE2_DEP, "lfo1_spd": FADE2_SPD, "lfo1_wave": "SIN", "lfo1_mode": "TRG",
            "lfo1_mult": FADE2_MULT}
    hz = lfo_hz(FADE2_SPD, FADE2_MULT)
    for tempo, disp in ((120, (0, -4, -8, -16, -24, -32, -40, -48, -56, -64)), (80, (0, -16, -32, -48))):
        cap(f"fade_{tempo}", "fade", fade, [
                {"param": "lfo1_fade", "value": max(0, FADE_ZERO + d), "display": d, "tempo": tempo,
                 "set": {"lfo1_fade": max(0, FADE_ZERO + d)}, "gate": FADE2_GATE[-d], "tail": 0.3,
                 "kind": "fade"} for d in disp],
            hand=["LFO1 DEST = VOLUME (AMP VOL)", f"project TEMPO = {tempo} BPM"], hand_keys=["lfo1_dest"],
            check_extra=[f"LFO1 SPD 63.00, DEP {FADE2_DEP - 64}.00 (a {hz:.2f} Hz sine at any tempo), VOL 64.00"])
    return caps


def build_plan(set_=1):
    return build_plan_2() if set_ == 2 else build_plan_1()


# the base's entries the plan prints on their own line (the list settings and MIX)
LISTED = ["algo", "mix", "f_type", "ratio_c", "ratio_a", "lfo1_wave", "lfo1_mode", "lfo1_mult",
          "lfo1_dest", "lfo2_dest"]


def listed(base):
    return " ".join(f"{k}={base[k]}" for k in LISTED if k in base)


def hand_text(c):
    return "; ".join(c["hand"]) if c["hand"] else "nothing"


GROUPS = ["amp_atk", "amp_dec", "amp_rel", "amp_hold", "filt_dec", "filt_atk", "filt_rel",
          "opA", "opB", "delay", "fade"]
PREFIX = {1: "", 2: "r2_"}      # capture file names: run 2's never overwrite run 1's


def capture_seconds(c):
    t = LEAD_IN + 2 * (MARK_GATE + 1.0 + SETTLE)
    for s in c["steps"]:
        t += SETTLE + s["gate"] + s["tail"] + GAP
    return t


def print_plan(caps, set_=1):
    total = 0.0
    nsteps = 0
    print(f"capture set {set_}" + (f": files {PREFIX[set_]}<capture>.wav/.json" if PREFIX[set_] else ""))
    for c in caps:
        d = capture_seconds(c)
        total += d
        print(f"\n== capture {c['name']}  ({d:.0f} s)")
        print(f"   lists over MIDI: {listed(c['base'])}")
        print(f"   by hand: {hand_text(c)}")
        print(f"   check: {check_line(c)}")
        print(f"   base over MIDI: { {k: v for k, v in c['base'].items() if k not in LISTED} }")
        for s in c["steps"]:
            nsteps += 1
            extra = f" tempo {s['tempo']}" if "tempo" in s else ""
            extra += f" (display {s['display']:+d})" if "display" in s else ""
            print(f"   {s['param']:9s}={s['value']:3d}{extra}  set {s['set']}  note C4 v{VEL} "
                  f"gate {s['gate']:.1f} s, tail {s['tail']:.1f} s ({s['kind']})")
    print(f"\n{len(caps)} captures, {nsteps} steps, about {total/60:.1f} min of recording "
          f"(plus the hand-set prompts).")
    print("amp_hold: the original Digitone has no AMP HOLD parameter; skipped.")
    print(END_NOTE)
    return total


END_NOTE = ("At the end the tool sends the init sound's values back (the owner's BASE.dnsnd export) for every "
            "parameter it touched, the list parameters too, and LFO1 DEST NONE after a fade capture; the tempo "
            "is not restored. Reload the sound without saving to be exact.")


def find_audio_index(name=DEVICE):
    p = subprocess.run(["ffmpeg", "-hide_banner", "-f", "avfoundation", "-list_devices",
                        "true", "-i", ""], capture_output=True, text=True)
    audio = False
    for line in p.stderr.splitlines():
        if "audio devices" in line:
            audio = True
        elif audio:
            m = re.search(r"\[(\d+)\] (.+)$", line)
            if m and m.group(2).strip() == name:
                return int(m.group(1))
    raise SystemExit(f"audio input '{name}' not found in ffmpeg avfoundation list")


def build_dt_rec():
    """build/dt_rec, (re)built from dt_rec.c when missing or older; None when it cannot be built"""
    src = os.path.join(HERE, "dt_rec.c")
    if not os.path.exists(DT_REC) or os.path.getmtime(DT_REC) < os.path.getmtime(src):
        p = subprocess.run(["sh", os.path.join(HERE, "build_rec.sh")], capture_output=True, text=True)
        if p.returncode != 0:
            sys.stderr.write(p.stdout + p.stderr)
    return DT_REC if os.access(DT_REC, os.X_OK) else None


FFMPEG_WARNING = """
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!! WARNING: build/dt_rec is missing and could not be built (tools/digitone/build_rec.sh).
!! Recording with ffmpeg avfoundation, which DROPS ABOUT 10 % OF THE SAMPLES on this
!! Mac: envelope times measured from these WAVs are wrong. Fix the build and re-run.
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
"""


class Recorder:
    """Records the named CoreAudio input to a WAV: build/dt_rec (every sample, reports the host time of
    sample 0 and any discontinuity), or ffmpeg avfoundation as a loudly warned fallback."""

    def __init__(self, device=DEVICE):
        self.dev = device
        self.exe = build_dt_rec()
        self.p = None
        if self.exe is None:
            sys.stderr.write(FFMPEG_WARNING)
            self.aidx = find_audio_index(device)
            return
        # probe: opens the device and stops at once (EOF on stdin); exit 2 lists the input devices
        p = subprocess.run([self.exe, device, "/dev/null"], stdin=subprocess.DEVNULL,
                           capture_output=True, text=True, timeout=20)
        if p.returncode != 0:
            raise SystemExit(f"dt_rec cannot record '{device}' (exit {p.returncode}):\n{p.stderr}")

    def start(self, wav):
        """start recording; returns the JSON fields: recorder, host_rec_launch and host_rec_start (dt_rec,
        host time of the first sample) or host_ff_start (ffmpeg: launch time only)"""
        t = time.monotonic()
        if self.exe is None:
            self.p = subprocess.Popen(
                ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "avfoundation",
                 "-i", f":{self.aidx}", "-ar", "48000", "-c:a", "pcm_s24le", wav],
                stdin=subprocess.PIPE)
            return {"recorder": "ffmpeg", "host_rec_launch": t, "host_ff_start": t}
        self.p = subprocess.Popen([self.exe, self.dev, wav], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.lines = []
        self.got = threading.Event()

        def read_out(p=self.p):
            for line in p.stdout:
                self.lines.append(line.split())
                self.got.set()
            self.got.set()
        self.reader = threading.Thread(target=read_out, daemon=True)
        self.reader.start()
        if not self.got.wait(5.0) or not self.lines or self.lines[0][0] != "start":
            err = self._kill()
            raise SystemExit(f"dt_rec: no audio from '{self.dev}' within 5 s:\n{err}")
        return {"recorder": "dt_rec", "host_rec_launch": t, "host_rec_start": float(self.lines[0][1])}

    def _kill(self):
        p, self.p = self.p, None
        p.kill()
        p.wait()
        return p.stderr.read()      # stdout belongs to the reader thread

    def stop(self):
        """stop at this host time; returns dt_rec's counts (frames, overruns, ...) or {} for ffmpeg"""
        p, self.p = self.p, None
        if p is None:
            return {}
        if self.exe is None:
            if p.poll() is None:
                try:
                    p.communicate(b"q", timeout=10)
                except Exception:
                    p.kill()
            return {}
        try:
            p.stdin.write("q\n")
            p.stdin.close()
        except OSError:
            pass
        try:
            p.wait(timeout=10)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
        self.reader.join(2)
        err = p.stderr.read()
        end = next((ln for ln in self.lines if ln and ln[0] == "frames"), None)
        if end is None:
            print(f"!! dt_rec ended without its frame count (exit {p.returncode}):\n{err}")
            return {"error": err.strip(), "exit": p.returncode}
        st = {k: int(v) for k, v in zip(end[0:-2:2], end[1:-2:2])}
        st["host_rec_end"] = float(end[-1])
        st["exit"] = p.returncode
        if st["overruns"] or p.returncode:
            print(f"!! dt_rec: {st['overruns']} discontinuities (ring {st['ring']}, sample-time jumps "
                  f"{st['jumps']}, render errors {st['errors']}; {st['zero_filled']} frames zero-filled, "
                  f"{st['dropped']} dropped), exit {p.returncode}: this capture's timing is suspect\n{err}")
        return st


class Rig:
    def __init__(self, channel, out_dir):
        import rtmidi
        self.ch = channel
        self.out_dir = out_dir
        self.mo = rtmidi.MidiOut()
        ports = self.mo.get_ports()
        idx = [i for i, p in enumerate(ports) if DEVICE in p]
        if not idx:
            raise SystemExit(f"MIDI out '{DEVICE}' not found: {ports}")
        self.mo.open_port(idx[0])
        self.rec = Recorder(DEVICE)
        self.touched = set()

    def send(self, msg):
        self.mo.send_message(msg)

    def setp(self, params):
        for k, v in params.items():
            for m in dt_cc.encode(k, v, self.ch):
                self.send(m)
                time.sleep(0.002)
            self.touched.add(k)

    def panic(self):
        for m in dt_cc.all_notes_off(self.ch):
            self.send(m)
        self.send(dt_cc.note_off(NOTE, self.ch))

    def start_rec(self, wav):
        return self.rec.start(wav)

    def stop_rec(self):
        return self.rec.stop()

    def note(self, gate, log):
        t_on = time.monotonic()
        self.send(dt_cc.note_on(NOTE, VEL, self.ch))
        time.sleep(gate)
        t_off = time.monotonic()
        self.send(dt_cc.note_off(NOTE, self.ch))
        log.update(on=t_on, off=t_off)

    def marker(self, base, log_list):
        self.setp(MARKER)
        time.sleep(SETTLE)
        ev = {}
        self.note(MARK_GATE, ev)
        log_list.append(ev["on"])
        time.sleep(1.0)
        self.setp({k: base[k] for k in MARKER})


def sound_check(rig, out_dir, prefix=""):
    """one 0.5 s note recorded before the first capture: stop at once when the Digitone does not answer over
    USB MIDI or its USB audio is silent (a whole capture of silence otherwise)"""
    from dt_analyse import load_wav
    wav = os.path.join(out_dir, f"{prefix}_sound_check.wav")
    rig.start_rec(wav)
    try:
        time.sleep(1.0)
        rig.send(dt_cc.note_on(NOTE, VEL, rig.ch))
        time.sleep(0.5)
        rig.send(dt_cc.note_off(NOTE, rig.ch))
        time.sleep(1.0)
    finally:
        rig.stop_rec()
    x, _ = load_wav(wav)
    peak = 20 * math.log10(max(float(abs(x).max()), 1e-12))
    if peak < -60:
        raise SystemExit(f"sound check: peak {peak:.0f} dBFS from '{DEVICE}' after a note on channel {rig.ch}. "
                         "Check MIDI CONFIG > PORT CONFIG (INPUT FROM USB or MIDI+USB, RECEIVE NOTES and "
                         "RECEIVE CC/NRPN on), the track's channel (--channel), and that the track is not muted.")
    print(f"sound check: peak {peak:.0f} dBFS, ok")


def run(caps, args):
    os.makedirs(args.out, exist_ok=True)
    rig = Rig(args.channel, args.out)
    stop = {"flag": False}

    def on_sig(*_):
        stop["flag"] = True
        raise KeyboardInterrupt
    signal.signal(signal.SIGINT, on_sig)
    rig.panic()
    checked = False
    try:
        for c in caps:
            base = c["base"]
            rig.setp(base)          # first, so the check line below reads what was sent
            rig.touched.update(c["hand_keys"])
            print(f"\n== {c['name']}\nsent: {listed(base)}\nBY HAND: {hand_text(c)}\n"
                  f"CHECK on the Digitone: {check_line(c)}")
            if c["hand"] or not checked:     # the first capture (check the screen) and by-hand ones wait
                input("Press Enter when set (Ctrl-C to stop) ... ")
            else:
                print("nothing by hand: starting (Ctrl-C to stop)")
                time.sleep(1.0)
            if not checked:
                sound_check(rig, args.out, PREFIX[args.set])
                checked = True
            wav = os.path.join(args.out, f"{c['name']}.wav")
            meta = {"capture": c["name"], "group": c["group"], "set": args.set, "wav": os.path.basename(wav),
                    "sample_rate": 48000, "note": NOTE, "velocity": VEL, "channel": args.channel,
                    "hand": c["hand"], "check": check_line(c), "base": base, "markers": [], "steps": []}
            jpath = wav[:-4] + ".json"
            try:
                meta.update(rig.start_rec(wav))
                time.sleep(LEAD_IN)
                rig.marker(base, meta["markers"])
                for s in c["steps"]:
                    rig.setp(s["set"])
                    time.sleep(SETTLE)
                    ev = {k: v for k, v in s.items() if k != "set"}
                    ev["set"] = s["set"]
                    rig.note(s["gate"], ev)
                    time.sleep(s["tail"])
                    ev["end"] = time.monotonic()
                    meta["steps"].append(ev)
                    print(f"  {s['param']}={s['value']} done")
                    time.sleep(GAP)
                    rig.setp({k: base[k] for k in s["set"]})
                rig.marker(base, meta["markers"])
            finally:
                rig.panic()
                meta["rec_stats"] = rig.stop_rec()
                with open(jpath, "w") as f:
                    json.dump(meta, f, indent=1)
                print(f"  wrote {wav} + {os.path.basename(jpath)}")
    except KeyboardInterrupt:
        print("\nstopped: all notes off, recorder stopped, JSON written for the partial capture")
    finally:
        rig.panic()
        rig.stop_rec()
        restore = {k: INIT_RESTORE[k] for k in rig.touched if k in INIT_RESTORE}
        rig.setp(restore)
        print(f"sent init values back for {len(restore)} parameters: {sorted(restore)}")
        print("Not restored: the tempo. For an exact restore reload the track's sound/project without saving.")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--plan", action="store_true")
    g.add_argument("--run", action="store_true")
    ap.add_argument("--only", default="")
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--channel", type=int, default=1, help="track 1 MIDI channel (1-16)")
    ap.add_argument("--set", type=int, default=1, choices=sorted(PREFIX),
                    help="capture set: 1 = run 1 (default), 2 = run 2 (files r2_*)")
    a = ap.parse_args()
    caps = build_plan(a.set)
    if a.only:
        want = a.only.split(",")
        have = sorted({c["group"] for c in caps})
        bad = [w for w in want if w not in GROUPS]
        if bad:
            ap.error(f"unknown group(s) {bad}; choose from {GROUPS}")
        if "amp_hold" in want:
            print("amp_hold: the original Digitone has no AMP HOLD; skipped.")
        missing = [w for w in want if w not in have and w != "amp_hold"]
        if missing:
            print(f"not in set {a.set}: {missing} (set {a.set} has {have})")
        caps = [c for c in caps if c["group"] in want]
    if a.plan:
        print_plan(caps, a.set)
    else:
        run(caps, a)


if __name__ == "__main__":
    main()
