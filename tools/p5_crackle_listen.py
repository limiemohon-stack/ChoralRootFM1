#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
"""The owner's crackle on P5 (HISTORY 3y44, docs/PROPHET.md "The crackle"): his gesture, to listen to.

    P5_SHIPPED=all sh tools/emu/build.sh && python3 tools/p5_crackle_listen.py [OUT-DIR]

OUT-DIR: ../audio-0.22/prophet/crackle by default (beside the repository: in a git worktree, name it). The emulator must be built with all 200 programs (his build: place
16 is Sequential's program 16, CANNED HAPPY); the tool stops when place 16 is another program. Every file is the
headless emulator's output (44.1 kHz, 16 bit, stereo), dry, no bass: minor + 6 + 9 on D4 held 1.5 s, the 6 let go and
the root struck again 100 ms after its release (minor + 9), and once more 30 ms after
(tools/emu/scripts/cr_prophet_crackle.txt).

  1_gesture.wav             as the host plays it: the firmware's own sound side, a unit that keeps up
  2_minor9_alone.wav        the second chord struck out of silence, the same three times (what 1 is compared with)
  3_shed_four_voices.wav    EMU_SHED_OVER=4: a unit on which four voices of this program do not fit; audio.c's shed
                            fades one voice of each chord (tools/emu/README.md "Overload, modelled")
  4_shed_three_voices.wav   EMU_SHED_OVER=3: nor three
  5_dual_core_on.wav        Options > Dual Core On (the emulator's second core runs the bass part's job inline)
  6_stale_every_other.wav   NOT the firmware's output: file 1 with, from the second chord's strike for 1.5 s, every
                            other half buffer (128 frames, 2.9 ms) replaced by the one two halves before it: what the
                            DAC plays when the audio ISR misses a boundary ("stale_halves"), as it would for as long
                            as a half took more than its 2902 us
  7_stale_one_in_eight.wav  the same with one half in eight, at random (a fixed seed)

6 and 7 are a model made by editing samples, to put a name to what was heard: the emulator makes no half late.

And the fault that WAS found on the way and fixed, which is not his crackle (docs/PROPHET.md "The click at a release's
end"): four stabs of minor + 9, 200 ms each (tools/emu/scripts/cr_prophet_relclick.txt), on three programs with a short
release, before and after:

  8_click_NNN_NAME_before.wav   74eae04's sound: a click 22 ms after every release (the part's DC blocker cut to 0)
  8_click_NNN_NAME_after.wav    3y44: the blocker discharges

The "before" files need an emulator with the old behaviour beside the other (skipped without it):
    P5_SHIPPED=all CC="clang -DP5_DC_REST=0x7FFFFFFF" sh tools/emu/build.sh && cp build/host/emu build/host/emu_p5cut
    P5_SHIPPED=all sh tools/emu/build.sh
Prints each file's peak, whether 1 and 5 are the same samples, and wavclicks.py's count for the click files."""
import os
import random
import struct
import subprocess
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EMU = ROOT / "build" / "host" / "emu"
SCRIPT = ROOT / "tools" / "emu" / "scripts" / "cr_prophet_crackle.txt"
HALF = 128
STRIKE2 = 4.76                    # the second chord's first strike (s), by the script's clock


def emu(script, wav, env=(), flash=None, exe=None):
    e = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", **dict(env))
    cmd = [str(exe or EMU), "--headless"] + (["--flash", str(flash)] if flash else []) + ["--script", str(script), "--wav", str(wav)]
    r = subprocess.run(cmd, capture_output=True, env=e, cwd=ROOT)
    return r.stdout.decode(errors="replace")


def frames(path):
    with wave.open(str(path), "rb") as w:
        assert w.getframerate() == 44100 and w.getnchannels() == 2 and w.getsampwidth() == 2
        return bytearray(w.readframes(w.getnframes()))


def save(path, data):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(bytes(data))


