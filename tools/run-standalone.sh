#!/usr/bin/env bash
#
# Run the retroplug standalone (JACK target) headlessly:
#   * Xvfb provides a virtual display
#   * jackd dummy backend provides an audio server with no real I/O
#   * the screenshot env var dumps PNGs of the LVGL screen periodically
#
# The window boots the React UI (start menu → grid + menu) over the plugin's real Engine.
# Designed for agent workflows on hosts with no display or audio hardware. Cleans up Xvfb/jackd on exit.
#
# Usage:
#   tools/run-standalone.sh                    # screenshot to /tmp/retroplug.png after 4s
#   tools/run-standalone.sh /tmp/x.png 6       # custom path + 6s run
#   tools/run-standalone.sh /tmp/x.png 6 250   # 6s with 250ms screenshot interval
#
# Drive input during the run via tools/standalone-key.sh with RETROPLUG_WINDOW_NAME="RetroPlug".
#
# Dependencies (one-time): sudo apt-get install xvfb jackd2 xdotool

set -euo pipefail

OUT="${1:-/tmp/retroplug.png}"
DURATION="${2:-4}"
INTERVAL="${3:-1000}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_DIR"

for cmd in Xvfb jackd; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "error: '$cmd' not installed. Run:" >&2
        echo "  sudo apt-get install xvfb jackd2 xdotool" >&2
        exit 1
    fi
done

BIN="$REPO_DIR/build/bin/retroplug"
if [ ! -x "$BIN" ]; then
    echo "building standalone..."
    cmake --build "$REPO_DIR/build" --target retroplug-jack -j"$(nproc)" >/dev/null
fi

# Find a free X display.
DISP=99
while [ -e "/tmp/.X${DISP}-lock" ]; do DISP=$((DISP + 1)); done
export DISPLAY=":${DISP}"

Xvfb "$DISPLAY" -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 &
XVFB_PID=$!

jackd -d dummy -r 44100 -p 1024 >/tmp/retroplug-jackd.log 2>&1 &
JACK_PID=$!

sleep 0.4

cleanup() {
    [ -n "${RETRO_PID:-}" ] && kill "$RETRO_PID" 2>/dev/null || true
    kill "$JACK_PID" 2>/dev/null || true
    kill "$XVFB_PID" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

RETROPLUG_SCREENSHOT_PATH="$OUT" \
RETROPLUG_SCREENSHOT_INTERVAL_MS="$INTERVAL" \
    "$BIN" >/tmp/retroplug-stdout.log 2>&1 &
RETRO_PID=$!

sleep "$DURATION"

# trap cleanup runs

if [ -f "$OUT" ]; then
    SHA=$(sha1sum "$OUT" | cut -c1-12)
    echo "screenshot: $OUT ($(stat -c '%s bytes' "$OUT"), sha1 $SHA)"
else
    echo "warning: no screenshot produced at $OUT" >&2
    echo "check stdout: /tmp/retroplug-stdout.log" >&2
    echo "check jackd:  /tmp/retroplug-jackd.log" >&2
    exit 1
fi
