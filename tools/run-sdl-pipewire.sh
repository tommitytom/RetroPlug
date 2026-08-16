#!/usr/bin/env bash
#
# PipeWire device-selection check for the SDL2 standalone (retroplug-sdl). The SDL smoke
# (tools/run-sdl-smoke.sh) runs with no audio server at all, so nothing there covers WHICH output device
# the PortAudio PipeWire backend picks — the failure users actually hit ("no sound, and Settings doesn't
# show a device selected"), because RetroPlug was playing into a sink they weren't listening to.
#
# This stands up a PRIVATE PipeWire server (its own XDG_RUNTIME_DIR, its own dbus session, two null sinks
# with different priority.session) and asserts the device the standalone opens in each configuration:
# the session default is followed, an explicit pick beats it, a stale pick falls back with a warning, and
# a PipeWire-less environment degrades to ALSA rather than dying.
#
# Not in CI: GitHub's runners have no PipeWire (and no session manager to link streams). Run it locally —
# the devcontainer ships pipewire/wireplumber for exactly this.
#
# Usage:  tools/run-sdl-pipewire.sh          (build first, or use `pnpm sdl:pipewire`)
#         RETROPLUG_SDL=/path/to/bin tools/run-sdl-pipewire.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="${RETROPLUG_SDL:-$REPO_DIR/build/bin/retroplug-sdl}"

for tool in pipewire wireplumber pw-cli pw-metadata dbus-launch; do
    command -v "$tool" >/dev/null 2>&1 || { echo "error: $tool not found (devcontainer installs pipewire/wireplumber/dbus-x11)" >&2; exit 1; }
done
[ -x "$BIN" ] || { echo "error: $BIN not found — build it: cmake --build build --target retroplug-sdl -j\$(nproc)" >&2; exit 1; }

TMP="$(mktemp -d)"
# The private XDG_RUNTIME_DIR is the isolation: that's where the server puts its socket and where every
# client (wireplumber, pw-cli, the standalone) looks for it, so a real desktop session is untouched.
export XDG_RUNTIME_DIR="$TMP/run"
export HOME="$TMP/home"               # audio.json lives in $HOME/.config/retroplug — keep the real one out of it
export SDL_VIDEODRIVER=dummy
mkdir -p "$XDG_RUNTIME_DIR" "$HOME/.config/retroplug"
chmod 700 "$XDG_RUNTIME_DIR"
CFG="$HOME/.config/retroplug/audio.json"
PASS=0; FAIL=0

