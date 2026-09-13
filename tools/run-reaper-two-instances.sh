#!/usr/bin/env bash
#
# Repro for "loading a SECOND RetroPlug instance crashes the host" (reported against Renoise).
#
# Every other reaper check runs exactly ONE instance, so nothing in the loop covers the state two
# instances share: two control-plane runtimes in one process, and — once both editors are open — two
# LVGL displays driven from one UI thread. This floats N editors in headless Reaper and watches
# whether the host is still alive afterwards.
#
# The verdict is the host's own survival, not a snapshot: a crash here takes Reaper's process down.
# tools/reaper-two-instances.lua appends a marker per step to $RP_STAGE_LOG, so a dead host is
# attributed to the step that killed it (second DSP instance / second editor / closing one editor
# while the others live) instead of just "reaper exited".
#
#   PASS (exit 0): every instance + editor coexisted, and closing one left the others alive.
#   FAIL (exit 1): the host died — the last stage marker names where.
#   SKIP (exit 2): the run never got that far (usually a failed plugin scan).
#
# NOT part of CI — needs a full DAW + X stack. Run via `pnpm reaper:two-instances` (builds
# retroplug-vst3 first) or directly.
#
# Env:
#   RETROPLUG_VST3_NAME  which built VST3/CLAP to host (default: retroplug)
#   RP_TWO_FORMAT        vst3 (default) or clap — Renoise and Reaper can disagree about which format
#                        they load, and the two take entirely different code paths in DPF
#   RP_TWO_COUNT         how many instances to load (default: 2)
#   RP_TWO_TIMEOUT       seconds to wait for the run to reach "done" (default: 90)
#   RP_TWO_AUTOLOAD      .rplg to autoload into EVERY instance (default: none — empty projects).
#                        An empty editor barely touches LVGL after mount; a loaded one re-renders its
#                        emulator tile every frame, so pass a project to exercise the shared state.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

TIMEOUT="${RP_TWO_TIMEOUT:-90}"
FORMAT="${RP_TWO_FORMAT:-vst3}"
COUNT="${RP_TWO_COUNT:-2}"
W=1280; H=720

# Insert the format BY ITS PREFIXED NAME: a bare "RetroPlug" lets Reaper pick whichever format it
# scanned first, which would silently run vst3 twice when clap was asked for.
case "$FORMAT" in
  vst3) FX_TOKEN="VST3i" ;;
  clap) FX_TOKEN="CLAPi"; export RP_WANT_CLAP=1 ;;
  *) echo "run-reaper-two-instances: unknown RP_TWO_FORMAT '$FORMAT' (want vst3 or clap)" >&2; exit 2 ;;
esac

# Cold scan (RP_SCAN_FRESH) like the other editor checks: a fresh scan constructs a throwaway
# runtime before the instances, which is part of the multi-runtime state this exercises.
: "${RP_JOB_TAG:=two-instances-$FORMAT}"
RP_SCAN_FRESH=1 RP_SCREEN_W=$W RP_SCREEN_H=$H
export RP_SCAN_FRESH RP_SCREEN_W RP_SCREEN_H
source "$SCRIPT_DIR/reaper-env.sh"
reaper_env_up

STAGE_LOG="$RP_LOG_DIR/two-instances-stages.log"
LOG="$RP_LOG_DIR/reaper-two-instances.log"
rm -f "$STAGE_LOG" "$LOG"

echo "run-reaper-two-instances: DISPLAY=$DISPLAY  format=$FORMAT  instances=$COUNT  stages=$STAGE_LOG"

# The editors race for one RETROPLUG_SCREENSHOT_PATH, so no snapshot is asserted here — an X grab of
# the whole screen below shows the windows instead.
if [ -n "${RP_TWO_AUTOLOAD:-}" ]; then export RETROPLUG_AUTOLOAD_PROJECT="$RP_TWO_AUTOLOAD"; fi
RP_STAGE_LOG="$STAGE_LOG" RP_TWO_COUNT="$COUNT" RP_TWO_FX="$FX_TOKEN: RetroPlug" \
  reaper -cfgfile "$REAPER_CFG/reaper.ini" -nosplash "$SCRIPT_DIR/reaper-two-instances.lua" \
  >"$LOG" 2>&1 & REAPER_PID=$!

# Wait for the script to reach "done", or for the host to die trying.
CRASHED=0
for ((i = 0; i < TIMEOUT * 2; i++)); do
    if grep -q '^done$' "$STAGE_LOG" 2>/dev/null; then break; fi
    if ! kill -0 "$REAPER_PID" 2>/dev/null; then CRASHED=1; break; fi
    sleep 0.5
done

LAST_STAGE="$(tail -1 "$STAGE_LOG" 2>/dev/null || echo '(no stages reached)')"

# Grab the screen while it is still up (the editors are visible when they survived).
SHOT="$RP_LOG_DIR/reaper-two-instances-screen.png"
if [ "$CRASHED" -eq 0 ] && command -v ffmpeg >/dev/null 2>&1; then
    ffmpeg -y -f x11grab -video_size ${W}x${H} -i "$DISPLAY" -frames:v 1 "$SHOT" >/dev/null 2>&1 || true
fi

# Reap the host ourselves so a normal teardown can't be mistaken for the crash.
if [ "$CRASHED" -eq 0 ]; then kill "$REAPER_PID" 2>/dev/null || true; fi
reaper_env_down
trap - EXIT INT TERM

echo "--- stages reached ---"
cat "$STAGE_LOG" 2>/dev/null || true
echo "----------------------"

# LVGL/DPF assertions print before the host goes down and name the failing invariant, so surface them
# either way — they survive even when the process is killed by a signal that leaves no other trace.
ASSERTS="$(grep -iE 'assert|Sanitizer|Segmentation|SIGSEGV|SIGABRT|terminate called|lv_' "$LOG" 2>/dev/null | head -20 || true)"
if [ -n "$ASSERTS" ]; then
    echo "--- host log (assertions / faults) ---"
    echo "$ASSERTS"
    echo "--------------------------------------"
fi

if [ "$CRASHED" -eq 1 ]; then
    echo "FAIL: the host DIED with $COUNT RetroPlug instances loaded." >&2
    echo "      last stage reached: $LAST_STAGE" >&2
    echo "      full host log: $LOG" >&2
    exit 1
fi
if ! grep -q '^done$' "$STAGE_LOG" 2>/dev/null; then
    echo "SKIP: the run never reached 'done' within ${TIMEOUT}s (last stage: $LAST_STAGE)." >&2
    echo "      usually a failed plugin scan, not a crash — see $LOG" >&2
    exit 2
fi
echo "PASS: $COUNT instances + $COUNT editors coexisted, and closing one left the others alive."
# Informational, never the verdict — so guard it: under `set -e` a bare `[ -f x ] && echo` would take
# the whole script down with status 1 whenever the grab is missing (no ffmpeg), right after PASS.
if [ -f "$SHOT" ]; then echo "      screen grab: $SHOT"; fi
exit 0
