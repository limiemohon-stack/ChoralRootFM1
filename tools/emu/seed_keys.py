#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Chord Keys bits of an emulator flash file's settings object: a ChoralRoot block as an older firmware wrote it
(0.25.1 moves every such record to the FM-1 chord keys once: cr_settings.h CRS_KEYS_MOVED; tools/emu/test_persist.sh).

  python3 tools/emu/seed_keys.py FLASH AS       rewrite every valid copy's block as firmware AS wrote it, from what it
                                                holds: 0.25-classic (Chord Keys Classic), 0.25-fm1 (FM-1), 0.23 (no
                                                Chord Keys: sticky keeps LATCH alone); each without 0.25.1's mark (bit 1
                                                of leds), the version 15 as all three wrote it. Prints the copies changed
  python3 tools/emu/seed_keys.py FLASH          print "STICKY LEDS" (hex) of the newest valid copy (or "none")
The object: sectors 0xFC000 / 0xFD000, storage.c's 32-byte header (magic "FELU", type, slot, seq, len, the payload's
CRC-32 at +16, the header's at +28), the payload at +256: persist_t, whose last 192 bytes are cr_settings_t
(cr_settings.h): magic "CRS1", version at +4, the FNV-1a check over [12, 192) at +8, sticky at +28, leds at +157."""
import struct
import sys
import zlib

SECT = (0xFC000, 0xFD000)
STICKY, LEDS = 28, 157


def fnv(b):
    h = 2166136261
    for x in b:
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h


def copies(b):
    for o in SECT:
        magic, _typ, _slot, seq, ln = struct.unpack_from("<IHHII", b, o)
        pay = o + 256
        if magic != 0x554C4546 or ln < 192:
            continue
        if struct.unpack_from("<I", b, o + 16)[0] != (zlib.crc32(bytes(b[pay:pay + ln])) & 0xFFFFFFFF):
            continue
        c = pay + ln - 192
        if b[c:c + 4] != b"CRS1" or struct.unpack_from("<I", b, c + 8)[0] != fnv(b[c + 12:c + 192]):
            continue
        yield o, pay, seq, ln, c


def main():
    path = sys.argv[1]
    b = bytearray(open(path, "rb").read())
    if len(sys.argv) < 3:
        best = None
        for _o, _pay, seq, _ln, c in copies(b):
            if best is None or seq > best[0]:
                best = (seq, b[c + STICKY], b[c + LEDS])
        print("%02X %02X" % best[1:] if best else "none")
        return
    want = sys.argv[2]
    if want not in ("0.25-classic", "0.25-fm1", "0.23"):
        sys.exit("seed_keys.py: AS is 0.25-classic, 0.25-fm1 or 0.23")
    n = 0
    for o, pay, _seq, ln, c in list(copies(b)):
        s = b[c + STICKY]
        if want == "0.25-classic":
            s &= 0x7D                                   # (bit 1 clear: Classic; the other Options kept)
        elif want == "0.25-fm1":
            s = (s | 0x02) & 0x7F
        else:
            s &= 0x01                                   # (0.23: LATCH alone, its range 0 .. 1)
        b[c + STICKY] = s
        b[c + LEDS] &= 0x01                             # (LEDs Glow / Stock; no mark)
        struct.pack_into("<H", b, c + 4, 15)
        struct.pack_into("<I", b, c + 8, fnv(b[c + 12:c + 192]))
        struct.pack_into("<I", b, o + 16, zlib.crc32(bytes(b[pay:pay + ln])) & 0xFFFFFFFF)
        struct.pack_into("<I", b, o + 28, zlib.crc32(bytes(b[o:o + 28])) & 0xFFFFFFFF)
        n += 1
    open(path, "wb").write(b)
    print(n)


if __name__ == "__main__":
    main()
