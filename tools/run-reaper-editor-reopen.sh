#!/usr/bin/env bash
#
# Regression test for the DAW close→reopen project-state-loss bug: in a DAW, load a
# project into the plugin, close the editor window, reopen it — and the editor drops
# back to the start menu instead of showing the loaded project.
#
# Reproduces it in the isolated headless harness (tools/reaper-env.sh): float the RetroPlug
# editor (start menu), click the "Load mGB" row to load a system through the UI (so the project
# shows — the system grid), then close and reopen the editor and check what it shows. It
# classifies each LVGL snapshot (RETROPLUG_SCREENSHOT_PATH) as the START MENU (a large text PNG)
# or a loaded PROJECT (a small, mostly-black grid PNG).
#
#   PASS (exit 0): the reopened editor still shows the project  — bug fixed.
#   FAIL (exit 1): the reopened editor shows the start menu     — bug present.
#   SKIP (exit 2): couldn't click-load the project (setup issue), result inconclusive.
#
# Keyboard input doesn't reach the plugin editor under headless Reaper, so the project is loaded
# with a synthesized MOUSE click on the "Load mGB" menu row (mouse events route through
# PluginUI::onMouse → LVGL). The click target is derived from the floating FX window geometry;
# override with RP_LOAD_X_OFF / RP_LOAD_Y_OFF if the layout shifts.
#
# NOT part of CI — it needs a full DAW + X stack. Run via `pnpm reaper:editor-reopen`
# (builds retroplug-vst3 first) or directly.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

SNAP="${RP_EDITOR_REOPEN_SNAP:-$REPO_DIR/build/reaper-editor-reopen.png}"
LOAD_X_OFF="${RP_LOAD_X_OFF:-135}"   # "Load mGB" row offset from the FX window origin
LOAD_Y_OFF="${RP_LOAD_Y_OFF:-110}"
# A start-menu snapshot is a large text PNG; a loaded system grid is a small mostly-black PNG.
GRID_MAX_BYTES="${RP_GRID_MAX_BYTES:-2500}"
W=1280; H=720

# Isolated headless stack — cold plugin scan (RP_SCAN_FRESH) to reproduce the class-id hazard.
: "${RP_JOB_TAG:=editor-reopen}"
RP_SCAN_FRESH=1 RP_SCREEN_W=$W RP_SCREEN_H=$H
export RP_SCAN_FRESH RP_SCREEN_W RP_SCREEN_H
source "$SCRIPT_DIR/reaper-env.sh"
reaper_env_up

# The ReaScript exchanges signal files with us via this dir (tag-scoped, so concurrent runs
# don't cross wires).
SIGDIR="$RP_LOG_DIR"
export RP_EDITOR_REOPEN_SIGDIR="$SIGDIR"
rm -f "$SNAP" "$SNAP".menu "$SNAP".loaded "$SNAP".reopened \
      "$SIGDIR/rp-editor-reopen-lua.log" "$SIGDIR/rp-editor-reopen-loaded" "$SIGDIR/rp-editor-reopen-done"

RETROPLUG_SCREENSHOT_PATH="$SNAP" \
RETROPLUG_SCREENSHOT_INTERVAL_MS=400 \
  reaper -cfgfile "$REAPER_CFG/reaper.ini" -nosplash "$SCRIPT_DIR/reaper-editor-reopen.lua" >"$SIGDIR/reaper.log" 2>&1 & REAPER_PID=$!

# Wait for the editor to float AND actually render a frame, then snapshot the start menu. Poll
# generously — under concurrent suite load a fresh-scan editor can take a while to come up.
for _ in $(seq 1 90); do grep -q floated "$SIGDIR/rp-editor-reopen-lua.log" 2>/dev/null && break; sleep 0.5; done
reaper_wait_snapshot "$SNAP" "${RP_EDITOR_TIMEOUT:-45}" || true
sleep 1
cp -f "$SNAP" "$SNAP".menu 2>/dev/null || true

