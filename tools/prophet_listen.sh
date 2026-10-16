#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# LISTENING FILES for PROPHET (docs/PROPHET.md): the same chord progression (D, Bm, G, A, D: MAJ / MIN + the root
# keys, three notes a chord) through the emulator on eight of Sequential's factory programs, each dry (the three
# sends at 0) and with the unit's default sends (reverb 35, chorus 45, delay 25); and, when the remix is beside the
# repository, three programs rendered by the remix's engine file and by this tree's (tools/prophet_ab.c: the engine
# alone, no effects, no master), to hear that it is the same instrument.
#   sh tools/prophet_listen.sh [OUTDIR [REMIX_DIR]]
#       OUTDIR     default ../audio-0.22/prophet beside the repository
#       REMIX_DIR  default ../ChoralRootRemix/Choralroot-remix-step20-timing (skipped with a line when it is not there)
#   builds nothing of the emulator: build/host/emu must be this tree's (sh tools/emu/build.sh)
# Headless: export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
set -e
cd "$(dirname "$0")/.."
O=${1:-../audio-0.22/prophet}
R=${2:-../ChoralRootRemix/Choralroot-remix-step20-timing}
W=build/scratch/prophet_listen
EMU=build/host/emu
[ -x "$EMU" ] || { echo "no $EMU: sh tools/emu/build.sh first"; exit 1; }
mkdir -p "$O" "$W" build/emu/test build/host
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

# the programs: Sequential's program number (its place in the pool is the shipped list's: gen_prophet_factory.py
# --pos; a program the list does not ship is skipped), the PRESETS name, a file name, what it is
PROGS="01:ITS PROPHET5:its-a-prophet-5:the bank's first, a poly sweep (SSI filter)
03:FOREVER KEYS:forever-keys:keys
06:STABBY BRASS:stabby-brass:brass
15:REEDY STRPAD:reedy-string-pad:a pad (Poly-Mod to the width and the filter)
44:CARS STRINGS:cars-strings:a string pad
62:VINTAGE WRLY:vintage-wurly:an electric piano
17:SYNCHROTRILL:synchrotrill:a sync lead
139:BASSMENT:bassment:a bass"

chord() {   # chord TYPEKEY ROOT MS
    printf 'key %s down\nwait 40\nbtn %s down\nwait %s\nbtn %s up\nkey %s up\nwait 150\n' "$1" "$2" "$3" "$2" "$1"
}
script() {  # script FILE PLACE dry|fx
    {
        echo "master 1023"
        echo "wait 300"
        printf 'btn GLO down\nwait 100\nknob PRESETS +4\nwait 100\nbtn GLO up\nwait 300\n'   # OPT + PRESETS: PROPHET, its place 01
        steps=$(( $2 - 1 ))
        [ "$steps" -gt 0 ] && printf 'knob PRESETS +%s\nwait 300\n' "$steps"
        if [ "$3" = dry ]; then                   # the FX layer's knobs: chorus, delay, reverb down to 0
            printf 'btn FX down\nwait 500\nknob KNOB2 -12\nwait 100\nknob KNOB3 -8\nwait 100\nknob KNOB4 -10\nwait 100\n'
            printf 'btn FX up\nwait 300\nbtn OCT-\nwait 300\n'
        fi
        echo dump
        chord F3 D4 1800
        chord F2 B3 1800
        chord F3 G3 1800
        chord F3 A3 1800
        chord F3 D4 3000
        printf 'wait 4000\ndump\nquit\n'
    } > "$1"
}

: > "$O/render.log"
while IFS=: read -r pos name file what; do
    place=$(python3 tools/gen_prophet_factory.py --pos "${pos#0}")
    [ "$place" -gt 0 ] || { echo "$pos $name: not in the shipped list, skipped"; continue; }
    for sends in dry fx; do
        n="$pos-$file-$sends"
        script "$W/$n.txt" "$place" "$sends"
        "$EMU" --headless --script "$W/$n.txt" --wav "$O/$n.wav" > "$W/$n.log" 2>&1
        LC_ALL=C grep -aq "part 0: P5 / $name" "$W/$n.log" || { echo "$n: the chord part is not on P5 $name"; exit 1; }
        {
            echo "== $n ($what)"
            LC_ALL=C grep -a "part 0:" "$W/$n.log" | head -1
            LC_ALL=C grep -a "session fx" "$W/$n.log" | head -1 | sed 's/ par .*//'
            echo "   $(python3 tools/emu/wavstat.py stereo "$O/$n.wav")"
        } >> "$O/render.log"
        echo "$n.wav"
    done
done <<EOF
$PROGS
EOF

