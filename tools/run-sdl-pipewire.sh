#!/usr/bin/env bash
#
# PipeWire device-selection check for the SDL2 standalone (retroplug-sdl), in BOTH audio modes: the PortAudio
# backend and the SDL2 one. The SDL smoke (tools/run-sdl-smoke.sh) runs on the null device, so nothing there covers
# WHICH output device a real backend picks - the failure users actually hit ("no sound, and Settings doesn't show a
# device selected"), because RetroPlug was playing into a sink they weren't listening to.
#
# This stands up a PRIVATE PipeWire server (its own XDG_RUNTIME_DIR) with three null sinks and asserts the device the
# standalone opens in each configuration: the session default is followed, an explicit pick beats it, a stale pick falls
# back with a warning, and a PipeWire-less environment degrades to ALSA (which this script points at a private ALSA
# configuration whose default is the null PCM, so that leg touches no sound card either).
#
# The audio choice is a stamped `audio` block of config.json (a root section, docs/modular-graph-phase-f5b-design.md
# section 6.2): { backend, driver, device, sampleRate, blockSize, outChannels }. A run pins its BACKEND with
# --audio-backend, which replaces only the backend (E12): a saved driver and device survive when the section names the
# same backend, which is what lets a leg pick a device by name.
#
# The server is deliberately session-manager-free. Everything the check needs - the sinks AND the "default" metadata
# object that normally only wireplumber creates - is declared as `context.objects` in a daemon-only config, so the
# objects exist the moment the daemon is up. wireplumber needs logind/dbus and exits without them in a container, which
# made an earlier version of this check silently degenerate (no metadata, so every expectation collapsed onto the
# priority fallback and only one case failed, intermittently). Nothing here asserts audio FLOW except the wide-output
# legs, which need a session manager to link a stream's ports and start their own (that leg starts it).
#
# `rp-decoy` exists to keep every assertion discriminating: it always outranks the others on priority.session and is
# never the session default, so "followed the default" can't be confused with "fell back to priority".
#
# THE GUARD. Every run goes through the /dev/snd guard built from tools/sdl-audio-guard/nosnd.c (LD_PRELOAD), whose
# NOSND_LOG records each refused /dev/snd path (every node but seq/timer) and each dlopen of an audio client library
# that was NOT already loaded. A desktop's ALSA default is a real sound card, and the PortAudio legs probe ALSA, so
# the guard is what keeps the card closed; its log is the proof. The counts are reported at the end, and what each
# mode may log is exact:
#
#   - a pipewire-mode run MAY dlopen pipewire/libjack and MAY log refusals of the cards' CONTROL nodes (PortAudio's ALSA
#     probe enumerates every card at Pa_Initialize and snd_ctl_open is refused), which is the backend probing, not a
#     stream;
#   - the ALSA-fallback leg must open NO card's PLAYBACK node (/dev/snd/pcm*), which is what the leg asserts (its
#     refusal count for pcm* must not move). It does not assert an empty log: the control-node refusals above are
#     PortAudio's device enumeration, which that leg performs like any other. It also does not assert an ALSA device
#     OPENED: in a container PortAudio's ALSA backend cannot enumerate the private config's null PCM (its format probe
#     rejects a virtual PCM) and the guard refuses the card control nodes, so the device list is empty and the host
#     falls back to `null` with a status. That CLEAN degradation - exit 0, a reason, no card playback node even
#     attempted - is what the leg holds; on a machine where PortAudio's ALSA can list that PCM it opens it, and the
#     leg reports which happened.
#
# Not in CI: GitHub's runners have no PipeWire. Run it locally - the devcontainer ships one.
#
# Usage:  tools/run-sdl-pipewire.sh          (build first, or use `pnpm sdl:pipewire`)
#         RETROPLUG_SDL=/path/to/bin tools/run-sdl-pipewire.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="${RETROPLUG_SDL:-$REPO_DIR/build/bin/retroplug-sdl}"

