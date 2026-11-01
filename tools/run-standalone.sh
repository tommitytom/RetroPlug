#!/usr/bin/env bash
#
# Run the retroplug standalone (JACK target) headlessly:
#   * Xvfb provides a virtual display
#   * jackd dummy backend provides an audio server with no real I/O
#   * the screenshot env var dumps PNGs of the LVGL screen periodically
#   * the editor idle's reclaim of what the audio thread handed back is asserted (host/IdleReclaimProbe), which is
#     the one check that the plugin's own frame hook still calls HostServices::reclaimOnIdle
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

# Self-heal orphaned JACK shm: a hard-killed jackd (e.g. from the reaper suite) leaks its
# /dev/shm/jack-shm-registry slot, and 8 leaks wedge every new jackd with "Too many servers already
# active". Purge it only when no jackd is live, so we never disturb a running server.
if ! pgrep -u "$(id -u)" -x jackd >/dev/null 2>&1; then
    uid="$(id -u)"
    rm -f "/dev/shm/jack-shm-registry" "/dev/shm/jack-${uid}-"* "/dev/shm/jack_sem.${uid}_"* 2>/dev/null || true
    rm -rf "/dev/shm/jack_db-${uid}" 2>/dev/null || true
fi

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

# RETROPLUG_IDLE_RECLAIM_TEST arms host/IdleReclaimProbe: the plugin's only per-frame hook is the editor's uiIdle,
# and that is where it owes the engine its idle tick (HostServices::reclaimOnIdle), which frees the plans the audio
# thread handed back with no further publish. The probe publishes a plan and the one that replaces it, and prints the
# pass only when a tick frees the replaced one, so an editor whose idle stops calling the seam fails the check below.
RETROPLUG_IDLE_RECLAIM_TEST=10 \
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

# The editor's idle reached the engine: the probe's pass line, printed by the tick that freed the plan it replaced.
if ! grep -qE '\[idle-reclaim\] self-test: tick [0-9]+ freed [1-9][0-9]* replaced plan' /tmp/retroplug-stdout.log; then
    sed -n '1,200p' /tmp/retroplug-stdout.log >&2
    echo "FAIL: the editor's idle did not reclaim (no idle-reclaim pass line); see /tmp/retroplug-stdout.log" >&2
    exit 1
fi
echo "idle reclaim: the editor's idle freed a plan the audio thread handed back"
