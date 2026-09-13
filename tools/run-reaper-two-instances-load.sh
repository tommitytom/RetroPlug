#!/usr/bin/env bash
#
# Repro for "loading a ROM in the SECOND instance freezes BOTH editors" (reported against Renoise,
# reproduced by the user in Reaper). The exact reported sequence:
#
#   1. instance 1, editor floated, ROM click-loaded through the UI  -> fine
#   2. instance 2, editor floated, showing the start menu           -> fine
#   3. ROM click-loaded through the UI in instance 2                -> BOTH editors freeze
#
# It has to be the UI-driven load, not an autoloaded project: tools/run-reaper-two-instances.sh already
# shows that two instances which come up with a ROM already in the control plane coexist happily. What
# is untested is a load arriving through the editor while ANOTHER editor is live on the same UI thread.
#
# Two independent liveness signals, because a freeze leaves the process alive and so proves nothing by
# itself:
#   heartbeat  - the ReaScript's defer counter. Reaper runs defer on the main thread, the same thread
#                both editors idle on, so a blocked UI thread stops it dead.
#   rendering  - the mtime of the plugin's own LVGL snapshot, rewritten every interval from uiIdle.
#                It stops advancing only when NO editor is still rendering.
# Both editors share one RETROPLUG_SCREENSHOT_PATH (it's process-wide), so the render signal proves
# "at least one editor lives" - which is the right test for a symptom reported as BOTH freezing, but
# it cannot single out one frozen editor. The heartbeat is the decisive one.
#
#   PASS (exit 0): both loads landed and the host stayed live afterwards.
#   FAIL (exit 1): the host froze - the phase it froze in is named.
#   SKIP (exit 2): a click-load never registered, so the sequence never got far enough to judge.
#
# NOT part of CI - needs a full DAW + X stack. Run via `pnpm reaper:two-instances-load`.
#
# Env:
#   RP_TWO_FORMAT    vst3 (default) or clap
#   RP_LOAD_MODE     direct (default) = the built-in "Load mGB" row; dialog = the "Load..." row, which
#                    opens an OS file browser first
#   RP_LOAD_X_OFF    start-menu row offset from the FX window origin (default 135 / 110)
#   RP_LOAD_Y_OFF
#   RP_WATCH_SECS    how long to watch for a freeze after the second load (default 25)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

FORMAT="${RP_TWO_FORMAT:-vst3}"
LOAD_X_OFF="${RP_LOAD_X_OFF:-135}"
LOAD_Y_OFF="${RP_LOAD_Y_OFF:-110}"          # the "Load mGB (GB MIDI Synth)" row
LOAD_DIALOG_Y_OFF="${RP_LOAD_DIALOG_Y_OFF:-80}" # the "Load..." row, 30px above it
# Which start-menu row loads the ROM. `direct` takes the built-in mGB item, which loads straight from
# the bundled ROM; `dialog` takes "Load...", which opens an OS file browser first - a different code
# path (NativeFileDialog/pfd where a desktop helper exists, DPF's own in-process X11 browser where it
# does not) and the one a user actually loads their own ROM through.
LOAD_MODE="${RP_LOAD_MODE:-direct}"
WATCH_SECS="${RP_WATCH_SECS:-25}"
GRID_MAX_BYTES="${RP_GRID_MAX_BYTES:-2500}"  # a loaded grid is small + mostly black; the menu is larger
BLANK_MAX_BYTES="${RP_BLANK_MAX_BYTES:-300}" # below this the frame is uniform, i.e. nothing rendered
W=1280; H=720

case "$FORMAT" in
  vst3) FX_TOKEN="VST3i" ;;
  clap) FX_TOKEN="CLAPi"; export RP_WANT_CLAP=1 ;;
  *) echo "run-reaper-two-instances-load: unknown RP_TWO_FORMAT '$FORMAT' (want vst3 or clap)" >&2; exit 2 ;;
esac

case "$LOAD_MODE" in
  direct) LOAD_ROW_NAME="Load mGB"; ROW_Y_OFF="$LOAD_Y_OFF"; SETTLE2=settle_no_menu ;;
  dialog) LOAD_ROW_NAME="Load..."; ROW_Y_OFF="$LOAD_DIALOG_Y_OFF"; SETTLE2=settle_none ;;
  *) echo "run-reaper-two-instances-load: unknown RP_LOAD_MODE '$LOAD_MODE' (want direct or dialog)" >&2; exit 2 ;;