for tool in pipewire pw-cli pw-metadata; do
    command -v "$tool" >/dev/null 2>&1 \
        || { echo "error: $tool not found (devcontainer installs pipewire + pipewire-bin)" >&2; exit 1; }
done
[ -x "$BIN" ] \
    || { echo "error: $BIN not found - build it: cmake --build build --target retroplug-sdl -j\$(nproc)" >&2; exit 1; }

TMP="$(mktemp -d)"
# The private XDG_RUNTIME_DIR is the isolation: that's where the server puts its socket and where every
# client (pw-cli, the standalone) looks for it, so a real desktop session is untouched.
export XDG_RUNTIME_DIR="$TMP/run"
export HOME="$TMP/home"               # the standalone's config lives under here - keep the real one out of it
export SDL_VIDEODRIVER=dummy
# The standalone's config, made explicit rather than left to HOME, and no MIDI port at all: a saved controlSurface
# would be claimed at startup and written to at quit. The filter's empty list means "every port", so it names one
# nothing has (midi/PortFilter.hpp's midiPortAllowedBy).
export RETROPLUG_USER_CONFIG_DIR="$TMP/home/.config/retroplug"
export RP_MIDI_PORT_ALLOW="RP_NO_PORT"
mkdir -p "$XDG_RUNTIME_DIR" "$HOME/.config/retroplug" "$TMP/pwconf/pipewire.conf.d" "$TMP/alsa"
chmod 700 "$XDG_RUNTIME_DIR"
CFG="$RETROPLUG_USER_CONFIG_DIR/config.json"
PASS=0; FAIL=0; SKIP=0

# The legs that need a session manager: the three SDL selection cases, the two wide-output ones and the live menu.
SDL_LEGS=6

# The guard, built here from the tracked source and ALWAYS in front of whatever LD_PRELOAD the caller had: the runs
# below probe every ALSA card, so a caller's other preloads must never be the state the guard is skipped in. A
# caller that already has the guard there (the verification kit) keeps its own. The log is this run's either way, so
# its counts belong to this run and nothing else.
: >"$TMP/nosnd.log"
export NOSND_LOG="$TMP/nosnd.log"
. "$REPO_DIR/tools/sdl-audio-guard/guard-env.sh" "$TMP/guard"
refused_count() { grep -c " refused /dev/snd/" "$NOSND_LOG" 2>/dev/null || true; }
pcm_refused_count() { grep -c " refused /dev/snd/pcm" "$NOSND_LOG" 2>/dev/null || true; }
dlopen_count() { grep -c " dlopen " "$NOSND_LOG" 2>/dev/null || true; }

# The private ALSA configuration: the ALSA-fallback leg's `default` is the NULL PCM, never a card. `type null` is a
# built-in plugin, so this file needs nothing else from /usr/share/alsa.
cat >"$TMP/alsa/alsa.conf" <<'CONF'
pcm.!default { type null }
pcm.null { type null }
ctl.!default { type null }
CONF

cleanup() {
    [ -n "${WP_PID:-}" ] && kill "$WP_PID" 2>/dev/null   # only set for the wide-output checks
    [ -n "${PW_PID:-}" ] && kill "$PW_PID" 2>/dev/null
    [ -n "${DBUS_PID:-}" ] && kill "$DBUS_PID" 2>/dev/null   # wireplumber's private bus, likewise
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
pw-cli info 0 >/dev/null 2>&1 \
    || { echo "error: the test PipeWire server never came up" >&2; cat "$TMP/pipewire.log" >&2; exit 1; }
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
            || { echo "error: the session default sink did not take '$want': the test server has no usable" \
                     "metadata" >&2; exit 1; }
    fi
}