# the remix's engine file against this tree's (the engine alone)
AB=
if [ -f "$R/firmware/src/eng_prophet_test.c" ]; then
    RA=$(cd "$R" && pwd)
    # (this tree's side with all 200 programs, so that preset k is program k as in the remix's file)
    mkdir -p "$W/all"
    python3 tools/gen_prophet_factory.py "$W/all/melodee_prophet_factory.h" all > /dev/null
    ${CC:-cc} -O2 -w "-I$W/all" -Ibuild/gen -Ifirmware/src '-DENGINE_FILE="eng_prophet_test.c"' -o build/host/prophet_ab tools/prophet_ab.c -lm
    ${CC:-cc} -O2 -w -Ibuild/gen -Ifirmware/src "-DENGINE_FILE=\"$RA/firmware/src/eng_prophet_test.c\"" \
        -o build/host/prophet_ab_remix tools/prophet_ab.c -lm
    # the remix's tables are this tree's (build/gen/felucca_tables.h), value for value
    python3 - "$RA/firmware/src/choralroot_prophet_tables.h" build/gen/felucca_tables.h >> "$O/render.log" <<'PY'
import re, sys
def tabs(path):
    t = open(path).read()
    t = re.sub(r"#if P5_OVERSAMPLE == 2.*?#else", "", t, flags=re.S)
    out = {m.group(1): [int(x) for x in re.findall(r"-?\d+", m.group(2))]
           for m in re.finditer(r"(P5_\w+)\[\d+\][^=;]*= \{(.*?)\};", t, re.S)}   # (the remix: a section attribute)
    m = re.search(r"#define P5_KEYTRACK_HALF_Q7 (\d+)", t)
    out["P5_KEYTRACK_HALF_Q7"] = [int(m.group(1))] if m else []
    return out
a, b = tabs(sys.argv[1]), tabs(sys.argv[2])
bad = [k for k in sorted(set(a) | set(b)) if a.get(k) != b.get(k)]
print("== tables: the remix's choralroot_prophet_tables.h against build/gen/felucca_tables.h: %d tables, %s" %
      (len(a), "the same values" if not bad and len(a) >= 10 else "DIFFERENT: " + ", ".join(bad)))
sys.exit(1 if bad or len(a) < 10 else 0)
PY
    for p in 01:its-a-prophet-5 15:reedy-string-pad 33:pickle-pincher; do
        pos=${p%%:*}; file=${p#*:}
        {
            echo "== ab-$pos-$file (the engine alone: remix, then this tree)"
            echo "   remix:      $(build/host/prophet_ab_remix "${pos#0}" "$O/ab-$pos-$file-remix.wav")"
            echo "   choralroot: $(build/host/prophet_ab "${pos#0}" "$O/ab-$pos-$file-choralroot.wav")"
        } >> "$O/render.log"
        echo "ab-$pos-$file-remix.wav ab-$pos-$file-choralroot.wav"
    done
    AB=1
else
    echo "no remix at $R: the A/B files are not rendered"
fi

{
    echo "P5 (Melodee's Prophet-5 engine in ChoralRoot FM-1, docs/PROPHET.md): listening files"
    echo "rendered by sh tools/prophet_listen.sh on $(date +%Y-%m-%d) from $(git rev-parse --short HEAD 2>/dev/null || echo '?')$(git diff --quiet 2>/dev/null || echo ' + uncommitted changes')"
    echo
    echo "44.1 kHz stereo, 16 bit. The emulator's output (the firmware's own code on the host: voice.c, the engine, the"
    echo "effects, the master in Output Clean, MASTER full), not a recording of the unit."
    echo
    echo "THE PROGRESSION, on eight of Sequential's 200 factory programs: D, Bm, G, A (1.8 s each), D (3 s), then 4 s of"
    echo "tail; MAJ / MIN + the root key, three notes a chord, the unit's default voicing, no bass part."
    echo "  NN-name-dry.wav   the three sends at 0 (the Prophet itself: the factory programs are dry)"
    echo "  NN-name-fx.wav    the unit's default sends: reverb 35, chorus 45, delay 25"
    echo
    echo "$PROGS" | while IFS=: read -r pos name file what; do
        echo "  $pos-$file-dry.wav / -fx.wav   Sequential's program $pos, $name: $what"
    done
    echo
    if [ -n "$AB" ]; then
        echo "THE REMIX'S ENGINE AGAINST THIS TREE'S (ab-*.wav): the engine alone, mono in both channels, no effects and no"
        echo "master, the same notes (D, Bm, G, A, D with a low root, four notes a chord), the same fixed gain, through"
        echo "tools/prophet_ab.c:"
        echo "  ab-NN-name-remix.wav        the remix's firmware/src/eng_prophet_test.c (Melodee b351a14 + its stage 36)"
        echo "  ab-NN-name-choralroot.wav   this tree's firmware/src/eng_prophet_test.c (Melodee 8a3074b + the same stage 36"
        echo "                              + the part's DC blocker)"
        echo "  01 ITS PROPHET5   the difference is the DC blocker and the interpolated last saturation stage (SSI)"
        echo "  15 REEDY STRPAD   the difference is the DC blocker (Curtis: the interpolated output knee)"
        echo "  33 PICKLE PINCH   Lo Freq B: 8a3074b made it seven octaves down and free-running (the remix's is ten"
        echo "                    octaves and starts every note at the bottom of the ramp: its filter stays shut)"
        echo
    fi
    echo "render.log: each file's part, sends and levels (tools/emu/wavstat.py); the A/B renders' digests and levels."
} > "$O/README.txt"
echo "written: $O/README.txt, $O/render.log"