cleanup() {
    [ -n "${WP_PID:-}" ] && kill "$WP_PID" 2>/dev/null
    [ -n "${PW_PID:-}" ] && kill "$PW_PID" 2>/dev/null
    [ -n "${DBUS_SESSION_BUS_PID:-}" ] && kill "$DBUS_SESSION_BUS_PID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT

# ---- the private server -------------------------------------------------------------------------------
pipewire >"$TMP/pipewire.log" 2>&1 &
PW_PID=$!
sleep 2
# wireplumber is what creates the "default" metadata object (the thing this test is about) and what links
# streams to sinks; it exits immediately without a session bus, hence dbus-launch.
eval "$(dbus-launch --sh-syntax)"
wireplumber >"$TMP/wireplumber.log" 2>&1 &
WP_PID=$!
sleep 3

mksink() {  # mksink <node.name> <description> <priority.session>
    pw-cli create-node adapter "{ factory.name=support.null-audio-sink node.name=$1 node.description=\"$2\" \
        media.class=Audio/Sink object.linger=true audio.position=[FL,FR] priority.session=$3 }" >/dev/null 2>&1
}
mksink rp-high "RetroPlug High Priority" 1000
mksink rp-low  "RetroPlug Low Priority"  500
sleep 2
SINKS=$(pw-cli ls Node 2>/dev/null | grep -c 'node.name = "rp-\(high\|low\)"')
[ "$SINKS" -eq 2 ] || { echo "error: test sinks did not appear (see $TMP/*.log)"; cat "$TMP/wireplumber.log" >&2; exit 1; }

setdefault() { # setdefault <node.name>|none
    if [ "$1" = none ]; then pw-metadata -n default -d 0 default.audio.sink >/dev/null 2>&1
    else pw-metadata -n default 0 default.audio.sink "{\"name\":\"$1\"}" >/dev/null 2>&1; fi
    sleep 0.5
}
writecfg() { if [ "$1" = none ]; then rm -f "$CFG"; else printf '%s' "$1" > "$CFG"; fi; }

# ---- the checks ---------------------------------------------------------------------------------------
# run <label> [ENV=VAL ...] -> $CHOSE = "HostApi|Device" the host opened ("" when it ran muted)
run() {
    local name="$1"; shift
    LOG="$TMP/$name.log"
    env "$@" RETROPLUG_DEBUG_AUDIO=1 RETROPLUG_SDL_EXIT_AFTER_FRAMES=45 timeout 90 "$BIN" >"$LOG" 2>&1
    RC=$?
    CHOSE=$(sed -n "s/.*audio: PortAudio \[\(.*\)\] '\(.*\)' [0-9]* Hz.*/\1|\2/p" "$LOG" | head -1)
}
check() { # check <what> <expected>
    if [ "$CHOSE" = "$2" ] && [ "$RC" -eq 0 ]; then echo "  ok: $1"; PASS=$((PASS+1))
    else echo "  FAIL: $1 — expected '$2', got '${CHOSE:-<muted>}' (rc=$RC)"; cat "$LOG" >&2; FAIL=$((FAIL+1)); fi
}

# The regression this exists for: the session default sink wins even though the OTHER sink outranks it on
# priority.session. Before the fix the backend went purely by priority and played into rp-high.
writecfg none; setdefault rp-low
run session-default; check "follows the session default sink" "PipeWire|RetroPlug Low Priority"

setdefault rp-high
run session-default-flipped; check "follows the session default when it flips" "PipeWire|RetroPlug High Priority"

# No session default configured (a server with no session manager): priority.session is the tiebreak.
setdefault none
run priority-fallback; check "falls back to priority.session" "PipeWire|RetroPlug High Priority"

# An explicit Settings > Audio > Output Device pick outranks the session default.
setdefault rp-high
writecfg '{"sampleRate":48000,"blockSize":512,"outChannels":2,"hostApi":-1,"outputDevice":"RetroPlug Low Priority"}'
run explicit-device; check "explicit device beats the default" "PipeWire|RetroPlug Low Priority"

# A saved device that no longer exists must warn and fall back, not mute.
writecfg '{"sampleRate":48000,"blockSize":512,"outChannels":2,"hostApi":-1,"outputDevice":"Device That Went Away"}'
run stale-device; check "stale device falls back to the default" "PipeWire|RetroPlug High Priority"
grep -q "not found in the chosen driver" "$LOG" && { echo "  ok: stale device warned"; PASS=$((PASS+1)); } \
    || { echo "  FAIL: stale device did not warn"; FAIL=$((FAIL+1)); }

# Explicitly picking the PipeWire driver (a persisted PaHostApiTypeId, 18) resolves the same way.
writecfg '{"sampleRate":44100,"blockSize":128,"outChannels":2,"hostApi":18,"outputDevice":""}'
run explicit-driver; check "explicit PipeWire driver at 44100/128" "PipeWire|RetroPlug High Priority"

# No libpipewire to dlopen: the host API drops out and ALSA takes over (the muOS/desktop-without-PipeWire
# path). The binary must still open a device rather than run muted.
writecfg none
run no-libpipewire PA_PIPEWIRE_SONAME=libpipewire-not-here.so.0
if [ "${CHOSE%%|*}" = "ALSA" ] && [ "$RC" -eq 0 ]; then echo "  ok: degrades to ALSA without libpipewire"; PASS=$((PASS+1))
else echo "  FAIL: expected an ALSA device without libpipewire, got '${CHOSE:-<muted>}' (rc=$RC)"; FAIL=$((FAIL+1)); fi

echo "SDL PipeWire check: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1
