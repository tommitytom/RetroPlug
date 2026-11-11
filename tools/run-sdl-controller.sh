#!/usr/bin/env bash
#
# The control-surface seam through the REAL standalone, with a fake Launchpad on the ALSA sequencer standing
# in for the hardware (tools/fixtures/fake-launchpad.c). Everything between the two is the real thing: the
# Launchpad pack's provider and facet, RtMidi, the midi package's shared stream, and the TS that decides
# (launchpadDevices.ts). Each leg is built so it would FAIL if the thing it names were broken - a log line alone
# proves nothing:
#
#   1. nothing selected      - nothing claimed, nothing written at the device; and with the shared stream on
#                              "All Devices" the fake's notes DO reach it, so leg 2's "they don't" is seeable
#   2. restore + exclusion   - a pair saved as connected is claimed before the stream opens any hardware port
#                              (and before the audio device opens), and the fake's notes then do NOT reach it
#   3. the device lifecycle  - with a controller-only project loaded, the device hears Programmer mode FIRST,
#                              then LED traffic, then the release LAST (a Pro MK3 in Programmer mode has its
#                              front panel locked until it is let go)
#   4. a real scan, claimed  - inquiry sweep + model confirm over the real facet and RtMidi, the find
#      mid-session             persisted to config.json AND claimed with audio running: from that line on the
#                              device's notes stop reaching the shared stream, a second (ordinary) device's
#                              keep arriving, and the Settings output pointed at the device is held back
#   5. two processes         - the second standalone does not claim the port the first one holds
#
# It drives a scan, and a scan sends a Universal Device Inquiry out of every output the MIDI system lists, while the
# shared stream on "All Devices" opens every input: on a developer's machine that is their own hardware. So the standalone
# runs under RP_MIDI_PORT_ALLOW (one rule, midi/PortFilter.hpp, for the stream and the pack alike), set below to the two
# fakes' FULL port names - "<client>:<port> <client#>:<port#>" as RtMidi reports them, the client names being ones no
# real device has - and a filtered process can neither list nor open anything else.
#
# The listing pass below is a REPORT, not the guard: it proves the filter is honoured on this machine before anything is
# claimed or probed, and fails the run if a listed port is not a fake or is named like a Launchpad. What actually keeps
# every leg off other gear is the filter itself, refused at every door - the pack's lister (what a scan's sweep reads), its
# opener, its pool and its link, and the shared stream - so the pool and the opener refuse a port the list did not
# name whether or not this pass ran.
#
# Linux only (ALSA sequencer). Not in CI. Usage: tools/run-sdl-controller.sh  (build retroplug-sdl first)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="${RETROPLUG_SDL:-$REPO_DIR/build/bin/retroplug-sdl}"

[ -x "$BIN" ] || { echo "error: $BIN not found - cmake --build build --target retroplug-sdl -j\$(nproc)" >&2; exit 1; }
[ -e /dev/snd/seq ] || { echo "SKIP: no ALSA sequencer (/dev/snd/seq)"; exit 0; }

# The test-only port filter. Its list is built further down, once both fakes are up, from their FULL port names: their
# client names are ones no real device has, which is what makes each entry match those fakes alone. A caller who already
# set the variable is refused outright, because a list this harness did not write is one it cannot vouch for.
if [ -n "${RP_MIDI_PORT_ALLOW:-}" ]; then
    echo "error: RP_MIDI_PORT_ALLOW is already '$RP_MIDI_PORT_ALLOW'; this harness sets it to its own two fakes' full" \
         "port names (unset it to run)" >&2
    exit 1
fi

# The null audio backend, pinned for every leg: these legs are about the MIDI seam, and audio that opened a real device
# would either play to whatever this machine's default card is or fail on a runner with none. No audio driver is named.
export SDL_VIDEODRIVER=dummy RETROPLUG_SDL_AUDIO_BACKEND=null
TMP="$(mktemp -d)"

