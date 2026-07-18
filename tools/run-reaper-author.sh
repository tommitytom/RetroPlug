#!/usr/bin/env bash
#
# Author a Reaper project from scratch by running a ReaScript inside
# headless Reaper. Optionally takes a pre-built project fixture (a thin `.rplg` JSON or an
# export `.rplg.zip` PKZIP, e.g. from tools/author-rplg.js) that the plugin auto-loads at
# construction, so the saved .RPP captures a configured RetroPlug state in its chunk.
#
# Usage:
#   tools/run-reaper-author.sh OUTPUT.RPP RENDER_DIR AUTHOR.lua [FIXTURE.rplg|.rplg.zip]
#
# OUTPUT.RPP     where the lua script writes the project (REAPER_AUTHOR_DEST)
# RENDER_DIR     absolute dir for the project's render output
#                (REAPER_AUTHOR_RENDER_DIR; the lua passes this to RENDER_FILE)
# AUTHOR.lua     ReaScript that builds + saves the project
# FIXTURE        optional. A pre-built `.rplg`/`.rplg.zip` becomes the plugin's
#                RETROPLUG_AUTOLOAD_PROJECT directly (build it via tools/author-rplg.js
#                or tools/author-lsdj-rplg.js).
#
# Same Xvfb + openbox + dummy-jackd + isolated-config + VST3-symlink
# setup as tools/run-reaper-render.sh.

set -euo pipefail

if [ $# -lt 3 ]; then
    echo "usage: $0 OUTPUT.RPP RENDER_DIR AUTHOR.lua [FIXTURE.rplg|.rplg.zip]" >&2
    exit 2
fi

DEST="$1"
RENDER_DIR="$2"
AUTHOR_LUA="$3"
FIXTURE="${4:-}"

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
# Which built VST3 to author against (RETROPLUG_VST3_NAME overrides the legacy default so the
# plugin is authored through the same harness).
VST3_NAME="${RETROPLUG_VST3_NAME:-retroplug}"
export HOME="$REAPER_CFG"
mkdir -p "$HOME/.vst3"
ln -sfn "$REPO_DIR/build/bin/${VST3_NAME}.vst3" "$HOME/.vst3/${VST3_NAME}.vst3"

export REAPER_AUTHOR_DEST="$DEST"
# Absolutize the render dir: the author lua stamps it into the .rpp's RENDER_FILE, and Reaper resolves
# a relative RENDER_FILE against the .rpp's own directory (not the repo root) — so a relative dir would
# scatter the render next to the fixture. Absolute keeps the output in the repo-root build/ tree.
export REAPER_AUTHOR_RENDER_DIR="$(realpath -m "$RENDER_DIR")"

# Optional fixture: point the plugin at a configured project state via the
# autoload env var so the .RPP chunk captures it. The .rplg is produced by a
# TS harness test with emu.saveRplg.
if [ -n "$FIXTURE" ]; then
    if [ ! -f "$FIXTURE" ]; then
        echo "error: fixture not found: $FIXTURE" >&2
        exit 1
    fi
    case "$FIXTURE" in
        *.rplg | *.rplg.zip)
            # Absolutize: the plugin's readFile resolves relative to the process cwd, but Reaper
            # changes its cwd when it loads the project / runs the script, so a relative fixture path
            # wouldn't resolve at plugin-construction time (autoload -> false, empty state captured).
            # A thin `.rplg` (JSON) or an export `.rplg.zip` (PKZIP) both load via the plugin's load().
            FIXTURE="$(realpath "$FIXTURE")"
            echo "fixture (rplg): $FIXTURE"
            export RETROPLUG_AUTOLOAD_PROJECT="$FIXTURE"
            ;;
        *)
            echo "error: fixture must be a .rplg or .rplg.zip: $FIXTURE" >&2
            exit 1
            ;;
    esac
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
jackd -d dummy -r 44100 -p "${REAPER_JACK_PERIOD:-1024}" >/tmp/reaper-jackd.log 2>&1 &
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
echo "  HOME=$HOME (.vst3 symlink: $HOME/.vst3/${VST3_NAME}.vst3)"
[ -n "${RETROPLUG_AUTOLOAD_PROJECT:-}" ] && echo "  autoload=$RETROPLUG_AUTOLOAD_PROJECT"

# Author fresh: drop any prior artifact so the settle-poll below keys on THIS run's save, not a stale
# file from a previous run.
rm -f "$DEST"

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

# Reaper's headless "Quit" (Main_OnCommand 40004) frequently never returns without a UI thread. The
# lua saves the .rpp synchronously BEFORE it asks to quit, so wait for the artifact to appear and its
# size to settle, then stop Reaper ourselves rather than blocking on a self-quit that may never come.
# (If Reaper does exit cleanly, kill -0 fails first and we simply reap it.)
DEADLINE=$((SECONDS + 180))
last_size=-1
while kill -0 "$REAPER_PID" 2>/dev/null; do
    if [ -f "$DEST" ]; then
        sz=$(stat -c '%s' "$DEST" 2>/dev/null || echo 0)
        if [ "$sz" -gt 0 ] && [ "$sz" = "$last_size" ]; then
            kill "$REAPER_PID" 2>/dev/null || true
            break
        fi
        last_size="$sz"
    fi
    if [ "$SECONDS" -ge "$DEADLINE" ]; then
        echo "error: timed out waiting for $DEST" >&2
        kill "$REAPER_PID" 2>/dev/null || true
        break
    fi
    sleep 1
done
wait "$REAPER_PID" 2>/dev/null || true

if [ -f "$DEST" ]; then
    echo "authored: $DEST ($(stat -c '%s bytes' "$DEST"))"
else
    echo "error: $DEST was not produced" >&2
    echo "  log: /tmp/reaper-author.log" >&2
    exit 1
fi