esac

: "${RP_JOB_TAG:=two-load-$FORMAT-$LOAD_MODE}"
RP_SCAN_FRESH=1 RP_SCREEN_W=$W RP_SCREEN_H=$H
export RP_SCAN_FRESH RP_SCREEN_W RP_SCREEN_H
source "$SCRIPT_DIR/reaper-env.sh"
reaper_env_up

SIGDIR="$RP_LOG_DIR"
SNAP="$REPO_DIR/build/reaper-two-load.png"
LOG="$RP_LOG_DIR/reaper-two-load.log"
export RP_TWO_SIGDIR="$SIGDIR"
rm -f "$SNAP" "$SNAP".loaded1 "$SNAP".menu2 "$SNAP".loaded2 "$LOG" \
      "$SIGDIR"/e1-shown "$SIGDIR"/e2-shown "$SIGDIR"/go2 "$SIGDIR"/hb "$SIGDIR"/giveup \
      "$SIGDIR"/two-load-lua.log

echo "run-reaper-two-instances-load: DISPLAY=$DISPLAY  format=$FORMAT  load-mode=$LOAD_MODE  snapshot=$SNAP"

RP_TWO_FX="$FX_TOKEN: RetroPlug" \
RETROPLUG_SCREENSHOT_PATH="$SNAP" \
RETROPLUG_SCREENSHOT_INTERVAL_MS=400 \
  reaper -cfgfile "$REAPER_CFG/reaper.ini" -nosplash "$SCRIPT_DIR/reaper-two-instances-load.lua" \
  >"$LOG" 2>&1 & REAPER_PID=$!

sizeof() { wc -c < "$1" 2>/dev/null || echo 0; }
# A loaded grid is small and mostly black, the start menu is a big text PNG - but a UNIFORM frame
# (blank/black editor, or the instant after the menu closed and before the grid drew) is smaller still,
# and without the floor it classifies as a grid and a missed click reads as a successful load.
is_grid() { [ "$1" -ge "$BLANK_MAX_BYTES" ] && [ "$1" -le "$GRID_MAX_BYTES" ]; }

# Grab the whole X screen, which is the only view that shows BOTH editors at once: the plugin's own
# LVGL snapshot is a single process-wide path the two editors take turns rewriting, so it can never
# say which of them is still rendering.
grab() { # grab <name>
    command -v ffmpeg >/dev/null 2>&1 || return 0
    ffmpeg -y -f x11grab -video_size ${W}x${H} -i "$DISPLAY" -frames:v 1 \
        "$REPO_DIR/build/reaper-two-load-screen-$1.png" >/dev/null 2>&1 || true
}

# A wedged host is exactly what this test looks for, and a wedged host does not always answer SIGTERM -
# the observed freeze spins at 100% CPU inside LVGL and never returns to a signal-handling point. So
# reap it hard, or the script hangs on the bug it just detected (reaper_env_down `wait`s on it).
reap_host() {
    [ -n "${REAPER_PID:-}" ] || return 0
    kill "$REAPER_PID" 2>/dev/null || true
    local i
    for ((i = 0; i < 10; i++)); do kill -0 "$REAPER_PID" 2>/dev/null || return 0; sleep 0.3; done
    kill -9 "$REAPER_PID" 2>/dev/null || true
    sleep 0.5
}

wait_for() { # wait_for <file> <secs>
    local f="$1" n="$2" i
    for ((i = 0; i < n * 2; i++)); do [ -f "$f" ] && return 0; sleep 0.5; done
    return 1
}

# Move an instance's editor somewhere known. Reaper stacks the two FX windows nearly on top of each
# other, which hides instance 1 behind instance 2 in every screen grab - and the grab is the ONLY view
# that shows what each editor is doing, since they share one snapshot path. Tiling them also makes the
# click coordinates stable instead of dependent on where the host happened to put the window.
place_editor() { # place_editor <track number> <x> <y>
    local fxw
    fxw=$(xdotool search --name "RetroPlug.*Track $1" 2>/dev/null | head -1 || true)
    [ -z "$fxw" ] && return 1
    xdotool windowmove "$fxw" "$2" "$3" 2>/dev/null || true
    sleep 0.3
}

