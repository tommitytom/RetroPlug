#!/usr/bin/env bash
#
# PipeWire device-selection check for the SDL2 standalone (retroplug-sdl). The SDL smoke
# (tools/run-sdl-smoke.sh) runs with no audio server at all, so nothing there covers WHICH output device
# the PortAudio PipeWire backend picks — the failure users actually hit ("no sound, and Settings doesn't
# show a device selected"), because RetroPlug was playing into a sink they weren't listening to.
#
# This stands up a PRIVATE PipeWire server (its own XDG_RUNTIME_DIR) with three null sinks and asserts the
# device the standalone opens in each configuration: the session default is followed, an explicit pick
# beats it, a stale pick falls back with a warning, and a PipeWire-less environment degrades to ALSA.
#
# The server is deliberately session-manager-free. Everything the check needs — the sinks AND the "default"
# metadata object that normally only wireplumber creates — is declared as `context.objects` in a daemon-only
# config, so the objects exist the moment the daemon is up. wireplumber needs logind/dbus and exits without
# them in a container, which made an earlier version of this check silently degenerate (no metadata → every
# expectation collapsed onto the priority fallback and only one case failed, intermittently). Nothing here
# asserts audio FLOW, which is the one thing a session manager would be needed for: these are enumeration
# and selection checks, read back from the host's own "audio: PortAudio [host] 'device'" line.
#
# `rp-decoy` exists to keep every assertion discriminating: it always outranks the others on
# priority.session and is never the session default, so "followed the default" can't be confused with
# "fell back to priority".
#
# Not in CI: GitHub's runners have no PipeWire. Run it locally — the devcontainer ships one.
#
# Usage:  tools/run-sdl-pipewire.sh          (build first, or use `pnpm sdl:pipewire`)
#         RETROPLUG_SDL=/path/to/bin tools/run-sdl-pipewire.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="${RETROPLUG_SDL:-$REPO_DIR/build/bin/retroplug-sdl}"

for tool in pipewire pw-cli pw-metadata; do
    command -v "$tool" >/dev/null 2>&1 || { echo "error: $tool not found (devcontainer installs pipewire + pipewire-bin)" >&2; exit 1; }
done
[ -x "$BIN" ] || { echo "error: $BIN not found — build it: cmake --build build --target retroplug-sdl -j\$(nproc)" >&2; exit 1; }

TMP="$(mktemp -d)"
# The private XDG_RUNTIME_DIR is the isolation: that's where the server puts its socket and where every
# client (pw-cli, the standalone) looks for it, so a real desktop session is untouched.
export XDG_RUNTIME_DIR="$TMP/run"
export HOME="$TMP/home"               # audio.json lives in $HOME/.config/retroplug — keep the real one out of it
export SDL_VIDEODRIVER=dummy
mkdir -p "$XDG_RUNTIME_DIR" "$HOME/.config/retroplug" "$TMP/pwconf/pipewire.conf.d"
chmod 700 "$XDG_RUNTIME_DIR"
CFG="$HOME/.config/retroplug/audio.json"
PASS=0; FAIL=0

