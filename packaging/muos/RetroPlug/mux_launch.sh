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

# Audio is PortAudio now (native PipeWire host API, ALSA fallback), not SDL. PortAudio reaches the muOS
# PipeWire daemon via its socket at $XDG_RUNTIME_DIR/pipewire-0 (the device runs pipewire at /run/pipewire-0),
# so point XDG_RUNTIME_DIR there when the session hasn't already set it. Without it PortAudio falls back to a
# raw ALSA device (hw:...) instead of PipeWire. (Verified on-device: audio: PortAudio [PipeWire] 'Built-in
# Audio Stereo' with this set.)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run}"

# The handheld is a fixed framebuffer panel with no window manager, so run FULLSCREEN. Without this the
# resizable window (added for desktop tiling WMs) treats itself as a floating window and shrinks its SDL
# surface to the grid size (e.g. 480x432 for one Game Boy) — leaving the rest of the 640x480 screen
# uncovered. Fullscreen makes __rp_isWindowSizeControlled report true, so the UI fits the grid via zoom
# into the full panel instead of driving SDL_SetWindowSize. (Verified on-device: onResize stays 640x480,
# wmC=1, vs 480x432 without it.)
export RETROPLUG_SDL_FULLSCREEN=1

# The compiled-in default audio block size is 512 frames (low latency, for desktops). The Cortex-A53 handheld
# can't sustain emulation + resampling + LVGL inside that deadline, so the audio callback underruns (choppy
# sound). Pass a big buffer here; it only seeds the default, so a user's own Settings > Audio pick (persisted to
# audio.json) still overrides it.
BLOCK_SIZE=4096

# Log to the app folder (writable, on the SD card) so a launch can be diagnosed over SSH.
./retroplug-sdl --width 640 --height 480 --block-size "$BLOCK_SIZE" >"$APP_DIR/retroplug.log" 2>&1
