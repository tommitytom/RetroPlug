#!/usr/bin/env bash
# Prove that a large SysEx survives RetroPlug's MIDI seam, with no hardware attached.
#
# Until the M3 widening, RtMidi was told to ignore SysEx outright and the standalone capped MIDI at 3 bytes,
# so a Launchpad's bulk-LED message (hundreds of bytes) could not cross in either direction. A Launchpad
# itself cannot test the RECEIVE half - it sends notes and CCs, and only emits SysEx in reply to an inquiry -
# so this loops our own virtual output back to our own virtual input and checks a whole-surface repaint
# arrives byte-identical.
#
# Usage: tools/run-launchpad-loopback.sh
set -euo pipefail

cd "$(dirname "$0")/.."
CLI=build/bin/retroplug-cli
CLIENT="RetroPlug Loopback"

[ -x "$CLI" ] || { echo "build $CLI first (cmake --build build --target retroplug-cli)"; exit 1; }
command -v aconnect >/dev/null || { echo "SKIP: aconnect not available (no ALSA sequencer)"; exit 0; }

log=$(mktemp)
"$CLI" launchpad-probe --loopback >"$log" 2>&1 &
cli=$!
trap 'kill $cli 2>/dev/null || true; rm -f "$log"' EXIT

# Wait for the virtual ports to appear, then wire output -> input. The probe holds its send for ~2 s
# precisely so this has time to land.
for _ in $(seq 1 40); do
    if aconnect -l 2>/dev/null | grep -q "$CLIENT"; then break; fi
    sleep 0.1
done

if ! aconnect -l 2>/dev/null | grep -q "$CLIENT"; then
    echo "FAIL: the CLI never registered ALSA ports named '$CLIENT'"
    cat "$log"
    exit 1
fi

# Each RtMidi port is its OWN ALSA client, and they all share the same client NAME - so the pair has to be
# addressed by client number, resolved from the port names. (`$2` of "client 129:" already carries the colon.)
port_client() { aconnect -l | awk -v want="$1" '/^client [0-9]+:/ { c = $2 } index($0, want) && !/^client/ { print c; exit }'; }
sender=$(port_client "$CLIENT Out")
receiver=$(port_client "$CLIENT In")
[ -n "$sender" ] && [ -n "$receiver" ] || { echo "FAIL: could not resolve the virtual port clients"; aconnect -l; exit 1; }
aconnect "${sender}0" "${receiver}0"

wait $cli && rc=0 || rc=$?
cat "$log"
if [ "$rc" -ne 0 ]; then
    echo "launchpad loopback FAILED (exit $rc)"
    exit "$rc"
fi
echo "launchpad loopback OK"