# The /dev/snd guard (tools/sdl-audio-guard), on top of the port filter above: two independent locks, one for MIDI and one
# for audio. The run is pinned to the null backend and must never reach a card even so.
. "$REPO_DIR/tools/sdl-audio-guard/guard-env.sh" "$TMP/guard"
export NOSND_LOG="${NOSND_LOG:-$TMP/nosnd.log}"
FAKE_PID=""
KEYS_PID=""
OTHER_PID=""
cleanup() {
    [ -n "$FAKE_PID" ] && kill "$FAKE_PID" 2>/dev/null
    [ -n "$KEYS_PID" ] && kill "$KEYS_PID" 2>/dev/null
    [ -n "$OTHER_PID" ] && kill "$OTHER_PID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT
fail() { echo "SDL controller FAIL: $*" >&2; exit 1; }

cc -O1 -o "$TMP/fake" "$SCRIPT_DIR/fixtures/fake-launchpad.c" -lasound || fail "could not build the fake device (needs libasound2-dev)"
"$TMP/fake" chatter >"$TMP/fake.log" 2>&1 &
FAKE_PID=$!
for _ in $(seq 1 50); do grep -q '^ready' "$TMP/fake.log" && break; sleep 0.1; done
grep -q '^ready' "$TMP/fake.log" || fail "the fake device never came up"
# A second device that is NOT a Launchpad (answers nothing) playing note 60: what a claim must leave alone. Its client
# is named too, so the allow list below can name it as exactly as the first.
"$TMP/fake" chatter noreply client=RPTestKeys name=RPTestKeys note=60 >"$TMP/keys.log" 2>&1 &
KEYS_PID=$!
for _ in $(seq 1 50); do grep -q '^ready' "$TMP/keys.log" && break; sleep 0.1; done
grep -q '^ready' "$TMP/keys.log" || fail "the second fake device never came up"

# The filter's list, as the two ports' WHOLE names (RtMidi's ALSA form: "<client>:<port> <client#>:<port#>"). An entry
# this long cannot also match a real device: a Pro MK3 on a kernel client lists as "Launchpad Pro MK3:Launchpad Pro MK3
# LPProMK3 MIDI 24:0", whose client name is the device's own.
ADDR="$(sed -n 's/^ready //p' "$TMP/fake.log")"
KEYS_ADDR="$(sed -n 's/^ready //p' "$TMP/keys.log")"
FAKE_PORT="RPFakeLPProMK3:LPProMK3 MIDI $ADDR"
KEYS_PORT="RPTestKeys:RPTestKeys $KEYS_ADDR"
export RP_MIDI_PORT_ALLOW="$FAKE_PORT,$KEYS_PORT"

export RETROPLUG_USER_CONFIG_DIR="$TMP/cfg"
mkdir -p "$RETROPLUG_USER_CONFIG_DIR"

# What the fake heard during a run: the lines after `mark`, once it has gone quiet (a message sent at quit
# may still be on its way when the standalone exits).
mark() { wc -l <"$TMP/fake.log"; }
heard_since() {
    local from="$1" prev=-1 now
    for _ in $(seq 1 20); do
        now="$(wc -l <"$TMP/fake.log")"
        [ "$now" = "$prev" ] && break
        prev="$now"; sleep 0.1
    done
    tail -n +"$((from + 1))" "$TMP/fake.log" | grep -v '^ready' || true
}
setcfg() {  # setcfg <python expression run with cfg bound to config.json's object>
    python3 - "$RETROPLUG_USER_CONFIG_DIR/config.json" "$1" <<'PY'
import json, sys
path, expr = sys.argv[1], sys.argv[2]
cfg = json.load(open(path))
exec(expr)
json.dump(cfg, open(path, "w"), indent=2)
PY
}

# The port name exactly as RtMidi lists it ("<client>:<port> <client#>:<port#>"), matched on OUR fake's client
# number so any other device (or a stale fake) with the same name is ignored.
RETROPLUG_SDL_MIDI_LIST=1 RETROPLUG_SDL_EXIT_AFTER_FRAMES=5 "$BIN" >"$TMP/list.log" 2>&1 || { cat "$TMP/list.log" >&2; fail "listing run exited non-zero"; }
# THE LISTING PASS, before anything is claimed or probed: under the filter the listing may hold exactly the two fakes and
# nothing else, which is what says the filter is honoured here. It is not what keeps the legs safe - the pool, the opener,
# the link and the stream each refuse a port the list did not name, so none of them can be reached from here whether or not
# this pass ran - but a build whose filter was broken would list this machine's own devices, and the run stops rather
# than find out the hard way. The list is printed either way, so a run that skips less than expected says why.
LISTED="$(sed -n "s/.*MIDI \(in\|out\) device: *'\([^']*\)'.*/\2/p" "$TMP/list.log")"
printf '%s\n' "$LISTED" | sed 's/^/  listed: /' >&2
[ -n "$LISTED" ] || { cat "$TMP/list.log" >&2; fail "the listing run listed no MIDI port at all"; }
STRAY="$(printf '%s\n' "$LISTED" | grep -v -F -x -e "$FAKE_PORT" -e "$KEYS_PORT" || true)"
[ -z "$STRAY" ] || { printf '%s\n' "$STRAY" >&2
    fail "the filter listed a port that is not one of the fakes - this build does not honour RP_MIDI_PORT_ALLOW"; }
