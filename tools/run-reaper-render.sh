#!/usr/bin/env bash
#
# Render a Reaper project headlessly: Xvfb display + dummy JACK + isolated
# config dir + RetroPlug VST3 on the plugin path. The plugin's autoload
# hook picks up the optional .rplg fixture so the .RPP itself can stay a
# generic template.
#
# Usage:
#   tools/run-reaper-render.sh PROJECT.RPP [AUTOLOAD.RPLG]
#
# PROJECT.RPP   reaper project to render (RENDER_FILE/RENDER_PATTERN in the
#               project decides where the output WAV lands; the wrapper cds
#               to the repo root so relative paths resolve there)
# AUTOLOAD.RPLG optional; exported as RETROPLUG_AUTOLOAD_PROJECT so the
#               plugin loads a preconfigured project at construction
#
# Dependencies (one-time): xvfb, jackd2, libgtk-3-0t64 (Dockerfile).

set -euo pipefail

if [ $# -lt 1 ]; then
    echo "usage: $0 PROJECT.RPP [AUTOLOAD.RPLG]" >&2
    exit 2
fi

PROJECT="$1"
AUTOLOAD="${2:-}"

if [ ! -f "$PROJECT" ]; then
    echo "error: project not found: $PROJECT" >&2
    exit 1
fi

for cmd in Xvfb jackd reaper; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "error: '$cmd' not installed" >&2
        exit 1
    fi
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_DIR"

# Isolated Reaper config under build/ so plugin scan caches + ini files
# don't pollute the user's ~/.config/REAPER. Keeps the test reproducible
# across hosts.
REAPER_CFG="$REPO_DIR/build/reaper-cfg"
mkdir -p "$REAPER_CFG"

# Reaper Linux always scans ~/.vst3; point HOME at the isolated cfg dir
# and symlink the build bundle there. VST3_PATH alone gets ignored by
# Reaper unless the user manually re-scans in the prefs UI.
# Which built VST3 to host (RETROPLUG_VST3_NAME overrides the legacy default so the greenfield
# plugin renders through the same harness).
VST3_NAME="${RETROPLUG_VST3_NAME:-retroplug}"
export HOME="$REAPER_CFG"
mkdir -p "$HOME/.vst3"
ln -sfn "$REPO_DIR/build/bin/${VST3_NAME}.vst3" "$HOME/.vst3/${VST3_NAME}.vst3"

# Autoload fixture (optional). The plugin's RETROPLUG_AUTOLOAD_PROJECT
# hook reads this .rplg at construction and applies it as the initial
# project — sidesteps DPF state-chunk authoring in the .RPP.
if [ -n "$AUTOLOAD" ]; then
    if [ ! -f "$AUTOLOAD" ]; then
        echo "error: autoload fixture not found: $AUTOLOAD" >&2
        exit 1
    fi
    export RETROPLUG_AUTOLOAD_PROJECT="$AUTOLOAD"
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
# openbox needed because Reaper's EULA dialog (and many plugin UIs) only
# accept keyboard focus via _NET_ACTIVE_WINDOW, which Xvfb alone lacks.
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

echo "reaper render: DISPLAY=$DISPLAY project=$PROJECT autoload=${AUTOLOAD:-(none)}"
echo "  HOME=$HOME (.vst3 symlink: $HOME/.vst3/retroplug.vst3)"
echo "  config=$REAPER_CFG"

reaper -cfgfile "$REAPER_CFG/reaper.ini" \
       -nosplash \
       -renderproject "$PROJECT" \
       >/tmp/reaper-render.log 2>&1 &
REAPER_PID=$!

# Background daemon: dismiss the EULA + About dialogs that Reaper opens
# from a fresh config dir. Polls for the duration of the render.
for _ in $(seq 1 60); do
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

echo "reaper exited: $RC"
echo "  log: /tmp/reaper-render.log"
exit $RC