def peak(data):
    v = struct.unpack("<%dh" % (len(data) // 2), data)
    return max(abs(x) for x in v) / 32768.0


def stale(data, pick):
    """the halves pick(k) says, from the second strike for 1.5 s, replaced by the half two before"""
    out = bytearray(data)
    hb = HALF * 4
    k0, k1 = int(STRIKE2 * 44100) // HALF, int((STRIKE2 + 1.5) * 44100) // HALF
    n = 0
    for k in range(k0, k1):
        if pick(k):
            out[k * hb:(k + 1) * hb] = data[(k - 2) * hb:(k - 1) * hb]
            n += 1
    return out, n


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT.parent / "audio-0.22" / "prophet" / "crackle"
    out.mkdir(parents=True, exist_ok=True)
    tmp = ROOT / "build" / "emu" / "p5_crackle"
    tmp.mkdir(parents=True, exist_ok=True)
    text = SCRIPT.read_text().replace("@STEPS@", "15")
    gesture = tmp / "gesture.txt"
    gesture.write_text(text)
    alone = tmp / "alone.txt"                         # the same script without the 6 key: minor + 9 three times
    alone.write_text("\n".join(l for l in text.splitlines() if not l.startswith("btn F3")) + "\n")
    log = emu(gesture, out / "1_gesture.wav")
    if "sound: part 0 pos 16 CANNED HAPPY" not in log:
        sys.exit("p5_crackle_listen: place 16 is not CANNED HAPPY: build the emulator with P5_SHIPPED=all")
    emu(alone, out / "2_minor9_alone.wav")
    emu(gesture, out / "3_shed_four_voices.wav", {"EMU_SHED_OVER": "4"})
    emu(gesture, out / "4_shed_three_voices.wav", {"EMU_SHED_OVER": "3"})
    flash = tmp / "dualcore.bin"
    if flash.exists():
        flash.unlink()
    emu(ROOT / "tools" / "emu" / "scripts" / "cr_dualcore_on.txt", tmp / "seed.wav", flash=flash)
    log5 = emu(gesture, out / "5_dual_core_on.wav", flash=flash)
    one = frames(out / "1_gesture.wav")
    d6, n6 = stale(one, lambda k: k & 1)
    save(out / "6_stale_every_other.wav", d6)
    rnd = random.Random(1644)
    d7, n7 = stale(one, lambda k: rnd.random() < 0.125)
    save(out / "7_stale_one_in_eight.wav", d7)
    said = []
    rel = (ROOT / "tools" / "emu" / "scripts" / "cr_prophet_relclick.txt").read_text()
    cut = ROOT / "build" / "host" / "emu_p5cut"
    for place, name in ((88, "dirty_lead"), (168, "perc_organ"), (174, "filter_mod")):
        sp = tmp / f"relclick_{place}.txt"
        sp.write_text(rel.replace("@STEPS@", str(place - 1)))
        for tag, exe in (("before", cut), ("after", EMU)):
            if not exe.exists():
                said.append(f"8_click_{place:03d}_{name}_{tag}.wav: not made (no {exe.name}: see the tool's text)")
                continue
            w = out / f"8_click_{place:03d}_{name}_{tag}.wav"
            emu(sp, w, exe=exe)
            r = subprocess.run([sys.executable, str(ROOT / "tools" / "emu" / "wavclicks.py"), str(w), "--from", "3.3", "--both-sides"],
                               capture_output=True, text=True).stdout.strip().splitlines()[-1]
            said.append(r.replace("wavclicks: ", "").split(": 0 jumps")[0] + ": " + r.split("mid-sound, ")[1].split(" (")[0] +
                        " after the first attack (wavclicks.py --from 3.3 --both-sides)")
    for f in sorted(out.glob("*.wav")):
        said.append(f"{f.name}: {len(frames(f)) / 4 / 44100:.1f} s, peak {peak(frames(f)):.3f}")
    said.append(f"6: {n6} halves replaced; 7: {n7} halves replaced")
    said.append("1 and 5 (Dual Core On, 'dualcore 1' in its settings: %s): %s" % (
        "yes" if " dualcore 1 " in log5 else "NO", "the same samples" if one == frames(out / "5_dual_core_on.wav") else "DIFFERENT samples"))
    print("\n".join(said))
    (out / "README.txt").write_text(README + "\n".join(said) + "\n")


README = """P5, the owner's crackle (ChoralRoot FM-1, HISTORY 3y44; docs/PROPHET.md "The crackle")

The owner, on the unit (74eae04 with all 200 programs, Options > Dual Core On):
  "On P5 though, preset 16 I was able to create some kind of glitch by using minor + 6 + 9 then playing chord again
   as minor + 9 which had a long crackling effect like the voice got stuck in a washing machine or something. All
   dry, no bass on."

THERE IS NO BEFORE AND AFTER OF THE CRACKLE: it did not come about on the host or in the emulator, so it was not
fixed. Files 1 to 7 are the gesture as the firmware's own sound side plays it when the unit keeps up, and what the
two kinds of overload would sound like, to put a name to what was heard. Files 8 are another fault, found on the way
and fixed: those have a before and an after.

Every file: the headless emulator built with all 200 programs, P5 place 16 (Sequential's program 16, CANNED HAPPY),
the three sends at 0, no bass, 44.1 kHz 16 bit stereo. MIN + 6 + 9 with the root D4 held 1.5 s (Dm6/9: five notes
for four voices) at 3.16 s; the 6 let go and the root struck again 100 ms after its release (Dm9) at 4.76 s; and once
more 30 ms after its release at 6.29 s (tools/emu/scripts/cr_prophet_crackle.txt).

  1_gesture.wav             as the host plays it
  2_minor9_alone.wav        minor + 9 three times at the same moments, the first out of silence: what 1 is compared with
  3_shed_four_voices.wav    a unit on which four voices of this program do not fit, modelled (EMU_SHED_OVER=4): the
                            firmware's overload shed fades one voice 6 ms after each strike
  4_shed_three_voices.wav   nor three (EMU_SHED_OVER=3): two voices
  5_dual_core_on.wav        Options > Dual Core On, as the emulator has it (the second core's job run inline: real
                            concurrency is not modelled)
  6_stale_every_other.wav   NOT the firmware's output: file 1 with, for 1.5 s from the second strike (4.76 s), every
                            other half buffer (128 frames, 2.9 ms) replaced by the one two halves before it. That is
                            what the DAC plays when the audio interrupt misses a boundary (--debug: stale_halves)
  7_stale_one_in_eight.wav  the same with one half in eight, at random

Which is it like? 3 / 4: a note dropping out of the chord. 6 / 7: a buzz or a rattle over the chord, "stuck".
6 and 7 were made by editing samples (tools/p5_crackle_listen.py): the emulator makes no half late.

THE CLICK AT A RELEASE'S END (found and fixed in 3y44; docs/PROPHET.md "The click at a release's end"). Not the
crackle: CANNED HAPPY's release is too long to have it. Four stabs of minor + 9 on D4, 200 ms each, struck at 3.16,
4.16, 5.16 and 6.16 s, dry, on three programs with a short release:

  8_click_088_dirty_lead_before.wav / _after.wav
  8_click_168_perc_organ_before.wav / _after.wav
  8_click_174_filter_mod_before.wav / _after.wav

before: 74eae04's sound (the emulator built with -DP5_DC_REST=0x7FFFFFFF): a click 22 ms after every release, in the
silence: the part's DC blocker (the output's coupling capacitor, 5 Hz) was set to 0 in the block after the part's
last voice ended, with all it still held: up to 8 dB under the chord's peak on DIRTY LEAD. after: it discharges.
Listen to the moment just after each stab.

Made by: P5_SHIPPED=all sh tools/emu/build.sh && python3 tools/p5_crackle_listen.py (and its text for the "before")

"""


if __name__ == "__main__":
    main()