# writecfg <backend> [driver] [device] [rate] [block] [channels] - the stamped `audio` section, and nothing else. The
# format is built from two pieces so it stays one printf argument without a line over 120 columns.
AUDIO_FMT='{"schemaVersion":3,"audio":{"backend":"%s","driver":"%s","device":"%s",'
AUDIO_FMT+='"sampleRate":%s,"blockSize":%s,"outChannels":%s}}\n'
writecfg() {
    local backend="$1" driver="${2:-}" device="${3:-}" rate="${4:-48000}" block="${5:-512}" channels="${6:-2}"
    printf "$AUDIO_FMT" "$backend" "$driver" "$device" "$rate" "$block" "$channels" >"$CFG"
}

# ---- the checks ---------------------------------------------------------------------------------------
# run <label> <backend> [ENV=VAL ...] -> $CHOSE = "driver|device" the host opened ("" when it ran muted), $RC its exit
run() {
    local name="$1" backend="$2"; shift 2
    LOG="$TMP/$name.log"
    # An EMPTY backend runs with no flag at all: that is the Auto case (the section says `auto`, which is what
    # every user starts with), and every other leg pins its backend for the run.
    local args=()
    [ -n "$backend" ] && args=(--audio-backend "$backend")
    env "$@" RETROPLUG_DEBUG_AUDIO=1 RETROPLUG_SDL_EXIT_AFTER_FRAMES=45 \
        timeout -k 5 90 "$BIN" ${args[@]+"${args[@]}"} >"$LOG" 2>&1
    RC=$?
    CHOSE=$(sed -n "s/.*audio: \(PortAudio\|SDL\) \[\(.*\)\] '\(.*\)' [0-9]* Hz.*/\2|\3/p" "$LOG" | head -1)
}
check() { # check <what> <expected>
    if [ "$CHOSE" = "$2" ] && [ "$RC" -eq 0 ]; then echo "  ok: $1"; PASS=$((PASS+1))
    else echo "  FAIL: $1 — expected '$2', got '${CHOSE:-<muted>}' (rc=$RC)"; cat "$LOG" >&2; FAIL=$((FAIL+1)); fi
}

# ---- PortAudio: the selection rules --------------------------------------------------------------------
# Each leg's section names portaudio, and the run pins it too: the override keeps the saved driver and device (E12),
# which is what makes an explicit pick expressible at all.

# Auto, with NO flag: the saved section says `auto`, which is the state a user's config.json is in, and Auto resolves to
# the first backend the product names that opens - PortAudio, whose host API preference takes PipeWire when the
# server is there - and its driver's default device, which IS the session default. (Before F5b the same three cases
# ran this way; pinning every leg had left the default path untested.)
setdefault rp-high
writecfg auto
run auto-default ""
check "Auto (no flag) opens PortAudio's PipeWire on the session default" "PipeWire|RetroPlug High Priority"

# The regression this exists for: the session default wins over the sink that outranks it on
# priority.session. Before the fix the backend went purely by priority and played into rp-decoy.
setdefault rp-low
writecfg portaudio
run session-default portaudio; check "PortAudio follows the session default sink" "PipeWire|RetroPlug Low Priority"

setdefault rp-high
run session-default-flipped portaudio
check "PortAudio follows the session default when it flips" "PipeWire|RetroPlug High Priority"

# No session default configured (a server with no session manager): priority.session is the tiebreak.
setdefault none
run priority-fallback portaudio; check "PortAudio falls back to priority.session" "PipeWire|RetroPlug Decoy"

# An explicit Settings > Audio > Output Device pick outranks the session default.
setdefault rp-high
writecfg portaudio "" "RetroPlug Low Priority"
run explicit-device portaudio; check "PortAudio: an explicit device beats the default" "PipeWire|RetroPlug Low Priority"

# A saved device that no longer exists must warn and fall back to the default, not mute.
writecfg portaudio "" "Device That Went Away"
run stale-device portaudio
check "PortAudio: a stale device falls back to the default" "PipeWire|RetroPlug High Priority"
grep -q "not found in the chosen driver" "$LOG" && { echo "  ok: stale device warned"; PASS=$((PASS+1)); } \
    || { echo "  FAIL: stale device did not warn"; FAIL=$((FAIL+1)); }

