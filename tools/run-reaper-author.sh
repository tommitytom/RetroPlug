#!/usr/bin/env bash
#
# Author a Reaper project from scratch by running a ReaScript inside
# headless Reaper. Optionally runs retroplug-cli first to produce a
# .rplg fixture that the plugin auto-loads at construction, so the
# saved .RPP captures a configured RetroPlug state in its chunk.
#
# Usage:
#   tools/run-reaper-author.sh OUTPUT.RPP RENDER_DIR AUTHOR.lua [BOOTSTRAP.json]
#
# OUTPUT.RPP     where the lua script writes the project (REAPER_AUTHOR_DEST)
# RENDER_DIR     absolute dir for the project's render output
#                (REAPER_AUTHOR_RENDER_DIR; the lua passes this to RENDER_FILE)
# AUTHOR.lua     ReaScript that builds + saves the project
# BOOTSTRAP.json optional retroplug-cli script; if given, its --save-rplg
#                output becomes the plugin's RETROPLUG_AUTOLOAD_PROJECT so
#                the .RPP chunk captures the configured state
#
# Same Xvfb + openbox + dummy-jackd + isolated-config + VST3-symlink
# setup as tools/run-reaper-render.sh.

set -euo pipefail

if [ $# -lt 3 ]; then
    echo "usage: $0 OUTPUT.RPP RENDER_DIR AUTHOR.lua [BOOTSTRAP.json]" >&2
    exit 2
fi

DEST="$1"
RENDER_DIR="$2"
AUTHOR_LUA="$3"
BOOTSTRAP="${4:-}"

if [ ! -f "$AUTHOR_LUA" ]; then
    echo "error: author lua script not found: $AUTHOR_LUA" >&2
    exit 1
fi

for cmd in Xvfb jackd reaper openbox xdotool; do
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

# Optional bootstrap: run the CLI to capture a configured project state
# into a .rplg, then point the plugin at it via the autoload env var.
# Naming: <bootstrap-stem>_author.rplg under build/ keeps fixtures from
# different tests separate.
if [ -n "$BOOTSTRAP" ]; then
    if [ ! -f "$BOOTSTRAP" ]; then
        echo "error: bootstrap script not found: $BOOTSTRAP" >&2
        exit 1
    fi
    if [ ! -x "$REPO_DIR/build/bin/retroplug-cli" ]; then
        echo "error: build/bin/retroplug-cli missing; build it first" >&2
        exit 1
    fi
    STEM=$(basename "$BOOTSTRAP" .json)
    RPLG="$REPO_DIR/build/${STEM}_author.rplg"
    echo "bootstrap: $BOOTSTRAP -> $RPLG"
    "$REPO_DIR/build/bin/retroplug-cli" \
        --script "$BOOTSTRAP" \
        --save-rplg "$RPLG" >/dev/null
    export RETROPLUG_AUTOLOAD_PROJECT="$RPLG"
fi

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

echo "reaper author: DISPLAY=$DISPLAY dest=$DEST lua=$AUTHOR_LUA"
echo "  HOME=$HOME (.vst3 symlink: $HOME/.vst3/retroplug.vst3)"
[ -n "${RETROPLUG_AUTOLOAD_PROJECT:-}" ] && echo "  autoload=$RETROPLUG_AUTOLOAD_PROJECT"

reaper -cfgfile "$REAPER_CFG/reaper.ini" \
       -nosplash \
       "$AUTHOR_LUA" \
       >/tmp/reaper-author.log 2>&1 &
REAPER_PID=$!

# Background dialog dismisser — fresh Reaper config triggers EULA + About.
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
