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
# already bind-mount /dev/bus/usb and grant the 189/166 device-cgroup rules; this
# just makes them usable without sudo and pins the probe's console name).
set +e

# 1. Raw USB (usbfs) nodes used by OpenOCD/picotool via libusb.
sudo chmod -R a+rw /dev/bus/usb 2>/dev/null

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
