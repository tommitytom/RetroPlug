# Included by packages/native/CMakeLists.txt, which is the only caller. `include()` keeps the
# CURRENT directory scope, so ${CMAKE_CURRENT_SOURCE_DIR} still means packages/native/ here and every
# relative source path below resolves exactly as it did inline. (add_subdirectory would NOT: it opens
# a child scope and re-roots that variable, which is the trap this split is avoiding.)
#
# Third-party runtime deps configured for OUR needs, plus the retroplug-cli N8 facets that ride on
# them. These were interleaved with target definitions, which is how the retroplug-sdl banner ended
# up split in half by seventy lines of rtmidi/serial/portaudio setup.

# RtMidi (deps/rtmidi submodule) — cross-platform MIDI I/O for live-MIDI hosts: the SDL standalone and the
# CLI n8-bridge (the plugin gets MIDI from its DAW host instead). Static; ALSA on Linux (JACK off — the
# handheld path is ALSA/PipeWire); RtMidi's own CMake selects the platform API + link libs. EXCLUDE_FROM_ALL
# so the `rtmidi` target is built only because a target links it (retroplug-sdl, or retroplug-cli when
# RETROPLUG_N8_BRIDGE is ON — see below).
set(RTMIDI_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(RTMIDI_API_JACK OFF CACHE BOOL "" FORCE)
set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/deps/rtmidi ${CMAKE_BINARY_DIR}/rtmidi EXCLUDE_FROM_ALL)
retroplug_silence_dep_warnings(rtmidi)

# --- retroplug-cli N8 transport facets: serial + live MIDI -> the TS N8 stack ------------------------
# The N8 tools (n8-load / n8-bridge / n8-sync) are all TS now (packages/retroplug/src/n8 + cli/sessions/):
# native only provides the thin transport facets they ride on - the serial byte transport (SerialRpcService
# over wjwwood/serial) and the live-MIDI-input facet (MidiRpcService over RtMidi). No C++ Edio/menu/translator
# here anymore (the TS edio.ts + risaSyncTranslator.ts replace them). ON by default; turn OFF to keep
# retroplug-cli (and thus the CI `all` build) free of the RtMidi/serial deps (ALSA dev headers on Linux) -
# the N8 tools then just fail to find a device.
option(RETROPLUG_N8_BRIDGE "Build the retroplug-cli N8 transport facets (links RtMidi + serial)" ON)
if(RETROPLUG_N8_BRIDGE)
    target_sources(retroplug-cli PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/host/n8/SerialRpcService.cpp   # serial byte-transport RPC facet (TS N8 seam)
        ${CMAKE_CURRENT_SOURCE_DIR}/src/host/n8/WjwwoodSerialPort.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/host/input/MidiIo.cpp          # RtMidi in/out seam (shared with retroplug-sdl)
        ${CMAKE_CURRENT_SOURCE_DIR}/src/host/input/MidiRpcService.cpp) # live-MIDI-input RPC facet (TS bridge seam)
    target_link_libraries(retroplug-cli PRIVATE serial rtmidi)
    target_compile_definitions(retroplug-cli PRIVATE RETROPLUG_N8_BRIDGE)
endif()

# PortAudio (deps/portaudio submodule, `pipewire` branch) — the standalone's audio backend, replacing the SDL
# audio device/callback for better latency + device handling than SDL's ALSA-compat path (the plugin uses its
# DAW host's audio, so only retroplug-sdl needs this). Static. The `portaudio` CMake auto-selects ALSA (dev
# present via rtmidi), the native PipeWire host API where libpipewire-0.3 is found, and JACK where jack dev is
# found. The Settings > Audio > Driver picker lists whatever host APIs end up compiled in here. EXCLUDE_FROM_ALL
# so `portaudio` builds only because retroplug-sdl links it.
set(PA_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# We want PipeWire (native) + ALSA (fallback) + JACK. PortAudio auto-enables every host API whose dev libs it
# finds, and libsdl2-dev drags in libpulse-dev + libsndio-dev — so without these the binary ends up NEEDing
# libpulse / libsndio.so.7, which the muOS device doesn't ship (it failed to load there). Force them off.
set(PA_USE_SNDIO OFF CACHE BOOL "" FORCE)
set(PA_USE_PULSEAUDIO OFF CACHE BOOL "" FORCE)
set(PA_USE_OSS OFF CACHE BOOL "" FORCE)
# Load the three Linux backends with dlopen rather than linking them, so none of libasound.so.2 /
# libpipewire-0.3.so.0 / libjack.so.0 is a DT_NEEDED entry on retroplug-sdl. Compiling a backend in no longer
# means the binary refuses to START where that lib is absent (a PipeWire-less desktop, a JACK-less box, the
# muOS handheld); a missing lib now just omits that one host API from the Settings > Audio > Driver picker and
# leaves the rest working (the backends' *_Initialize soft-fail: *hostApi=NULL + paNoError, which Pa_Initialize
# skips). This is the runtime half of the same problem the PA_USE_* force-offs above solve at compile time, and
# it's why those are still needed: dlopen only helps for backends we actually want. libSDL2 stays linked — it's
# the window/input shell.
set(PA_ALSA_DYNAMIC ON CACHE BOOL "" FORCE)
set(PA_PIPEWIRE_DYNAMIC ON CACHE BOOL "" FORCE)
set(PA_JACK_DYNAMIC ON CACHE BOOL "" FORCE)
# With no launch penalty left, JACK needs no opt-in flag: `unset` clears any stale cached PA_USE_JACK so the
# fork's cmake_dependent_option(PA_USE_JACK ... ON JACK_FOUND OFF) re-decides on JACK_FOUND — compiled in
# wherever jack dev headers are present, and absent by construction where they aren't (the arm sysroot has
# none, so the handheld build is simply JACK-less, and it launches fine regardless).
unset(PA_USE_JACK CACHE)
add_subdirectory(${CMAKE_SOURCE_DIR}/deps/portaudio ${CMAKE_BINARY_DIR}/portaudio EXCLUDE_FROM_ALL)
retroplug_silence_dep_warnings(portaudio)
