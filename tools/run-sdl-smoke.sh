#!/usr/bin/env bash
#
# Headless smoke for the SDL2 standalone (retroplug-sdl). Boots the FULL React/LVGL UI over the real
# Engine with no display server and no audio hardware (SDL's `dummy` video + audio drivers — `dummy` is
# always compiled in, unlike `offscreen`, which the macOS libsdl.org framework omits), then exercises the
# standalone-only seams the plugin/JACK harnesses can't reach: on-screen render, the MIDI-clock transport
# estimator, multi-output audio, window resize, and the close guard.
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

# The null audio backend pins this run to no device at all: with SDL_AUDIODRIVER exported and SDL second in the
# product's AUDIO_BACKENDS order, an `auto` run on a runner whose PortAudio finds no device would open SDL's dummy
# driver and run on that instead of the sleep-paced null pump these checks mean (and on a desk with no ALSA config the
# default device is the real card). So no audio driver is named and the backend is the null one.
export SDL_VIDEODRIVER=dummy RETROPLUG_SDL_AUDIO_BACKEND=null
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# The /dev/snd guard (tools/sdl-audio-guard): the backend is pinned to null, and this is the second lock - a stray library
# initialisation or a driver fallback still cannot open a real card (PortAudio probes every ALSA device at
# Pa_Initialize). The kit may already have put the same guard in front; this does not add a second copy then.
. "$REPO_DIR/tools/sdl-audio-guard/guard-env.sh" "$TMP/guard"
# This run's own log, unless an outer harness (the verification kit) already set one for the whole run: only a log the
# smoke owns says anything about the smoke alone.
if [ -n "${NOSND_LOG:-}" ]; then OWN_NOSND_LOG=0; else OWN_NOSND_LOG=1; fi
export NOSND_LOG="${NOSND_LOG:-$TMP/nosnd.log}"
: >"$NOSND_LOG"

# This is the standalone a developer runs, so it reads their own config: a saved, enabled controlSurface is CLAIMED at
# startup and its close bytes go out at quit, and a project's saved `midi` selection opens real inputs. A smoke that
# touched the desk's hardware would be a hardware test in disguise, so it gets a scratch config dir of its own (nothing
# saved means nothing claimed) and the test-only port filter set to a name no port has (see midi/PortFilter.hpp's
# midiPortAllowedBy: an EMPTY list means every port, so it has to name something).
export RETROPLUG_USER_CONFIG_DIR="$TMP/cfg"
export RP_MIDI_PORT_ALLOW="RP_NO_PORT"
mkdir -p "$RETROPLUG_USER_CONFIG_DIR"

fail() { echo "SDL smoke FAIL: $*" >&2; exit 1; }

# Run the binary with the given env (KEY=VAL ...) and a frame budget; capture stdout+stderr to $LOG.
# Usage: run <logname> <frames> ENV=VAL ...
run() {
    local name="$1" frames="$2"; shift 2
    LOG="$TMP/$name.log"
    if ! env "$@" RETROPLUG_SDL_EXIT_AFTER_FRAMES="$frames" "$BIN" >"$LOG" 2>&1; then
        cat "$LOG" >&2
        # On a crash, capture a backtrace (lldb on macOS / gdb on Linux) to make CI failures diagnosable.
        if command -v lldb >/dev/null 2>&1; then
            echo "--- lldb backtrace ($name) ---" >&2
            env "$@" RETROPLUG_SDL_EXIT_AFTER_FRAMES="$frames" lldb --batch -o run -k "bt all" -k quit -- "$BIN" >&2 2>&1 || true
        elif command -v gdb >/dev/null 2>&1; then
            echo "--- gdb backtrace ($name) ---" >&2
            env "$@" RETROPLUG_SDL_EXIT_AFTER_FRAMES="$frames" gdb -batch -ex run -ex "thread apply all bt" --args "$BIN" >&2 2>&1 || true
        fi
        fail "$name: binary exited non-zero"
    fi
}
want() { grep -qE "$2" "$1" || { cat "$1" >&2; fail "$3"; }; }
# A line the run MUST NOT print: the rendezvous's own failure report. It means the audio service never applied the
# `audio` section, so the app started on the defaults after the half-second bound.
refuse() { ! grep -qF "audio: the UI did not apply its settings" "$1" || { cat "$1" >&2; fail "$2"; }; }

