#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# The release build on GitHub's runner: The Firmware Hub's shared workflow (zvenson/fm1-build@v1) runs this as
# its build command (.github/workflows/release.yml; BUILDING.md "Releasing"). It also runs on the owner's Mac.
#   tools/hub_build.sh [X.Y]      build the release: the version from the argument, else from the tag (vX.Y), else
#                                 (run from the Actions tab) from the run's version input
#   tools/hub_build.sh --version [X.Y]    print version=X.Y and board=fm1|smk37 (release.yml's outputs), no build
#   tools/hub_build.sh --check DIR [X.Y]  check the packages in DIR against web/releases.json (FM-1) or
#                                 web/releases-smk37.json (X.Y-smk37), no build
# On GitHub's runner first the Python packages our packages are built with (the reference builds': Pillow with
# Raqm, fontTools), then ./build.sh --release (the three SMK models for X.Y-smk37), the files a release needs, the
# sha256s against the list the installer pins, and last the built files in the repository root as choralroot-*
# (the packages and their apps): the shared workflow attests those, keeps them as the run's artifact "packages"
# and, on a tag, uploads them with its SHA256SUMS to the release. A package whose sha256 is not the listed one
# stops here, before anything is signed or released.
# The shared workflow's Python 3.12 with these pins makes the owner's packages (Python 3.11, Docker) byte for byte:
# v0.25.1 rebuilt so on ubuntu 24.04 (HISTORY 3y190); the newest Pillow / fontTools (13.0 / 4.67) do not.
set -euo pipefail
cd "$(dirname "$0")/.."

PILLOW_VERSION=11.1.0           # the reference builds' (the UI fonts and icons: another version draws them otherwise)
FONTTOOLS_VERSION=4.66.1

mode=build
case "${1:-}" in
    --version) mode=version; shift ;;
    --check) mode=check; dir="${2:?--check DIR}"; shift 2 ;;
esac

# the version: the argument, the tag, or the version input of a run from the Actions tab
if [ -n "${1:-}" ]; then
    v="$1"
elif [ "${GITHUB_REF_TYPE:-}" = tag ]; then
    v="${GITHUB_REF_NAME#v}"
elif [ "${GITHUB_EVENT_NAME:-}" = workflow_dispatch ]; then
    v="$(python3 -c 'import json, os; print(json.load(open(os.environ["GITHUB_EVENT_PATH"]))["inputs"]["version"])')"
else
    echo "hub_build.sh: no version (an argument X.Y, a tag vX.Y or a run's version input)" >&2
    exit 1
fi
if [[ "$v" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?(-[A-Za-z0-9.]+)?-smk37(\.[0-9]+)?$ ]]; then
    board=smk37             # the SMK-37 test build (SMK37.md); -smk37.N: a respin on the same FM-1 base
    rel=build/release-smk37
elif [[ "$v" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?(-[A-Za-z0-9.]+)?$ ]] && [[ "$v" != *smk* ]]; then
    board=fm1
    rel="build/release-$v"
else
    echo "::error::version '$v' is not X.Y, X.Y.Z or X.Y-suffix (FM-1), nor X.Y[-suffix]-smk37[.N] (SMK-37); tags are vX.Y"
    exit 1
fi
if [ "$mode" = version ]; then
    echo "version=$v"
    echo "board=$board"
    exit 0
fi

# the sha256s of the packages in $1 against the list the installer pins: the FM-1's web/releases.json (both
# packages), the SMK's web/releases-smk37.json (six). A version the list does not have: a notice, unless it is the
# tag of an FM-1 release that publishes the site (X.Y, X.Y-betaN), which pages.yml requires to be listed
check() {
    python3 - "$v" "$board" "$1" <<'PY'
import hashlib, json, os, sys
from pathlib import Path
v, board, d = sys.argv[1], sys.argv[2], Path(sys.argv[3])
sha = lambda name: hashlib.sha256((d / name).read_bytes()).hexdigest()
if board == "fm1":
    entry = next((r for r in json.loads(Path("web/releases.json").read_text())["releases"] if r["version"] == v), None)
    if entry is None:
        if os.environ.get("GITHUB_REF_TYPE") == "tag" and ("-" not in v or "-beta" in v):
            sys.exit(f"::error::{v} is not in web/releases.json: its packages' sha256s go there before the tag "
                     "(BUILDING.md \"Releasing\")")
        print(f"::notice::{v} is not in web/releases.json: the installer will not offer it")
        sys.exit(0)
    want = {f"choralroot-{v}.fwsc": entry["sha256"]}
    if entry.get("sha256_1core"):
        want[f"choralroot-{v}-1core.fwsc"] = entry["sha256_1core"]
    listed = "web/releases.json"
else:
    f = Path("web/releases-smk37.json")
    entry = next((r for r in json.loads(f.read_text())["releases"] if r["version"] == v), None) if f.is_file() else None
    if entry is None:
        print(f"::notice::{v} is not in web/releases-smk37.json: the SMK-37 test installer will not offer it")
        sys.exit(0)
    want = {f"choralroot-{v}{sfx}{one}.fwsc": entry["packages"][key][field]
            for key, sfx in (("pro", ""), ("elite", "-elite"), ("mke", "-mke"))
            for one, field in (("", "sha256"), ("-1core", "sha256_1core"))}
    listed = "web/releases-smk37.json"
bad = [n for n, s in want.items() if not (d / n).is_file() or sha(n) != s]
if bad:
    sys.exit(f"::error::{', '.join(bad)} in {d} differ from the sha256s {listed} lists for {v} (or are missing): "
             "the build is not the one the installer pins")
print(f"{listed}: {v}'s {len(want)} sha256s match")
PY
}
if [ "$mode" = check ]; then
    check "$dir"
    exit 0
