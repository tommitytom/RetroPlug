#!/usr/bin/env bash
#
# Verify the DAW-hosted plugin EDITOR actually renders (the blank-white-UI repro).
# Boots the isolated headless harness (tools/reaper-env.sh), floats the RetroPlug editor via a
# ReaScript, and dumps the plugin's own LVGL snapshot (RETROPLUG_SCREENSHOT_PATH). A populated
# snapshot proves the JS mounted and React rendered; a ~159-byte uniform PNG means the UI is blank.
#
# The `reaper:*` render tests only exercise AUDIO and `test:ui` uses RenderCore, so this is the
# only headless check of the hosted editor's on-screen rendering. It reproduces the multi-runtime
# class-id hazard on purpose by clearing Reaper's plugin-scan cache first (RP_SCAN_FRESH=1), so a
# fresh scan constructs a throwaway txiki runtime before the instance the editor attaches to.
#
# NOT part of CI — it needs a full DAW + X stack. Run via `pnpm reaper:editor` (which builds
# retroplug-vst3 first) or directly.
#
# Usage:
#   tools/run-reaper-editor.sh [AUTHOR.lua]
#
# AUTHOR.lua   ReaScript that inserts + floats the editor (default: tools/reaper-editor-open.lua)
#
# Env:
#   RETROPLUG_VST3_NAME   which built VST3 to host (default: retroplug)
#   RP_EDITOR_OUT         where to write the LVGL snapshot PNG (default: build/reaper-editor-lvgl.png)
#   RP_EDITOR_MIN_BYTES   snapshot must exceed this to count as rendered (default: 1000)
set -euo pipefail

command -v ffmpeg >/dev/null 2>&1 || { echo "run-reaper-editor: missing 'ffmpeg'" >&2; exit 127; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

LUA="${1:-$SCRIPT_DIR/reaper-editor-open.lua}"
OUT="${RP_EDITOR_OUT:-$(cd "$SCRIPT_DIR/.." && pwd)/build/reaper-editor-lvgl.png}"
MIN_BYTES="${RP_EDITOR_MIN_BYTES:-1000}"
W=1280; H=720

# Isolated headless stack — cold plugin scan (RP_SCAN_FRESH) to reproduce the class-id hazard.
: "${RP_JOB_TAG:=editor}"
RP_SCAN_FRESH=1 RP_SCREEN_W=$W RP_SCREEN_H=$H
export RP_SCAN_FRESH RP_SCREEN_W RP_SCREEN_H
source "$SCRIPT_DIR/reaper-env.sh"
reaper_env_up

rm -f "$OUT" "$RP_LOG_DIR/reaper-editor-window.png"
echo "reaper editor: DISPLAY=$DISPLAY  HOME=$HOME  out=$OUT"

RETROPLUG_SCREENSHOT_PATH="$OUT" \
RETROPLUG_SCREENSHOT_INTERVAL_MS=500 \
  reaper -cfgfile "$REAPER_CFG/reaper.ini" -nosplash "$LUA" >"$RP_LOG_DIR/reaper-editor.log" 2>&1 & REAPER_PID=$!

# Poll for the LVGL snapshot instead of a fixed sleep — under concurrent suite load a fixed wait
# fires before the editor has rendered (see reaper_wait_snapshot).
reaper_wait_snapshot "$OUT" "${RP_EDITOR_TIMEOUT:-45}" || true
ffmpeg -y -f x11grab -video_size ${W}x${H} -i "$DISPLAY" -frames:v 1 "$RP_LOG_DIR/reaper-editor-window.png" >/dev/null 2>&1 \
  || echo "  (ffmpeg window grab unavailable — the LVGL snapshot below is the authoritative check)"
reaper_env_down
trap - EXIT INT TERM

# The LVGL snapshot is the verdict: a populated tree ⇒ JS mounted + React rendered.
if [ ! -f "$OUT" ]; then
  echo "FAIL: no LVGL snapshot written ($OUT) — editor never rendered." >&2
  echo "      see $RP_LOG_DIR/reaper-editor.log" >&2
  exit 1
fi
BYTES=$(wc -c < "$OUT")
if [ "$BYTES" -le "$MIN_BYTES" ]; then
  echo "FAIL: LVGL snapshot is only ${BYTES}B (<= ${MIN_BYTES}B) — the editor is blank." >&2
  exit 1
fi
echo "PASS: editor rendered — LVGL snapshot $OUT is ${BYTES}B (blank would be ~159B)."
