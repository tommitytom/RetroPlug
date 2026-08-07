#!/bin/sh
# HELP: RetroPlug — Game Boy / chiptune multi-system emulator (LSDj, mGB, NES)
# ICON: retroplug
# GRID: RetroPlug
#
# muOS Application launcher for the SDL2 standalone (retroplug-sdl). Mirrors the
# stock muOS app launchers (e.g. Triangle / PPSSPP): muOS has already stopped its
# frontend and owns the framebuffer by the time this runs. SETUP_APP exports a
# writable HOME + XDG_CONFIG_HOME (so RetroPlug's config / SRAM persist) and runs
# SETUP_SDL_ENVIRONMENT to configure the patched SDL2 (rotation / scaler / game
# controller mapping). SDL's only video driver here is `mali` (fbdev EGL), picked
# by default; audio goes through ALSA (PipeWire's ALSA compat).

. /opt/muos/script/var/func.sh

APP_DIR="$(dirname "$0")"

# arg1 = foreground process name (muOS monitors/kills it); arg2 = "" default controller style.
SETUP_APP "retroplug-sdl" ""
SETUP_STAGE_OVERLAY

# RetroPlug is a real-time emulator. muOS Applications default to the `powersave` governor, which pins the
# Cortex-A53 at its minimum clock (~480 MHz) — too slow to sustain emulation + resampling + LVGL, so the
# SDL audio callback misses its deadline and ALSA underruns (choppy / no sound). Force `performance`
# (max clock) like the emulator launchers do; muOS restores the default governor when the app exits.
for _g in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor; do
	[ -w "$_g" ] && echo performance >"$_g" 2>/dev/null
done

cd "$APP_DIR" || exit 1

export SDL_AUDIODRIVER=alsa

# Log to the app folder (writable, on the SD card) so a launch can be diagnosed over SSH.
./retroplug-sdl --width 640 --height 480 >"$APP_DIR/retroplug.log" 2>&1
