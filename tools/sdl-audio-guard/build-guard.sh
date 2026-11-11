#!/usr/bin/env bash
#
# Build the /dev/snd guard a harness prepends to LD_PRELOAD: nosnd.c beside this script, as a shared object in the
# caller's scratch directory, printed on stdout so a script can do `GUARD=$(tools/sdl-audio-guard/build-guard.sh "$TMP")`.
#
# It is built rather than shipped because a .so in a repository is an artifact (and a binary one), and because the
# harnesses already own a scratch directory per run. Linux only: the guard wraps open/__open_2, which is how ALSA's
# legacy (non-sequencer) device layer opens /dev/snd/*; macOS and Windows have no /dev/snd to guard.
#
# Usage: tools/sdl-audio-guard/build-guard.sh <output-dir>    (CC overrides the compiler)
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <output-dir>" >&2
    exit 2
fi

case "$(uname -s)" in
    Linux) : ;;
    *) echo "error: the /dev/snd guard is a Linux facility (this is $(uname -s))" >&2; exit 1 ;;
esac

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$1"
[ -f "$HERE/nosnd.c" ] || { echo "error: $HERE/nosnd.c is missing" >&2; exit 1; }
mkdir -p "$OUT"
"${CC:-cc}" -shared -fPIC -O2 -o "$OUT/nosnd.so" "$HERE/nosnd.c" -ldl
echo "$OUT/nosnd.so"
