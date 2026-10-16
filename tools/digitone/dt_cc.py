#!/usr/bin/env python3
"""Elektron Digitone (original, OS 1.4x) MIDI CC / NRPN map for one synth track.

Source: Digitone User Manual OS 1.44 (260930), "APPENDIX B: MIDI IMPLEMENTATION",
tables B.1-B.6:
  https://www.elektron.se/wp-content/uploads/2026/10/Digitone-User-Manual_ENG_OS1.44_260930.pdf
(linked from https://www.elektron.se/en/digitone ; the tables are unchanged across 1.4x.)

Channels (manual 14.5.3): synth tracks listen on TRACK 1-4 CHANNEL (factory 1-4;
the manual does not state the default, check SETTINGS > MIDI CONFIG > CHANNELS);
the AUTO channel (default 10) reaches the active track. SETTINGS > MIDI CONFIG >
PORT CONFIG > RECEIVE CC/NRPN and RECEIVE NOTES must be on, and INPUT FROM must
include USB.

Parameters with a CC LSB column are 14-bit; they are sent as NRPN (CC99 MSB,
CC98 LSB, CC6 value MSB, CC38 value LSB) so the fine part is set to 0 too.
7-bit parameters are sent as plain CCs. Bipolar parameters are assumed to take
64 = 0, except HARM: HARM 0.00 is coarse 63 (HARM_ZERO; proved on the owner's
device 2026-10-14, CC6 = 63 / CC38 = 0 shows 0.00, 64 shows non-zero; the .dnsnd
record stores HARM 0.00 as 63 too). LFO1 SPD 64 = 0 is proved by run 1 (SPD 96
= +32.00: the SQR period was 7.99 s at 120 BPM, 11.99 s at 80, MULT 1). The rest
(MIX, ENV depth, PAN, LFO DEP, LFO FADE) are not proved.

List parameters (LISTS below) take the value the Digitone shows, not 0..127. How
the Digitone reads a plain 7-bit CC on the track channel for them was PROVED on the
owner's device on 2026-10-14 (his test, OS 1.44): the CC value is the position in
the parameter's list.
  ALGO, CC 90:        algorithm - 1 (sent 3 -> ALGO 4, sent 6 -> ALGO 7).
  FILTER TYPE, CC 74: OFF, LP2, HP, LP4 (sent 2 -> HP).
  RATIO C, CC 91:     index into RATIO_CB = 0.25 0.5 0.75 1 2 3 .. 16 (sent 10 -> 8;
                      web/fm1dnsnd.js DN_RATIO_CB).
  RATIO A, CC 92:     index into RATIO_A (web/fm1dnsnd.js DN_RATIO_A; sent 5 -> 1.5,
                      sent 15 -> 4.00). Steps 16..24 and 26..34 are interpolated in that
                      table, not proved: refused here.
  LFO1 MULT, CC 108:  index into LFO_MULTS = 1 2 4 8 16 .. 2k, F1 .. F2k (sent 4 -> 16;
                      eng_quad.c N_QUAD_MULT; only the first entries were seen). Index 0
                      ("1") is tempo-synced: run 1's LFO period scaled exactly with the
                      tempo, matching the manual's table (cycle = 128 / (SPD x MULT) whole
                      notes). F1 .. F2k (index 12 ..) as the fixed-120-BPM half is assumed.
  LFO1 WAVE, CC 111:  TRI SIN SQR .. (sent 2 -> SQR; the rest in the manual's order).
  LFO1 MODE, CC 113:  FRE TRG HLD ONE HLF (sent 1 -> TRG).
  LFO1 DEST, CC 110:  NOT a list position (sent 5 -> "Ratio C", sent 66 -> "Amp
                      Attack"): unsolved. Only NONE = 0 is sent (the first entry under
                      any scheme, assumed); any other destination is set by hand.
  LFO2's CCs (114 MULT, 117 WAVE, 119 MODE, 116 DEST) are assumed to work as LFO1's.

Still set by hand: RATIO B (NRPN 1/75, not probed), any LFO DEST but NONE, tempo.
The original Digitone has no AMP HOLD.
"""