# A saved DRIVER this run does not have (a "pipewire" saved before the server came up, a "wasapi" on a machine without
# it) must do what a stale device does: warn and fall back to the automatic choice, never mute. The leg holds BOTH
# halves: the device the automatic choice picks, and the driver the status names being the one that really opened.
writecfg portaudio "pipewire-that-isnt-here"
run stale-driver portaudio
check "PortAudio: a saved driver this PortAudio does not have falls back to Auto" "PipeWire|RetroPlug High Priority"
grep -q "driver 'pipewire-that-isnt-here' is not in this PortAudio" "$LOG" \
    && { echo "  ok: the missing driver warned"; PASS=$((PASS+1)); } \
    || { echo "  FAIL: a missing driver fell back silently"; cat "$LOG" >&2; FAIL=$((FAIL+1)); }

# Explicitly picking the PipeWire driver.
setdefault rp-low
writecfg portaudio "pipewire" "" 44100 128
run explicit-driver portaudio
check "PortAudio: explicit PipeWire driver at 44100/128" "PipeWire|RetroPlug Low Priority"

# No libpipewire to dlopen: the PipeWire host API drops out and ALSA is all PortAudio has (the muOS/desktop-without-
# PipeWire path). ALSA's `default` here is the private config's null PCM, and the run must not open any card's PLAYBACK
# node. What this leg therefore HOLDS is the clean degradation, not an ALSA open: in a container PortAudio's ALSA
# backend cannot enumerate a virtual (null) PCM (its format probe rejects one), and the guard refuses the card
# control nodes it would need to list hardware, so its device list is empty and the host falls back to `null` WITH A
# STATUS (the "no default output device" reason) and exit 0. On a machine where PortAudio's ALSA can list that PCM
# it opens it instead, and this leg passes on either and says which happened. A crash, a mute with no reason, or a
# card playback node is not either.
writecfg portaudio
PCM_BEFORE=$(pcm_refused_count)
run no-libpipewire portaudio ALSA_CONFIG_PATH="$TMP/alsa/alsa.conf" PA_PIPEWIRE_SONAME=libpipewire-not-here.so.0
PCM_AFTER=$(pcm_refused_count)
if [ "$RC" -eq 0 ] && { [ "${CHOSE%%|*}" = "ALSA" ] || grep -q "audio: on the null device (PortAudio:" "$LOG"; }; then
    echo "  ok: PortAudio degrades without libpipewire (ALSA: ${CHOSE:-muted with a status})"; PASS=$((PASS+1))
else
    echo "  FAIL: PortAudio without libpipewire did not degrade cleanly (rc=$RC)"; cat "$LOG" >&2; FAIL=$((FAIL+1))
fi
if [ "$PCM_AFTER" -eq "$PCM_BEFORE" ]; then echo "  ok: the ALSA leg opened no card's playback node"; PASS=$((PASS+1))
else
    echo "  FAIL: the ALSA leg opened a /dev/snd playback node"
    grep " refused /dev/snd/pcm" "$NOSND_LOG" >&2
    FAIL=$((FAIL+1))
fi
unset ALSA_CONFIG_PATH

