#!/usr/bin/env bash
#
# Stress the MIDI bridge end to end: USB MIDI cable -> opto -> Pico (GP5) -> N8 cart FIFO ->
# the MIDI ROM -> 2A03. Boots the ROM fresh through loop.sh, then drives it with amidi and judges
# each case on the Pico's own [stats] counters (nothing lost on the way in, everything forwarded)
# AND on the running ROM's state read back through the N8 sniffer (it is still in sync).
#
#   ./stress.sh                 # every case
#   ./stress.sh --no-boot       # reuse the ROM that is already running (it must be primed)
#   ./stress.sh --soak 120      # soak length in seconds (default 30)
#   ./stress.sh --rate 900      # soak rate in messages/s (default 500; MIDI wire speed is ~1000)
#   ./stress.sh --no-recover    # skip the NES power-cycle under a live stream
#   ./stress.sh --rom ../../../bliptoaster/build/bliptoaster.nes   # boot this ROM (staged, see loop.sh)
#
# Needs the CH345 USB MIDI cable's OUT in the Pico's TRS jack (see README.md).
# Exit: 0 = every case passed, 1 = a case failed, 2 = setup failed.
set -u

here=$(cd "$(dirname "$0")" && pwd)
console=/dev/ttyDbgProbe
log=/tmp/n8-stress-console.log
boot=1 soak=30 rate=500 recover=1
boot_args=()
while [ $# -gt 0 ]; do
	case $1 in
		--no-boot) boot=0 ;;
		--soak)    soak=$2; shift ;;
		--rate)    rate=$2; shift ;;
		--no-recover) recover=0 ;;
		--rom)     boot_args+=(--rom "$2"); shift ;;
		*) echo "stress.sh: unknown option '$1'" >&2; exit 2 ;;
	esac
	shift
done
die() { echo "stress.sh: $*" >&2; exit 2; }

port=$(amidi -l 2>/dev/null | awk '/CH345/{print $2; exit}')
[ -n "$port" ] || die "no CH345 MIDI port (run .devcontainer/hw-usb-perms.sh after plugging it in)"

if [ $boot = 1 ]; then
	echo "== boot the ROM through the Pico (loop.sh)"
	"$here/loop.sh" "${boot_args[@]}" --until 'booted by the Pico' --fail "no 'k'|FAILED" >/tmp/n8-stress-boot.log 2>&1 \
		|| { tail -5 /tmp/n8-stress-boot.log; die "loop.sh could not boot the ROM"; }
fi

stty -F "$console" 115200 raw -echo || die "cannot configure $console"
: >"$log"
( while IFS= read -r l; do printf '%s\n' "${l%$'\r'}"; done <"$console" >>"$log" ) &
reader=$!
trap 'kill $reader 2>/dev/null; wait 2>/dev/null' EXIT

send() { amidi -p "$port" -S "$*" || die "amidi send failed"; }

# The newest [stats] line, after giving the firmware time to print one (it reports once a second,
# and only when a counter moved).
stats() { sleep 1.4; grep '^\[stats\]' "$log" | tail -1; }
field() { grep -oE "(^| )$1=[0-9]+" <<<"$2" | grep -oE '[0-9]+$' || echo 0; }
sniff() { sleep 0.6; grep '^\[sniff\]' "$log" | tail -1; }

fails=0
check() { # <case> <condition-text> <ok?>
	if [ "$3" = 1 ]; then echo "   ok   $2"; else echo "   FAIL $2"; fails=$((fails + 1)); fi
}

# n note-on/note-off pairs on <ch> (0-based), cycling <lo>..<lo+span-1>, as one hex string.
pairs() { # <n> <ch> <lo> <span>
	local n=$1 ch=$2 lo=$3 span=$4 i note out=
	for ((i = 0; i < n; i++)); do
		note=$((lo + i % span))
		out+=$(printf '%02X %02X 7F %02X %02X 00 ' $((0x90 | ch)) "$note" $((0x80 | ch)) "$note")
	done
	echo "$out"
}

# The ROM is alive and in sync: A4 on ch1 sounds as Pulse1 timer 235, then goes silent. The ROM
# keeps the timer at note-off and drops the volume, so the level after the slash is the test.
reference() { # <label>
	send '90 45 7F'; local on; on=$(sniff)
	send '80 45 00'; local off; off=$(sniff)
	check "$1" "ROM still in sync: A4 -> p1=235 sounding, then silent  [${on#\[sniff\] } / ${off#\[sniff\] }]" \
		$( [[ $on =~ p1=235/[1-9a-f]\  && $off =~ p1=235/0\  ]] && echo 1 || echo 0 )
}

