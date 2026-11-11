# PortAudio (deps/portaudio submodule, `pipewire` branch): the standalone's audio backend, behind the audio package's
# interface (packages/audio-portaudio, whose target links it; the plugin uses its DAW host's audio, and the CLI and the
# headless host name no audio backend at all). Static. Included at ROOT scope, BESIDE cmake/rtmidi.cmake and before
# cmake/packages.cmake, because a package's native/CMakeLists.txt is added while that list configures the packages and
# retroplug-audio-portaudio must find this target then. Its own cache options and probes (ALSA, PipeWire, JACK) run for
# every build, plugin-only included.
#
# The `portaudio` CMake auto-selects ALSA (dev present), the native PipeWire host API where libpipewire-0.3 is found,
# and JACK where jack dev is found. The Settings > Audio > Driver picker lists whatever host APIs end up compiled in here.
# libasound is still a DT_NEEDED of retroplug-sdl, but not through RtMidi any more: retroplug-audio-portaudio names
# ALSA itself (its quietening calls snd_lib_error_set_handler) and retroplug-midi brings its own through RtMidi.
# EXCLUDE_FROM_ALL, so `portaudio` builds only because a product's audio backend links it.
set(PA_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# We want PipeWire (native) + ALSA (fallback) + JACK. PortAudio auto-enables every host API whose dev libs it
# finds, and libsdl2-dev drags in libpulse-dev + libsndio-dev - so without these the binary ends up NEEDing
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
# it's why those are still needed: dlopen only helps for backends we actually want. libSDL2 stays linked: it is the
# window/input shell.
set(PA_ALSA_DYNAMIC ON CACHE BOOL "" FORCE)
set(PA_PIPEWIRE_DYNAMIC ON CACHE BOOL "" FORCE)
set(PA_JACK_DYNAMIC ON CACHE BOOL "" FORCE)
# With no launch penalty left, JACK needs no opt-in flag: `unset` clears any stale cached PA_USE_JACK so the
# fork's cmake_dependent_option(PA_USE_JACK ... ON JACK_FOUND OFF) re-decides on JACK_FOUND: compiled in
# wherever jack dev headers are present, and absent by construction where they aren't (the arm sysroot has
# none, so the handheld build is simply JACK-less, and it launches fine regardless).
unset(PA_USE_JACK CACHE)
add_subdirectory(${CMAKE_SOURCE_DIR}/deps/portaudio ${CMAKE_BINARY_DIR}/portaudio EXCLUDE_FROM_ALL)
retroplug_silence_dep_warnings(portaudio)
