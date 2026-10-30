#!/usr/bin/env bash
#
# Fault-injection soak for the standalone box: cut the NES's power at random moments and require the
# box to recover by itself and play. Each round makes three cuts - "early" (0.3-2.5 s after power-on:
# enumeration, the probe, *t, the install) or "late" (8-15 s: a game running) - and then, on the last
# power-on, the box must boot the ROM on the LAST mount (a boot that began before a cut doesn't count)
# and an A4 on ch1 must sound: Pulse1 at timer 235 with its level up, read back through the sniffer.
#
#   ./faults.sh                       # 8 rounds on whatever is staged
#   ./faults.sh --rounds 20
#   ./faults.sh --rom a.nes --rom b.nes   # restage first (loop.sh), then soak
#
# It never cuts deliberately during an upload: with the choice saved on the SD, an unchanged ROM is
# not uploaded at all, and a card write is the one thing a badly timed cut can damage.
# Needs the CH345 MIDI cable in the Pico's jack (README.md). Exit 0 = every round passed.
set -u

here=$(cd "$(dirname "$0")" && pwd)
console=/dev/ttyDbgProbe
log=/tmp/n8-faults-console.log
power=/workspaces/.nes-power.sh
rounds=8 boot_args=()
while [ $# -gt 0 ]; do
	case $1 in
		--rounds) rounds=$2; shift ;;
		--rom)    boot_args+=(--rom "$2"); shift ;;
		*) echo "faults.sh: unknown option '$1'" >&2; exit 2 ;;
	esac
	shift
done
die() { echo "faults.sh: $*" >&2; exit 2; }

port=$(amidi -l 2>/dev/null | awk '/CH345/{print $2; exit}')
[ -n "$port" ] || die "no CH345 MIDI port (run .devcontainer/hw-usb-perms.sh after plugging it in)"
if [ ${#boot_args[@]} -gt 0 ]; then
	echo "== stage + boot through loop.sh"
	"$here/loop.sh" "${boot_args[@]}" --until 'booted by the Pico' --fail "no 'k'|FAILED" >/tmp/n8-faults-boot.log 2>&1 \
		|| { tail -5 /tmp/n8-faults-boot.log; die "loop.sh could not boot the ROM"; }
fi

stty -F "$console" 115200 raw -echo || die "cannot configure $console"
: >"$log"; T0=$(date +%s.%N)
now() { awk "BEGIN{printf \"%.1f\", $(date +%s.%N)-$T0}"; }
( exec 3<"$console"; while IFS= read -r l <&3; do printf '[%s] %s\n' "$(now)" "${l%$'\r'}"; done >>"$log" ) &
reader=$!
trap 'kill $reader 2>/dev/null; wait 2>/dev/null' EXIT

rnd() { awk -v s=$RANDOM -v lo="$1" -v span="$2" 'BEGIN{srand(s); printf "%.1f", lo + rand()*span}'; }
pass=0
for r in $(seq 1 "$rounds"); do
	for k in 1 2 3; do
		if (( RANDOM % 2 )); then d=$(rnd 0.3 2.2); kind=early; else d=$(rnd 8 7); kind=late; fi
		sleep "$d"; echo "[$(now)] >>> round $r cut $k ($kind, ${d}s after power-on)" >>"$log"
		"$power" reset >/dev/null || die "power-cycle failed"
	done
	mark=$(wc -l <"$log"); ok=0
	for i in $(seq 1 120); do
		lm=$(tail -n +"$mark" "$log" | grep -n '<- N8!' | tail -1 | cut -d: -f1)
		lb=$(tail -n +"$mark" "$log" | grep -n 'booted by the Pico\|a game is already running' | tail -1 | cut -d: -f1)
		[ -n "$lm" ] && [ -n "$lb" ] && [ "$lb" -gt "$lm" ] && { ok=1; break; }
		sleep 0.5
	done
	sleep 1
	amidi -p "$port" -S 'B0 07 7F'; sleep 0.3                   # the fresh ROM drops its first message
	amidi -p "$port" -S '90 45 7F'; sleep 1.2; amidi -p "$port" -S '80 45 00'; sleep 1
	note=$(tail -n +"$mark" "$log" | grep -c 'p1=235/[1-9a-f] ')
	stuck=$(tail -n +"$mark" "$log" | grep -c 'menu is not answering')
	if [ $ok = 1 ] && [ "$note" -ge 1 ]; then verdict=PASS; pass=$((pass + 1)); else verdict=FAIL; fi
	echo "round $r: booted=$ok note=$note stuck-menu=$stuck -> $verdict"
done

echo "== $pass / $rounds rounds recovered and played"
echo "   abandoned boots: $(grep -c 'boot abandoned' "$log"); stuck menus: $(grep -c 'menu is not answering' "$log");" \
     "recovery reboots: $(grep -c 'restarted to recover' "$log"); uploads: $(grep -c 'uploaded in' "$log")"
echo "   console: $log"
[ "$pass" = "$rounds" ]