# Send <hex>, expecting <msgs> channel-voice messages and <bytes> bytes; judge the counters.
judge() { # <label> <msgs> <bytes> <hex...>
	local label=$1 msgs=$2 bytes=$3; shift 3
	local a b; a=$(stats)
	send "$*"
	b=$(stats)
	local d_rx=$(( $(field rx "$b") - $(field rx "$a") ))
	local d_msgs=$(( $(field msgs "$b") - $(field msgs "$a") ))
	local d_fwd=$(( $(field fwd "$b") - $(field fwd "$a") ))
	local d_fail=$(( $(field fail "$b") - $(field fail "$a") ))
	local d_oe=$(( $(field oe "$b") - $(field oe "$a") ))
	local d_fe=$(( $(field fe "$b") - $(field fe "$a") ))
	local d_ring=$(( $(field ringfull "$b") - $(field ringfull "$a") ))
	local d_wr=$(( $(field writes "$b") - $(field writes "$a") ))
	check "$label" "bytes in: $d_rx / $bytes" $( [ $d_rx = "$bytes" ] && echo 1 || echo 0 )
	check "$label" "messages parsed: $d_msgs / $msgs" $( [ $d_msgs = "$msgs" ] && echo 1 || echo 0 )
	check "$label" "forwarded: $d_fwd / $msgs, write failures: $d_fail" \
		$( [ $d_fwd = "$msgs" ] && [ $d_fail = 0 ] && echo 1 || echo 0 )
	check "$label" "UART overruns: $d_oe, framing errors: $d_fe, ring overflows: $d_ring" \
		$( [ $d_oe = 0 ] && [ $d_fe = 0 ] && [ $d_ring = 0 ] && echo 1 || echo 0 )
	echo "        ($d_wr fifoWRs; largest batch so far $(field maxbatch "$b") msgs; slowest write $(field maxfwd "$b") us)"
}

echo "== prime (the ROM drops its first message after boot) + reference"
send 'B0 07 7F'; sleep 0.3
reference prime

echo "== latency: 40 isolated messages, last byte off the UART -> the N8 acknowledging the write"
# Each travels alone (150 ms apart). The floor is one USB frame: PIO-USB sends at most one packet
# per endpoint per 1 ms frame, so a write waits up to 1 ms for the next one. Anything past ~1.5 ms
# means something is holding the main loop again (a print, a long USB read).
mark=$(wc -l <"$log")
for ((i = 0; i < 20; i++)); do send '90 45 7F'; sleep 0.15; send '80 45 00'; sleep 0.15; done
sleep 1.4
lats=$(tail -n +"$((mark + 1))" "$log" | grep '^\[stats\]' | grep -oE 'lat=[0-9]+/[0-9]+' | grep -v 'lat=0/0')
worst=$(grep -oE '/[0-9]+' <<<"$lats" | tr -d / | sort -n | tail -1)
avg=$(grep -oE '=[0-9]+' <<<"$lats" | tr -d = | awk '{s+=$1; n++} END {print n ? int(s/n) : 0}')
check latency "worst ${worst:-?} us, average ~${avg} us  (budget 1500 us)" \
	$( [ -n "$worst" ] && [ "$worst" -le 1500 ] && echo 1 || echo 0 )

for n in 25 100 500; do
	echo "== burst: $n note on/off pairs on ch1 at wire speed ($((n * 6)) bytes, one amidi write)"
	judge "burst$n" $((n * 2)) $((n * 6)) "$(pairs $n 0 48 25)"
	reference "burst$n"
done

echo "== chord: A4 on ch1-ch3 + a noise hit on ch4, in one write"
send '90 45 7F 91 45 7F 92 45 7F 93 3C 7F'; s=$(sniff)
check chord "all four voices sounding  [${s#\[sniff\] }]" \
	$( [[ $s =~ p1=235/[1-9a-f].*p2=[0-9]+/[1-9a-f].*tri=[0-9]+/[1-9a-f].*noise=[0-9]+/[1-9a-f] ]] && echo 1 || echo 0 )
send '80 45 00 81 45 00 82 45 00 83 3C 00'; s=$(sniff)
check chord "all four released  [${s#\[sniff\] }]" \
	$( [[ $s =~ p1=[0-9]+/0\ .*p2=[0-9]+/0\ .*tri=[0-9]+/0\ .*noise=[0-9]+/0$ ]] && echo 1 || echo 0 )

echo "== running status + realtime bytes (F8 clock, FE sensing)"
# 90 45 7F then running-status 48 7F / 4C 7F, with realtime bytes between and inside them; then
# the three released by running-status note-ons at velocity 0. A USB MIDI cable cannot carry that
# shape: USB-MIDI packets hold whole messages, so the cable re-adds the four omitted status bytes
# (20 sent -> 24 on the wire) and puts each realtime byte between messages. This case therefore
# tests what the cable re-serialises; the parser's own interleave handling is midi-in/host_test.c.
judge runstat 6 24 '90 F8 45 7F F8 48 FE 7F 4C F8 7F' '90 45 00 F8 48 00 4C FE 00'
reference runstat

