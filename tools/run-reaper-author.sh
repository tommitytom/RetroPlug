#!/usr/bin/env bash
#
# Author a Reaper project from scratch by running a ReaScript inside the isolated headless
# harness (tools/reaper-env.sh). Optionally takes a pre-built project fixture (a thin `.rplg`
# JSON or an export `.rplg.zip` PKZIP, e.g. from tools/author-rplg.js) that the plugin
# auto-loads at construction, so the saved .RPP captures a configured RetroPlug state.
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
# Isolation is keyed off RP_JOB_TAG (default: the output basename) — see tools/reaper-env.sh.

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

export REAPER_AUTHOR_DEST="$DEST"
# Absolutize the render dir: the author lua stamps it into the .rpp's RENDER_FILE, and Reaper
# resolves a relative RENDER_FILE against the .rpp's own directory (not the repo root) — so a
# relative dir would scatter the render next to the fixture. Absolute keeps the output in build/.
export REAPER_AUTHOR_RENDER_DIR="$(realpath -m "$RENDER_DIR")"

# Optional fixture: point the plugin at a configured project state via the autoload env var so the
# .RPP chunk captures it. Absolutize (Reaper changes cwd when it runs the script). A thin `.rplg`
# (JSON) or an export `.rplg.zip` (PKZIP) both load via the plugin's load().
if [ -n "$FIXTURE" ]; then
    if [ ! -f "$FIXTURE" ]; then
        echo "error: fixture not found: $FIXTURE" >&2
        exit 1
    fi
    case "$FIXTURE" in
        *.rplg | *.rplg.zip)
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

: "${RP_JOB_TAG:=author-$(basename "$DEST" .rpp)}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/reaper-env.sh"
reaper_env_up

echo "reaper author: DISPLAY=$DISPLAY dest=$DEST lua=$AUTHOR_LUA"
echo "  HOME=$HOME  config=$REAPER_CFG"
[ -n "${RETROPLUG_AUTOLOAD_PROJECT:-}" ] && echo "  autoload=$RETROPLUG_AUTOLOAD_PROJECT"

# The .rpp fixtures are gitignored (derived), and git can't track an empty dir, so on a fresh
# checkout the destination directory doesn't exist — Main_SaveProjectEx would then silently write
# nothing. Create it, and drop any prior artifact so the settle-poll keys on THIS run's save.
mkdir -p "$(dirname "$DEST")"
rm -f "$DEST"

reaper -cfgfile "$REAPER_CFG/reaper.ini" \
       -nosplash \
       "$AUTHOR_LUA" \
       >"$RP_LOG_DIR/reaper-author.log" 2>&1 &
REAPER_PID=$!

# Reaper's headless "Quit" (Main_OnCommand 40004) frequently never returns without a UI thread. The
# lua saves the .rpp synchronously BEFORE it asks to quit, so wait for the artifact to appear and its
# size to settle, then stop Reaper ourselves rather than blocking on a self-quit that may never come.
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
    echo "  log: $RP_LOG_DIR/reaper-author.log" >&2
    exit 1
fi
