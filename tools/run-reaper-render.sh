#!/usr/bin/env bash
#
# Render a Reaper project headlessly through the isolated harness (tools/reaper-env.sh:
# Xvfb + openbox + dummy JACK + isolated config + RetroPlug VST3 symlink). The plugin's
# autoload hook picks up the optional .rplg fixture so the .RPP itself can stay a generic
# template.
#
# Usage:
#   tools/run-reaper-render.sh PROJECT.RPP [AUTOLOAD.RPLG]
#
# PROJECT.RPP   reaper project to render (RENDER_FILE/RENDER_PATTERN in the project decides
#               where the output WAV lands; the wrapper cds to the repo root so relative paths
#               resolve there)
# AUTOLOAD.RPLG optional; exported as RETROPLUG_AUTOLOAD_PROJECT so the plugin loads a
#               preconfigured project at construction
#
# Isolation is keyed off RP_JOB_TAG (default: the project basename) so concurrent renders of
# different projects don't collide — see tools/reaper-env.sh.
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

# Autoload fixture (optional). The plugin's RETROPLUG_AUTOLOAD_PROJECT hook reads this .rplg at
# construction and applies it as the initial project. Absolutize it FIRST: the plugin's readFile
# resolves relative to the process cwd, and Reaper changes its cwd when it loads the project, so a
# relative path wouldn't resolve at construction.
if [ -n "$AUTOLOAD" ]; then
    if [ ! -f "$AUTOLOAD" ]; then
        echo "error: autoload fixture not found: $AUTOLOAD" >&2
        exit 1
    fi
    export RETROPLUG_AUTOLOAD_PROJECT="$(realpath "$AUTOLOAD")"
fi

# One isolated headless Reaper stack. Default the tag to the project basename so a bare single run
# is self-consistent, while the parallel suite passes a distinct RP_JOB_TAG per scenario.
: "${RP_JOB_TAG:=$(basename "$PROJECT" .rpp)}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/reaper-env.sh"
reaper_env_up

echo "reaper render: DISPLAY=$DISPLAY project=$PROJECT autoload=${AUTOLOAD:-(none)}"
echo "  HOME=$HOME  config=$REAPER_CFG"

reaper -cfgfile "$REAPER_CFG/reaper.ini" \
       -nosplash \
       -renderproject "$PROJECT" \
       >"$RP_LOG_DIR/reaper-render.log" 2>&1 &
REAPER_PID=$!

wait "$REAPER_PID"
RC=$?

echo "reaper exited: $RC"
echo "  log: $RP_LOG_DIR/reaper-render.log"
exit $RC
