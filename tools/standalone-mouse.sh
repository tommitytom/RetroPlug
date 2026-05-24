#!/usr/bin/env bash
#
# Drive mouse input against a running retroplug standalone via xdotool.
# Pairs with tools/run-standalone.sh (shared virtual DISPLAY) and is the
# mouse counterpart to tools/standalone-key.sh.
#
# Usage:
#   DISPLAY=:99 tools/standalone-mouse.sh move X Y               # move pointer (window-relative)
#   DISPLAY=:99 tools/standalone-mouse.sh click [BUTTON]         # click at current pos
#   DISPLAY=:99 tools/standalone-mouse.sh at X Y [BUTTON]        # move + click in one call
#
# BUTTON: 1=left (default), 2=middle, 3=right.
# Coordinates are window-relative (the retroplug standalone window's
# top-left is (0, 0)).
#
# Dependencies (one-time):
#   sudo apt-get install xdotool

set -euo pipefail

if ! command -v xdotool >/dev/null 2>&1; then
    echo "error: xdotool not installed. Run: sudo apt-get install xdotool" >&2
    exit 1
fi
if [ -z "${DISPLAY:-}" ]; then
    echo "error: DISPLAY not set. Did you start Xvfb / run-standalone.sh first?" >&2
    exit 1
fi

WINDOW=$(xdotool search --name "retroplug" 2>/dev/null | head -1 || true)
if [ -z "$WINDOW" ]; then
    echo "warning: no retroplug window found on $DISPLAY" >&2
fi

usage() {
    echo "usage: $0 move X Y | click [BUTTON] | at X Y [BUTTON]" >&2
    exit 2
}

do_move() {
    local x="$1" y="$2"
    if [ -n "$WINDOW" ]; then
        xdotool mousemove --window "$WINDOW" "$x" "$y"
    else
        xdotool mousemove "$x" "$y"
    fi
}

do_click() {
    local btn="${1:-1}"
    if [ -n "$WINDOW" ]; then
        xdotool click --window "$WINDOW" "$btn"
    else
        xdotool click "$btn"
    fi
}

case "${1:-}" in
    move)
        [ $# -eq 3 ] || usage
        do_move "$2" "$3"
        ;;
    click)
        [ $# -le 2 ] || usage
        do_click "${2:-1}"
        ;;
    at)
        [ $# -ge 3 ] && [ $# -le 4 ] || usage
        do_move "$2" "$3"
        sleep 0.05
        do_click "${4:-1}"
        ;;
    *)
        usage
        ;;
esac
