#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The sound pool's migration and downgrade (0.22, docs/PRESETS.md "The pool"):
#   1. 377264f's firmware (the last of the 0.21 format: 32 slots, the engine stores), built from that commit by
#      `git archive` into build/scratch/pool/old377 (its emulator; its own build/gen, generated there), writes an image:
#      32 user sounds of the four engines with a store, each with its deep patch edited, favourites on slots 1, 6, 14
#      (typed Bass), 32 and on a factory preset (tools/emu/pool_legacy.c make); it lists what it sees (show)
#   2. this firmware's emulator powers on from a copy: the migration; tools/emu/pool_image.py compares the pool's slots
#      1..32 with the legacy slots byte for byte (record and patch) and the legacy sectors with the image before
#      (0x97000..0xB3FFF, 0xDC000..0xDFFFF: unchanged); its dump's favourites
#   3. a second power-on: no second migration (the pool image unchanged)
#   4. DOWNGRADE: 377264f's firmware on the migrated image lists the same 32 slots and the same marks as in 1
#   5. this firmware saves a sound into slot 100 and marks it (the section 0x81), then 377264f's firmware on that image:
#      its 32 slots and marks as before (it skips the section it does not know)
#   sh tools/emu/test_pool.sh
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
OLD=build/scratch/pool/old377
D=build/emu/test/pool
EMU=build/host/emu
fail=0
ok()  { echo "  ok    $*"; }
bad() { echo "  FAIL  $*"; fail=1; }
mkdir -p "$D"
if [ ! -x "$EMU" ] || [ -n "$(find tools/emu firmware/src tests/hostsim.c -newer "$EMU" -name '*.[ch]' 2>/dev/null | head -1)" ]; then
    sh tools/emu/build.sh || { echo "build failed"; exit 1; }
fi
if [ ! -f "$OLD/firmware/src/upreset.c" ]; then
    mkdir -p "$OLD" && git archive 377264f | tar -x -C "$OLD" || { echo "377264f: git archive failed"; exit 1; }
fi
mkdir -p "$OLD/build"
# 377264f's generated headers from ITS OWN generators (this tree's build/gen no longer has the shapes its sources
# read: the flash diet, HISTORY 3y126 .. 3y132, changed the fonts' kerning tables and packed CZ-1's factory tones)
export DYLD_FALLBACK_LIBRARY_PATH="${DYLD_FALLBACK_LIBRARY_PATH:-/opt/homebrew/lib}"   # (Pillow's Raqm; /bin/sh drops it)
[ -d "$OLD/build/gen" ] || (cd "$OLD" && python3 -c 'import sys; sys.path.insert(0, "tools"); import build; build.generate()') >/dev/null || exit 1
OABS=$(cd "$OLD" && pwd)
${CC:-cc} -std=gnu11 -O1 -w -I"$OLD/build/gen" -I"$OLD/firmware/src" -I"$OLD/tests" "-DEMU_TREE=$OABS" \
    -o "$D/pool_legacy" tools/emu/pool_legacy.c -lm || { echo "pool_legacy (377264f) build failed"; exit 1; }
[ -x "$OLD/build/host/emu" ] || sh "$OLD/tools/emu/build.sh" >/dev/null 2>&1 || { echo "377264f's emulator: build failed"; exit 1; }
show() { "$D/pool_legacy" show "$1" 2>/dev/null | grep -a '^U\|^marks'; }

echo "the sound pool: a 0.21-format image (377264f) migrated, and back"
rm -f "$D/legacy.bin" "$D/migrated.bin" "$D/down.bin" "$D/new100.bin"
"$D/pool_legacy" make "$D/legacy.bin" 2>/dev/null | grep -a '^make:' > "$D/make.txt"
show "$D/legacy.bin" > "$D/before.txt"
n=$(grep -c '^U' "$D/before.txt"); np=$(grep '^U' "$D/before.txt" | grep -vc 'patch -$')
grep -q 'make: 32 slots, 5 marks' "$D/make.txt" && [ "$n" = 32 ] && [ "$np" = 32 ] &&
    grep -q '^marks 5: U01 U06 U14(bass) U32 FM6:1$' "$D/before.txt" &&
    ok "377264f wrote 32 user sounds (8 FM6, 8 FM TONE, 8 VA, 8 CZ-1, each with its edited patch in its engine's store) and 5 marks: $D/legacy.bin" ||
    bad "the legacy image: $(cat "$D/make.txt") / $n slots, $np with a patch / $(grep marks "$D/before.txt")"
