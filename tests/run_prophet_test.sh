#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# P5's host checks beside tests/cr_prophet_test.c's own (docs/PROPHET.md "Tests"), no make needed:
#   sh tests/run_prophet_test.sh            (tests/run_cr_tests.sh runs it)
# 1. The remix's three optimisations are bit-exact (eng_prophet_test.c P5_STAGE36: 1 a silent oscillator skipped,
#    2 the envelope's terms once a sample, 4 hard sync's 32-bit products): cr_prophet_test --pcm writes a digest of
#    every sample of a fixed render of every shipped program and of 683 stress programs (sync, Poly-Mod, the wheel on
#    every destination, both filters, noise); the file is built with P5_STAGE36 0 (Melodee 8a3074b's code), 1, 2, 4 and
#    7 (the default) and the five outputs must be the same bytes.
# 2. FELUCCA_P5_FACTORY=0: the engine with INIT alone (no factory data in the build) compiles, and
#    cr_prophet_test's checks pass on it (the bank's are replaced by "INIT alone").
# 3. THE SHIPPED LIST (assets/prophet5-factory/shipped.txt; docs/PROPHET.md "The shipped list"): the test is built
#    with three lists: the tree's (S), all 200 (A) and one of its own (B: seven programs in an order of their own,
#    two of them not in the tree's list). Each build passes its own checks; a program's render (--pcm) is the same
#    samples in every build that ships it; and the session's record of a part (--id-out: what cr_settings.c writes)
#    written by one build is read by the others (--id-in): the same program comes back, at the place THAT build has
#    it at, or from the record alone when it is not shipped there. So changing the list does not change what a saved
#    session plays.
# 4. THE OWNER'S GESTURE (3y44, docs/PROPHET.md "The crackle"): the build with all 200 (his build; the tree's list
#    does not ship Sequential's program 16) plays minor + 6 + 9 and then minor + 9 again on program 16 through the
#    chord engine, the allocator and the mix: 600 runs over the timing of the second strike, Key Mode, five ways of
#    going from the one chord to the other, and a unit that does not keep up (cr_prophet_test --gesture; the whole
#    sweep, minutes: --gesture-sweep). The crackle he heard does not come about here: the runs' rules must hold.
# 7. THE LEVEL (3y46, docs/PROPHET.md "Level"): a voice's output is exactly half of 3y45's. cr_prophet_test --half
#    writes every shipped program's render with the samples at 3y45's scale; built with the part's capacitor off
#    (-DP5_DC_BLOCK=0: its 32-bit state rounds) once as this tree is and once with -DP5_OUT_SHIFT=1 (3y45's "x 2"
#    after the VCA), the two files must be the same bytes, and no sample of the old level may be odd. And one voice of
#    a UNISON stack (one core) against the whole stack (two): cr_prophet_test --stack on the build with all 200.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host/p5_all build/host/p5_b
CC="sh tests/hostcc.sh cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests"
# list B: reversed against the bank, with programs the provisional list has (1, 3, 82, 135) and has not (200, 2, 7)
printf '# run_prophet_test.sh: a list of its own\n200  # CAT\n135\n82\n7    # NO WHINING (UNISON)\n3\n2\n1\n' > build/host/p5_b/shipped.txt
python3 tools/gen_prophet_factory.py build/host/p5_all/melodee_prophet_factory.h all > /dev/null
python3 tools/gen_prophet_factory.py build/host/p5_b/melodee_prophet_factory.h build/host/p5_b/shipped.txt > /dev/null
NS=$(python3 tools/gen_prophet_factory.py --list | wc -l | tr -d ' ')
for n in 0 1 2 4 7; do
    $CC -DP5_STAGE36=$n -o build/host/cr_prophet_s$n tests/cr_prophet_test.c -lm &
