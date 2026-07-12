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
# Before the fix, the editor composed its own store graph and ignored the control-plane
# project, so it showed the start menu (~4301B). After the fix it reuses the control
# plane's graph and shows the grid (a small, mostly-black PNG).
#
#   PASS (exit 0): the editor shows the autoloaded project.
#   FAIL (exit 1): the editor shows the start menu — the project wasn't reflected.
#
# NOT part of CI — needs a full DAW + X stack. Run via `pnpm reaper:editor-autoload`
# (builds retroplug-vst3 + authors the fixture first) or directly.
set -euo pipefail

for cmd in Xvfb openbox jackd reaper xdotool; do
  command -v "$cmd" >/dev/null 2>&1 || { echo "run-reaper-editor-autoload: missing '$cmd'" >&2; exit 127; }
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_DIR"

VST3_NAME="${RETROPLUG_VST3_NAME:-retroplug}"
AUTOLOAD="${RP_EDITOR_AUTOLOAD:-$REPO_DIR/build/mgb.rplg}"
SNAP="${RP_EDITOR_AUTOLOAD_SNAP:-$REPO_DIR/build/reaper-editor-autoload.png}"
GRID_MAX_BYTES="${RP_GRID_MAX_BYTES:-2500}"  # a loaded grid is small + mostly black; the start menu is larger
W=1280; H=720

VST3_BUNDLE="$REPO_DIR/build/bin/${VST3_NAME}.vst3"
[ -e "$VST3_BUNDLE" ] || { echo "run-reaper-editor-autoload: $VST3_BUNDLE not built (run: pnpm reaper:editor-autoload)" >&2; exit 1; }
[ -e "$AUTOLOAD" ]    || { echo "run-reaper-editor-autoload: fixture $AUTOLOAD missing (run: node tools/author-rplg.js)" >&2; exit 1; }

REAPER_CFG="$REPO_DIR/build/reaper-cfg-editor"
mkdir -p "$REAPER_CFG"; export HOME="$REAPER_CFG"
mkdir -p "$HOME/.vst3"; ln -sfn "$VST3_BUNDLE" "$HOME/.vst3/${VST3_NAME}.vst3"
rm -f "$REAPER_CFG"/reaper-vstplugins*.ini "$REAPER_CFG"/reaper-vstplugins*.ini.bak 2>/dev/null || true

unset WAYLAND_DISPLAY REMOTE_CONTAINERS_DISPLAY_SOCK 2>/dev/null || true
export GDK_BACKEND=x11

DISP=209
while [ -e "/tmp/.X${DISP}-lock" ]; do DISP=$((DISP + 1)); done
export DISPLAY=":${DISP}"

Xvfb "$DISPLAY" -screen 0 ${W}x${H}x24 -nolisten tcp >/dev/null 2>&1 & XVFB_PID=$!
sleep 0.3
openbox >/dev/null 2>&1 & WM_PID=$!
sleep 0.2
jackd -d dummy -r 44100 -p 1024 >/tmp/reaper-editor-autoload-jackd.log 2>&1 & JACK_PID=$!
sleep 0.4

cleanup() {
  for p in "${REAPER_PID:-}" "${DISMISS_PID:-}" "$JACK_PID" "$WM_PID" "$XVFB_PID"; do
    [ -n "$p" ] && kill "$p" 2>/dev/null || true
  done
  wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

rm -f "$SNAP"

RETROPLUG_AUTOLOAD_PROJECT="$AUTOLOAD" \
RETROPLUG_SCREENSHOT_PATH="$SNAP" \
RETROPLUG_SCREENSHOT_INTERVAL_MS=400 \
  reaper -cfgfile "$REAPER_CFG/reaper.ini" -nosplash "$SCRIPT_DIR/reaper-editor-open.lua" >/tmp/reaper-editor-autoload-reaper.log 2>&1 & REAPER_PID=$!

# Dismiss the fresh-config EULA + About dialogs so the FX window can map.
( for _ in $(seq 1 40); do
    sleep 0.5
    E=$(xdotool search --name "EVALUATION LICENSE" 2>/dev/null | head -1 || true)
    [ -n "$E" ] && { xdotool windowactivate --sync "$E" 2>/dev/null || true; sleep 0.2; xdotool key Tab Tab Tab space 2>/dev/null || true; }
    A=$(xdotool search --name "^About REAPER" 2>/dev/null | head -1 || true)
    [ -n "$A" ] && { xdotool windowactivate --sync "$A" 2>/dev/null || true; sleep 0.2; xdotool key Escape 2>/dev/null || true; }
  done ) & DISMISS_PID=$!

sleep 12
cleanup
trap - EXIT INT TERM

cp -f "$SNAP" "$REPO_DIR/build/reaper-editor-autoload.png" 2>/dev/null || true
BYTES=$(wc -c < "$SNAP" 2>/dev/null || echo 0)
echo "run-reaper-editor-autoload: DISPLAY=$DISPLAY  autoload=$AUTOLOAD  snapshot=${BYTES}B (grid <= ${GRID_MAX_BYTES}B, start-menu larger)"

if [ "$BYTES" -eq 0 ]; then
  echo "SKIP: no LVGL snapshot written — editor never rendered (see /tmp/reaper-editor-autoload-reaper.log)." >&2
  exit 2
fi
if [ "$BYTES" -le "$GRID_MAX_BYTES" ]; then
  echo "PASS: the editor shows the autoloaded project (control-plane restore reflected in the UI)."
  exit 0
fi
echo "FAIL: the editor shows the START MENU (${BYTES}B) despite an autoloaded project —"
echo "      the editor isn't reflecting the control-plane store. build/reaper-editor-autoload.png"
exit 1
