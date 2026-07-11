#!/usr/bin/env bash
#
# Verify the DAW-hosted plugin EDITOR actually renders (the blank-white-UI repro).
# Boots headless Reaper (Xvfb + openbox + dummy JACK + isolated config + VST3
# symlink), floats the RetroPlug editor via a ReaScript, and dumps the plugin's
# own LVGL snapshot (RETROPLUG_SCREENSHOT_PATH). A populated snapshot proves the
# JS mounted and React rendered; a ~159-byte uniform PNG means the UI is blank.
#
# The `reaper:*` render tests only exercise AUDIO and `test:ui` uses RenderCore,
# so this is the only headless check of the hosted editor's on-screen rendering.
# It reproduces the multi-runtime class-id hazard on purpose by clearing Reaper's
# plugin-scan cache first, so a fresh scan constructs a throwaway txiki runtime
# before the instance the editor attaches to (see spec / ClassIdSpace.hpp).
#
# NOT part of CI — it needs a full DAW + X stack. Run it manually via
# `pnpm reaper:editor` (which builds retroplug-vst3 first) or directly.
#
# Usage:
#   tools/run-reaper-editor.sh [AUTHOR.lua]
#
# AUTHOR.lua   ReaScript that inserts + floats the editor (default:
#              tools/reaper-editor-open.lua)
#
# Env:
#   RETROPLUG_VST3_NAME   which built VST3 to host (default: retroplug)
#   RP_EDITOR_OUT         where to write the LVGL snapshot PNG
#                         (default: build/reaper-editor-lvgl.png)
#   RP_EDITOR_MIN_BYTES   snapshot must exceed this to count as rendered
#                         (default: 1000; a blank uniform PNG is ~159 bytes)
set -euo pipefail

for cmd in Xvfb openbox jackd reaper ffmpeg xdotool; do
  command -v "$cmd" >/dev/null 2>&1 || { echo "run-reaper-editor: missing '$cmd'" >&2; exit 127; }
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_DIR"

LUA="${1:-$SCRIPT_DIR/reaper-editor-open.lua}"
VST3_NAME="${RETROPLUG_VST3_NAME:-retroplug}"
OUT="${RP_EDITOR_OUT:-$REPO_DIR/build/reaper-editor-lvgl.png}"
MIN_BYTES="${RP_EDITOR_MIN_BYTES:-1000}"
W=1280; H=720

VST3_BUNDLE="$REPO_DIR/build/bin/${VST3_NAME}.vst3"
[ -e "$VST3_BUNDLE" ] || { echo "run-reaper-editor: $VST3_BUNDLE not built (run: pnpm reaper:editor)" >&2; exit 1; }

# Isolated Reaper config: Linux Reaper always scans ~/.vst3, so point HOME here
# and symlink the built bundle in. Clearing the scan cache forces a fresh scan.
REAPER_CFG="$REPO_DIR/build/reaper-cfg-editor"
mkdir -p "$REAPER_CFG"
export HOME="$REAPER_CFG"
mkdir -p "$HOME/.vst3"
ln -sfn "$VST3_BUNDLE" "$HOME/.vst3/${VST3_NAME}.vst3"
rm -f "$REAPER_CFG"/reaper-vstplugins*.ini "$REAPER_CFG"/reaper-vstplugins*.ini.bak 2>/dev/null || true

# GTK inside Reaper prefers Wayland/the host desktop otherwise; force our Xvfb.
unset WAYLAND_DISPLAY REMOTE_CONTAINERS_DISPLAY_SOCK 2>/dev/null || true
export GDK_BACKEND=x11

DISP=209
while [ -e "/tmp/.X${DISP}-lock" ]; do DISP=$((DISP + 1)); done
export DISPLAY=":${DISP}"

Xvfb "$DISPLAY" -screen 0 ${W}x${H}x24 -nolisten tcp >/dev/null 2>&1 & XVFB_PID=$!
sleep 0.3
openbox >/dev/null 2>&1 & WM_PID=$!   # a WM so windows accept _NET_ACTIVE_WINDOW focus
sleep 0.2
jackd -d dummy -r 44100 -p 1024 >/tmp/reaper-editor-jackd.log 2>&1 & JACK_PID=$!
sleep 0.4

cleanup() {
  for p in "${REAPER_PID:-}" "${DISMISS_PID:-}" "$JACK_PID" "$WM_PID" "$XVFB_PID"; do
    [ -n "$p" ] && kill "$p" 2>/dev/null || true
  done
  wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

rm -f "$OUT" /tmp/reaper-editor-lua.log /tmp/reaper-editor-window.png
echo "reaper editor: DISPLAY=$DISPLAY  HOME=$HOME  vst3=${VST3_NAME}  out=$OUT"

RETROPLUG_SCREENSHOT_PATH="$OUT" \
RETROPLUG_SCREENSHOT_INTERVAL_MS=500 \
  reaper -cfgfile "$REAPER_CFG/reaper.ini" -nosplash "$LUA" >/tmp/reaper-editor-reaper.log 2>&1 & REAPER_PID=$!

# Dismiss the fresh-config EULA + About dialogs so the FX window can map.
( for _ in $(seq 1 40); do
    sleep 0.5
    E=$(xdotool search --name "EVALUATION LICENSE" 2>/dev/null | head -1 || true)
    [ -n "$E" ] && { xdotool windowactivate --sync "$E" 2>/dev/null || true; sleep 0.2; xdotool key Tab Tab Tab space 2>/dev/null || true; }
    A=$(xdotool search --name "^About REAPER" 2>/dev/null | head -1 || true)
    [ -n "$A" ] && { xdotool windowactivate --sync "$A" 2>/dev/null || true; sleep 0.2; xdotool key Escape 2>/dev/null || true; }
  done ) & DISMISS_PID=$!

sleep 13
ffmpeg -y -f x11grab -video_size ${W}x${H} -i "$DISPLAY" -frames:v 1 /tmp/reaper-editor-window.png >/dev/null 2>&1 \
  || echo "  (ffmpeg window grab unavailable — the LVGL snapshot below is the authoritative check)"
cleanup
trap - EXIT INT TERM

# The LVGL snapshot is the verdict: a populated tree ⇒ JS mounted + React rendered.
if [ ! -f "$OUT" ]; then
  echo "FAIL: no LVGL snapshot written ($OUT) — editor never rendered." >&2
  echo "      see /tmp/reaper-editor-lua.log and /tmp/reaper-editor-reaper.log" >&2
  exit 1
fi
BYTES=$(wc -c < "$OUT")
if [ "$BYTES" -le "$MIN_BYTES" ]; then
  echo "FAIL: LVGL snapshot is only ${BYTES}B (<= ${MIN_BYTES}B) — the editor is blank." >&2
  exit 1
fi
echo "PASS: editor rendered — LVGL snapshot $OUT is ${BYTES}B (blank would be ~159B)."
