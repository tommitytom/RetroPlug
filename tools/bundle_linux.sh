#!/usr/bin/env bash
#
# Bundle the Linux build artifacts (standalone + CLI + CLAP + VST2 + VST3)
# into a versioned retroplug-v<version>.7z next to the binaries.
#
# Usage: tools/bundle_linux.sh [build-bin-dir]
# Defaults to build/bin relative to the repo root.
#
# Version is read from packages/native/src/PluginDSP.cpp's d_version(maj, min, pat) line, or
# overridden by the RETROPLUG_VERSION env var.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN_DIR="${1:-$REPO_ROOT/build/bin}"

if [[ ! -d "$BIN_DIR" ]]; then
    echo "error: $BIN_DIR does not exist" >&2
    exit 1
fi

VERSION="${RETROPLUG_VERSION:-}"
if [[ -z "$VERSION" ]]; then
    VERSION="$(sed -n -E 's/.*d_version\(([0-9]+),[[:space:]]*([0-9]+),[[:space:]]*([0-9]+)\).*/\1.\2.\3/p' "$REPO_ROOT/packages/native/src/PluginDSP.cpp" | head -n1)"
fi
if [[ -z "$VERSION" ]]; then
    echo "error: could not determine version from packages/native/src/PluginDSP.cpp" >&2
    exit 1
fi

ARCHIVE="$BIN_DIR/retroplug-v${VERSION}.7z"
ITEMS=(
    retroplug
    retroplug-cli
    retroplug.clap
    retroplug-vst2.so
    retroplug.vst3
)

for item in "${ITEMS[@]}"; do
    if [[ ! -e "$BIN_DIR/$item" ]]; then
        echo "error: missing build artifact $BIN_DIR/$item" >&2
        exit 1
    fi
done

rm -f "$ARCHIVE"

cd "$BIN_DIR"
7z a -t7z -mx=9 "$ARCHIVE" "${ITEMS[@]}"
