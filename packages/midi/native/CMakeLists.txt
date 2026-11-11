# The midi package (docs/modular-graph-phase-f5b-design.md sections 4.1, 4.2 and 4.5): the backend interface and the
# one copy of the port rules, RtMidi as the real backend (RtMidiBackend.cpp, the file that includes RtMidi.h), the
# host's MIDI stream over the interface, the in-process fake a test injects, and the facet as a provider with an rpc.
# Added by retroplug_package(midi) (cmake/packages.cmake).
#
# `retroplug-midi` is NOT a manifest `native` item: cmake/packages.cmake links every non-provider target of a listed
# package into every product that lists it, which would put RtMidi into the host, the render-host test, the Node addon,
# retroplug-ui-test and every test binary that names the desktop list. It is an unlisted library target, as
# retroplug-launchpad-harness is, linked BY NAME by the products that use it (retroplug-sdl) and by this package's
# facet, so a product that lists midi without naming it in PROVIDERS links no RtMidi. The test binary below links it
# directly.
#
# The fake is test support, not part of the library: it sits under native/test-support, an include root only the test
# binaries and the headless host add (FakeMidiBackend.hpp), so a product cannot compile it.
#
# Every target here is the desktop's (the facet opens ports, runs threads and answers an RPC), so a device build defines
# none of them: the manifest names no `device` item, and the early return keeps a device configure from building RtMidi.
if(RP_DEVICE_BUILD)
    return()
endif()

# Threads: RtMidi's callback threads are its own, but the stream's epoch handshake and the test's swap case are ours.
find_package(Threads REQUIRED)

# --- retroplug-midi: the interface, the RtMidi backend, the port rules and the stream
retroplug_package_library(retroplug-midi
    ${CMAKE_CURRENT_SOURCE_DIR}/src/midi/MidiStream.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/midi/RtMidiBackend.cpp)
target_include_directories(retroplug-midi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-midi
    PRIVATE rtmidi              # deps/rtmidi, behind RtMidiBackend.cpp alone (root scope, cmake/rtmidi.cmake)
            Threads::Threads)

# --- retroplug-midi-facet: MidiRpcService as a provider with an rpc (the manifest's `native` entry)
retroplug_package_library(retroplug-midi-facet
    ${CMAKE_CURRENT_SOURCE_DIR}/src/midi/facet/MidiProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/midi/facet/MidiRpcService.cpp)
target_include_directories(retroplug-midi-facet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-midi-facet
    PUBLIC  retroplug-midi
            retroplug-node-sdk)  # graph/rp_provider.h, and rfl's byte string in the RPC's DTOs

# --- retroplug-midi-test-support: the fake's include root, for the binaries that script a MIDI system.
# An interface target, so the header is reachable by a test binary and by the headless host (both link this) and by no
# product: nothing in the library above puts test-support on a product's include path.
add_library(retroplug-midi-test-support INTERFACE)
target_include_directories(retroplug-midi-test-support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/test-support)

# --- the package's tests (cmake/test-binary.cmake), built by name. Every source resolves against this directory.

# The port rules and the stream are pure logic over a backend the test scripts, so neither case opens a MIDI system:
# retroplug-midi-test links the fake's header and nothing of RtMidi's at runtime, though the library carries it. Build
# this target in build-tsan/ too: the output-swap, the close and the delivery-thread cases are what ThreadSanitizer is
# for.
retroplug_add_test_binary(
    NAME     retroplug-midi-test
    SOURCES  test/MidiPortSelect.test.cpp
             test/MidiStream.test.cpp
    LIBS     retroplug-midi               # the interface and the stream
             retroplug-midi-test-support)  # midi/FakeMidiBackend.hpp
