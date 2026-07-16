#!/usr/bin/env bash
#
# Run Reaper headlessly: Xvfb for the display, jackd dummy backend for audio.
# Mirrors tools/run-standalone.sh; useful for ReaScript-driven fixture
# generation or for poking at a live Reaper via xdotool / x11vnc without
# any real display or audio hardware.
#
# Usage:
#   tools/run-reaper-headless.sh                 # run until Ctrl-C
#   tools/run-reaper-headless.sh -nonewinst foo.rpp
#   DISPLAY_HOLD=20 tools/run-reaper-headless.sh # auto-exit after 20s
#
# Any positional args are forwarded to reaper. DISPLAY_HOLD (seconds, optional)
# auto-terminates after the timeout; without it the script foregrounds Reaper
# and exits when Reaper exits.
#
# Dependencies (one-time):
#   sudo apt-get install xvfb jackd2

set -euo pipefail

for cmd in Xvfb jackd reaper; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "error: '$cmd' not installed" >&2
        echo "  rebuild the devcontainer to pick up Reaper + xvfb + jackd2" >&2
        exit 1
    fi
done

DISP=99
while [ -e "/tmp/.X${DISP}-lock" ]; do DISP=$((DISP + 1)); done
export DISPLAY=":${DISP}"

Xvfb "$DISPLAY" -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 &
XVFB_PID=$!

jackd -d dummy -r 44100 -p 1024 >/tmp/reaper-jackd.log 2>&1 &
JACK_PID=$!

sleep 0.4

cleanup() {
    [ -n "${REAPER_PID:-}" ] && kill "$REAPER_PID" 2>/dev/null || true
    kill "$JACK_PID" 2>/dev/null || true
    kill "$XVFB_PID" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "reaper headless: DISPLAY=$DISPLAY (xvfb pid $XVFB_PID, jackd pid $JACK_PID)"

reaper "$@" >/tmp/reaper-stdout.log 2>&1 &
REAPER_PID=$!

if [ -n "${DISPLAY_HOLD:-}" ]; then
    sleep "$DISPLAY_HOLD"
else
    wait "$REAPER_PID"
fi
