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

# RetroPlug registers one JACK client "RetroPlug" with four stereo output pairs
# (out_1_l/r … out_4_l/r); each system routes to a pair per the project's Audio Routing setting.
# Wait for the first pair to register (up to ~5s), then link every pair that exists to the default
# sink (each pair sums into the same speakers, so all systems are audible regardless of routing).
CLIENT="RetroPlug"
for _ in $(seq 1 50); do
    if pw-link -o 2>/dev/null | grep -q "^${CLIENT}:out_1_l$"; then break; fi
    sleep 0.1
done

linked=0
for n in 1 2 3 4; do
    if pw-link -o 2>/dev/null | grep -q "^${CLIENT}:out_${n}_l$"; then
        pw-link "${CLIENT}:out_${n}_l" "${SINK}:playback_FL" 2>/dev/null || true
        pw-link "${CLIENT}:out_${n}_r" "${SINK}:playback_FR" 2>/dev/null || true
        linked=$((linked + 1))
    fi
done
if [ "$linked" -gt 0 ]; then
    echo "linked $linked ${CLIENT} output pair(s) -> $SINK"
else
    echo "warning: no '${CLIENT}' output ports found — actual output ports:" >&2
    pw-link -o 2>/dev/null | grep -i "retroplug" >&2 || echo "  (none matching retroplug)" >&2
fi

wait "$RP"