# ---- wide output and a live menu: they need a session manager ------------------------------------------
# The adapter only materialises its ports once something links the stream, and wireplumber is what links it - so these
# legs start their own. wireplumber also rewrites default.audio.sink, which is why they run LAST (the selection cases
# above depend on `setdefault` being the only thing that moves it).
start_session_manager() {
    command -v wireplumber >/dev/null 2>&1 || return 1
    # Its bluetooth context loads the logind module, which finds no /run/systemd in a container and takes
    # the whole daemon down (exit 70). Drop that one context; main + policy are what do the linking.
    #
    # Its DEVICE MONITORS go too. Out of the box wireplumber enables alsa_monitor, v4l2_monitor and libcamera_monitor:
    # the first opens every ALSA card (this machine's default is the Zoom L6) and mirrors the sequencer's MIDI ports, and
    # the other two open the video capture card on /dev/video0 and /dev/video1. This harness needs none of them: the
    # private server's sinks are `context.objects` already, and what a stream needs from a session manager is its
    # linking, which is the policy context. (The guard refuses those nodes anyway; a monitor that still ran would show
    # up as a refusal here.)
    cp -r /usr/share/wireplumber "$TMP/wpconf" 2>/dev/null || return 1
    python3 - "$TMP/wpconf/wireplumber.conf" "$TMP/wpconf/main.lua.d/90-enable-all.lua" <<'PY' || return 1
import sys
p = sys.argv[1]
s = open(p).read().replace("{ name = bluetooth.lua, type = config/lua }", "# bluetooth.lua dropped (logind)")
open(p, "w").write(s)
# 90-enable-all.lua is the only place the three monitors are turned on, and they are off by DEFAULT unless it calls
# them: commenting those calls leaves the daemon with the policy, the metadata and the stream/device defaults.
p = sys.argv[2]
s = open(p).read()
for m in ("alsa_monitor", "v4l2_monitor", "libcamera_monitor"):
    s = s.replace("%s.enable()" % m, "-- %s.enable() disabled by run-sdl-pipewire.sh" % m)
open(p, "w").write(s)
PY
    # The config edit is what turns them off, so a copy that silently did not take is a run that opens the card: check
    # the generated files, not the intent.
    if grep -qE '^[[:space:]]*(alsa|v4l2|libcamera)_monitor\.enable\(\)' "$TMP/wpconf/main.lua.d/90-enable-all.lua"; then
        echo "  FAIL: the private wireplumber's device monitors are still enabled in the copied config" >&2
        exit 1
    fi
    # It also needs a D-Bus session bus, and quits at startup ("Cannot autolaunch D-Bus without X11
    # $DISPLAY") when there is none. Give it a private one: that also keeps its device reservations off any real
    # session.
    command -v dbus-daemon >/dev/null 2>&1 || return 1
    dbus-daemon --session --address="unix:path=$TMP/dbus.sock" --nofork --nopidfile >"$TMP/dbus.log" 2>&1 &
    DBUS_PID=$!
    for _ in $(seq 20); do [ -S "$TMP/dbus.sock" ] && break; sleep 0.1; done
    DBUS_SESSION_BUS_ADDRESS="unix:path=$TMP/dbus.sock" \
    WIREPLUMBER_CONFIG_DIR="$TMP/wpconf" WIREPLUMBER_DATA_DIR="$TMP/wpconf" \
        wireplumber >"$TMP/wireplumber.log" 2>&1 &
    WP_PID=$!
    sleep 3
    kill -0 "$WP_PID" 2>/dev/null || return 1
    # ...and prove the daemon that came up loaded none of them. A monitor announces its subsystem in the log - the v4l2
    # one opens each camera and warns through `spa_v4l2_open`, the libcamera one through `script/libcamera`, the ALSA one
    # names its cards - so the absence of all three names IS the record that none ran (and the config check above is what
    # turned them off). A daemon that wrote nothing would pass vacuously, so the log must also have its own startup in it.
    if [ ! -s "$TMP/wireplumber.log" ]; then
        echo "  FAIL: the private wireplumber wrote no log (see $TMP/wireplumber.log); the monitor check proved nothing" >&2
        exit 1
    fi
    if grep -qiE '(alsa|v4l2|libcamera)' "$TMP/wireplumber.log"; then
        echo "  FAIL: the private wireplumber loaded a device monitor (see $TMP/wireplumber.log)" >&2
        grep -iE '(alsa|v4l2|libcamera)' "$TMP/wireplumber.log" >&2 || true
        exit 1
    fi
    return 0
}

