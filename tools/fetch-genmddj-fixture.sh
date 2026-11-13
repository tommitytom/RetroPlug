#!/usr/bin/env bash
# Builds genmddj's ROM from a checkout and copies it into the resources tree, where the fixture-gated cases look for it
# (RETROPLUG_RESOURCES_DIR). Nothing vendors the ROM and nothing in CI builds it (design section 5): the resources tree
# is a sibling checkout, and a case that needs the ROM skips, counted, when it is absent.
#
#   tools/fetch-genmddj-fixture.sh [checkout]        (default /workspaces/genmddj)
#
# Needs vasmm68k_mot (vasm: `apt install vasm`, or https://sun.hasenbraten.de/vasm/) plus wla-z80 and wlalink (wla-dx:
# `apt install wla-dx`, or https://github.com/vhelin/wla-dx) on PATH, and python3 with Pillow and numpy for the cart's
# own generators. The cart builds its own ROM with `make all`: src/main.asm plus the Z80 driver, the font and wave
# banks and the sample table. This script builds a throwaway copy of the checkout and vendors no binary of its own.
set -euo pipefail

SRC=${1:-/workspaces/genmddj}
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST=${RETROPLUG_RESOURCES_DIR:-$ROOT/../resources}/roms

for tool in make python3 vasmm68k_mot wla-z80 wlalink; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: $tool is not on PATH (vasm and wla-dx are the cart's assemblers):" >&2
        echo "  apt install vasm wla-dx, or build them from" >&2
        echo "  https://sun.hasenbraten.de/vasm/ and https://github.com/vhelin/wla-dx and put them on PATH" >&2
        exit 2
    fi
done
if ! python3 -c 'import PIL, numpy' >/dev/null 2>&1; then
    echo "error: python3 has no Pillow or numpy (the cart's tools/makesplash.py, tools/makesamples.py and" >&2
    echo "  tools/fixheader.py need them): pip install pillow numpy" >&2
    exit 2
fi
if [ ! -f "$SRC/src/main.asm" ]; then
    echo "error: no genmddj checkout at $SRC" >&2
    exit 2
fi
if [ -n "$(git -C "$SRC" status --porcelain 2>/dev/null)" ]; then
    echo "warning: $SRC is dirty; the ROM is stamped with a '+' and its name is not reproducible" >&2
fi

# The build happens in a copy, because the checkout is the cart author's working tree and its Makefile hardcodes its
# output directory to ./build, so it offers no out-of-tree build. The copy carries .git so the ROM's own stamp (the
# short hash, plus a '+' for a dirty tree) is still the checkout's.
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/genmddj"
cp -a "$SRC/." "$WORK/genmddj/"
cd "$WORK/genmddj"
make all

# The version, read from the ROM the cart just built (the identity the app itself matches on) rather than re-parsed out
# of src/main.asm: the Makefile already stamps the string, and a second derivation is a second thing to keep right.
VER=$(grep -boa 'V[0-9][0-9]*\.[0-9][0-9]*' build/genmddj.bin | head -1 | sed 's/^[0-9]*://' |
    tr 'A-Z' 'a-z' | tr '.' '_' || true)
if [ -z "$VER" ]; then
    echo "error: no version string in build/genmddj.bin" >&2
    exit 1
fi
mkdir -p "$DEST"
OUT="$DEST/genmddj_${VER}.bin"
cp build/genmddj.bin "$OUT"
echo "wrote $OUT ($(wc -c <"$OUT") bytes, $(sha256sum "$OUT" | cut -d' ' -f1))"
echo "set RETROPLUG_RESOURCES_DIR to the tree this is in, or the genmddj cases will skip"
