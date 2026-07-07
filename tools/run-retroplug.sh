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
BIN="$REPO_DIR/build/bin/retroplug-greenfield"

[ -x "$BIN" ] || { echo "error: $BIN not built" >&2; exit 1; }
command -v pw-link >/dev/null || { echo "error: pw-link not found (install pipewire)" >&2; exit 1; }

SINK="$(pactl get-default-sink)"
echo "default sink: $SINK"

"$BIN" "$@" &
RP=$!

cleanup() { kill "$RP" 2>/dev/null || true; }
trap cleanup EXIT INT TERM

# Greenfield registers one JACK client "RetroPlug Greenfield" with a single hard-stereo pair
# (all cores mix into audio_out_1 = L, audio_out_2 = R — DPF's default port symbols, since the
# greenfield plugin doesn't override initAudioPort). Wait for it to register (up to ~5s), then link.
CLIENT="RetroPlug Greenfield"
for _ in $(seq 1 50); do
    if pw-link -o 2>/dev/null | grep -q "^${CLIENT}:audio_out_1$"; then break; fi
    sleep 0.1
done

if pw-link -o 2>/dev/null | grep -q "^${CLIENT}:audio_out_1$"; then
    pw-link "${CLIENT}:audio_out_1" "${SINK}:playback_FL" 2>/dev/null || true
    pw-link "${CLIENT}:audio_out_2" "${SINK}:playback_FR" 2>/dev/null || true
    echo "linked ${CLIENT} stereo out -> $SINK"
else
    echo "warning: no '${CLIENT}' output ports found — actual output ports:" >&2
    pw-link -o 2>/dev/null | grep -i "retroplug\|greenfield" >&2 || echo "  (none matching retroplug/greenfield)" >&2
fi

wait "$RP"
