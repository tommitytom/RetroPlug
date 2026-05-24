#!/usr/bin/env bash
#
# Author the mgb_smoke Reaper project from scratch: launches Reaper
# headlessly with reaper-mgb-author.lua, which builds a track + VST3i +
# MIDI item and saves the .RPP at $REAPER_AUTHOR_DEST. This is the
# one-time bootstrap that produces examples/reaper/mgb_smoke.rpp — the
# rendering path (tools/run-reaper-render.sh) reads that .RPP back.
#
# Usage:
#   tools/run-reaper-author.sh OUTPUT.RPP [RENDER_DIR]
#
# Same Xvfb + dummy-jackd + isolated-config + VST3-symlink setup as
# run-reaper-render.sh.

set -euo pipefail

if [ $# -lt 1 ]; then
    echo "usage: $0 OUTPUT.RPP [RENDER_DIR]" >&2
    exit 2
fi

DEST="$1"
RENDER_DIR="${2:-}"

for cmd in Xvfb jackd reaper; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "error: '$cmd' not installed" >&2
        exit 1
    fi
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_DIR"

REAPER_CFG="$REPO_DIR/build/reaper-cfg"
mkdir -p "$REAPER_CFG"

# Reaper Linux always scans ~/.vst3; point HOME at the isolated cfg dir
# and symlink the build bundle there. VST3_PATH alone gets ignored by
# Reaper unless the user manually re-scans in the prefs UI.
export HOME="$REAPER_CFG"
mkdir -p "$HOME/.vst3"
ln -sfn "$REPO_DIR/build/bin/retroplug.vst3" "$HOME/.vst3/retroplug.vst3"

export REAPER_AUTHOR_DEST="$DEST"
export REAPER_AUTHOR_RENDER_DIR="$RENDER_DIR"

# Generate the .rplg fixture so the plugin loads mGB at construction;
# Reaper's getState() then captures it into the .RPP chunk, making the
# committed project file self-contained (no env var needed to play it
# back). Requires retroplug-cli to be built.
RPLG="$REPO_DIR/build/mgb_smoke_author.rplg"
if [ ! -x "$REPO_DIR/build/bin/retroplug-cli" ]; then
    echo "error: build/bin/retroplug-cli missing; build it first" >&2
    exit 1
fi
"$REPO_DIR/build/bin/retroplug-cli" \
    --script "$REPO_DIR/examples/scripts/mgb_smoke.json" \
    --save-rplg "$RPLG" >/dev/null
export RETROPLUG_AUTOLOAD_PROJECT="$RPLG"

# Force isolation from any host X11 / Wayland forwarding the devcontainer
# may be doing. Without this, GTK inside Reaper prefers WAYLAND_DISPLAY
# and reaches the user's host desktop instead of our Xvfb.
unset WAYLAND_DISPLAY
unset REMOTE_CONTAINERS_DISPLAY_SOCK
export GDK_BACKEND=x11

DISP=199
while [ -e "/tmp/.X${DISP}-lock" ]; do DISP=$((DISP + 1)); done
export DISPLAY=":${DISP}"

Xvfb "$DISPLAY" -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 &
XVFB_PID=$!
sleep 0.3
openbox >/dev/null 2>&1 &
WM_PID=$!
sleep 0.2
jackd -d dummy -r 44100 -p 1024 >/tmp/reaper-jackd.log 2>&1 &
JACK_PID=$!
sleep 0.4

cleanup() {
    [ -n "${REAPER_PID:-}" ] && kill "$REAPER_PID" 2>/dev/null || true
    [ -n "${DISMISS_PID:-}" ] && kill "$DISMISS_PID" 2>/dev/null || true
    kill "$JACK_PID" 2>/dev/null || true
    kill "$WM_PID" 2>/dev/null || true
    kill "$XVFB_PID" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "reaper author: DISPLAY=$DISPLAY dest=$DEST"
echo "  HOME=$HOME (.vst3 symlink: $HOME/.vst3/retroplug.vst3)"

reaper -cfgfile "$REAPER_CFG/reaper.ini" \
       -nosplash \
       "$REPO_DIR/tools/reaper-mgb-author.lua" \
       >/tmp/reaper-author.log 2>&1 &
REAPER_PID=$!

# Dismiss dialog stack from a fresh Reaper config: first the EULA (Tab
# Tab Tab Space lands on "I agree"), then the "About REAPER" splash
# (Escape), then any other modal we don't know about (Escape again).
# Polls for ~15s — Reaper opens these as the lua script is still
# parsing, so we have to keep checking.
for _ in $(seq 1 30); do
    sleep 0.5
    EULA=$(xdotool search --name "EVALUATION LICENSE" 2>/dev/null | head -1 || true)
    if [ -n "$EULA" ]; then
        xdotool windowactivate --sync "$EULA" 2>/dev/null || true
        sleep 0.2
        xdotool key Tab Tab Tab space
    fi
    ABOUT=$(xdotool search --name "^About REAPER" 2>/dev/null | head -1 || true)
    if [ -n "$ABOUT" ]; then
        xdotool windowactivate --sync "$ABOUT" 2>/dev/null || true
        sleep 0.2
        xdotool key Escape
    fi
    # Reaper opens the ReaScript console as a non-modal info window
    # when the script writes to it. Leave it alone.
done &
DISMISS_PID=$!

wait "$REAPER_PID"
RC=$?

if [ -f "$DEST" ]; then
    echo "authored: $DEST ($(stat -c '%s bytes' "$DEST"))"
else
    echo "error: $DEST was not produced" >&2
    echo "  log: /tmp/reaper-author.log" >&2
    exit 1
fi
exit $RC
