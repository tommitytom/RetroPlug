#!/usr/bin/env bash
#
# Headless smoke for the SDL2 standalone (retroplug-sdl). Boots the FULL React/LVGL UI over the real
# Engine with no display server (SDL_VIDEODRIVER=offscreen) and no audio hardware (SDL_AUDIODRIVER=dummy),
# then exercises the standalone-only seams the plugin/JACK harnesses can't reach: on-screen render, the
# MIDI-clock transport estimator, multi-output audio, window resize, and the close guard.
#
# CI-friendly: needs neither Xvfb nor JACK (unlike tools/run-standalone.sh, which drives retroplug-jack).
# Each check is a separate short process; a non-zero exit or a missing marker fails the smoke.
#
# Usage:  tools/run-sdl-smoke.sh            (build first: cmake --build build --target retroplug-sdl)
#         RETROPLUG_SDL=/path/to/bin tools/run-sdl-smoke.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="${RETROPLUG_SDL:-$REPO_DIR/build/bin/retroplug-sdl}"

if [ ! -x "$BIN" ]; then
    echo "error: $BIN not found — build it:" >&2
    echo "  cmake --build build --target retroplug-sdl -j\$(nproc)" >&2
    exit 1
fi

export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail() { echo "SDL smoke FAIL: $*" >&2; exit 1; }

# Run the binary with the given env (KEY=VAL ...) and a frame budget; capture stdout+stderr to $LOG.
# Usage: run <logname> <frames> ENV=VAL ...
run() {
    local name="$1" frames="$2"; shift 2
    LOG="$TMP/$name.log"
    if ! env "$@" RETROPLUG_SDL_EXIT_AFTER_FRAMES="$frames" "$BIN" >"$LOG" 2>&1; then
        cat "$LOG" >&2
        fail "$name: binary exited non-zero"
    fi
}
want() { grep -qE "$2" "$1" || { cat "$1" >&2; fail "$3"; }; }

# 1) Boot + render: the UI must actually draw (RETROPLUG_SDL_REQUIRE_NONBLANK exits non-zero on a flat frame),
#    not merely boot. This is the standalone analogue of the jack `screenshot` check, minus Xvfb.
run boot 90 RETROPLUG_SDL_REQUIRE_NONBLANK=1 RETROPLUG_SDL_SCREENSHOT="$TMP/frame.png"
want "$LOG" "running " "boot: no 'running' banner"
[ -s "$TMP/frame.png" ] || fail "boot: no screenshot written"
echo "  ok: boot + non-blank render"

# 2) Transport from MIDI clock (P4): a 140 BPM synthetic clock derives ~140 and reverts to free-run after stop.
run clock 2 RETROPLUG_SDL_TEST_CLOCK=140
want "$LOG" "clock self-test: target=140.0 derived=1(39|40|41)\." "clock: derived BPM off target"
want "$LOG" "afterStop=0" "clock: 0xFC stop did not clear transport"
echo "  ok: MIDI-clock transport"

# 3) Multi-output audio (P5): an 8-channel device opens with 8 planar buffers.
run multiout 20 RETROPLUG_SDL_TEST_MULTIOUT=8
want "$LOG" "post-multiout: numOutputs=8 planarBufs=8" "multiout: did not open 8 channels"
echo "  ok: multi-output audio"

# 4) Window resize (P6): the SDL window + LVGL display + buffer resize in lockstep.
run resize 20 RETROPLUG_SDL_TEST_RESIZE=800x704
want "$LOG" "post-resize: state=800x704 window=800x704" "resize: did not apply"
echo "  ok: window resize"

# 5) Close guard (P6): an OS close on a clean project isn't vetoed → exits well before the 120-frame budget.
run quit 120 RETROPLUG_SDL_TEST_QUIT=1
want "$LOG" "close-guard test: exited at frame ([0-9]|[1-9][0-9])$" "quit: close guard did not exit early"
echo "  ok: close guard"

echo "SDL smoke OK"
