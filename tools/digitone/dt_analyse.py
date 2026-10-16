#!/usr/bin/env python3
"""Analyse dt_measure.py captures: envelope times per step, fitted T(value) curves.

  python3 tools/digitone/dt_analyse.py [DIR]     (default: dt_measure's --out)

For each <capture>.wav + .json: finds the two sync-marker onsets in the audio and
maps host time to audio time, then per step:
  attack          10/50/90 % rise times of the linear amplitude from note-on, the
                  98 % time, the level at half the 98 % time (0.5 = linear) and the
                  full ramp (t90 - t10) / 0.8. A rise that ends below the capture's
                  plateau (the gate was short) is fitted as a line and extrapolated.
  decay/release   T60 from the slope of a line fitted to dB vs time between -1 and
                  -50 dB below the start (above the capture's noise floor + 15 dB);
                  when that fall is short (< 30 ms or < 10 dB) from the -20/-40/-60 dB
                  times instead, measured from the peak (decay) or note-off (release)
  filt_*, op_*    the spectral-centroid track (log Hz; attacks through a 31 ms median):
                  10/50/90 % of its sweep. An attack that ends below the fastest step's
                  end level is measured against that level; if it never reaches 90 %,
                  T = its t50 x the median t90/t50 of the finished sweeps (the same
                  shape, scaled). No sweep (< 0.2 oct, value 0): T none, "instant"
  delay           time from note-on to 50 % of the centroid step
  fade            tremolo depth per LFO cycle (one-cycle windows; peak/trough in dB):
                  time to 90 % of the full depth (the FADE 0 step's, else the step's
                  own end); a fade that does not finish is extrapolated by a line
                  fitted to the linear swing (peak - trough), the fade being a ramp
Then per parameter (and tempo) fits T(v) = a*v^p and T(v) = a*10^(k v), keeps the
better (rms residual in log10), writes analysis.json and prints a table. PNG plots
if matplotlib is installed.
"""
import glob
import json
import math
import os
import sys

import numpy as np

HOP = 0.001     # envelope hop, s
WIN = 0.001     # envelope smoothing window, s


def read_riff(path):
    """(rate, channels, sample width, sample bytes, float?) of a WAV: integer PCM, IEEE float (dt_rec's
    32-bit float), also WAVE_FORMAT_EXTENSIBLE (ffmpeg's 24-bit header), which the wave module of Python
    3.11 refuses"""
    with open(path, "rb") as fh:
        d = fh.read()
    if d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a WAV file")
    i, fmt, data = 12, None, None
    while i + 8 <= len(d):
        cid, size = d[i:i + 4], int.from_bytes(d[i + 4:i + 8], "little")
        body = d[i + 8:i + 8 + size]
        if cid == b"fmt ":
            fmt = body
        elif cid == b"data":
            data = body
        i += 8 + size + (size & 1)
    if fmt is None or data is None:
        raise ValueError(f"{path}: no fmt or data chunk")
    tag = int.from_bytes(fmt[0:2], "little")
    if tag == 0xFFFE and len(fmt) >= 26:     # EXTENSIBLE: the sub-format GUID starts with the real tag
        tag = int.from_bytes(fmt[24:26], "little")
    ch, sr = int.from_bytes(fmt[2:4], "little"), int.from_bytes(fmt[4:8], "little")
    sw = int.from_bytes(fmt[14:16], "little") // 8
    return sr, ch, sw, data[:len(data) - len(data) % (ch * sw)], tag == 3


def load_wav(path):
    sr, ch, sw, raw, is_float = read_riff(path)
    if is_float:
        if sw != 4:
            raise ValueError(f"float sample width {sw}")
        x = np.frombuffer(raw, "<f4").astype(np.float64)
    elif sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
        x = (b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16))
        x = np.where(x >= 1 << 23, x - (1 << 24), x) / float(1 << 23)
    elif sw == 2:
        x = np.frombuffer(raw, "<i2") / 32768.0
    elif sw == 4:
        x = np.frombuffer(raw, "<i4") / 2147483648.0
    else:
        raise ValueError(f"sample width {sw}")
    return x.reshape(-1, ch).mean(axis=1), sr


