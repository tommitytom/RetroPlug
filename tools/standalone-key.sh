#!/usr/bin/env bash
#
# Send keystrokes to a running retroplug standalone via xdotool. Pairs with
# tools/run-standalone.sh — they share a virtual DISPLAY through the env.
#
# Typical agent flow: launch run-standalone.sh in the background, then call
# this script to drive the UI before the screenshot interval fires.
#
# Usage:
#   DISPLAY=:99 tools/standalone-key.sh Escape           # press Escape
#   DISPLAY=:99 tools/standalone-key.sh Down Down Return # arrow + select
#   DISPLAY=:99 tools/standalone-key.sh --repeat z 10    # press 'z' ten times
#
# Key names follow X11 keysym conventions (Return, Escape, Left, Right, etc).

set -euo pipefail

if ! command -v xdotool >/dev/null 2>&1; then
    echo "error: xdotool not installed. Run: sudo apt-get install xdotool" >&2
    exit 1
fi
if [ -z "${DISPLAY:-}" ]; then
    echo "error: DISPLAY not set. Did you start Xvfb / run-standalone.sh first?" >&2
    exit 1
fi

# Ensure retroplug has window focus before sending keys.
WINDOW=$(xdotool search --name "retroplug" 2>/dev/null | head -1 || true)
if [ -z "$WINDOW" ]; then
    echo "warning: no retroplug window found on $DISPLAY" >&2
fi

if [ "${1:-}" = "--repeat" ]; then
    KEY="$2"
    N="$3"
    for _ in $(seq "$N"); do
        if [ -n "$WINDOW" ]; then xdotool key --window "$WINDOW" "$KEY"; else xdotool key "$KEY"; fi
        sleep 0.05
    done
else
    for KEY in "$@"; do
        if [ -n "$WINDOW" ]; then xdotool key --window "$WINDOW" "$KEY"; else xdotool key "$KEY"; fi
        sleep 0.1
    done
fi
