#!/usr/bin/env bash
#
# Regression test for the "editor reflects a control-plane-loaded project" half of the
# store-unification fix (the DAW session-restore / setState path, and the counterpart
# to tools/run-reaper-editor-reopen.sh's UI-load path).
#
# A DAW restores a saved project by calling setState with the saved chunk BEFORE the
# editor opens; the plugin's autoload hook (RETROPLUG_AUTOLOAD_PROJECT → __rp_loadProjectPath)
# drives the exact same control-plane store, so it's a deterministic stand-in that needs
# no UI interaction. This floats the editor with a project already loaded into the control
# plane and checks the editor shows that project (the system grid), not the start menu.
#
#   PASS (exit 0): the editor shows the autoloaded project.
#   FAIL (exit 1): the editor shows the start menu — the project wasn't reflected.
#
# NOT part of CI — needs a full DAW + X stack. Run via `pnpm reaper:editor-autoload`
# (builds retroplug-vst3 + authors the fixture first) or directly.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

AUTOLOAD="${RP_EDITOR_AUTOLOAD:-$REPO_DIR/build/mgb.rplg.zip}"
SNAP="${RP_EDITOR_AUTOLOAD_SNAP:-$REPO_DIR/build/reaper-editor-autoload.png}"
GRID_MAX_BYTES="${RP_GRID_MAX_BYTES:-2500}"  # a loaded grid is small + mostly black; the start menu is larger
W=1280; H=720

[ -e "$AUTOLOAD" ] || { echo "run-reaper-editor-autoload: fixture $AUTOLOAD missing (run: node tools/author-rplg.js)" >&2; exit 1; }

# Isolated headless stack — cold plugin scan (RP_SCAN_FRESH) to reproduce the class-id hazard.
: "${RP_JOB_TAG:=editor-autoload}"
RP_SCAN_FRESH=1 RP_SCREEN_W=$W RP_SCREEN_H=$H
export RP_SCAN_FRESH RP_SCREEN_W RP_SCREEN_H
source "$SCRIPT_DIR/reaper-env.sh"
reaper_env_up

rm -f "$SNAP"

RETROPLUG_AUTOLOAD_PROJECT="$AUTOLOAD" \
RETROPLUG_SCREENSHOT_PATH="$SNAP" \
RETROPLUG_SCREENSHOT_INTERVAL_MS=400 \
  reaper -cfgfile "$REAPER_CFG/reaper.ini" -nosplash "$SCRIPT_DIR/reaper-editor-open.lua" >"$RP_LOG_DIR/reaper-editor-autoload.log" 2>&1 & REAPER_PID=$!

# Poll for the LVGL snapshot instead of a fixed sleep — robust when the suite runs concurrently.
reaper_wait_snapshot "$SNAP" "${RP_EDITOR_TIMEOUT:-45}" || true
reaper_env_down
trap - EXIT INT TERM

BYTES=$(wc -c < "$SNAP" 2>/dev/null || echo 0)
echo "run-reaper-editor-autoload: DISPLAY=$DISPLAY  autoload=$AUTOLOAD  snapshot=${BYTES}B (grid <= ${GRID_MAX_BYTES}B, start-menu larger)"

if [ "$BYTES" -eq 0 ]; then
  echo "SKIP: no LVGL snapshot written — editor never rendered (see $RP_LOG_DIR/reaper-editor-autoload.log)." >&2
  exit 2
fi
if [ "$BYTES" -le "$GRID_MAX_BYTES" ]; then
  echo "PASS: the editor shows the autoloaded project (control-plane restore reflected in the UI)."
  exit 0
fi
echo "FAIL: the editor shows the START MENU (${BYTES}B) despite an autoloaded project —"
echo "      the editor isn't reflecting the control-plane store. build/reaper-editor-autoload.png"
exit 1