echo "== soak: ${soak}s at ~${rate} messages/s, rotating ch1-ch3"
a=$(stats); t0=$SECONDS; t_end=$((SECONDS + soak)); sent=0; i=0
per=$(( rate / 20 )); [ $per -lt 1 ] && per=1     # note pairs per 100 ms chunk
while [ $SECONDS -lt $t_end ]; do
	send "$(pairs $per $((i % 3)) $((45 + i % 3 * 5)) 12)"
	sent=$((sent + per * 2)); i=$((i + 1)); sleep 0.1
done
b=$(stats)
d_msgs=$(( $(field msgs "$b") - $(field msgs "$a") ))
d_fwd=$(( $(field fwd "$b") - $(field fwd "$a") ))
d_oe=$(( $(field oe "$b") - $(field oe "$a") )); d_fe=$(( $(field fe "$b") - $(field fe "$a") ))
d_ring=$(( $(field ringfull "$b") - $(field ringfull "$a") ))
check soak "parsed $d_msgs / $sent, forwarded $d_fwd  (~$(( sent / (SECONDS - t0 > 0 ? SECONDS - t0 : 1) )) msgs/s achieved)" \
	$( [ $d_msgs = $sent ] && [ $d_fwd = $sent ] && echo 1 || echo 0 )
check soak "UART overruns: $d_oe, framing errors: $d_fe, ring overflows: $d_ring" \
	$( [ $d_oe = 0 ] && [ $d_fe = 0 ] && [ $d_ring = 0 ] && echo 1 || echo 0 )
echo "        (largest batch so far $(field maxbatch "$b") msgs; slowest write $(field maxfwd "$b") us)"
reference soak

echo "== sustained: 40000 messages as one unbroken stream (120 KB, as fast as the cable goes)"
# One amidi call, so there is no gap between chunks at all - the soak above is paced by spawning
# amidi every 100 ms, which tops out near 300 msgs/s. This runs at the cable's own limit (~800/s).
awk 'BEGIN{for(i=0;i<20000;i++){n=48+i%25; printf "%c%c%c%c%c%c",144,n,127,128,n,0}}' >/tmp/n8-stress-wire.bin
a=$(stats); t0=$SECONDS
amidi -p "$port" -s /tmp/n8-stress-wire.bin || die "amidi send failed"
secs=$((SECONDS - t0)); b=$(stats)
d_rx=$(( $(field rx "$b") - $(field rx "$a") ))
d_msgs=$(( $(field msgs "$b") - $(field msgs "$a") )); d_fwd=$(( $(field fwd "$b") - $(field fwd "$a") ))
d_lost=$(( $(field oe "$b") - $(field oe "$a") + $(field fe "$b") - $(field fe "$a") + $(field ringfull "$b") - $(field ringfull "$a") ))
check sustained "bytes in $d_rx / 120000, parsed $d_msgs / 40000, forwarded $d_fwd  (~$(( 40000 / (secs > 0 ? secs : 1) )) msgs/s)" \
	$( [ $d_rx = 120000 ] && [ $d_msgs = 40000 ] && [ $d_fwd = 40000 ] && echo 1 || echo 0 )
check sustained "bytes lost to overrun / framing / ring: $d_lost" $( [ $d_lost = 0 ] && echo 1 || echo 0 )
echo "        (largest batch so far $(field maxbatch "$b") msgs; slowest write $(field maxfwd "$b") us)"
reference sustained

if [ $recover = 1 ]; then
	echo "== recovery: power-cycle the NES under a live stream"
	# MIDI arriving while the N8 is gone or its menu boots is meant to be dropped - it must never
	# reach the menu's FIFO, where it would corrupt the menu commands. What matters is that the
	# Pico re-boots the ROM by itself and the stream plays again afterwards.
	( while :; do amidi -p "$port" -S "$(pairs 5 0 50 12)" 2>/dev/null; sleep 0.1; done ) &
	streamer=$!
	mark=$(wc -l <"$log")
	/workspaces/.nes-power.sh reset >/dev/null
	booted=0
	for ((s = 0; s < 90; s++)); do
		tail -n +"$((mark + 1))" "$log" | grep -q 'booted by the Pico' && { booted=1; break; }
		sleep 1
	done
	sleep 2; kill $streamer 2>/dev/null; wait $streamer 2>/dev/null
	check recover "the Pico saw the N8 go and come back, and re-booted the ROM through the menu" $booted
	send 'B0 07 7F'; sleep 0.3                          # the fresh ROM drops its first message
	reference recover
fi

echo "== final counters: $(grep '^\[stats\]' "$log" | tail -1)"
[ $fails = 0 ] && { echo "== PASS"; exit 0; }
echo "== FAIL ($fails checks)"; exit 1