def hilbert_mag(x, sr):
    """|analytic signal| with a 10.7 ms Blackman-windowed FIR Hilbert transformer.

    A finite kernel keeps the envelope local (a whole-file FFT Hilbert smears an
    abrupt stop across the file at -60 dB). Accurate above about 150 Hz."""
    taps = 513
    k = np.arange(taps) - taps // 2
    h = np.where(k % 2 == 1, 2.0 / (np.pi * np.where(k == 0, 1, k)), 0.0) * np.blackman(taps)
    n = len(x) + taps - 1
    nf = 1 << (n - 1).bit_length()
    y = np.fft.irfft(np.fft.rfft(x, nf) * np.fft.rfft(h, nf), nf)[taps // 2: taps // 2 + len(x)]
    return np.sqrt(x * x + y * y)


def rms_env(x, sr):
    """Amplitude envelope: FIR-Hilbert magnitude, 1 ms moving average, 1 ms hop.

    (A plain 5 ms RMS ripples by a few % on a 262 Hz tone: 1.3 periods per window.)
    The result is scaled to RMS units (peak / sqrt 2) for a sine."""
    hop, win = int(sr * HOP), int(sr * 0.001)
    a = hilbert_mag(x, sr) / np.sqrt(2)
    c = np.concatenate([[0.0], np.cumsum(a)])
    starts = np.arange(0, len(x) - win, hop)
    e = (c[starts + win] - c[starts]) / win
    t = (starts + win / 2) / sr
    return t, e


def db(a):
    return 20 * np.log10(np.maximum(a, 1e-9))


def centroid_track(x, sr, n=512, hop=48):
    w = np.hanning(n)
    f = np.fft.rfftfreq(n, 1 / sr)
    idx = np.arange(0, len(x) - n, hop)
    out = np.empty(len(idx))
    lev = np.empty(len(idx))
    for i, s in enumerate(idx):
        m = np.abs(np.fft.rfft(x[s:s + n] * w))
        p = m * m
        tot = p.sum()
        lev[i] = tot
        out[i] = (p * f).sum() / tot if tot > 0 else 0.0
    return (idx + n / 2) / sr, np.log2(np.maximum(out, 1.0)), lev


def find_onset(t, edb, t0, t1):
    sel = (t >= t0) & (t <= t1)
    if not sel.any():
        return None
    seg, ts = edb[sel], t[sel]
    floor = np.percentile(seg, 5)
    peak = seg.max()
    thr = max(floor + 10, peak - 30)    # floor + 10: a marker over an earlier release's tail
    i = np.argmax(seg >= thr)
    return float(ts[i])


def host_map(meta, t, edb):
    """Return f(host) -> audio seconds from the marker onsets."""
    # host time of audio sample 0: dt_rec reports it (host_rec_start); ffmpeg captures only have the
    # launch time. Either way it only places the first marker's search window.
    h0 = meta.get("host_rec_start", meta.get("host_ff_start", meta["markers"][0] - 1.5))
    found = []
    off = None   # audio - host offset, known after the first marker
    for m in meta["markers"]:
        exp = m - h0
        if off is None:
            on = find_onset(t, edb, max(0.0, exp - 1.4), exp + 0.6)
        else:   # later markers: narrow window so earlier notes' tails are not taken
            on = find_onset(t, edb, exp + off - 0.15, exp + off + 0.15)
        if on is not None:
            found.append((m, on))
            if off is None:
                off = on - exp
    if not found:
        return lambda h: h - h0, {"markers": 0}
    if len(found) == 1:
        (m, on), = found
        return lambda h: h - m + on, {"markers": 1, "offset": on - (m - h0)}
    (ma, oa), (mb, ob) = found[0], found[-1]
    a = (ob - oa) / (mb - ma)
    return (lambda h: oa + (h - ma) * a), {"markers": len(found), "rate": a,
                                         "offset": oa - (ma - h0)}


def crossing(ts, ys, level, rising=True):
    """First time ys crosses level (linear interpolation)."""
    ok = ys >= level if rising else ys <= level
    if not ok.any():
        return None
    i = int(np.argmax(ok))
    if i == 0:
        return float(ts[0])
    y0, y1 = ys[i - 1], ys[i]
    f = 0.0 if y1 == y0 else (level - y0) / (y1 - y0)
    return float(ts[i - 1] + f * (ts[i] - ts[i - 1]))


def noise_floor(t, edb, first_marker):
    """median level of the lead-in before the first marker (silence): the capture's noise floor"""
    if first_marker is None:
        return None
    sel = (t >= 0.1) & (t <= first_marker - 0.1)
    return float(np.median(edb[sel])) if sel.sum() > 50 else None


def medfilt(y, k=31):
    """running median, odd kernel k, edges padded with the median of the end half-kernels (not the end
    values: the first centroid frame is the onset click)"""
    if len(y) < 3:
        return y
    k = min(k, len(y) - (1 - len(y) % 2))
    h = k // 2
    yp = np.concatenate([np.full(h, np.median(y[:h + 1])), y, np.full(h, np.median(y[-h - 1:]))])
    return np.median(np.lib.stride_tricks.sliding_window_view(yp, k), axis=1)


PROXY = ("filt_decay", "filt_attack", "filt_release", "op_attack", "op_decay", "delay")
RISING = ("filt_attack", "op_attack", "delay")


def proxy_track(s, f, ct, cc, cl):
    """the step's centroid track (log2 Hz from its reference time), frames > 40 dB below its loudest
    dropped; attacks through a 31 ms median (run 1's filter attack crossed on noise bursts; a decay keeps
    the raw track, its value-0 sweep lasts a few ms)"""
    on, off, end = f(s["on"]), f(s["off"]), f(s["end"])
    ref_t = off if s["kind"] == "filt_release" else on
    stop = end if s["kind"] == "filt_release" else off
    sel = (ct >= ref_t) & (ct <= stop)
    if sel.any():  # drop frames more than 40 dB below the step's loudest (noise centroid)
        sel &= cl >= cl[sel].max() * 1e-4
    ys = medfilt(cc[sel]) if s["kind"] in ("filt_attack", "op_attack") else cc[sel]
    return ct[sel] - ref_t, ys


def slope_t60(ts, ys, ref, floor):
    """fit dB vs time between ref - 1 dB and max(ref - 50, floor + 15) dB (the first fall through that range);
    returns (T60, slope dB/s, span dB, rms dB, points) or None when the fall is short (< 30 ms or < 10 dB)"""
    hi, lo = ref - 1.0, max(ref - 50.0, floor + 15.0)
    below = np.nonzero(ys <= hi)[0]
    if hi - lo < 10 or not len(below):
        return None
    i0 = int(below[0])
    past = np.nonzero(ys[i0:] <= lo)[0]
    i1 = i0 + (int(past[0]) if len(past) else len(ys) - i0)
    tt, yy = ts[i0:i1], ys[i0:i1]
    if len(tt) < 30 or yy[0] - yy.min() < 10:
        return None
    A = np.vstack([np.ones_like(tt), tt]).T
    c, *_ = np.linalg.lstsq(A, yy, rcond=None)
    if c[1] >= 0:
        return None
    rms = float(np.sqrt(np.mean((A @ c - yy) ** 2)))
    return -60.0 / c[1], float(c[1]), float(yy[0] - yy.min()), rms, len(tt)


def analyse_step(s, f, t, e, edb, ct, cc, x, sr, cl=None, noise=None):
    on, off, end = f(s["on"]), f(s["off"]), f(s["end"])
    k = s["kind"]
    r = {"param": s["param"], "value": s["value"], "kind": k}
    for key in ("tempo", "display"):
        if key in s:
            r[key] = s[key]
    if k == "attack":
        sel = (t >= on) & (t <= off)
        ts, ys = t[sel] - on, e[sel]
        plat = np.median(ys[-max(5, len(ys) // 20):])
        r["plateau"] = float(plat)
        for p in (10, 50, 90, 98):
            r[f"t{p}"] = crossing(ts, ys, plat * p / 100)
        if r["t98"]:
            r["level_at_half_t98"] = float(np.interp(r["t98"] / 2, ts, ys) / plat)
        r["onset_audio"] = find_onset(t, edb, on - 0.05, off)
        r["onset_lag"] = None if r["onset_audio"] is None else r["onset_audio"] - on
        r["rise_full"] = (r["t90"] - r["t10"]) / 0.8 if r["t90"] is not None and r["t10"] is not None else None
        r["T"] = r["t90"]
    elif k in ("decay", "release"):
        ref_t = on if k == "decay" else off
        stop = off if k == "decay" else end
        sel = (t >= ref_t) & (t <= stop)
        ts, ys = t[sel] - ref_t, edb[sel]
        if k == "decay":
            ip = int(np.argmax(ys[: max(1, int(0.2 / HOP))]))
            ref = ys[ip]
        else:
            ref = float(np.median(edb[(t >= off - 0.02) & (t < off)]))
            ip = 0
        if noise is not None:
            floor = noise
        else:       # no lead-in: the level after the step (a release cut short by the gate is still sounding)
            floor = float(np.percentile(edb[(t >= end) & (t <= end + 0.5)], 50)) \
                if ((t >= end) & (t <= end + 0.5)).any() else -200
        r["ref_db"], r["floor_db"] = float(ref), floor
        t0 = float(ts[ip]) if len(ts) else 0.0     # decay: from the peak (a late onset does not count)
        for d in (20, 40, 60):
            c = crossing(ts[ip:], ys[ip:], ref - d, rising=False) if ref - d > floor + 3 else None
            r[f"t_{d}db"] = None if c is None else c - t0
        sl = slope_t60(ts[ip:], ys[ip:], ref, floor) if len(ts) else None
        if sl:
            r["T60_slope"], r["slope_db_s"], r["slope_span_db"], r["slope_rms_db"], _ = sl
            r["T"] = sl[0]
            r["T_from"] = f"slope over {sl[2]:.0f} dB"
        else:   # T60: measured, else extrapolated from the deepest point (exp. decay)
            for d in (60, 40, 20):
                if r[f"t_{d}db"]:
                    r["T"] = r[f"t_{d}db"] * 60 / d
                    r["T_from"] = f"-{d} dB"
                    break
            else:
                r["T"] = None
    elif k in PROXY:
        ts, ys = proxy_track(s, f, ct, cc, cl)
        if len(ys) < 4:
            r["T"] = None
            return r
        r["centroid_hz"] = [float(2 ** ys.max()), float(2 ** ys.min())]
        nfin = max(3, len(ys) // 20)
        n20 = max(3, int(0.02 * sr / 48))
        first = float(np.median(ys[:n20])) if k in RISING else float(ys[:n20].max())
        if k == "filt_release":
            first = float(ys[0])
        last = float(np.median(ys[-nfin:]))
        span = last - first
        r["first_oct"], r["last_oct"] = first, last
        r["span_oct"] = float(span)
        rising = span > 0
        for p in (10, 50, 90):
            r[f"t{p}"] = crossing(ts, ys, first + span * p / 100, rising=rising)
        r["T"] = r["t50"] if k == "delay" else r["t90"]
        if abs(span) < 0.2 and k != "delay":    # the crossings would be noise
            r["T"] = None
            r["note"] = "no sweep (< 0.2 oct): the end state from the start (instant at value 0)"
    elif k == "fade":
        r["T"] = None       # analysed per capture (fade_capture): the LFO period and the full depth
    return r


def attack_capture(steps, rs, f, t, e):
    """rises that end below the capture's plateau (the same sustain level for every step): fit a line to the
    rise above 10 % of that plateau and extrapolate"""
    plats = [r["plateau"] for r in rs if r["kind"] == "attack" and r.get("plateau")]
    if len(plats) < 2:
        return
    ref = max(plats)
    for s, r in zip(steps, rs):
        if r["kind"] != "attack" or r["plateau"] >= 0.97 * ref:
            continue
        on, off = f(s["on"]), f(s["off"])
        sel = (t >= on) & (t <= off)
        ts, ys = t[sel] - on, e[sel]
        m = ys >= 0.1 * ref
        if m.sum() < 50:
            continue
        b, a = np.polyfit(ts[m], ys[m], 1)
        if b <= 0:
            continue
        for p in (10, 50, 90, 98):
            r[f"t{p}"] = float((p / 100 * ref - a) / b)
        r["rise_full"] = float(ref / b)
        r["reached_pct"] = float(100 * r["plateau"] / ref)
        r["T"] = r["t90"]
        r["T_from"] = f"ramp fit (rise reached {r['reached_pct']:.0f} %)"


def proxy_capture(steps, rs, f, ct, cc, cl):
    """attack sweeps that end below the fastest step's end level (gate too short): measured against that
    level; if 90 % is not reached, T = t50 x the median t90/t50 of the finished sweeps"""
    for kind in ("filt_attack", "op_attack"):
        grp = [(s, r) for s, r in zip(steps, rs) if r["kind"] == kind and r.get("last_oct") is not None]
        if len(grp) < 2:
            continue
        ref = min(grp, key=lambda sr: sr[1]["value"])[1]["last_oct"]
        unfinished = []
        for s, r in grp:
            if ref - r["last_oct"] <= max(0.05, 0.05 * (ref - r["first_oct"])):
                continue
            ts, ys = proxy_track(s, f, ct, cc, cl)
            span = ref - r["first_oct"]
            if span <= 0:
                continue
            for p in (10, 50, 90):
                r[f"t{p}"] = crossing(ts, ys, r["first_oct"] + span * p / 100, rising=True)
            r["reached_pct"] = float(100 * (r["last_oct"] - r["first_oct"]) / span)
            r["T"] = r["t90"]
            r["T_from"] = f"against the fastest step's end (sweep reached {r['reached_pct']:.0f} %)"
            unfinished.append(r)
        ratios = [r["t90"] / r["t50"] for _, r in grp if r not in unfinished and r.get("T") and r.get("t50")
                  and r["t50"] >= 0.05]
        for r in unfinished:
            if r["t90"] is None and r.get("t50") and ratios:
                q = float(np.median(ratios))
                r["T"] = r["t50"] * q
                r["T_from"] = (f"t50 x {q:.2f} (median t90/t50 of the finished sweeps; this one reached "
                               f"{r['reached_pct']:.0f} %)")


def lfo_period(yd, lo=0.004, hi=1.0):
    """the tremolo period (s) from the autocorrelation of a dB envelope (HOP spaced), None when there is no
    clear cycle between lo and hi"""
    y = yd - np.mean(yd)
    n = len(y)
    if n < 20 or not np.any(y):
        return None
    nf = 1 << (2 * n - 1).bit_length()
    F = np.fft.rfft(y, nf)
    ac = np.fft.irfft(F * np.conj(F), nf)[:n]
    ac /= ac[0]
    i_lo, i_hi = max(2, int(lo / HOP)), min(n // 3, int(hi / HOP))
    if i_hi <= i_lo + 2:
        return None
    neg = np.nonzero(ac[i_lo:i_hi] < 0)[0]
    if not len(neg):
        return None
    j0 = i_lo + int(neg[0])
    j = j0 + int(np.argmax(ac[j0:i_hi]))
    if ac[j] < 0.5 or j >= i_hi - 1:
        return None
    if 0 < j < n - 1:      # parabolic peak
        a, b, c = ac[j - 1], ac[j], ac[j + 1]
        d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
        return (j + d) * HOP
    return j * HOP


def tremolo(ts, ys, period):
    """one-cycle windows every 1/8 cycle: (centre times, peak, trough) of the linear envelope"""
    n = max(3, int(round(period / HOP)))
    if len(ys) < n + 1:
        return np.array([]), np.array([]), np.array([])
    w = np.lib.stride_tricks.sliding_window_view(ys, n)[::max(1, n // 8)]
    tc = ts[: len(ys) - n + 1: max(1, n // 8)][: len(w)] + period / 2
    return tc, w.max(axis=1), w.min(axis=1)


def fade_capture(steps, rs, f, t, e, edb):
    """the LFO fade: tremolo depth per cycle against the full depth (the FADE 0 step's)"""
    idx = [i for i, r in enumerate(rs) if r["kind"] == "fade"]
    if not idx:
        return
    win = {}
    for i in idx:
        s = steps[i]
        on, off = f(s["on"]), f(s["off"])
        o = find_onset(t, edb, on - 0.05, min(off, on + 0.3))
        a = (o if o is not None else on) + 0.003
        sel = (t >= a) & (t <= off)
        win[i] = (t[sel] - on, e[sel], edb[sel])
    big = max(idx, key=lambda i: np.std(win[i][2]) if len(win[i][2]) else 0)
    period = lfo_period(win[big][2])
    if period is None:
        for i in idx:
            rs[i]["note"] = "no LFO cycle (4 ms .. 1 s) found: the fade is not measurable from this capture"
        return
    tracks = {}
    for i in idx:
        ts, ys, _ = win[i]
        tc, pk, tr = tremolo(ts, ys, period)
        tracks[i] = (tc, 20 * np.log10(np.maximum(pk, 1e-9) / np.maximum(tr, 1e-9)), pk - tr)
    ref = next((i for i in idx if rs[i].get("display") == 0), None)
    ref_db = ref_sw = None
    if ref is not None:
        tc, dd, sw = tracks[ref]
        m = tc >= 2 * period
        if m.sum() >= 3:
            ref_db, ref_sw = float(np.median(dd[m])), float(np.median(sw[m]))
    for i in idx:
        r = rs[i]
        tc, dd, sw = tracks[i]
        r["lfo_hz"] = 1.0 / period
        r["resolution_s"] = period / 2      # the first window's centre: a faster fade reads as this
        if len(tc) < 3:
            r["note"] = "gate shorter than three LFO cycles"
            continue
        nend = max(3, len(tc) // 10)
        end_db, end_sw = float(np.median(dd[-nend:])), float(np.median(sw[-nend:]))
        fin_db, fin_sw = (ref_db, ref_sw) if ref_db else (end_db, end_sw)
        r["final_depth_db"], r["end_depth_db"] = fin_db, end_db
        r["reached_pct"] = float(100 * end_sw / fin_sw) if fin_sw > 0 else None
        r["t90_db"] = crossing(tc, dd, 0.9 * fin_db) if fin_db > 0 else None
        sn = sw / fin_sw if fin_sw > 0 else sw * 0
        m = (tc >= period) & (sn >= 0.02) & (sn <= 0.85)
        r["t90_lin"] = r["t_full"] = None
        if m.sum() >= 4 and np.ptp(sn[m]) >= 0.03:
            b, a = np.polyfit(tc[m], sn[m], 1)
            if b > 0:
                r["t90_lin"], r["t_full"] = float((0.9 - a) / b), float((1.0 - a) / b)
        done = r["reached_pct"] is not None and r["reached_pct"] >= 90
        if done and r["t90_db"] is not None:
            r["T"], r["T_from"] = r["t90_db"], "measured (dB depth)"
            if r["t_full"] is None:
                r["t_full"] = r["t90_db"] / 0.9
        elif r["t90_lin"] is not None:
            r["T"] = r["t90_lin"]
            r["T_from"] = f"extrapolated: line fitted to the swing (reached {r['reached_pct']:.0f} %)"


def fit(points):
    pts = [(v, T) for v, T in points if T and T > 0 and v > 0]
    if len(pts) < 2:
        return None
    v = np.array([p[0] for p in pts], float)
    y = np.log10([p[1] for p in pts])
    out = {}
    A = np.vstack([np.ones_like(v), np.log10(v)]).T
    c, *_ = np.linalg.lstsq(A, y, rcond=None)
    res = float(np.sqrt(np.mean((A @ c - y) ** 2)))
    out["power"] = {"a": float(10 ** c[0]), "p": float(c[1]), "rms_log10": res,
                    "form": "T = a * v**p"}
    A = np.vstack([np.ones_like(v), v]).T
    c, *_ = np.linalg.lstsq(A, y, rcond=None)
    res = float(np.sqrt(np.mean((A @ c - y) ** 2)))
    out["exp"] = {"a": float(10 ** c[0]), "k": float(c[1]), "rms_log10": res,
                  "form": "T = a * 10**(k*v)"}
    out["best"] = min(("power", "exp"), key=lambda n: out[n]["rms_log10"])
    return out


def analyse_dir(d, plots=True):
    results, curves = [], {}
    for jp in sorted(glob.glob(os.path.join(d, "*.json"))):
        if os.path.basename(jp) == "analysis.json":
            continue
        meta = json.load(open(jp))
        wav = os.path.join(d, meta["wav"])
        if not os.path.exists(wav):
            continue
        x, sr = load_wav(wav)
        rs = meta.get("rec_stats") or {}
        if rs.get("overruns"):
            print(f"!! {meta['capture']}: the recorder reported {rs['overruns']} discontinuities "
                  f"({rs.get('zero_filled')} frames zero-filled, {rs.get('dropped')} dropped)")
        t, e = rms_env(x, sr)
        edb = db(e)
        f, minfo = host_map(meta, t, edb)
        minfo["recorder"] = meta.get("recorder", "ffmpeg")
        noise = noise_floor(t, edb, f(meta["markers"][0]) if meta["markers"] else None)
        minfo["noise_db"] = noise
        need_c = any(s["kind"] in PROXY for s in meta["steps"])
        ct, cc, cl = centroid_track(x, sr) if need_c else (None, None, None)
        rs = [analyse_step(s, f, t, e, edb, ct, cc, x, sr, cl, noise) for s in meta["steps"]]
        attack_capture(meta["steps"], rs, f, t, e)
        if need_c:
            proxy_capture(meta["steps"], rs, f, ct, cc, cl)
        fade_capture(meta["steps"], rs, f, t, e, edb)
        for r in rs:
            r["capture"] = meta["capture"]
            r["sync"] = minfo
            results.append(r)
    groups = {}
    for r in results:
        key = r["param"] + (f"@{r['tempo']}" if "tempo" in r else "")
        groups.setdefault(key, []).append(r)
    for key, rs in groups.items():
        pts = [(r["value"] if "display" not in r else abs(r["display"]), r.get("T")) for r in rs]
        curves[key] = {"kind": rs[0]["kind"], "points": pts, "fit": fit(pts)}
    out = {"steps": results, "curves": curves}
    with open(os.path.join(d, "analysis.json"), "w") as fh:
        json.dump(out, fh, indent=1)
    print_table(results, curves)
    if plots:
        make_plots(d, curves)
    return out


def _f(v):
    return "   -   " if v is None else f"{v:7.3f}"


def print_table(results, curves):
    print(f"{'capture':10s} {'param':10s} {'val':>4s} {'kind':12s} {'T (s)':>8s}  details")
    for r in results:
        det = {k: r[k] for k in ("t10", "t50", "t90", "rise_full", "t_20db", "t_40db", "t_60db",
                                 "slope_rms_db", "level_at_half_t98", "span_oct", "lfo_hz", "final_depth_db",
                                 "t_full") if k in r}
        det = " ".join(f"{k}={_f(v).strip()}" for k, v in det.items())
        det += "".join(f" [{r[k]}]" for k in ("T_from", "note") if k in r)
        print(f"{r['capture']:10s} {r['param']:10s} {r['value']:4d} {r['kind']:12s} "
              f"{_f(r.get('T'))}  {det}")
    print("\nfitted curves (rms residual in log10 units):")
    for k, c in curves.items():
        ft = c["fit"]
        if not ft:
            print(f"  {k:14s} not enough points")
            continue
        p, x = ft["power"], ft["exp"]
        print(f"  {k:14s} power a={p['a']:.4g} p={p['p']:.3f} (res {p['rms_log10']:.3f}) | "
              f"exp a={x['a']:.4g} k={x['k']:.4f} (res {x['rms_log10']:.3f}) -> {ft['best']}")


def make_plots(d, curves):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not installed: plots skipped")
        return
    for k, c in curves.items():
        pts = [(v, T) for v, T in c["points"] if T]
        if not pts:
            continue
        fig, ax = plt.subplots(figsize=(6, 4))
        ax.semilogy([p[0] for p in pts], [p[1] for p in pts], "o", label="measured")
        ft = c["fit"]
        if ft:
            vv = np.linspace(1, 127, 200)
            ax.semilogy(vv, ft["power"]["a"] * vv ** ft["power"]["p"], label="power")
            ax.semilogy(vv, ft["exp"]["a"] * 10 ** (ft["exp"]["k"] * vv), label="exp")
        ax.set_xlabel("value")
        ax.set_ylabel("time (s)")
        ax.set_title(k)
        ax.legend()
        fig.tight_layout()
        fig.savefig(os.path.join(d, f"curve_{k.replace('@', '_')}.png"), dpi=100)
        plt.close(fig)


if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from dt_measure import DEFAULT_OUT
    analyse_dir(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_OUT)
