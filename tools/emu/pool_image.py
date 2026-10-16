#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The user sounds in an emulator flash image (tools/emu/test_pool.sh; docs/PRESETS.md "The pool").

  pool_image.py legacy IMAGE         the 0.21-format slots (OBJ_UPRESET0/1 + the engine stores): one line a used slot
  pool_image.py pool IMAGE           the pool's slots (0.22: storage.c sp_*, upool.c's slot layout): one line a used slot
  pool_image.py compare OLD NEW      NEW's pool slots 1..32 == OLD's legacy slots byte for byte (record + patch), and the
                                     legacy sectors of NEW byte-identical to OLD's; exit 1 otherwise
  pool_image.py legacy-crc IMAGE     the CRC-32 of the legacy sectors (0x97000..0xB3FFF, 0xDC000..0xDFFFF)

A line: slot (1-based), engine, name, the record's CRC-32, the patch's engine and length and CRC-32."""
import struct
import sys
import zlib

MAGIC = 0x554C4546
SECTOR = 4096
PAYLOAD = 256
OBJ_VASTORE, OBJ_FM6S0, OBJ_FM6S1, OBJ_CZS0, OBJ_UP0, OBJ_UP1, OBJ_CZS1, OBJ_QUAD = 1, 2, 3, 4, 5, 6, 8, 17
ADDR = {OBJ_VASTORE: 0x97000, OBJ_FM6S0: 0x99000, OBJ_FM6S1: 0x9B000, OBJ_CZS0: 0x9D000, OBJ_UP0: 0xDC000,
        OBJ_UP1: 0xDE000, OBJ_CZS1: 0xA0000, OBJ_QUAD: 0xB2000}
ENGI_VA, ENGI_FM6, ENGI_CZ, ENGI_QUAD, ENGI_P5 = 13, 12, 14, 15, 16
BLOB = {ENGI_VA: 110, ENGI_FM6: 128, ENGI_CZ: 144, ENGI_QUAD: 80, ENGI_P5: 138}
NAMES = {ENGI_VA: 'VA', ENGI_FM6: 'FM6', ENGI_CZ: 'CZ-1', ENGI_QUAD: 'FM TONE', ENGI_P5: 'P5', 1: 'DIGITAL', 0: 'ANALOG'}
SP_BASE, SP_NSEC, SP_NBANK, SP_PER, SP_SLOT, SP_TYPE = 0xB8000, 16, 13, 10, 384, 0x4C50
LEGACY = [(0x97000, 0xB4000), (0xDC000, 0xE0000)]


def head(img, base, typ, slot, pool=False):
    h = img[base:base + 32]
    magic, t, s, seq, ln, crc, r0, r1, hcrc = struct.unpack('<IHHIIIIII', h)
    if magic != MAGIC or t != typ or zlib.crc32(h[:28]) != hcrc or ln > SECTOR - PAYLOAD:
        return None
    if (not pool and s != slot) or (pool and (s >= SP_NBANK or ln != SECTOR - PAYLOAD or r0 != 1)):
        return None
    body = img[base + PAYLOAD:base + PAYLOAD + ln]
    if zlib.crc32(body) != crc:
        return None
    return seq, s, body


def newer(a, b):
    return a != b and ((a - b) & 0xFFFFFFFF) < 0x80000000


def load(img, obj):
    """storage.c st_current: the newest valid copy's payload, None"""
    a = head(img, ADDR[obj], obj, 0)
    b = head(img, ADDR[obj] + SECTOR, obj, 1)
    if b and (not a or newer(b[0], a[0])):
        return b[2]
    return a[2] if a else (b[2] if b else None)


def rec_valid(r):
    used, ver, eng, np_ = r[0], r[1], r[2], r[3]
    if not (used == 0xA5 and 1 <= ver <= 5 and eng < 17 and 8 <= np_ <= (144 if ver >= 4 else 72) and r[4]):
        return False
    return not (ver >= 4 and any(r[16 + i] > 191 for i in range(np_)))