python3 tools/emu/pool_image.py legacy "$D/legacy.bin" > "$D/legacy.txt"
c0=$(python3 tools/emu/pool_image.py legacy-crc "$D/legacy.bin")

cp "$D/legacy.bin" "$D/migrated.bin"
printf 'wait 1500\ndump\nquit\n' > "$D/boot.txt"
"$EMU" --headless --flash "$D/migrated.bin" --script "$D/boot.txt" > "$D/new_1.log" 2>&1 || bad "this firmware on the legacy image: exit status"
python3 tools/emu/pool_image.py compare "$D/legacy.bin" "$D/migrated.bin" > "$D/compare.txt"
rc=$?
python3 tools/emu/pool_image.py pool "$D/migrated.bin" > "$D/pool.txt"
[ $rc = 0 ] && grep -q '^pool: migration 32$' "$D/new_1.log" &&
    ok "migration: slots 1..32 byte for byte as the legacy slots (record and patch), the legacy sectors unchanged ($c0): $D/pool.txt" ||
    bad "migration: $(cat "$D/compare.txt" | tr '\n' ' ') / $(grep -a '^pool:' "$D/new_1.log")"
grep -a '^  favs:' "$D/new_1.log" | grep -q 'favs: 5 marked.* : 128:0 128:5 128:13b 128:31 12:2$' &&
    ok "migration: the 5 marks as they were (slots 1, 6, 14 typed Bass, 32; FM6 03)" || bad "the marks: $(grep -a '^  favs:' "$D/new_1.log")"
cp "$D/migrated.bin" "$D/m1.bin"
"$EMU" --headless --flash "$D/migrated.bin" --script "$D/boot.txt" > "$D/new_2.log" 2>&1
python3 tools/emu/pool_image.py pool "$D/migrated.bin" > "$D/pool2.txt"
cmp -s "$D/pool.txt" "$D/pool2.txt" && ! grep -q '^pool: migration' "$D/new_2.log" &&
    python3 tools/emu/pool_image.py compare "$D/legacy.bin" "$D/migrated.bin" >/dev/null &&
    ok "a second power-on: no second migration, the pool as it was" || bad "second power-on: $(grep -a '^pool:' "$D/new_2.log")"

cp "$D/migrated.bin" "$D/down.bin"
show "$D/down.bin" > "$D/down.txt"
cmp -s "$D/before.txt" "$D/down.txt" &&
    ok "DOWNGRADE: 377264f on the migrated image sees its 32 slots (each record and patch CRC) and its 5 marks as before" ||
    bad "downgrade: $(diff "$D/before.txt" "$D/down.txt" | head -5 | tr '\n' ' ')"

# this firmware: a sound into slot 100, marked (section 0x81); then 377264f on it
cp "$D/migrated.bin" "$D/new100.bin"
cat > "$D/save100.txt" <<'EOF'
wait 600
btn SAVE
wait 300
btn OCT+
wait 300
btn OCT+
wait 900
btn SAVE
wait 300
knob KNOB4 +1
wait 300
btn OCT-
wait 300
btn SAVE 900
wait 3500
dump
quit
EOF
"$EMU" --headless --flash "$D/new100.bin" --script "$D/save100.txt" > "$D/new100.log" 2>&1
# (Save as new takes the first free slot, 33 here: the mark's section is 0x81 as for any slot above 32; a mark on slot 100
# itself is tests/cr_pool_test.c's)
grep -a '^save: part 0 slot U033 ' "$D/new100.log" >/dev/null && grep -a '^  favs:' "$D/new100.log" | grep -q ' 129:0 ' &&
    ok "this firmware: a sound saved into U033 (the first free slot), marked: section 0x81" ||
    bad "save into the pool: $(grep -a '^save:\|^fav:' "$D/new100.log" | tr '\n' ' ') / $(grep -a '^  favs:' "$D/new100.log")"
show "$D/new100.bin" > "$D/down100.txt"
cmp -s "$D/before.txt" "$D/down100.txt" &&
    ok "DOWNGRADE after a 0.22 save and a mark on U033: 377264f's 32 slots and 5 marks as before (the new section skipped)" ||
    bad "downgrade after 0.22 writes: $(diff "$D/before.txt" "$D/down100.txt" | head -5 | tr '\n' ' ')"
[ $fail = 0 ] && echo PASS || echo FAIL
exit $fail