# 1) Boot + render: the UI must actually draw (RETROPLUG_SDL_REQUIRE_NONBLANK exits non-zero on a flat frame),
#    not merely boot. This is the standalone analogue of the jack `screenshot` check, minus Xvfb.
run boot 90 RETROPLUG_SDL_REQUIRE_NONBLANK=1 RETROPLUG_SDL_SCREENSHOT="$TMP/frame.png"
want "$LOG" "running " "boot: no 'running' banner"
refuse "$LOG" "boot: the audio service never applied its settings"
[ -s "$TMP/frame.png" ] || fail "boot: no screenshot written"
echo "  ok: boot + non-blank render"

# 1b) The order the host opens things in (docs/modular-graph-phase-f5b-design.md section 6.6): the MIDI stream, then the
#     providers' restore, then the audio device LAST. A device that came up first would start pulling blocks into an
#     engine the restore has not shaped yet, and the order is the one the app always had (the control surface's claim,
#     then MIDI, then audio). Read off the host's own lines, which is what a user's log shows.
run order 20
midi_at=$(grep -n -F "MIDI: virtual ports" "$LOG" | head -1 | cut -d: -f1)
audio_at=$(grep -n -F "audio: on the null device" "$LOG" | head -1 | cut -d: -f1)
[ -n "$midi_at" ] && [ -n "$audio_at" ] || fail "startup order: the MIDI stream or the audio device never reported"
[ "$midi_at" -lt "$audio_at" ] \
    || fail "startup order: the audio device opened before the MIDI stream (line $audio_at, MIDI at $midi_at)"
echo "  ok: the MIDI stream opens before the audio device"

# 2) Transport from MIDI clock (P4): a 140 BPM synthetic clock derives ~140 and reverts to free-run after stop.
run clock 2 RETROPLUG_SDL_TEST_CLOCK=140
want "$LOG" "clock self-test: target=140.0 derived=1(39|40|41)\." "clock: derived BPM off target"
want "$LOG" "afterStop=0" "clock: 0xFC stop did not clear transport"
# ...and with no clock master, the host's own transport is STOPPED at boot. That default is what keeps a
# SYNC=MIDI cart from being clocked by a standalone nobody has pressed play on (Instance menu > MIDI Clock).
want "$LOG" "live=0" "clock: the host transport is running at boot (should start stopped)"
# ...and the MIDI Clock submenu's controls actually reach it: this starts the transport through the JS global the row
# calls and loads a project whose `midi.clockBpm` is 140 (the tempo is the PROJECT's now, applied by the midi package's
# service), so a missing bind reads back the default instead of quietly dropping the submenu from the menu.
printf '{"schemaVersion":"4","settings":{"midi":{"clockBpm":140}},"systems":[]}\n' >"$TMP/tempo.rplg"
run transport 30 RETROPLUG_SDL_TEST_TRANSPORT=1 RETROPLUG_AUTOLOAD_PROJECT="$TMP/tempo.rplg"
want "$LOG" "transport self-test: playing=1 bpm=140" "transport: the MIDI Clock seam did not start/retempo the clock"
refuse "$LOG" "transport: the audio service never applied its settings"
echo "  ok: MIDI-clock transport + stopped at boot + menu play/stop"

# 3) Multi-output audio (P5): an 8-channel device opens with 8 planar buffers.
run multiout 20 RETROPLUG_SDL_TEST_MULTIOUT=8
want "$LOG" "post-multiout: numOutputs=8 planarBufs=8" "multiout: did not open 8 channels"
echo "  ok: multi-output audio"

# 4) Window resize (P6): the SDL window + LVGL display + buffer resize in lockstep.
run resize 20 RETROPLUG_SDL_TEST_RESIZE=800x704
want "$LOG" "post-resize: state=800x704 window=800x704" "resize: did not apply"
echo "  ok: window resize"

# 4b) A request below the 480x432 floor (a 1x/2x grid) must land CLAMPED, in lockstep, and must not read as a
#     tiling takeover. Applying the raw request left the LVGL surface smaller than the window can ever be (a
#     mismatch the compositor then scales - the stretched-tile bug), and the clamped size coming back looked
#     like a size we never asked for, latching wmControlled and killing fit-to-grid for the session.
run resize-min 20 RETROPLUG_SDL_TEST_RESIZE=320x288
want "$LOG" "post-resize: state=480x432 window=480x432 .*wmControlled=0" "resize: sub-minimum request not clamped in lockstep"
echo "  ok: window resize clamps to the minimum"

