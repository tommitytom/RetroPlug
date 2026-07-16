#!/usr/bin/env bash
#
# Copy one or more files into the reaper-mcp-server projects dir so its
# audio-analysis / project-discovery tools can see them. Called by the
# reaper-analyze-* CMake targets after retroplug-cli has rendered output.
#
# Usage:
#   tools/reaper-stage.sh FILE [FILE...]
#
# Env:
#   RETROPLUG_REAPER_DIR  override the resources dir (default: ../resources/reaper)

set -euo pipefail

if [ $# -eq 0 ]; then
    echo "usage: $0 FILE [FILE...]" >&2
    exit 2
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${RETROPLUG_REAPER_DIR:-$ROOT/../resources/reaper}/projects"
mkdir -p "$DEST"

for src in "$@"; do
    if [ ! -f "$src" ]; then
        echo "warning: missing source file $src" >&2
        continue
    fi
    cp "$src" "$DEST/"
    echo "staged: $DEST/$(basename "$src")"
done
