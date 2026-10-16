#!/bin/sh
# Build the CoreAudio recorder tools/digitone/dt_rec.c into build/dt_rec (macOS, clang).
set -e
root="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$root/build"
clang -O2 -Wall -Wextra -o "$root/build/dt_rec" "$root/tools/digitone/dt_rec.c" \
    -framework CoreAudio -framework AudioToolbox -framework CoreFoundation
echo "built $root/build/dt_rec"