cleanup() {
    [ -n "${WP_PID:-}" ] && kill "$WP_PID" 2>/dev/null   # only set for the wide-output checks
    [ -n "${PW_PID:-}" ] && kill "$PW_PID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT

# ---- the private server -------------------------------------------------------------------------------
# PIPEWIRE_CONFIG_DIR is set on the daemon only — exporting it would send clients looking for a client.conf
# that isn't there.
cp /usr/share/pipewire/pipewire.conf "$TMP/pwconf/pipewire.conf"
cat > "$TMP/pwconf/pipewire.conf.d/99-retroplug-test.conf" <<'CONF'
context.objects = [
  # The object a session manager would own: what "the default sink" means for this session.
  { factory = metadata args = { metadata.name = default } }

  { factory = adapter args = { factory.name = support.null-audio-sink node.name = rp-decoy
      node.description = "RetroPlug Decoy" media.class = Audio/Sink object.linger = true
      audio.position = [ FL FR ] priority.session = 2000 } }
  { factory = adapter args = { factory.name = support.null-audio-sink node.name = rp-high
      node.description = "RetroPlug High Priority" media.class = Audio/Sink object.linger = true
      audio.position = [ FL FR ] priority.session = 1000 } }
  { factory = adapter args = { factory.name = support.null-audio-sink node.name = rp-low
      node.description = "RetroPlug Low Priority" media.class = Audio/Sink object.linger = true
      audio.position = [ FL FR ] priority.session = 500 } }
]
CONF

PIPEWIRE_CONFIG_DIR="$TMP/pwconf" pipewire >"$TMP/pipewire.log" 2>&1 &
PW_PID=$!
for _ in $(seq 40); do pw-cli info 0 >/dev/null 2>&1 && break; sleep 0.25; done
pw-cli info 0 >/dev/null 2>&1 || { echo "error: the test PipeWire server never came up" >&2; cat "$TMP/pipewire.log" >&2; exit 1; }
SINKS=$(pw-cli ls Node 2>/dev/null | grep -c 'node.name = "rp-\(decoy\|high\|low\)"')
[ "$SINKS" -eq 3 ] || { echo "error: expected 3 test sinks, found $SINKS" >&2; cat "$TMP/pipewire.log" >&2; exit 1; }

# setdefault <node.name>|none — and PROVE it took, so a silent metadata failure can't quietly turn every
# expectation into the priority fallback (which is exactly how the first version of this check went flaky).
setdefault() {
    local want="$1" got
    if [ "$want" = none ]; then
        pw-metadata -n default -d 0 default.audio.sink >/dev/null 2>&1
        sleep 0.3
        got=$(pw-metadata -n default 2>/dev/null | grep -c "key:'default.audio.sink' value:'{")
        [ "$got" -eq 0 ] || { echo "error: could not clear the session default sink" >&2; exit 1; }
    else
        pw-metadata -n default 0 default.audio.sink "{\"name\":\"$want\"}" >/dev/null 2>&1
        sleep 0.3
        pw-metadata -n default 2>/dev/null | grep -q "default.audio.sink' value:'{\"name\":\"$want\"}'" \
            || { echo "error: the session default sink did not take '$want' — the test server has no usable metadata" >&2; exit 1; }
    fi
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

# The regression this exists for: the session default wins over the sink that outranks it on
# priority.session. Before the fix the backend went purely by priority and played into rp-decoy.
writecfg none; setdefault rp-low
run session-default; check "follows the session default sink" "PipeWire|RetroPlug Low Priority"

setdefault rp-high
run session-default-flipped; check "follows the session default when it flips" "PipeWire|RetroPlug High Priority"

# No session default configured (a server with no session manager): priority.session is the tiebreak.
setdefault none
run priority-fallback; check "falls back to priority.session" "PipeWire|RetroPlug Decoy"

# An explicit Settings > Audio > Output Device pick outranks the session default.
setdefault rp-high
writecfg '{"sampleRate":48000,"blockSize":512,"outChannels":2,"hostApi":-1,"outputDevice":"RetroPlug Low Priority"}'
run explicit-device; check "explicit device beats the default" "PipeWire|RetroPlug Low Priority"

# A saved device that no longer exists must warn and fall back to the default, not mute.
writecfg '{"sampleRate":48000,"blockSize":512,"outChannels":2,"hostApi":-1,"outputDevice":"Device That Went Away"}'
run stale-device; check "stale device falls back to the default" "PipeWire|RetroPlug High Priority"
grep -q "not found in the chosen driver" "$LOG" && { echo "  ok: stale device warned"; PASS=$((PASS+1)); } \
    || { echo "  FAIL: stale device did not warn"; FAIL=$((FAIL+1)); }

# Explicitly picking the PipeWire driver (a persisted PaHostApiTypeId, 18) resolves the same way.
setdefault rp-low
writecfg '{"sampleRate":44100,"blockSize":128,"outChannels":2,"hostApi":18,"outputDevice":""}'
run explicit-driver; check "explicit PipeWire driver at 44100/128" "PipeWire|RetroPlug Low Priority"

# No libpipewire to dlopen: the host API drops out and ALSA takes over (the muOS/desktop-without-PipeWire
# path). The binary must still open a device rather than run muted.
writecfg none
run no-libpipewire PA_PIPEWIRE_SONAME=libpipewire-not-here.so.0
if [ "${CHOSE%%|*}" = "ALSA" ] && [ "$RC" -eq 0 ]; then echo "  ok: degrades to ALSA without libpipewire"; PASS=$((PASS+1))
else echo "  FAIL: expected an ALSA device without libpipewire, got '${CHOSE:-<muted>}' (rc=$RC)"; FAIL=$((FAIL+1)); fi

# ---- wide output (Settings > Audio > Out Channels 4/6/8) ----------------------------------------------
# Runs LAST and needs a session manager, so it can't share the hermetic server above: the adapter only
# materialises its ports once something links the stream, and wireplumber also rewrites default.audio.sink
# (which would trample the selection cases). The sinks here are stereo, which is the case that used to be
# refused outright with "Invalid number of channels" — and, once it opened, the case where the stream came
# out 2 ports wide with the other stems silently dropped. Hence both assertions.
start_session_manager() {
    command -v wireplumber >/dev/null 2>&1 || return 1
    # Its bluetooth context loads the logind module, which finds no /run/systemd in a container and takes
    # the whole daemon down (exit 70). Drop that one context; main + policy are what do the linking.
    cp -r /usr/share/wireplumber "$TMP/wpconf" 2>/dev/null || return 1
    python3 - "$TMP/wpconf/wireplumber.conf" <<'PY' || return 1
import sys
p = sys.argv[1]
s = open(p).read().replace("{ name = bluetooth.lua, type = config/lua }", "# bluetooth.lua dropped (logind)")
open(p, "w").write(s)
PY
    WIREPLUMBER_CONFIG_DIR="$TMP/wpconf" WIREPLUMBER_DATA_DIR="$TMP/wpconf" \
        wireplumber >"$TMP/wireplumber.log" 2>&1 &
    WP_PID=$!
    sleep 3
    kill -0 "$WP_PID" 2>/dev/null
}

if start_session_manager; then
    writecfg '{"sampleRate":48000,"blockSize":512,"outChannels":8,"hostApi":18,"outputDevice":"RetroPlug Low Priority"}'
    LOG="$TMP/wide-output.log"
    RETROPLUG_DEBUG_AUDIO=1 RETROPLUG_SDL_EXIT_AFTER_FRAMES=240 timeout 90 "$BIN" >"$LOG" 2>&1 &
    APP_PID=$!
    for _ in $(seq 60); do grep -q "running " "$LOG" 2>/dev/null && break; sleep 0.25; done
    sleep 3
    PORTS=$(pw-link -o 2>/dev/null | grep -c '^retroplug-sdl:')
    LINKED=$(pw-link -l 2>/dev/null | grep -A1 '^retroplug-sdl:output_F' | grep -c 'playback_F')
    wait "$APP_PID"; RC=$?
    CHOSE=$(sed -n "s/.*audio: PortAudio \[\(.*\)\] '\(.*\)' [0-9]* Hz.*/\1|\2/p" "$LOG" | head -1)

    check "opens 8 channels against a stereo sink" "PipeWire|RetroPlug Low Priority"
    # Anchored on the "audio: PortAudio ..." line the OPEN prints: a bare "512 frames, 8 ch" also appears
    # in the Pa_OpenStream FAILURE message, which would pass this on a binary that opened nothing.
    grep -q "audio: PortAudio .*512 frames, 8 ch" "$LOG" \
        && { echo "  ok: host reports 8 channels"; PASS=$((PASS+1)); } \
        || { echo "  FAIL: host did not report 8 channels"; cat "$LOG" >&2; FAIL=$((FAIL+1)); }
    # The port count is the real assertion: without an explicit channel layout on the stream the adapter
    # takes its width from the sink, so this reads 2 while the log still claims 8.
    [ "$PORTS" -eq 8 ] \
        && { echo "  ok: the stream really is 8 ports wide"; PASS=$((PASS+1)); } \
        || { echo "  FAIL: expected 8 output ports on the stream, found $PORTS"; FAIL=$((FAIL+1)); }
    # ...and the first pair must still reach the stereo sink, or wide output would cost you all audio.
    [ "$LINKED" -ge 2 ] \
        && { echo "  ok: pair 1 is linked to the sink"; PASS=$((PASS+1)); } \
        || { echo "  FAIL: pair 1 is not linked to the sink (found $LINKED links)"; FAIL=$((FAIL+1)); }
else
    echo "  SKIPPED: wide-output checks need wireplumber (not installed, or it would not start)"
fi

echo "SDL PipeWire check: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1
