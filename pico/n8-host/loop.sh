#!/usr/bin/env bash
#
# One iteration of the n8-host hardware loop, with no hands on the rig:
#   build -> power-cycle the NES (Home Assistant) -> flash over SWD (the Debug Probe) ->
#   capture the Pico's console (the probe's UART) until a pattern or a timeout.
#
# The N8's USB cable stays in the Pico's host socket throughout. The power-cycle puts the N8 at
# a fresh file-browser menu, so every run starts from the same state - the repro for the
# FIFO-write blocker (`*t` at a fresh menu, see ../pico-n8-fifo-write-bug.md) needs no ROM and
# no PC. It comes BEFORE the flash: the other way round, the power-off lands while the freshly
# booted Pico is enumerating the N8, and the host stack never sees that device go or come back.
#
#   ./loop.sh                                   # full loop, 90 s of console
#   ./loop.sh --until 'menu up' --fail "no 'k'" # stop at the first verdict, exit 0 / 1
#   ./loop.sh --no-power                        # keep whatever the NES is running (the Pico
#                                               # reset still re-mounts the N8)
#   ./loop.sh --native                          # the RP_NATIVE_USB build (build-native/)
#   ./loop.sh --debug                           # + TinyUSB attach/enumeration trace (build-debug/)
#
# Exit: 0 = done (or --until matched), 1 = --fail matched or --until timed out, 2 = setup failed.
# The timestamped console is kept in --log (default /tmp/n8-host-console.log).
set -u

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
power=/workspaces/.nes-power.sh
console=/dev/ttyDbgProbe

do_build=1 do_flash=1 do_power=1 native=0 debug=0 force=0
secs=90 until_re= fail_re= log=/tmp/n8-host-console.log

usage() { sed -n '3,/^set -u/{/^#/p}' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
while [ $# -gt 0 ]; do
	case $1 in
		--no-build) do_build=0 ;;
		--no-flash) do_flash=0 ;;
		--no-power) do_power=0 ;;
		--native)   native=1 ;;
		--debug)    debug=1 ;;   # TinyUSB attach/enumeration trace on the console
		--force)    force=1 ;;   # run even though the N8 is enumerated on the PC
		--secs)     secs=$2; shift ;;
		--until)    until_re=$2; shift ;;
		--fail)     fail_re=$2; shift ;;
		--log)      log=$2; shift ;;
		-h|--help)  usage ;;
		*) echo "loop.sh: unknown option '$1'" >&2; usage ;;
	esac
	shift
done