done
$CC -DFELUCCA_P5_FACTORY=0 -o build/host/cr_prophet_init tests/cr_prophet_test.c -lm &
$CC -DP5_IDLE_SKIP=0 -o build/host/cr_prophet_idle0 tests/cr_prophet_test.c -lm &
$CC -DP5_STEAL_BLOCKS=0 -o build/host/cr_prophet_steal0 tests/cr_prophet_test.c -lm &
$CC -DP5_DC_BLOCK=0 -o build/host/cr_prophet_half0 tests/cr_prophet_test.c -lm &
$CC -DP5_DC_BLOCK=0 -DP5_OUT_SHIFT=1 -o build/host/cr_prophet_half1 tests/cr_prophet_test.c -lm &
$CC -DP5_ONE_POOL=5 -o build/host/cr_prophet_pool5 tests/cr_prophet_test.c -lm &
$CC -DP5_CORE=3 -o build/host/cr_prophet_core3 tests/cr_prophet_test.c -lm &
$CC -DP5_WAIT_SLOT=0 -o build/host/cr_prophet_wait0 tests/cr_prophet_test.c -lm &
# (the list's directory first: its header is the one the engine's #include finds)
sh tests/hostcc.sh cc -std=gnu11 -O2 -w -Ibuild/host/p5_all -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_prophet_all tests/cr_prophet_test.c -lm &
sh tests/hostcc.sh cc -std=gnu11 -O2 -w -Ibuild/host/p5_b -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_prophet_b tests/cr_prophet_test.c -lm &
wait
for n in 0 1 2 4 7; do
    [ -x build/host/cr_prophet_s$n ] || { echo "run_prophet_test.sh: build/host/cr_prophet_s$n did not build"; exit 1; }
    ./build/host/cr_prophet_s$n --pcm build/host/cr_prophet_s$n.pcm > /dev/null &
done
wait
lines=$(wc -l < build/host/cr_prophet_s0.pcm | tr -d ' ')
[ "$lines" -eq $((NS + 1 + 683)) ] || { echo "run_prophet_test.sh: $lines renders in build/host/cr_prophet_s0.pcm, want INIT + $NS + 683"; exit 1; }
for n in 1 2 4 7; do
    cmp -s build/host/cr_prophet_s0.pcm build/host/cr_prophet_s$n.pcm || {
        echo "run_prophet_test.sh: P5_STAGE36=$n renders other samples than 8a3074b's code:"
        diff build/host/cr_prophet_s0.pcm build/host/cr_prophet_s$n.pcm | head -5
        exit 1
    }
done
# 5. an unplayed part (3y45): the wheel's block skipped while the part has no voice leaves the same samples
[ -x build/host/cr_prophet_idle0 ] || { echo "run_prophet_test.sh: P5_IDLE_SKIP=0 did not build"; exit 1; }
./build/host/cr_prophet_s7 --idle build/host/cr_prophet_idle1.txt > /dev/null
./build/host/cr_prophet_idle0 --idle build/host/cr_prophet_idle0.txt > /dev/null
il=$(wc -l < build/host/cr_prophet_idle1.txt | tr -d ' ')
ib=$(awk '{ s += $5 } END { print s + 0 }' build/host/cr_prophet_idle1.txt)
[ "$il" -eq $((NS + 1)) ] && [ "$ib" -gt $((il * 100)) ] || { echo "run_prophet_test.sh: --idle wrote $il programs with $ib silent blocks"; exit 1; }
cmp -s build/host/cr_prophet_idle0.txt build/host/cr_prophet_idle1.txt || {
    echo "run_prophet_test.sh: a part that skips the wheel's block while it has no voice plays other samples after it:"
    diff build/host/cr_prophet_idle0.txt build/host/cr_prophet_idle1.txt | head -5
    exit 1
}
echo "prophet: a part with no voice skips the wheel's block (P5_IDLE_SKIP) and plays the same samples after it ($il programs, $ib silent blocks, bit for bit)"
# 6. a stolen voice (3y45): P5_STEAL_BLOCKS=0 is a773d30's one-block ramp, and the test's rules hold for it too
[ -x build/host/cr_prophet_steal0 ] || { echo "run_prophet_test.sh: P5_STEAL_BLOCKS=0 did not build"; exit 1; }
./build/host/cr_prophet_steal0 > build/host/cr_prophet_steal0.log || { grep -a "FAIL\|failed" build/host/cr_prophet_steal0.log; exit 1; }
echo "prophet: P5_STEAL_BLOCKS=0 (a773d30's steal) builds and passes ($(grep -ac '^ok ' build/host/cr_prophet_steal0.log) checks ok)"
# 7. the level: every sample exactly half of 3y45's
for n in 0 1; do
    [ -x build/host/cr_prophet_half$n ] || { echo "run_prophet_test.sh: the level's build $n did not build"; exit 1; }
    ./build/host/cr_prophet_half$n --half build/host/cr_prophet_half$n.txt > /dev/null