# name: (cc_msb, cc_lsb or None, nrpn_msb, nrpn_lsb)
PARAMS = {
    # B.1 track / B.2 trig
    "track_level": (95, None, 1, 100),
    "velocity": (4, None, 3, 1),
    # B.3 SYN1
    "algo": (90, None, 1, 72),
    "ratio_c": (91, None, 1, 73),
    "ratio_a": (92, None, 1, 74),
    "ratio_b": (16, 48, 1, 75),
    "harm": (17, 49, 1, 76),
    "dtun": (18, 50, 1, 77),
    "fdbk": (19, 51, 1, 78),
    "mix": (20, 52, 1, 79),
    # B.3 SYN2
    "a_atk": (75, None, 1, 80),
    "a_dec": (76, None, 1, 81),
    "a_end": (77, None, 1, 82),
    "a_lev": (78, None, 1, 83),
    "b_atk": (79, None, 1, 84),
    "b_dec": (80, None, 1, 85),
    "b_end": (81, None, 1, 86),
    "b_lev": (82, None, 1, 87),
    "a_del": (83, None, 1, 88),
    "a_trig": (84, None, 1, 89),
    "b_del": (86, None, 1, 91),
    "b_trig": (87, None, 1, 92),
    # B.4 FILTER
    "f_freq": (23, 55, 1, 20),
    "f_reso": (24, 56, 1, 21),
    "f_type": (74, None, 1, 22),
    "f_atk": (70, None, 1, 16),
    "f_dec": (71, None, 1, 17),
    "f_sus": (72, None, 1, 18),
    "f_rel": (73, None, 1, 19),
    "f_env": (25, 57, 1, 23),
    "f_del": (43, None, 1, 66),
    "f_base": (26, 58, 1, 24),
    "f_width": (27, 59, 1, 25),
    # B.5 AMP
    "amp_atk": (104, None, 1, 32),
    "amp_dec": (105, None, 1, 33),
    "amp_sus": (106, None, 1, 34),
    "amp_rel": (107, None, 1, 35),
    "drive": (9, 41, 1, 36),
    "pan": (10, 42, 1, 37),
    "vol": (7, 39, 1, 38),
    "chr_send": (12, 44, 1, 41),
    "del_send": (13, 45, 1, 40),
    "rev_send": (14, 46, 1, 39),
    # B.6 LFO
    "lfo1_spd": (28, 60, 1, 48),
    "lfo1_mult": (108, None, 1, 49),
    "lfo1_fade": (109, None, 1, 50),
    "lfo1_dest": (110, None, 1, 51),
    "lfo1_wave": (111, None, 1, 52),
    "lfo1_mode": (113, None, 1, 54),
    "lfo1_dep": (29, 61, 1, 55),
    "lfo2_spd": (30, 62, 1, 57),
    "lfo2_mult": (114, None, 1, 58),
    "lfo2_fade": (115, None, 1, 59),
    "lfo2_dest": (116, None, 1, 60),
    "lfo2_wave": (117, None, 1, 61),
    "lfo2_mode": (119, None, 1, 63),
    "lfo2_dep": (31, 63, 1, 64),
}

# Parameters this tool never sends: the owner sets them by hand (and any LFO DEST but
# NONE: list_value refuses it).
HAND_ONLY = {"ratio_b"}

# HARM 0.00 = NRPN 1/76 coarse 63 (not 64): proved on the owner's device 2026-10-14 (the
# screen showed HARM non-zero with 64 all through run 1); the .dnsnd record stores 0.00 as 63.
HARM_ZERO = 63

# The list parameters' tables (the docstring: proved 2026-10-14)
F_TYPES = ["OFF", "LP2", "HP", "LP4"]
RATIO_CB = [0.25, 0.5, 0.75, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]
RATIO_A = [round((i + 1) / 4 if i <= 15 else 4 + (i - 15) * 0.4 if i <= 25 else 8 + (i - 25) * 0.8, 2)
           for i in range(36)]
RATIO_A_PROVED = set(range(16)) | {25, 35}
LFO_MULTS = ["1", "2", "4", "8", "16", "32", "64", "128", "256", "512", "1K", "2K",
             "F1", "F2", "F4", "F8", "F16", "F32", "F64", "F128", "F256", "F512", "F1K", "F2K"]