# Click-load mGB through the UI menu (mouse routes to the plugin; keyboard does not).
FXW=$(xdotool search --name "VST3i: RetroPlug" 2>/dev/null | head -1 || true)
[ -z "$FXW" ] && FXW=$(xdotool search --name "VST2i: RetroPlug" 2>/dev/null | head -1 || true)
eval "$(xdotool getwindowgeometry --shell "$FXW" 2>/dev/null || true)" # sets X, Y, WIDTH, HEIGHT
CX=$(( ${X:-114} + LOAD_X_OFF )); CY=$(( ${Y:-100} + LOAD_Y_OFF ))
echo "run-reaper-editor-reopen: DISPLAY=$DISPLAY  FXW=${FXW:-none}  click 'Load mGB' at ($CX,$CY)"
# Retry the load-click until the snapshot classifies as the grid: a single press is easily missed
# at 60fps, and under concurrent suite load LVGL's indev polling is slower still. The plugin
# rewrites $SNAP every interval, so poll its size to detect the grid appearing.
loaded_sz=0
for attempt in $(seq 1 6); do
  xdotool mousemove "$CX" "$CY" 2>/dev/null || true
  sleep 0.3
  # Hold the press for a few LVGL indev frames — an instant click can be missed.
  xdotool mousedown 1 2>/dev/null || true; sleep 0.3; xdotool mouseup 1 2>/dev/null || true
  for _ in $(seq 1 16); do
    sleep 0.5
    loaded_sz=$(wc -c < "$SNAP" 2>/dev/null || echo 0)
    { [ "$loaded_sz" -gt 0 ] && [ "$loaded_sz" -le "$GRID_MAX_BYTES" ]; } && break
  done
  { [ "$loaded_sz" -gt 0 ] && [ "$loaded_sz" -le "$GRID_MAX_BYTES" ]; } && break
  echo "run-reaper-editor-reopen: load-click attempt $attempt didn't register (snapshot ${loaded_sz}B), retrying…"
done
cp -f "$SNAP" "$SNAP".loaded 2>/dev/null || true

# Tell the ReaScript the project is loaded → it closes + reopens the editor.
touch "$SIGDIR/rp-editor-reopen-loaded"
for _ in $(seq 1 90); do [ -f "$SIGDIR/rp-editor-reopen-done" ] && break; sleep 0.5; done

reaper_env_down
trap - EXIT INT TERM

sizeof() { wc -c < "$1" 2>/dev/null || echo 0; }
M=$(sizeof "$SNAP".menu); LD=$(sizeof "$SNAP".loaded); R=$(sizeof "$SNAP".reopened)
# Keep the three frames for inspection.
cp -f "$SNAP".menu     "$REPO_DIR/build/reaper-editor-reopen-1-menu.png"     2>/dev/null || true
cp -f "$SNAP".loaded   "$REPO_DIR/build/reaper-editor-reopen-2-loaded.png"   2>/dev/null || true
cp -f "$SNAP".reopened "$REPO_DIR/build/reaper-editor-reopen-3-reopened.png" 2>/dev/null || true
echo "run-reaper-editor-reopen: snapshot bytes — menu=$M loaded=$LD reopened=$R (grid <= ${GRID_MAX_BYTES}B, start-menu larger)"

is_grid() { [ "$1" -gt 0 ] && [ "$1" -le "$GRID_MAX_BYTES" ]; }
if [ "$LD" -eq 0 ] || [ "$R" -eq 0 ]; then
  echo "SKIP: missing snapshots (editor never rendered?) — see $SIGDIR/reaper.log" >&2
  exit 2
fi
if ! is_grid "$LD"; then
  echo "SKIP: could not click-load the project (the 'loaded' snapshot is still the menu at ${LD}B)." >&2
  echo "      Adjust RP_LOAD_X_OFF / RP_LOAD_Y_OFF; result is inconclusive." >&2
  exit 2
fi
if is_grid "$R"; then
  echo "PASS: the reopened editor still shows the loaded project — close/reopen preserves state."
  exit 0
fi
echo "FAIL: the reopened editor dropped back to the START MENU (${R}B) after close/reopen —"
echo "      the loaded project (${LD}B grid) was lost. Bug reproduced."
echo "      frames: build/reaper-editor-reopen-{1-menu,2-loaded,3-reopened}.png"
exit 1