# 5) Mouse wheel: a real SDL_MOUSEMOTION + SDL_MOUSEWHEEL pair through the SDL queue reaches the handler with
#    the sign intact, dispatched at the motion-tracked cursor (non-zero = the motion landed; the window is
#    whatever size the UI settles on, so don't pin the centre). Only the SDL half — the scroll it drives is
#    shared with the plugin/UI-test hosts and is asserted end-to-end (rows move, bounded at both ends) by
#    `pnpm test:ui wheel-scroll`.
run wheel 60 RETROPLUG_SDL_TEST_WHEEL=-3
want "$LOG" "wheel: notches=\(0\.00,-3\.00\) at \([1-9][0-9]*,[1-9][0-9]*\)" \
     "wheel: SDL_MOUSEWHEEL did not reach the handler at the tracked cursor"
echo "  ok: mouse wheel"

# 5b) Settings > Audio through the JS seam: the Apply row's own call, with an `audio` section's fields, then the
#     description read back. The hooks answer what the rows read: the backends, and the status the device adopted (the
#     rate and block an Apply asked for), with the null fallback this run is pinned to and no mute.
run audio-hook 20 RETROPLUG_SDL_TEST_AUDIO_HOOK=1
want "$LOG" 'post-audio-hook: .*"backends":\[\{"id":"portaudio"' "audio hooks: the description lost the backends"
want "$LOG" 'post-audio-hook: .*"backend":"null"' "audio hooks: the null backend pin did not hold"
want "$LOG" 'post-audio-hook: .*"sampleRate":44100' "audio hooks: an Apply did not adopt the rate"
want "$LOG" 'post-audio-hook: .*"blockFrames":256' "audio hooks: an Apply did not adopt the block"
want "$LOG" 'post-audio-hook: .*"error":""' "audio hooks: an Apply muted the run"
echo "  ok: Settings > Audio through the JS seam"

# 5c) `--block-size` over the section (E12): the file asks for 512, the flag for 256, and the device the service
#     applied runs at the flag's block. A launch script's quantum must win over the saved choice without being saved.
SAVED_CFG="$RETROPLUG_USER_CONFIG_DIR/config.json"
printf '%s\n' '{"schemaVersion":3,"audio":{"backend":"null","driver":"","device":"","sampleRate":48000,"blockSize":512,'\
'"outChannels":2}}' >"$SAVED_CFG"
run block-saved 30 RETROPLUG_SDL_TEST_AUDIO_STATUS=1
want "$LOG" 'post-audio-status: .*"blockFrames":512' "block override: the saved section did not open at its block"
LOG="$TMP/block-flag.log"
if ! env RETROPLUG_SDL_TEST_AUDIO_STATUS=1 RETROPLUG_SDL_EXIT_AFTER_FRAMES=30 "$BIN" --block-size 256 >"$LOG" 2>&1; then
    cat "$LOG" >&2; fail "block override: binary exited non-zero"
fi
want "$LOG" 'post-audio-status: .*"blockFrames":256' "block override: --block-size did not win over the section"
rm -f "$SAVED_CFG"
echo "  ok: --block-size overrides the audio section"
printf '%s\n' '{"schemaVersion":3,"audio":{"backend":"null","driver":"","device":"","sampleRate":48000,"blockSize":512,'\
'"outChannels":2}}' >"$RETROPLUG_USER_CONFIG_DIR/config.json"

# 6) Close guard (P6): an OS close on a clean project isn't vetoed → exits well before the 120-frame budget.
run quit 120 RETROPLUG_SDL_TEST_QUIT=1
want "$LOG" "close-guard test: exited at frame ([0-9]|[1-9][0-9])$" "quit: close guard did not exit early"
echo "  ok: close guard"

# 7) Idle reclaim: the frame loop frees the plans the audio thread handed back, with no further publish, which is the
#    tick HostServices::reclaimOnIdle exists for. The probe publishes a plan, then one that replaces it; it passes only when
#    an idle tick frees the replaced one, so a loop that stops calling the seam never prints the pass line (it prints
#    the give-up instead). Arming at frame 60 keeps the swap clear of the control plane's own boot publishes.
run idle 200 RETROPLUG_IDLE_RECLAIM_TEST=60
want "$LOG" "\[idle-reclaim\] self-test: tick [0-9]+ freed [1-9][0-9]* replaced plan" \
     "idle: the frame loop did not reclaim what the audio thread handed back"
echo "  ok: idle reclaim"

# The null pin's other half, and the one CI can hold: a run on the null backend refuses NOTHING and loads NO audio
# library. The guard's log is where that shows - an entry means a backend or a device was reached for after all, which is
# the thing the pin exists to prevent. Only when the smoke owns the log (an outer harness's covers its whole run).
if [ "$OWN_NOSND_LOG" = 1 ] && [ -s "$NOSND_LOG" ]; then
    cat "$NOSND_LOG" >&2
    fail "the null-pinned run touched a device path or loaded an audio library (see the log above)"
fi
echo "SDL smoke OK"