# Click a start-menu row in a specific instance's editor. Reaper suffixes the FX window title with the
# track, which is the only thing telling the two editors apart on screen. Retried: a single press is
# easily missed at LVGL's 60fps indev poll, so the press is HELD and the whole click repeated.
click_load() { # click_load <track number> <settle-callback> <row y offset>
    local track="$1" settle="$2" yoff="$3" attempt fxw
    fxw=$(xdotool search --name "RetroPlug.*Track $track" 2>/dev/null | head -1 || true)
    if [ -z "$fxw" ]; then echo "  (no FX window found for Track $track)"; return 1; fi
    eval "$(xdotool getwindowgeometry --shell "$fxw" 2>/dev/null || true)" # sets X, Y, WIDTH, HEIGHT
    local cx=$(( ${X:-114} + LOAD_X_OFF )) cy=$(( ${Y:-100} + yoff ))
    echo "  click '$LOAD_ROW_NAME' for Track $track at ($cx,$cy) [win $fxw]"
    for attempt in 1 2 3 4 5 6; do
        xdotool mousemove "$cx" "$cy" 2>/dev/null || true
        sleep 0.3
        xdotool mousedown 1 2>/dev/null || true; sleep 0.3; xdotool mouseup 1 2>/dev/null || true
        if "$settle"; then return 0; fi
        # In dialog mode a second click would stack a second file browser, so one attempt is the lot.
        [ "$LOAD_MODE" = "dialog" ] && return 1
        echo "  load-click attempt $attempt for Track $track didn't register, retrying…"
    done
    return 1
}

# In dialog mode nothing loads, so there is no settle to wait for — the file browser opening (or
# wedging the UI thread) is the whole event. Give it a moment, then let the watch phase judge.
settle_none() { sleep 3; return 0; }

# Settle predicate for instance 1: the shared snapshot becoming a grid means the only editor open has
# loaded the ROM.
settle_grid() {
    local i sz
    for ((i = 0; i < 16; i++)); do
        sleep 0.5
        sz=$(sizeof "$SNAP")
        is_grid "$sz" && return 0
    done
    return 1
}

# Settle predicate for instance 2: instance 1 is ALREADY a grid, so the shared snapshot alternates
# between the two editors and a small read proves nothing on its own. Instance 2 has loaded once the
# menu-sized frame stops appearing across a run of consecutive reads.
settle_no_menu() {
    local i sz small=0
    for ((i = 0; i < 24; i++)); do
        sleep 0.5
        sz=$(sizeof "$SNAP")
        if is_grid "$sz"; then small=$((small + 1)); else small=0; fi
        [ "$small" -ge 8 ] && return 0
    done
    return 1
}

PHASE="startup"
fail_skip() { echo "SKIP: $1" >&2; reap_host; reaper_env_down; trap - EXIT INT TERM; exit 2; }

# --- 1. instance 1 up, ROM click-loaded through its editor ---
wait_for "$SIGDIR/e1-shown" 90 || fail_skip "instance 1 never floated (plugin scan failed? see $LOG)"
reaper_wait_snapshot "$SNAP" "${RP_EDITOR_TIMEOUT:-45}" || fail_skip "instance 1's editor never rendered"
place_editor 1 10 60 || true   # left half — see place_editor on why they must not overlap
sleep 1
PHASE="loading instance 1"
LOAD_ROW_NAME="Load mGB" click_load 1 settle_grid "$LOAD_Y_OFF" || fail_skip "could not click-load the ROM into instance 1 (adjust RP_LOAD_X_OFF/Y_OFF)"
cp -f "$SNAP" "$SNAP".loaded1 2>/dev/null || true
grab 1-inst1-loaded
echo "  instance 1 loaded ($(sizeof "$SNAP".loaded1)B grid)"

# --- 2. instance 2 up, start menu ---
PHASE="opening instance 2"
touch "$SIGDIR/go2"
wait_for "$SIGDIR/e2-shown" 60 || fail_skip "instance 2 never floated"
place_editor 2 650 60 || true  # right half
# Its start menu is the big frame that now shows up in the shared snapshot.
for _ in $(seq 1 40); do sz=$(sizeof "$SNAP"); is_grid "$sz" || break; sleep 0.5; done
cp -f "$SNAP" "$SNAP".menu2 2>/dev/null || true
grab 2-inst2-menu
echo "  instance 2 open ($(sizeof "$SNAP".menu2)B — start menu expected to be the larger frame)"