def legacy(img):
    """{slot: (record bytes, patch engine, patch bytes)} as 0.21 holds them"""
    out = {}
    banks = [load(img, OBJ_UP0), load(img, OBJ_UP1)]
    va = load(img, OBJ_VASTORE)
    fm6 = [load(img, OBJ_FM6S0), load(img, OBJ_FM6S1)]
    cz = [load(img, OBJ_CZS0), load(img, OBJ_CZS1)]
    quad = load(img, OBJ_QUAD)
    for k in range(32):
        bk = banks[k // 16]
        if not bk or len(bk) != 8 + 16 * 192 or struct.unpack('<IHH', bk[:8]) != (0x31425055, 192, 16):
            continue
        r = bk[8 + (k % 16) * 192:8 + (k % 16) * 192 + 192]
        if not rec_valid(r):
            continue
        e, patch = r[2], b''

        def bit(store, i):
            return store and (struct.unpack('<I', store[12:16] if store is not va and store is not quad else store[8:12])[0] >> i) & 1
        if e == ENGI_VA and va and len(va) == 16 + 32 * 110 and struct.unpack('<HH', va[4:8]) == (3, 32) and bit(va, k):
            patch = va[16 + k * 110:16 + k * 110 + 110]
        elif e == ENGI_FM6 and fm6[k // 16] and bit(fm6[k // 16], k % 16):
            patch = fm6[k // 16][16 + (k % 16) * 128:16 + (k % 16) * 128 + 128]
        elif e == ENGI_CZ and cz[k // 16] and bit(cz[k // 16], k % 16):
            patch = cz[k // 16][16 + (k % 16) * 144:16 + (k % 16) * 144 + 144]
        elif e == ENGI_QUAD and quad and bit(quad, k):
            patch = quad[16 + k * 80:16 + k * 80 + 80]
        out[k + 1] = (r, e if patch else 0, patch)
    return out


def pool(img):
    """{slot: (record bytes, patch engine, patch bytes)} of the pool (0.22)"""
    cand = {}
    for s in range(SP_NSEC):
        h = head(img, SP_BASE + s * SECTOR, SP_TYPE, 0, pool=True)
        if h:
            seq, b, body = h
            if b not in cand or newer(seq, cand[b][0]):
                cand[b] = (seq, body)
    out = {}
    for b, (_, body) in cand.items():
        for j in range(SP_PER):
            sl = body[j * SP_SLOT:(j + 1) * SP_SLOT]
            r = sl[:192]
            if b * SP_PER + j >= 128 or not rec_valid(r):
                continue
            pe, pn = sl[192], sl[193]
            out[b * SP_PER + j + 1] = (r, pe - 1 if pe else 0, sl[196:196 + pn] if pe else b'')
    return out


def show(slots):
    for k in sorted(slots):
        r, pe, p = slots[k]
        name = r[4:16].split(b'\0')[0].decode('ascii', 'replace')
        print('U%03u %-7s %-12s rec %08x patch %s %u %08x' % (k, NAMES.get(r[2], str(r[2])), name, zlib.crc32(r),
                                                             NAMES.get(pe, '-') if pe else '-', len(p), zlib.crc32(p)))


def legacy_crc(img):
    return ' '.join('%05x..%05x %08x' % (a, b - 1, zlib.crc32(img[a:b])) for a, b in LEGACY)


def main():
    cmd = sys.argv[1]
    img = open(sys.argv[2], 'rb').read()
    if cmd == 'legacy':
        show(legacy(img))
    elif cmd == 'pool':
        show(pool(img))
    elif cmd == 'legacy-crc':
        print(legacy_crc(img))
    elif cmd == 'compare':
        new = open(sys.argv[3], 'rb').read()
        old, got = legacy(img), pool(new)
        bad = 0
        for k in range(1, 33):
            if old.get(k) != got.get(k):
                print('slot U%03u differs: legacy %s, pool %s' % (k, 'used' if k in old else 'empty', 'used' if k in got else 'empty'))
                bad += 1
        extra = [k for k in got if k > 32]
        if extra:
            print('pool slots above 32 used:', extra)
            bad += 1
        if legacy_crc(img) != legacy_crc(new):
            print('legacy sectors changed:', legacy_crc(img), '->', legacy_crc(new))
            bad += 1
        print('compare: %u legacy slots, %u pool slots, %s' % (len(old), len(got), 'identical' if not bad else '%u differences' % bad))
        sys.exit(1 if bad else 0)
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