# ...and a port named like a Launchpad is refused whatever the list above holds, so a list loosened later cannot spend the
# scan's Device Inquiry on somebody's real Pro MK3: that is the one device this whole filter exists to keep clear of.
LAUNCHPADISH="$(printf '%s\n' "$LISTED" | grep -i -e 'LPProMK3' -e 'Launchpad' \
    | grep -v -F -x -e "$FAKE_PORT" -e "$KEYS_PORT" || true)"
[ -z "$LAUNCHPADISH" ] || { printf '%s\n' "$LAUNCHPADISH" >&2
    fail "a real Launchpad is attached and listed; refusing to scan it"; }
printf '%s\n' "$LISTED" | grep -q -F -x -e "$FAKE_PORT" \
    || { cat "$TMP/list.log" >&2; fail "the standalone does not list the fake device"; }
PORT="$FAKE_PORT"

# The MIDI selection is the PROJECT's now (a `midi` section), so a leg that wants hardware inputs to reach the shared
# stream autoloads a project that asks for them. The section is the shell-free form (packages/midi/ts/configSection.ts):
# an explicit "every input" choice, or the named ports a rig saved.
writeproject() {  # writeproject <path> <midi-section-json>
    printf '{"schemaVersion":"4","settings":{"midi":%s},"systems":[]}\n' "$2" >"$1"
}
ALL_INPUTS='{"inputs":[],"allInputs":true,"outputs":[],"clockBpm":120}'
writeproject "$TMP/all-inputs.rplg" "$ALL_INPUTS"

# The shared stream listens to every hardware input when the PROJECT asks for it, so the only thing keeping the fake's
# notes out of it is the claim. RETROPLUG_MIDI_LOG makes the stream log each message it receives.
export RETROPLUG_MIDI_LOG=1
NOTE='MIDI in: 90 51 7F'
KEYS='MIDI in: 90 3C 7F'

# 1) Nothing selected: nothing claimed, nothing written - and the fake's notes reach the shared stream.
m="$(mark)"
RETROPLUG_SDL_EXIT_AFTER_FRAMES=90 "$BIN" --autoload "$TMP/all-inputs.rplg" >"$TMP/idle.log" 2>&1 \
    || { cat "$TMP/idle.log" >&2; fail "idle run exited non-zero"; }
grep -q 'reserved MIDI input' "$TMP/idle.log" && fail "claimed a port with nothing selected"
# Nothing to claim is still a restore: the host waits for that word before opening MIDI.
grep -q 'did not report its restore' "$TMP/idle.log" && { cat "$TMP/idle.log" >&2; fail "the UI never reported its restore"; }
# The rendezvous's other half: the audio service applied the `audio` section (the null pin, here). A UI that never
# applies costs the bound and starts on the defaults, which is the line this asserts never prints.
grep -qF 'audio: the UI did not apply its settings' "$TMP/idle.log" \
    && { cat "$TMP/idle.log" >&2; fail "the audio service never applied its settings"; }
[ -z "$(heard_since "$m")" ] || { heard_since "$m" >&2; fail "wrote at the device with nothing selected"; }
grep -qF "$NOTE" "$TMP/idle.log" || { cat "$TMP/idle.log" >&2; fail "the shared stream never heard the fake - leg 2's exclusion would prove nothing"; }
grep -qF "$KEYS" "$TMP/idle.log" || { cat "$TMP/idle.log" >&2; fail "the shared stream never heard the second device"; }
echo "  ok: nothing selected, nothing claimed; unclaimed, the device's notes reach the shared stream"

# 2) Left connected last session: reclaimed before the audio device opens and before the stream opens a hardware port,
#    and excluded from the stream. The virtual ports open at once now (the stream is up before any project is applied),
#    so the claim cannot precede MIDI opening - what it still precedes is the first hardware open, which must never
#    happen for the claimed port at all.
setcfg "cfg['controlSurface'] = {'input': '$PORT', 'output': '$PORT', 'enabled': True, 'device': 'Launchpad Pro [MK3]'}"
m="$(mark)"
RETROPLUG_SDL_EXIT_AFTER_FRAMES=90 "$BIN" --autoload "$TMP/all-inputs.rplg" >"$TMP/restore.log" 2>&1 \
    || { cat "$TMP/restore.log" >&2; fail "restore run exited non-zero"; }