# --- 3. the reported trigger: load a ROM in instance 2 while instance 1 is live ---
PHASE="loading instance 2"
HB_BEFORE=$(cat "$SIGDIR/hb" 2>/dev/null || echo 0)
click_load 2 "$SETTLE2" "$ROW_Y_OFF" || true   # a freeze here IS the bug, so never bail out on a failed settle
cp -f "$SNAP" "$SNAP".loaded2 2>/dev/null || true
grab 3-inst2-loaded

# --- 4. watch both liveness signals ---
PHASE="post-load watch"
echo "watching for ${WATCH_SECS}s (heartbeat before second load: $HB_BEFORE)"
HB_STALL=0; RENDER_STALL=0
LAST_HB="$(cat "$SIGDIR/hb" 2>/dev/null || echo 0)"
LAST_MTIME="$(stat -c %Y "$SNAP" 2>/dev/null || echo 0)"
DIED=0
for ((i = 0; i < WATCH_SECS * 2; i++)); do
    sleep 0.5
    if ! kill -0 "$REAPER_PID" 2>/dev/null; then DIED=1; break; fi
    HB_NOW="$(cat "$SIGDIR/hb" 2>/dev/null || echo 0)"
    MT_NOW="$(stat -c %Y "$SNAP" 2>/dev/null || echo 0)"
    if [ "$HB_NOW" = "$LAST_HB" ]; then HB_STALL=$((HB_STALL + 1)); else HB_STALL=0; LAST_HB="$HB_NOW"; fi
    if [ "$MT_NOW" = "$LAST_MTIME" ]; then RENDER_STALL=$((RENDER_STALL + 1)); else RENDER_STALL=0; LAST_MTIME="$MT_NOW"; fi
    # 6 consecutive half-second reads without motion = 3s dead. The snapshot interval is 400ms and the
    # heartbeat 250ms, so 3s is far past any ordinary hiccup.
    [ "$HB_STALL" -ge 6 ] && break
    [ "$RENDER_STALL" -ge 6 ] && break
done
HB_AFTER="$(cat "$SIGDIR/hb" 2>/dev/null || echo 0)"
[ "$DIED" -eq 0 ] && grab 4-final  # both editors side by side: the only frame that shows each one's state

reap_host
reaper_env_down
trap - EXIT INT TERM

L1=$(sizeof "$SNAP".loaded1); M2=$(sizeof "$SNAP".menu2); L2=$(sizeof "$SNAP".loaded2)
cp -f "$SNAP".loaded1 "$REPO_DIR/build/reaper-two-load-1-loaded.png"  2>/dev/null || true
cp -f "$SNAP".menu2   "$REPO_DIR/build/reaper-two-load-2-menu.png"    2>/dev/null || true
cp -f "$SNAP".loaded2 "$REPO_DIR/build/reaper-two-load-3-loaded2.png" 2>/dev/null || true
echo "snapshot bytes — inst1-loaded=$L1  inst2-menu=$M2  inst2-loaded=$L2 (grid <= ${GRID_MAX_BYTES}B)"
echo "heartbeat — before second load: $HB_BEFORE  after watch: $HB_AFTER"
echo "screen grabs: build/reaper-two-load-screen-{1-inst1-loaded,2-inst2-menu,3-inst2-loaded,4-final}.png"

if [ "$DIED" -eq 1 ]; then
    echo "FAIL: the host DIED during '$PHASE' (a crash, not a freeze)." >&2
    echo "      see $LOG" >&2
    exit 1
fi
if [ "$HB_STALL" -ge 6 ]; then
    echo "FAIL: the host's main thread FROZE after loading a ROM in the second instance —" >&2
    echo "      the ReaScript heartbeat stopped at $HB_AFTER. Bug reproduced." >&2
    echo "      frames: build/reaper-two-load-{1-loaded,2-menu,3-loaded2}.png" >&2
    exit 1
fi
if [ "$RENDER_STALL" -ge 6 ]; then
    echo "FAIL: both editors STOPPED RENDERING after loading a ROM in the second instance —" >&2
    echo "      the LVGL snapshot stopped being rewritten while the host stayed responsive." >&2
    echo "      frames: build/reaper-two-load-{1-loaded,2-menu,3-loaded2}.png" >&2
    exit 1
fi
if ! is_grid "$L2"; then
    echo "SKIP: instance 2 never showed a loaded grid (${L2}B), but nothing froze either —" >&2
    echo "      the click probably missed; adjust RP_LOAD_X_OFF / RP_LOAD_Y_OFF." >&2
    exit 2
fi
echo "PASS: a ROM loaded through the second instance's editor left both instances live."
exit 0