port_count() { pw-link -o 2>/dev/null | grep -c "^$1:" || true; }

# The standalone's own PipeWire node. Read from the graph and DISCRIMINATED against this server's own objects: every
# sink in the private server exports monitor ports, so "the first output node alphabetically" can be rp-decoy's monitor
# (a sink's) or the graph's MIDI bridge, and the leg would then count the wrong node's ports. The test sinks and their
# monitors are excluded by name, which leaves the client's stream. A node name can carry spaces ("Audio Stream"), so the
# port is split off at its LAST colon and the two are joined with a delimiter awk can rely on.
stream_node() {
    pw-link -o 2>/dev/null | sed -n 's/^\(.*\):\([^:]*\)$/\1|\2/p' \
        | awk -F'|' '$1 !~ /^rp-(decoy|high|low)$/ && $2 !~ /^monitor_/ {print $1}' | sort -u | head -1
}

# How many of a node's first-pair outputs are linked to a sink's playback ports. `grep -c` prints 0 and EXITS 1, so the
# fallback must not print a second 0 (a two-line number fails every [ ] comparison after it).
linked_pair1() {
    [ -n "$1" ] || { echo 0; return; }
    pw-link -l 2>/dev/null | grep -A1 "^$1:output_F" | grep -c 'playback_F' || true
}