done
hl=$(wc -l < build/host/cr_prophet_half0.txt | tr -d ' ')
[ "$hl" -eq $((NS + 1)) ] || { echo "run_prophet_test.sh: --half wrote $hl programs, want INIT + $NS"; exit 1; }
if grep -qv ' odd 0$' build/host/cr_prophet_half1.txt; then
    echo "run_prophet_test.sh: a sample of 3y45's level is odd: half of it is not exact"; grep -v ' odd 0$' build/host/cr_prophet_half1.txt | head -3; exit 1
fi
cmp -s build/host/cr_prophet_half0.txt build/host/cr_prophet_half1.txt || {
    echo "run_prophet_test.sh: the engine's output is not exactly half of 3y45's (P5_OUT_SHIFT 0 against 1, the capacitor off):"
    diff build/host/cr_prophet_half0.txt build/host/cr_prophet_half1.txt | head -5
    exit 1
}
echo "prophet: the level: every sample of every voice is exactly half of 3y45's ($hl programs, a four-note chord held and released, the capacitor off: this tree's samples x 2 are P5_OUT_SHIFT=1's, bit for bit)"
# the rules that are a switch: -DP5_ONE_POOL=5 (one core: four chord voices and the bass's one),
# -DP5_CORE=3 (three voices a core, six with two: docs/PROPHET.md "Will
# it fit?"): they build; the test's own counts are the default's, so only the checks that do not name them are
# asked for
for v in pool5 core3; do
    [ -x build/host/cr_prophet_$v ] || { echo "run_prophet_test.sh: the build '$v' did not build"; exit 1; }
    ./build/host/cr_prophet_$v > build/host/cr_prophet_$v.log 2>&1 || true
    grep -aq "every voice of both parts free after the release" build/host/cr_prophet_$v.log || { echo "run_prophet_test.sh: the build '$v' does not run through"; exit 1; }
done
echo "prophet: -DP5_ONE_POOL=5 (one core: 4 + 1) and -DP5_CORE=3 (three voices a core) build and run ($(grep -ac '  FAIL' build/host/cr_prophet_pool5.log) and $(grep -ac '  FAIL' build/host/cr_prophet_core3.log) of the default's counts differ, as they must)"
c3=$(grep -a 'two cores: ten keys' build/host/cr_prophet_core3.log | sed -n 's/.*(core 0 \([0-9]*\), core 1 \([0-9]*\) at the most), the pool never above eight (\([0-9]*\)).*/\1 \2 \3/p')
[ "$c3" = "3 3 6" ] || { echo "run_prophet_test.sh: -DP5_CORE=3: ten keys give '$c3' (core 0, core 1, the pool), want 3 3 6"; exit 1; }
echo "prophet: -DP5_CORE=3: ten keys sound six voices, three rendered by each core"
# the chord first, over its own release (voice.c voice_alloc: a note waits for a slot that is fading before it takes a
# held voice of its own chord). Its proof (-DP5_WAIT_SLOT=0 lost chord notes in the progression) ended with 3y53: the
# bass keeps its one voice, the eighth chord note takes one of its own chord either way, and the 6- and 7-note
# chords lose none without the wait too. The build still runs every check
[ -x build/host/cr_prophet_wait0 ] || { echo "run_prophet_test.sh: -DP5_WAIT_SLOT=0 did not build"; exit 1; }
./build/host/cr_prophet_wait0 > build/host/cr_prophet_wait0.log 2>&1 || {
    echo "run_prophet_test.sh: -DP5_WAIT_SLOT=0 fails:"; grep -a "  FAIL" build/host/cr_prophet_wait0.log | head -3; exit 1; }
echo "prophet: a chord struck over its own release with a P5 bass: no chord note lost of 6 or 7, one of 8 (3y53: the bass keeps one); -DP5_WAIT_SLOT=0 passes too (since 3y53 the wait changes nothing there)"
echo "prophet: P5_STAGE36 1, 2, 4 and 7 render 8a3074b's samples ($lines renders: INIT, the $NS shipped programs, the stress programs, bit for bit)"
[ -x build/host/cr_prophet_init ] || { echo "run_prophet_test.sh: FELUCCA_P5_FACTORY=0 did not build"; exit 1; }
./build/host/cr_prophet_init > build/host/cr_prophet_init.log || { grep -a "FAIL\|failed" build/host/cr_prophet_init.log; exit 1; }
echo "prophet: FELUCCA_P5_FACTORY=0 builds and plays ($(grep -ac '^ok ' build/host/cr_prophet_init.log) checks ok, INIT alone)"