grep -qF "reserved MIDI input '$PORT'" "$TMP/restore.log" || { cat "$TMP/restore.log" >&2; fail "the persisted pair was not reclaimed"; }
# WHEN matters as much as whether: claimed after audio is up, a pad press reaches the stream for the first frames (the
# reservation still closes the port without an audio stop, but the first frames are audible). The claim must land
# before the app starts running, and the claimed port must never be opened by the stream - not even for one frame.
reserved_at="$(grep -nF "reserved MIDI input '$PORT'" "$TMP/restore.log" | head -1 | cut -d: -f1)"
running_at="$(grep -n '\] running ' "$TMP/restore.log" | head -1 | cut -d: -f1)"
[ -n "$reserved_at" ] && [ -n "$running_at" ] || { cat "$TMP/restore.log" >&2; fail "no running line in the restore run"; }
[ "$reserved_at" -lt "$running_at" ] \
    || { cat "$TMP/restore.log" >&2; fail "the pair was reclaimed too late (line $reserved_at; running $running_at)"; }
grep -qF "MIDI in: connected hardware port '$PORT'" "$TMP/restore.log" \
    && { cat "$TMP/restore.log" >&2; fail "the claimed port was opened by the shared stream"; }
grep -qF "$NOTE" "$TMP/restore.log" && { cat "$TMP/restore.log" >&2; fail "the claimed device's notes still reached the shared stream"; }
# ...while everything ELSE still does: a claim that took the whole stream down would pass the line above.
grep -qF "$KEYS" "$TMP/restore.log" || { cat "$TMP/restore.log" >&2; fail "claiming the device cut the shared stream off from other devices"; }
heard="$(heard_since "$m")"
[ "$heard" = "f0 00 20 29 02 0e 0e 00 f7" ] || { echo "$heard" >&2; fail "with no project, the device should hear exactly the release at quit"; }
echo "  ok: reclaimed before the app starts running, its port never opened by the stream; released at quit"

# 3) A project with the controller on: Programmer mode first, LEDs, the release last.
cat >"$TMP/controller.rplg" <<'JSON'
{ "schemaVersion": "4", "settings": { "controller": { "enabled": true, "app": "lsdj-midimap", "target": "midiOut" } }, "systems": [] }
JSON
m="$(mark)"
RETROPLUG_SDL_EXIT_AFTER_FRAMES=90 "$BIN" --autoload "$TMP/controller.rplg" >"$TMP/project.log" 2>&1 || { cat "$TMP/project.log" >&2; fail "project run exited non-zero"; }
heard="$(heard_since "$m")"
first="$(echo "$heard" | head -1)"; last="$(echo "$heard" | tail -1)"
[ "$first" = "f0 00 20 29 02 0e 0e 01 f7" ] || { echo "$heard" >&2; fail "the device did not hear Programmer mode FIRST"; }
echo "$heard" | grep -q '^f0 00 20 29 02 0e 03 ' || { echo "$heard" >&2; fail "no LED traffic reached the device"; }
[ "$last" = "f0 00 20 29 02 0e 0e 00 f7" ] || { echo "$heard" >&2; fail "the release was not the LAST thing the device heard"; }
echo "  ok: Programmer mode first, LED traffic, the release last"

# 4) A real scan, CLAIMED mid-session: the pair is wanted (enabled) but not known, and the Settings output is
# pointed at the device. The scan finds it, persists it, and claims it with audio running - the live path:
# The stream reopens around a running audio callback, and its output is held back.
setcfg "cfg['controlSurface'] = {'input': '', 'output': '', 'enabled': True, 'device': ''}"
# The Settings output is the project's: a `midi` section that names the device the control surface will claim.
writeproject "$TMP/scan-project.rplg" "{\"inputs\":[],\"allInputs\":true,\"outputs\":[{\"name\":\"$PORT\"}],\"clockBpm\":120}"
m="$(mark)"
# Long enough for the whole scan at this host's frame rate: SCAN_WINDOW_MS (300) per probed output, plus the same window
# again for the model follow-up on the pair that answered - about 900 ms, or 54 frames of this loop - with the rest of the
# budget for the UI's mount and restore. The scan is stepped by the UI's frame poll, so its windows land a frame apart.
RETROPLUG_SDL_TEST_SCAN=1 RETROPLUG_SDL_EXIT_AFTER_FRAMES=480 "$BIN" --autoload "$TMP/scan-project.rplg" \
    >"$TMP/scan.log" 2>&1 || { cat "$TMP/scan.log" >&2; fail "scan run exited non-zero"; }