if start_session_manager; then
    # SDL's PipeWire driver connects through the session manager's graph, so its legs run here (a daemon-only server
    # with no manager left SDL's open blocked before it printed anything). The manager also rewrites the session
    # default when it starts, which is why the selection cases come after it is up.
    #
    # SDL's driver ids are its own lowercase names ("pipewire"), and its labels are the ids: "audio: SDL [pipewire]
    # 'x'".
    setdefault rp-low
    writecfg sdl "pipewire"
    run sdl-default sdl; check "SDL follows the session default sink" "pipewire|RetroPlug Low Priority"

    setdefault rp-high
    writecfg sdl "pipewire" "RetroPlug Low Priority"
    run sdl-explicit sdl; check "SDL: an explicit device beats the default" "pipewire|RetroPlug Low Priority"

    # A saved device that no longer exists must warn and fall back to the driver's default, not mute (the same rule as
    # PortAudio's).
    setdefault rp-high
    writecfg sdl "pipewire" "Device That Went Away"
    run sdl-stale sdl
    check "SDL: a stale device falls back to the default" "pipewire|RetroPlug High Priority"
    grep -q "not found in the chosen driver" "$LOG" && { echo "  ok: SDL stale device warned"; PASS=$((PASS+1)); } \
        || { echo "  FAIL: SDL stale device did not warn"; FAIL=$((FAIL+1)); }

    # PortAudio, 8 channels wide. Anchored on the "audio: PortAudio ..." line the OPEN prints: a bare "512 frames, 8 ch"
    # also appears in the Pa_OpenStream FAILURE message, which would pass this on a binary that opened nothing.
    writecfg portaudio "pipewire" "RetroPlug Low Priority" 48000 512 8
    LOG="$TMP/wide-output.log"
    RETROPLUG_DEBUG_AUDIO=1 RETROPLUG_SDL_EXIT_AFTER_FRAMES=240 \
        timeout -k 5 90 "$BIN" --audio-backend portaudio >"$LOG" 2>&1 &
    APP_PID=$!
    for _ in $(seq 60); do grep -q "running " "$LOG" 2>/dev/null && break; sleep 0.25; done
    sleep 3
    PORTS=$(port_count "retroplug-sdl")
    LINKED=$(pw-link -l 2>/dev/null | grep -A1 '^retroplug-sdl:output_F' | grep -c 'playback_F')
    wait "$APP_PID"; RC=$?
    CHOSE=$(sed -n "s/.*audio: PortAudio \[\(.*\)\] '\(.*\)' [0-9]* Hz.*/\1|\2/p" "$LOG" | head -1)
    check "PortAudio opens 8 channels against a stereo sink" "PipeWire|RetroPlug Low Priority"
    grep -q "audio: PortAudio .*512 frames, 8 ch" "$LOG" \
        && { echo "  ok: PortAudio reports 8 channels"; PASS=$((PASS+1)); } \
        || { echo "  FAIL: PortAudio did not report 8 channels"; cat "$LOG" >&2; FAIL=$((FAIL+1)); }
    [ "$PORTS" -eq 8 ] \
        && { echo "  ok: the PortAudio stream really is 8 ports wide"; PASS=$((PASS+1)); } \
        || { echo "  FAIL: expected 8 output ports on the PortAudio stream, found $PORTS"; FAIL=$((FAIL+1)); }
    [ "$LINKED" -ge 2 ] \
        && { echo "  ok: PortAudio pair 1 is linked to the sink"; PASS=$((PASS+1)); } \
        || { echo "  FAIL: PortAudio pair 1 is not linked to the sink (found $LINKED links)"; FAIL=$((FAIL+1)); }

    # SDL2 audio, 8 channels wide, the same two facts. SDL names its PipeWire node from its app-name hints
    # (setupSdl sets SDL_HINT_APP_NAME), so the node prefix is READ FROM THE RUN, never assumed.
    writecfg sdl "pipewire" "RetroPlug Low Priority" 48000 512 8
    LOG="$TMP/sdl-wide.log"
    RETROPLUG_DEBUG_AUDIO=1 RETROPLUG_SDL_EXIT_AFTER_FRAMES=240 \
        timeout -k 5 90 "$BIN" --audio-backend sdl >"$LOG" 2>&1 &
    APP_PID=$!
    for _ in $(seq 60); do grep -q "running " "$LOG" 2>/dev/null && break; sleep 0.25; done
    sleep 3
    SDL_NODE=$(stream_node)
    SDL_PORTS=$([ -n "$SDL_NODE" ] && port_count "$SDL_NODE" || echo 0)
    SDL_LINKED=$(linked_pair1 "$SDL_NODE")
    wait "$APP_PID"; RC=$?
    CHOSE=$(sed -n "s/.*audio: SDL \[\(.*\)\] '\(.*\)' [0-9]* Hz.*/\1|\2/p" "$LOG" | head -1)
    check "SDL opens 8 channels against a stereo sink" "pipewire|RetroPlug Low Priority"
    grep -q "audio: SDL .*512 frames, 8 ch" "$LOG" \
        && { echo "  ok: SDL reports 8 channels"; PASS=$((PASS+1)); } \
        || { echo "  FAIL: SDL did not report 8 channels"; cat "$LOG" >&2; FAIL=$((FAIL+1)); }
    # SDL 2.30's PipeWire driver takes the 8-channel request at the API (the host renders 8, and SDL accepts 8) but its
    # graph node is a STEREO stream: SDL mixes the extra channels into the first pair inside itself, so the node has 2
    # output ports (design R11; the backend warns about it at open). What must hold either way is that the stream exists
    # and its first pair reaches the sink.
    [ "$SDL_PORTS" -ge 2 ] \
        && { printf '  ok: the SDL stream has its ports (node %s, %s - its PipeWire node is stereo)\n' \
                    "$SDL_NODE" "$SDL_PORTS"
             PASS=$((PASS+1)); } \
        || { echo "  FAIL: the SDL stream has no output ports (node '$SDL_NODE')"; FAIL=$((FAIL+1)); }
    [ "$SDL_LINKED" -ge 2 ] \
        && { echo "  ok: SDL pair 1 is linked to the sink"; PASS=$((PASS+1)); } \
        || { echo "  FAIL: SDL pair 1 is not linked to the sink (found $SDL_LINKED links)"; FAIL=$((FAIL+1)); }

    # A live stream survives a menu build (S2 review C2): the Settings > Audio rows ask the SDL backend for its device
    # list, and a listing must never switch the driver (SDL_AudioQuit closes every open device). The hook drives the
    # real menu to Settings > Audio from the SDL queue; the leg holds the stream OPEN across it, the same node/ports
    # before and after, and exactly one open line.
    writecfg sdl "pipewire" "RetroPlug Low Priority" 48000 512 2
    LOG="$TMP/sdl-menu.log"
    RETROPLUG_DEBUG_AUDIO=1 RETROPLUG_SDL_EXIT_AFTER_FRAMES=150 RETROPLUG_SDL_TEST_MENU=40 \
        timeout -k 5 90 "$BIN" --audio-backend sdl >"$LOG" 2>&1 &
    APP_PID=$!
    for _ in $(seq 60); do grep -q "running " "$LOG" 2>/dev/null && break; sleep 0.25; done
    sleep 1
    NODE_BEFORE=$(stream_node)
    PORTS_BEFORE=$([ -n "$NODE_BEFORE" ] && port_count "$NODE_BEFORE" || true)
    wait "$APP_PID"; RC=$?
    grep -q "menu test: settings open, audio live=1 backend=sdl" "$LOG" \
        && { echo "  ok: Settings > Audio opened with the SDL stream running, and it is still live"
             PASS=$((PASS+1)); } \
        || { echo "  FAIL: Settings > Audio did not open over a live SDL stream"; cat "$LOG" >&2; FAIL=$((FAIL+1)); }
    OPENS=$(grep -c "audio: SDL \[" "$LOG")
    [ "$OPENS" -eq 1 ] \
        && { echo "  ok: the stream was opened exactly once (the menu did not reopen it)"; PASS=$((PASS+1)); } \
        || { echo "  FAIL: the SDL stream opened $OPENS times (a menu build disturbed it)"; cat "$LOG" >&2
             FAIL=$((FAIL+1)); }
    [ "$PORTS_BEFORE" -ge 2 ] \
        && { echo "  ok: the stream was linked before the menu opened ($PORTS_BEFORE ports)"; PASS=$((PASS+1)); } \
        || { echo "  FAIL: the SDL stream had no linked ports before the menu ($PORTS_BEFORE)"; FAIL=$((FAIL+1)); }