die() { echo "loop.sh: $*" >&2; exit 2; }
usb_present() { grep -qx "$1" <(for d in /sys/bus/usb/devices/*; do
	[ -e "$d/idVendor" ] && echo "$(cat "$d/idVendor"):$(cat "$d/idProduct")"; done); }

# --- preflight -------------------------------------------------------------------------------
# Mirror the host's USB + tty nodes first: the probe or the N8 may have re-enumerated since the
# container started, and OpenOCD needs the probe's /dev/bus/usb node.
bash "$repo/.devcontainer/hw-usb-perms.sh" >/dev/null 2>&1
usb_present 2e8a:000c || die "no Raspberry Pi Debug Probe (2e8a:000c) on USB"
[ -c "$console" ] || die "$console missing (hw-usb-perms.sh found no probe UART)"
# The host's /sys lists every USB device on the machine, so the N8 showing up there means its
# cable is in the PC, not the Pico - and the run would test nothing.
if usb_present 38df:0017 && [ $force = 0 ]; then
	die "the N8 (38df:0017) is enumerated on the PC - move its cable to the Pico's host socket (or --force)"
fi

# Use whichever SDK actually carries TinyUSB: the image's baked /opt/pico-sdk has no submodules.
sdk=${PICO_SDK_PATH:-}
[ -n "$sdk" ] && [ -e "$sdk/lib/tinyusb/src/tusb.h" ] || sdk=/workspaces/pico-sdk
[ -e "$sdk/lib/tinyusb/src/tusb.h" ] || die "no Pico SDK with TinyUSB (tried \$PICO_SDK_PATH and $sdk)"

if [ $native = 1 ]; then bdir=$here/build-native cfg=-DRP_NATIVE_USB=ON
else bdir=$here/build cfg=-DRP_NATIVE_USB=OFF; fi
if [ $debug = 1 ]; then bdir=$bdir-debug cfg="$cfg -DN8_TUSB_DEBUG=2"; fi
elf=$bdir/n8_host.elf

# --- build -----------------------------------------------------------------------------------
if [ $do_build = 1 ]; then
	echo "== build ($(basename "$bdir"), SDK $sdk)"
	if [ ! -f "$bdir/build.ninja" ]; then
		mkdir -p "$bdir"
		PICO_SDK_PATH=$sdk cmake -S "$here" -B "$bdir" -G Ninja $cfg >"$bdir/loop-configure.log" 2>&1 \
			|| { tail -20 "$bdir/loop-configure.log"; die "configure failed (log: $bdir/loop-configure.log)"; }
	fi
	cmake --build "$bdir" -j"$(nproc)" >"$bdir/loop-build.log" 2>&1 \
		|| { grep -E 'error|Error' "$bdir/loop-build.log" | head -20; die "build failed (log: $bdir/loop-build.log)"; }
fi
[ -f "$elf" ] || die "$elf not built"

# --- power-cycle the NES -> fresh N8 menu, before the Pico boots and enumerates it -----------
if [ $do_power = 1 ]; then
	echo "== power-cycle the NES"
	"$power" reset >/dev/null || die "power-cycle failed ($power)"
fi

# --- halt the running firmware, so nothing it already printed reaches this run's log ---------
ocd=(openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "adapter speed 5000")
"${ocd[@]}" -c "init; reset halt; exit" >/tmp/n8-host-openocd.log 2>&1 \
	|| { tail -15 /tmp/n8-host-openocd.log; die "halt failed (log: /tmp/n8-host-openocd.log)"; }

# --- console capture -------------------------------------------------------------------------
# Started BEFORE the flash so the boot banner is caught. Each line is stamped with seconds since
# the capture began, which is what the menu-readiness and retry timings get read from.
stty -F "$console" 115200 raw -echo || die "cannot configure $console"
timeout 0.3 cat "$console" >/dev/null   # drop what the old firmware left in the tty buffer
: >"$log"
t0=$(date +%s%N)
(
	while IFS= read -r line; do
		printf '[%6.1f] %s\n' "$(( ($(date +%s%N) - t0) / 1000000 ))e-3" "${line%$'\r'}"
	done <"$console" >>"$log"
) &
reader=$!
cleanup() { kill "$reader" 2>/dev/null; wait 2>/dev/null; }
trap cleanup EXIT
sleep 0.3

# --- flash (or just reset, so every run starts from a firmware boot) -------------------------
if [ $do_flash = 1 ]; then
	echo "== flash $(basename "$elf")"
	"${ocd[@]}" -c "program $elf verify reset exit" >/tmp/n8-host-openocd.log 2>&1 \
		|| { tail -15 /tmp/n8-host-openocd.log; die "flash failed (log: /tmp/n8-host-openocd.log)"; }
else
	echo "== reset the Pico"
	"${ocd[@]}" -c "init; reset run; exit" >/tmp/n8-host-openocd.log 2>&1 \
		|| { tail -15 /tmp/n8-host-openocd.log; die "reset failed (log: /tmp/n8-host-openocd.log)"; }
fi


# --- wait for a verdict ----------------------------------------------------------------------
echo "== console (${secs}s${until_re:+, until /$until_re/}${fail_re:+, fail /$fail_re/})"
# The loop is also the viewer: it prints whatever the reader appended since the last pass, so the
# matching line is always on screen before the verdict (tail -f polls too slowly to promise that).
shown=0 verdict=
show_new() { local n; n=$(wc -l <"$log"); [ "$n" -gt "$shown" ] && sed -n "$((shown + 1)),${n}p" "$log"; shown=$n; }
deadline=$((SECONDS + secs))
while [ $SECONDS -lt $deadline ]; do
	show_new
	if [ -n "$fail_re" ] && grep -qE -- "$fail_re" "$log"; then verdict=fail; break; fi
	if [ -n "$until_re" ] && grep -qE -- "$until_re" "$log"; then verdict=ok; break; fi
	sleep 0.2
done
show_new
case $verdict in
	fail) echo "== FAIL: /$fail_re/ seen"; rc=1 ;;
	ok)   echo "== OK: /$until_re/ seen"; rc=0 ;;
	*)    if [ -n "$until_re" ]; then echo "== TIMEOUT: /$until_re/ not seen in ${secs}s"; rc=1; else rc=0; fi ;;
esac
echo "== log: $log"
exit $rc