fi

# GitHub's runner: the reference builds' Python packages and Raqm (Pillow's wheels carry Raqm and load FriBiDi at
# run time; without it the UI fonts lose their kerning). The shared workflow installs the newest Pillow and no
# fontTools: replaced here by the pinned ones
PY="${PYTHON:-python3}"
# (the Mac: Pillow finds Homebrew's libraqm, as in build.sh; macOS strips DYLD_* on the way into a script)
if [ "$(uname -s)" = Darwin ] && [ -z "${DYLD_FALLBACK_LIBRARY_PATH:-}" ] && [ -f /opt/homebrew/lib/libraqm.dylib ]; then
    export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib
fi
if [ "${GITHUB_ACTIONS:-}" = true ]; then
    sudo apt-get update -q
    sudo apt-get install -y -q --no-install-recommends libfribidi0 libraqm0 zip
    "$PY" -m pip install --disable-pip-version-check -q "Pillow==$PILLOW_VERSION" "fonttools==$FONTTOOLS_VERSION"
fi
"$PY" - "$PILLOW_VERSION" "$FONTTOOLS_VERSION" <<'PY'
import sys
import fontTools
import PIL
from PIL import features
pillow, fonttools = sys.argv[1:]
print(f"Python {sys.version.split()[0]}, Pillow {PIL.__version__} (raqm {features.version('raqm')}, fribidi "
      f"{features.version('fribidi')}), fontTools {fontTools.version}")
if not features.check("raqm"):
    sys.exit("hub_build.sh: Pillow has no Raqm layout (libfribidi missing?): the UI fonts would differ")
if (PIL.__version__, fontTools.version) != (pillow, fonttools):
    sys.exit(f"hub_build.sh: the reference builds use Pillow {pillow} and fontTools {fonttools}: another version "
             "makes other packages (PYTHON=... for another python3)")
PY
if [ "$(uname -s)" = Linux ]; then     # (the Mac runs it in Docker: tools/build.py)
    "${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}/pi32v2/bin/clang" --version | head -1
fi

ulimit -n 8192 || true          # the JieLi linker opens many files (the toolchain's README)
if [ "$board" = smk37 ]; then
    for m in pro elite mke; do
        PYTHON="$PY" BOARD=smk37 MODEL=$m ./build.sh --release "$v"
    done
    names=()
    for sfx in "" -elite -mke; do
        for one in "" -1core; do names+=("choralroot-$v$sfx$one.fwsc" "choralroot-$v$sfx$one-app.bin"); done
    done
    docs=(SHA256SUMS LICENSE LICENSING.md SMK37.md)
else
    PYTHON="$PY" ./build.sh --release "$v"
    names=("choralroot-$v.fwsc" "choralroot-$v-app.bin" "choralroot-$v-1core.fwsc" "choralroot-$v-1core-app.bin")
    docs=(SHA256SUMS LICENSE LICENSING.md)
fi
for f in "${names[@]}" "${docs[@]}"; do
    [ -s "$rel/$f" ] || { echo "::error::$rel/$f is missing"; exit 1; }
done
if [ -e "$rel/ATTRIBUTION.txt" ]; then          # (FELUCCA_SAMPLE=1 only; ChoralRoot ships no samples)
    echo "::error::$rel/ATTRIBUTION.txt: a build with samples needs its credit on the release (release.yml)"
    exit 1
fi
# SHA256SUMS lists every package and app (4, or the SMK's 12): each must be there and match
if [ "$(grep -c . "$rel/SHA256SUMS")" != "${#names[@]}" ]; then
    echo "::error::$rel/SHA256SUMS does not list the ${#names[@]} packages and apps"
    exit 1
fi
(cd "$rel" && sha256sum -c SHA256SUMS)
check "$rel"

# what the shared workflow signs and releases (its "packages": choralroot-*), in the root so that its SHA256SUMS
# names them as ours does; on GitHub's runner only (on the Mac $rel/ has them)
if [ "${GITHUB_ACTIONS:-}" = true ]; then
    rm -f choralroot-*
    for f in "${names[@]}"; do cp "$rel/$f" .; done
    ls -l choralroot-*
fi