# 3. the shipped list
for v in all b; do
    [ -x build/host/cr_prophet_$v ] || { echo "run_prophet_test.sh: the build with list '$v' did not build"; exit 1; }
    ./build/host/cr_prophet_$v > build/host/cr_prophet_$v.log || { grep -a "FAIL\|failed" build/host/cr_prophet_$v.log; exit 1; }
    ./build/host/cr_prophet_$v --pcm build/host/cr_prophet_$v.pcm > /dev/null
    ./build/host/cr_prophet_$v --id-out build/host/cr_prophet_$v.ids > /dev/null
done
./build/host/cr_prophet_s7 --id-out build/host/cr_prophet_s.ids > /dev/null
echo "prophet: the test passes built with all 200 ($(grep -ac '^ok ' build/host/cr_prophet_all.log) checks ok) and with a list of 7 ($(grep -ac '^ok ' build/host/cr_prophet_b.log) checks ok)"
# 7. a UNISON key with one core against two (all 200: 40 programs with the switch)
./build/host/cr_prophet_all --stack > build/host/cr_prophet_stack.log
echo "prophet: $(tail -1 build/host/cr_prophet_stack.log)"
./build/host/cr_prophet_all --unichord > build/host/cr_prophet_unichord.log
echo "prophet: $(tail -1 build/host/cr_prophet_unichord.log)"
# 4. the owner's gesture on Sequential's program 16
./build/host/cr_prophet_all --gesture 16 > build/host/cr_prophet_gesture.log || {
    echo "run_prophet_test.sh: the owner's gesture (minor + 6 + 9, then minor + 9) broke a rule:"
    grep -a "GESTURE\|^gesture:" build/host/cr_prophet_gesture.log | head -8; exit 1; }
echo "prophet: the owner's $(tail -1 build/host/cr_prophet_gesture.log)"
# a program's samples do not depend on the list: every "program NNN" line of a list's renders is in the 200's
for v in s7 b; do
    grep '^program ' build/host/cr_prophet_$v.pcm > build/host/cr_prophet_$v.prog
    n=$(wc -l < build/host/cr_prophet_$v.prog | tr -d ' ')
    m=$(grep -c -x -F -f build/host/cr_prophet_$v.prog build/host/cr_prophet_all.pcm || true)
    [ "$n" -ge 2 ] && [ "$m" = "$n" ] || { echo "run_prophet_test.sh: list '$v': $m of $n programs render the samples they render among all 200"; exit 1; }
done
echo "prophet: a program renders the same samples wherever it stands (the tree's list and the list of 7 against all 200)"
# the session's record across lists: writer -> reader
id() {  # id READER WRITER-IDS WHAT
    ./build/host/cr_prophet_$1 --id-in build/host/cr_prophet_$2.ids > build/host/cr_prophet_id.log || {
        echo "run_prophet_test.sh: $3: a record does not bring its program back:"; cat build/host/cr_prophet_id.log; exit 1; }
    echo "prophet: $3: $(sed -n 's/^id-in: //p' build/host/cr_prophet_id.log)"
}
id b all "a session saved with all 200, read with the list of 7"
# the favourites across lists (docs/PRESETS.md "Favourites"): marked in one build, read by another: a program's mark
# is on its Sequential number, so the star stands at the place THAT build has the program at
fav() {  # fav READER WRITER WHAT
    ./build/host/cr_prophet_$2 --fav-out build/host/cr_prophet_$2.fav > /dev/null &&
        ./build/host/cr_prophet_$1 --fav-in build/host/cr_prophet_$2.fav > build/host/cr_prophet_fav.log || {
        echo "run_prophet_test.sh: $3: a mark does not follow its program:"; cat build/host/cr_prophet_fav.log; exit 1; }
    echo "prophet: $3: $(sed -n 's/^fav-in: //p' build/host/cr_prophet_fav.log)"
}
fav b all "favourites marked with all 200, read with the list of 7"
fav all b "favourites marked with the list of 7, read with all 200"
fav s7 all "favourites marked with all 200, read with the tree's list"
fav b s7 "favourites marked with the tree's list, read with the list of 7"
id all b "a session saved with the list of 7, read with all 200"
id s7 all "a session saved with all 200, read with the tree's list"
id b s "a session saved with the tree's list, read with the list of 7"
id s7 s "a session saved and read with the tree's list"