grep -q 'scan test: started=true' "$TMP/scan.log" || { cat "$TMP/scan.log" >&2; fail "the scan did not start"; }
heard="$(heard_since "$m")"
echo "$heard" | grep -qx 'f0 7e 7f 06 01 f7' || { echo "$heard" >&2; fail "the device was never asked the Device Inquiry"; }
echo "$heard" | grep -qx 'f0 00 20 29 02 0e 00 f7' || { echo "$heard" >&2; fail "the find was never confirmed with the Pro's model query"; }
python3 - "$RETROPLUG_USER_CONFIG_DIR/config.json" "$PORT" <<'PY' || fail "the scan's find was not persisted to config.json"
import json, sys
cs = json.load(open(sys.argv[1]))["controlSurface"]
assert cs["input"] == sys.argv[2] and cs["output"] == sys.argv[2] and cs["device"] == "Launchpad Pro [MK3]", cs
PY
claim_at="$(grep -nF "reserved MIDI input '$PORT'" "$TMP/scan.log" | head -1 | cut -d: -f1)"
running_at="$(grep -n '\] running ' "$TMP/scan.log" | head -1 | cut -d: -f1)"
[ -n "$claim_at" ] && [ -n "$running_at" ] && [ "$claim_at" -gt "$running_at" ] \
    || { cat "$TMP/scan.log" >&2; fail "the find was not claimed mid-session (claim line ${claim_at:-none}, running $running_at)"; }
last_note="$(grep -nF "$NOTE" "$TMP/scan.log" | tail -1 | cut -d: -f1)"
[ -n "$last_note" ] && [ "$last_note" -lt "$claim_at" ] \
    || { cat "$TMP/scan.log" >&2; fail "the device's notes (last at line ${last_note:-none}) did not stop at the claim (line $claim_at)"; }
tail -n +"$claim_at" "$TMP/scan.log" | grep -qF "$KEYS" \
    || { cat "$TMP/scan.log" >&2; fail "the second device's notes stopped at the claim"; }
grep -qF "MIDI out: '$PORT' is held by the control surface" "$TMP/scan.log" \
    || { cat "$TMP/scan.log" >&2; fail "the Settings output pointed at the device was not held back"; }
# A scan's held edge is Windows-only (midi/PortFilter.hpp's scanSuspendsInputs): an ALSA input is shared, so the scan hears the
# very port the stream holds and the stream must not close it - doing so costs the user their MIDI and an audio restart
# for the scan's seconds. A "suspended" line here means that regression is back.
grep -q 'hardware inputs suspended' "$TMP/scan.log" \
    && { cat "$TMP/scan.log" >&2; fail "the scan closed the shared stream's hardware inputs off Windows"; }
echo "  ok: a real scan found and confirmed the device, saved it, and claimed it mid-session - its notes stop, others' do not, the shared output steps aside"

# 5) Two standalones, one Launchpad: the second must not claim what the first holds.
RETROPLUG_SDL_EXIT_AFTER_FRAMES=240 "$BIN" --autoload "$TMP/all-inputs.rplg" >"$TMP/first.log" 2>&1 &
OTHER_PID=$!
for _ in $(seq 1 50); do grep -qF "reserved MIDI input '$PORT'" "$TMP/first.log" && break; sleep 0.1; done
grep -qF "reserved MIDI input '$PORT'" "$TMP/first.log" || { cat "$TMP/first.log" >&2; fail "the first standalone did not claim the device"; }
RETROPLUG_SDL_EXIT_AFTER_FRAMES=60 "$BIN" --autoload "$TMP/all-inputs.rplg" >"$TMP/second.log" 2>&1 \
    || { cat "$TMP/second.log" >&2; fail "second run exited non-zero"; }
wait "$OTHER_PID" || { cat "$TMP/first.log" >&2; fail "first run exited non-zero"; }
OTHER_PID=""
# The refusal is SAID (a silent second process - one that never even tried - must not pass), and the pair is
# still kept out of the second one's shared stream: its input is "All Devices", so without that it would hear
# every pad the first one takes, as raw notes.
grep -qF "could not claim '$PORT'" "$TMP/second.log" && grep -qF "in use by another RetroPlug" "$TMP/second.log" \
    || { cat "$TMP/second.log" >&2; fail "the second standalone did not report the device as held by the first"; }
grep -qF "$NOTE" "$TMP/second.log" && { cat "$TMP/second.log" >&2; fail "the second standalone hears the first one's pad presses"; }
grep -qF "$NOTE" "$TMP/first.log" && { cat "$TMP/first.log" >&2; fail "the first standalone's claimed device leaked into its own stream"; }
echo "  ok: a second standalone leaves the first one's device alone, and does not hear its pads"

echo "SDL controller OK"