else
    SKIP=$SDL_LEGS
    echo "  SKIP: $SKIP SDL-audio legs need wireplumber + dbus-daemon (not installed, or it would not start)"
fi

REFUSED=$(refused_count)
DLOPENS=$(dlopen_count)
# A refusal of anything but a card CONTROL node is a run that reached for hardware: a card PLAYBACK node, the capture
# card's video nodes (the wireplumber monitors, which the config above disables), or a serial device. The control-node
# refusals are PortAudio's ALSA enumeration, which every run performs.
PCM=$(pcm_refused_count)
OTHER=$(grep -cE ' refused /dev/(video|media|v4l-subdev|ttyACM|ttyUSB|ttyDbgProbe|serial/|hidraw|bus/usb/)' "$NOSND_LOG" 2>/dev/null || true)
echo "  guard: $REFUSED refused /dev/snd path(s) (${PCM:-0} card pcm), ${OTHER:-0} video/serial path(s)," \
     "$DLOPENS audio-library dlopen(s) (see $NOSND_LOG)"
if [ "${PCM:-0}" -ne 0 ] || [ "${OTHER:-0}" -ne 0 ]; then
    echo "  FAIL: the guard refused a card playback, video or serial node - this run reached for hardware" >&2
    grep ' refused /dev/' "$NOSND_LOG" >&2 || true
    FAIL=$((FAIL+1))
fi
echo "SDL PipeWire check: $PASS passed, $FAIL failed, $SKIP skipped"
[ "$FAIL" -eq 0 ] || exit 1
