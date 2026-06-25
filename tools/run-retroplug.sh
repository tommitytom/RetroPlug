#!/usr/bin/env bash
#
# Launch the retroplug standalone and wire its audio outputs to the default
# sink. The standalone registers as a JACK client (via pipewire-jack); PipeWire
# does NOT auto-connect JACK app outputs to the speakers, so without this the
# emulator runs silently. This waits for RetroPlug's ports to appear, links
# every out_N stereo pair to the current default sink, then hands the terminal
# back to the app.
#
# Usage: tools/run-retroplug.sh [extra retroplug args...]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="$REPO_DIR/build/bin/retroplug"

[ -x "$BIN" ] || { echo "error: $BIN not built" >&2; exit 1; }
command -v pw-link >/dev/null || { echo "error: pw-link not found (install pipewire)" >&2; exit 1; }

SINK="$(pactl get-default-sink)"
echo "default sink: $SINK"

"$BIN" "$@" &
RP=$!

cleanup() { kill "$RP" 2>/dev/null || true; }
trap cleanup EXIT INT TERM

# Wait for the main output port to register (up to ~5s).
for _ in $(seq 1 50); do
    if pw-link -o 2>/dev/null | grep -q '^RetroPlug:out_1_l$'; then break; fi
    sleep 0.1
done

# Link every RetroPlug stereo pair that exists to the default sink. Each system
# output sums into the same speakers; unused/silent systems are harmless.
linked=0
for n in 1 2 3 4; do
    if pw-link -o 2>/dev/null | grep -q "^RetroPlug:out_${n}_l$"; then
        pw-link "RetroPlug:out_${n}_l" "${SINK}:playback_FL" 2>/dev/null || true
        pw-link "RetroPlug:out_${n}_r" "${SINK}:playback_FR" 2>/dev/null || true
        linked=$((linked + 1))
    fi
done
echo "linked $linked RetroPlug output pair(s) -> $SINK"

wait "$RP"
