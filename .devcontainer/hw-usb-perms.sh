#!/usr/bin/env bash
#
# Runs at container start (postStartCommand). Opens USB permissions for the
# vscode user and exposes the Raspberry Pi Debug Probe's UART under a stable
# name, so that from inside the container:
#   - OpenOCD / picotool can reach the CMSIS-DAP probe (libusb, via /dev/bus/usb)
#     to flash RP2040 firmware over SWD, and
#   - the target's serial console (the probe's CDC-ACM bridge) is readable at a
#     predictable path regardless of which ttyACMn the host assigned it.
#
# Everything here is best-effort: it must NEVER fail container start (the runArgs
# only grant the device-cgroup majors; this creates the nodes those rules permit,
# opens them to the user and pins the probe's console name).
set +e

# 0. Hot-pluggable char devices: the N8's USB-serial (ttyACM*, major 166), the NES capture card
#    (video*, 81) and ALSA (snd/*, 116). devcontainer.json deliberately passes none of them as
#    --device (a missing node would fail container start), so mirror whatever the host has RIGHT
#    NOW from sysfs: mknod each present node with its current major:minor, and drop nodes whose
#    device has gone. Idempotent - re-run after plugging/unplugging hardware.
sync_class() { # <sysfs class dir> <name glob> <dev dir> <major>
	local cls=$1 glob=$2 dir=$3 major=$4 t name mm cur
	sudo mkdir -p "$dir"
	for t in "$cls"/$glob; do
		[ -e "$t/dev" ] || continue
		name=$(basename "$t")
		mm=$(cat "$t/dev" 2>/dev/null) || continue
		[ "${mm%:*}" = "$major" ] || continue   # only majors the cgroup rules grant
		cur=$(stat -c '%t:%T' "$dir/$name" 2>/dev/null)   # hex major:minor
		[ -n "$cur" ] && [ "$((16#${cur%:*})):$((16#${cur#*:}))" = "$mm" ] && { sudo chmod a+rw "$dir/$name"; continue; }
		sudo rm -f "$dir/$name"
		sudo mknod "$dir/$name" c "${mm%:*}" "${mm#*:}" && sudo chmod a+rw "$dir/$name"
	done
	for t in "$dir"/$glob; do
		[ -c "$t" ] && [ ! -e "$cls/$(basename "$t")" ] && sudo rm -f "$t"
	done
}
sync_class /sys/class/tty 'ttyACM*' /dev 166
sync_class /sys/class/video4linux 'video*' /dev 81
sync_class /sys/class/sound '*' /dev/snd 116

# 1. Raw USB (usbfs) nodes used by OpenOCD/picotool via libusb: /dev/bus/usb/BBB/DDD, major 189.
#    Same mirror-from-sysfs as above, but the node name is the bus + device NUMBER, which the host
#    reassigns on every replug - including a Pico re-enumerating into BOOTSEL for picotool - so a
#    node made earlier is stale, not just missing. If /dev/bus/usb is instead a bind mount of the
#    host's tree it is already live and its nodes are the HOST's: only open them, never mknod/rm.
sync_usb() {
	local d mm bus dev path t cur
	local -A want=()
	if mountpoint -q /dev/bus/usb 2>/dev/null; then
		sudo chmod -R a+rw /dev/bus/usb
		return
	fi
	for d in /sys/bus/usb/devices/*; do
		[ -e "$d/dev" ] && [ -e "$d/busnum" ] || continue
		mm=$(cat "$d/dev" 2>/dev/null) || continue
		[ "${mm%:*}" = 189 ] || continue
		bus=$(printf %03d "$(cat "$d/busnum")") dev=$(printf %03d "$(cat "$d/devnum")")
		path=/dev/bus/usb/$bus/$dev
		want[$path]=1
		cur=$(stat -c '%t:%T' "$path" 2>/dev/null)
		[ -n "$cur" ] && [ "$((16#${cur%:*})):$((16#${cur#*:}))" = "$mm" ] && { sudo chmod a+rw "$path"; continue; }
		sudo mkdir -p "/dev/bus/usb/$bus"
		sudo rm -f "$path"
		sudo mknod "$path" c 189 "${mm#*:}" && sudo chmod a+rw "$path"
	done
	for t in /dev/bus/usb/*/*; do
		[ -c "$t" ] && [ -z "${want[$t]}" ] && sudo rm -f "$t"
	done
}
sync_usb

# 2. The Raspberry Pi Debug Probe (VID:PID 2e8a:000c) exposes two USB interfaces:
#    interface 00 = CMSIS-DAP (reached over /dev/bus/usb above), interface 01 =
#    a CDC-ACM UART carrying the target's stdio console. That UART lands on a
#    dynamic /dev/ttyACMn on the host and its node is not auto-created in the
#    container, so find it in sysfs and mknod a stable /dev/ttyDbgProbe with the
#    same major:minor (the c 166:* cgroup rule permits it).
for t in /sys/class/tty/ttyACM*; do
	[ -e "$t" ] || continue
	dev_path=$(readlink -f "$t/device" 2>/dev/null)
	intf=$(cat "$dev_path/bInterfaceNumber" 2>/dev/null)
	up="$dev_path"
	while [ "$up" != "/" ] && [ ! -f "$up/idVendor" ]; do up=$(dirname "$up"); done
	vid=$(cat "$up/idVendor" 2>/dev/null)
	pid=$(cat "$up/idProduct" 2>/dev/null)
	if [ "$vid" = "2e8a" ] && [ "$pid" = "000c" ] && [ "$intf" = "01" ]; then
		mm=$(cat "$t/dev" 2>/dev/null)   # "major:minor", e.g. 166:1
		[ -n "$mm" ] || continue
		sudo rm -f /dev/ttyDbgProbe 2>/dev/null
		if sudo mknod /dev/ttyDbgProbe c "${mm%:*}" "${mm#*:}" 2>/dev/null; then
			sudo chmod a+rw /dev/ttyDbgProbe 2>/dev/null
			echo "hw-usb-perms: Debug Probe console -> /dev/ttyDbgProbe (from /dev/$(basename "$t"), $mm)"
		fi
		break
	fi
done
true