LFO_WAVES = ["TRI", "SIN", "SQR", "SAW", "EXP", "RMP", "RND"]
LFO_MODES = ["FRE", "TRG", "HLD", "ONE", "HLF"]


def _pos(name, table, value):
    key = str(value).strip().upper()
    if key not in table:
        raise ValueError(f"{name}={value!r} not in {table}")
    return table.index(key)


def _ratio(name, table, value, proved=None):
    i = next((i for i, r in enumerate(table) if abs(r - float(value)) < 1e-6), None)
    if i is None:
        raise ValueError(f"{name}={value} is not a step of {table}")
    if proved is not None and i not in proved:
        raise ValueError(f"{name}={value} (step {i}) is not a proved step: set it by hand")
    return i


def _dest(name, value):
    if str(value).strip().upper() != "NONE":
        raise ValueError(f"{name}={value!r}: only NONE is known over CC; set the destination by hand")
    return 0


def _algo(value):
    if int(value) != value or not 1 <= value <= 8:
        raise ValueError(f"algo={value} out of 1..8")
    return int(value) - 1


# name: the displayed value -> the CC value
LISTS = {
    "algo": _algo,
    "f_type": lambda v: _pos("f_type", F_TYPES, v),
    "ratio_c": lambda v: _ratio("ratio_c", RATIO_CB, v),
    "ratio_a": lambda v: _ratio("ratio_a", RATIO_A, v, RATIO_A_PROVED),
}
for _k in (1, 2):
    LISTS[f"lfo{_k}_mult"] = lambda v, n=f"lfo{_k}_mult": _pos(n, LFO_MULTS, v)
    LISTS[f"lfo{_k}_wave"] = lambda v, n=f"lfo{_k}_wave": _pos(n, LFO_WAVES, v)
    LISTS[f"lfo{_k}_mode"] = lambda v, n=f"lfo{_k}_mode": _pos(n, LFO_MODES, v)
    LISTS[f"lfo{_k}_dest"] = lambda v, n=f"lfo{_k}_dest": _dest(n, v)


def encode(name, value, channel=1):
    """Return a list of 3-byte MIDI messages setting `name` to `value`: an integer 0..127,
    or for a list parameter (LISTS) the value the Digitone shows (algo 4, f_type "HP",
    ratio_c 8, lfo1_wave "SQR" ...).

    channel is 1-based. 14-bit parameters go out as NRPN with the fine byte 0.
    """
    if name in HAND_ONLY:
        raise ValueError(f"{name} must be set by hand")
    msb, lsb, nm, nl = PARAMS[name]
    v = LISTS[name](value) if name in LISTS else int(value)
    if not 0 <= v <= 127:
        raise ValueError(f"{name}={value} out of 0..127")
    st = 0xB0 | ((channel - 1) & 0x0F)
    if lsb is None:
        return [[st, msb, v]]
    return [[st, 99, nm], [st, 98, nl], [st, 6, v], [st, 38, 0]]


def note_on(note, vel, channel=1):
    return [0x90 | (channel - 1), note, vel]


def note_off(note, channel=1):
    return [0x80 | (channel - 1), note, 0]


def all_notes_off(channel=1):
    st = 0xB0 | (channel - 1)
    return [[st, 123, 0], [st, 120, 0]]


EXAMPLE = {"algo": 4, "f_type": "HP", "ratio_c": 8, "ratio_a": 1.5, "lfo1_mult": 16, "lfo2_mult": 16,
           "lfo1_wave": "SQR", "lfo2_wave": "SQR", "lfo1_mode": "TRG", "lfo2_mode": "TRG",
           "lfo1_dest": "NONE", "lfo2_dest": "NONE"}

if __name__ == "__main__":
    for n in sorted(PARAMS):
        tag = "HAND" if n in HAND_ONLY else "LIST" if n in LISTS else (
            "NRPN" if PARAMS[n][1] is not None else "CC")
        v = EXAMPLE.get(n, 100)
        msgs = "" if n in HAND_ONLY else f"{v}: " * (n in LISTS) + " ".join(
            "".join(f"{b:02X}" for b in m) for m in encode(n, v))
        print(f"{n:11s} {tag:4s} {PARAMS[n]}  {msgs}")
