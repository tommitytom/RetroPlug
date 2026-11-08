# RtMidi (deps/rtmidi submodule): cross-platform MIDI I/O for live-MIDI hosts: the SDL standalone, the CLI's N8 bridges
# and a pack's own MIDI ports (the Launchpad's, packages/launchpad/native's desktop target). Static; ALSA on Linux (JACK
# off: the handheld path is ALSA/PipeWire); RtMidi's own CMake selects the platform API and its link libs.
# EXCLUDE_FROM_ALL, so the `rtmidi` target is built only because a target links it.
#
# Included at ROOT scope, before cmake/packages.cmake: a package's native/CMakeLists.txt is added while that list's
# packages are configured, and one of them (launchpad) links this target, which must exist by then. packages/native's
# cmake/deps.cmake used to add it; its own targets link the target defined here.
set(RTMIDI_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(RTMIDI_API_JACK OFF CACHE BOOL "" FORCE)
set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/deps/rtmidi ${CMAKE_BINARY_DIR}/rtmidi EXCLUDE_FROM_ALL)
retroplug_silence_dep_warnings(rtmidi)
# A product that starts a provider links a package's desktop library, and the plugin formats are shared modules: the
# archive has to be PIC wherever it is added from. packages/native's cmake/deps.cmake added it inside a directory whose
# targets already were; at root scope the property is off unless it is asked for.
set_target_properties(rtmidi PROPERTIES POSITION_INDEPENDENT_CODE ON)
