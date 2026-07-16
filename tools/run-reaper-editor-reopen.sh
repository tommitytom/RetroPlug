#!/usr/bin/env bash
#
# Regression test for the DAW close→reopen project-state-loss bug: in a DAW, load a
# project into the plugin, close the editor window, reopen it — and the editor drops
# back to the start menu instead of showing the loaded project.
#
# Reproduces it in headless Reaper: float the RetroPlug editor (start menu), click the
# "Load mGB" row to load a system through the UI (so the project shows — the system
# grid), then close and reopen the editor and check what it shows. It classifies each
# LVGL snapshot (RETROPLUG_SCREENSHOT_PATH) as the START MENU (a large text PNG) or a
# loaded PROJECT (a small, mostly-black grid PNG).
#
#   PASS (exit 0): the reopened editor still shows the project  — bug fixed.
#   FAIL (exit 1): the reopened editor shows the start menu     — bug present (today).
#   SKIP (exit 2): couldn't click-load the project (setup issue), result inconclusive.
#
# Keyboard input doesn't reach the plugin editor under headless Reaper, so the project
# is loaded with a synthesized MOUSE click on the "Load mGB" menu row (mouse events do
# route through PluginUI::onMouse → LVGL). The click target is derived from the floating
# FX window geometry; override with RP_LOAD_X_OFF / RP_LOAD_Y_OFF if the layout shifts.
#
# NOT part of CI — it needs a full DAW + X stack. Run via `pnpm reaper:editor-reopen`
# (builds retroplug-vst3 first) or directly.
set -euo pipefail

for cmd in Xvfb openbox jackd reaper xdotool; do
  command -v "$cmd" >/dev/null 2>&1 || { echo "run-reaper-editor-reopen: missing '$cmd'" >&2; exit 127; }
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_DIR"

VST3_NAME="${RETROPLUG_VST3_NAME:-retroplug}"
SNAP="${RP_EDITOR_REOPEN_SNAP:-$REPO_DIR/build/reaper-editor-reopen.png}"
SIGDIR="$(mktemp -d)"
LOAD_X_OFF="${RP_LOAD_X_OFF:-135}"   # "Load mGB" row offset from the FX window origin
LOAD_Y_OFF="${RP_LOAD_Y_OFF:-110}"
# A start-menu snapshot is a large text PNG; a loaded system grid is a small mostly-black PNG.
GRID_MAX_BYTES="${RP_GRID_MAX_BYTES:-2500}"
W=1280; H=720

VST3_BUNDLE="$REPO_DIR/build/bin/${VST3_NAME}.vst3"
[ -e "$VST3_BUNDLE" ] || { echo "run-reaper-editor-reopen: $VST3_BUNDLE not built (run: pnpm reaper:editor-reopen)" >&2; exit 1; }

REAPER_CFG="$REPO_DIR/build/reaper-cfg-editor"
mkdir -p "$REAPER_CFG"; export HOME="$REAPER_CFG"
mkdir -p "$HOME/.vst3"; ln -sfn "$VST3_BUNDLE" "$HOME/.vst3/${VST3_NAME}.vst3"
rm -f "$REAPER_CFG"/reaper-vstplugins*.ini "$REAPER_CFG"/reaper-vstplugins*.ini.bak 2>/dev/null || true

unset WAYLAND_DISPLAY REMOTE_CONTAINERS_DISPLAY_SOCK 2>/dev/null || true
export GDK_BACKEND=x11
export RP_EDITOR_REOPEN_SIGDIR="$SIGDIR"

DISP=209
while [ -e "/tmp/.X${DISP}-lock" ]; do DISP=$((DISP + 1)); done
export DISPLAY=":${DISP}"

Xvfb "$DISPLAY" -screen 0 ${W}x${H}x24 -nolisten tcp >/dev/null 2>&1 & XVFB_PID=$!
sleep 0.3
openbox >/dev/null 2>&1 & WM_PID=$!
sleep 0.2
jackd -d dummy -r 44100 -p 1024 >"$SIGDIR/jackd.log" 2>&1 & JACK_PID=$!
sleep 0.4

cleanup() {
  for p in "${REAPER_PID:-}" "${DISMISS_PID:-}" "$JACK_PID" "$WM_PID" "$XVFB_PID"; do
    [ -n "$p" ] && kill "$p" 2>/dev/null || true
  done
  wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

rm -f "$SNAP" "$SNAP".menu "$SNAP".loaded "$SNAP".reopened

RETROPLUG_SCREENSHOT_PATH="$SNAP" \
RETROPLUG_SCREENSHOT_INTERVAL_MS=400 \
  reaper -cfgfile "$REAPER_CFG/reaper.ini" -nosplash "$SCRIPT_DIR/reaper-editor-reopen.lua" >"$SIGDIR/reaper.log" 2>&1 & REAPER_PID=$!

# Dismiss the fresh-config EULA + About dialogs so the FX window can map.
( for _ in $(seq 1 40); do
    sleep 0.5
    E=$(xdotool search --name "EVALUATION LICENSE" 2>/dev/null | head -1 || true)
    [ -n "$E" ] && { xdotool windowactivate --sync "$E" 2>/dev/null || true; sleep 0.2; xdotool key Tab Tab Tab space 2>/dev/null || true; }
    A=$(xdotool search --name "^About REAPER" 2>/dev/null | head -1 || true)
    [ -n "$A" ] && { xdotool windowactivate --sync "$A" 2>/dev/null || true; sleep 0.2; xdotool key Escape 2>/dev/null || true; }
  done ) & DISMISS_PID=$!

# Wait for the editor to float, then snapshot the start menu.
for _ in $(seq 1 40); do grep -q floated "$SIGDIR/rp-editor-reopen-lua.log" 2>/dev/null && break; sleep 0.5; done
sleep 2
cp -f "$SNAP" "$SNAP".menu 2>/dev/null || true

# Click-load mGB through the UI menu (mouse routes to the plugin; keyboard does not).
FXW=$(xdotool search --name "VST3i: RetroPlug" 2>/dev/null | head -1 || true)
[ -z "$FXW" ] && FXW=$(xdotool search --name "VST2i: RetroPlug" 2>/dev/null | head -1 || true)
eval "$(xdotool getwindowgeometry --shell "$FXW" 2>/dev/null || true)" # sets X, Y, WIDTH, HEIGHT
CX=$(( ${X:-114} + LOAD_X_OFF )); CY=$(( ${Y:-100} + LOAD_Y_OFF ))
echo "run-reaper-editor-reopen: DISPLAY=$DISPLAY  FXW=${FXW:-none}  click 'Load mGB' at ($CX,$CY)"
xdotool mousemove "$CX" "$CY" 2>/dev/null || true
sleep 0.3
# Hold the press for a few LVGL indev frames — an instant click can be missed at 60fps.
xdotool mousedown 1 2>/dev/null || true; sleep 0.25; xdotool mouseup 1 2>/dev/null || true
sleep 3
cp -f "$SNAP" "$SNAP".loaded 2>/dev/null || true

# Tell the ReaScript the project is loaded → it closes + reopens the editor.
touch "$SIGDIR/rp-editor-reopen-loaded"
for _ in $(seq 1 30); do [ -f "$SIGDIR/rp-editor-reopen-done" ] && break; sleep 0.5; done

cleanup
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
