# The Launchpad pack (docs/modular-graph-phase-f3-design.md sections 2 and 3): rp.launchpad.surface-in / -out, the
# control surface as nodes, and the link interface they drive the device through (launchpad/SurfaceLink.hpp), with the
# desktop's half beside it: the claimed pair's transport (ControllerLink), the loose MIDI ports a TS scan opens
# (MidiPortPool), the cross-process claim (PortClaim), the RtMidi backend (RtMidiPort), the provider that installs the
# link and the facet the TS client reaches it through. Added by retroplug_package(launchpad) (cmake/packages.cmake).
# Like any pack it depends on node-sdk alone; nothing here links a packages/native target.

# --- retroplug-launchpad: the rp_launchpad_package entry and rp.launchpad.surface-in / -out
# (src/launchpad/SurfaceNodes.cpp), over the rp::launchpad::SurfaceLink a host binds by name (rp.host.devices/2). A
# `device` entry: the desktop build compiles it as a device does too (-fno-exceptions, -fno-rtti, through the
# -device object library every package's device entry gets), and the device check links it into the RP2350 image. The
# nodes own no thread and no port; the link's own callback and writer threads are the desktop provider's.
retroplug_package_library(retroplug-launchpad
    ${CMAKE_CURRENT_SOURCE_DIR}/src/launchpad/SurfaceNodes.cpp)
target_include_directories(retroplug-launchpad PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-launchpad PUBLIC retroplug-node-sdk)

# The device build stops here: the desktop's half below is MIDI ports, file locks and threads, which a device has none
# of, and its manifest entry is not `device`.
if(RP_DEVICE_BUILD)
    return()
endif()

# Threads: ControllerLink's writer thread, the pool's mutexes. Found here as well as in packages/native, as an imported
# target is visible only in the directory that finds it and below.
find_package(Threads REQUIRED)

# --- retroplug-launchpad-desktop: the transport (src/launchpad/desktop/): ControllerLink, the claimed pair with its
# fixed-slot rings and its writer thread; MidiPortPool, the loose ports a scan opens; PortClaim, the cross-process claim
# under <configDir>/locks; RtMidiPort, the only RtMidi user (SysEx on). Its manifest entry names the provider
# (rp_launchpad_provider, LaunchpadProvider.cpp: the link installed as `launchpad`, the pool, the facet's service) and
# the facet (LaunchpadRpc.hpp, implemented in LaunchpadRpc.cpp; only its registerRpc is a template, which a product's
# server instantiates, so the pack itself needs neither rpcpp nor QuickJS, only reflect-cpp's byte string). A product
# starts the provider through its generated rp_start_providers (cmake/packages.cmake) and names nothing here.
retroplug_package_library(retroplug-launchpad-desktop
    ${CMAKE_CURRENT_SOURCE_DIR}/src/launchpad/desktop/ControllerLink.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/launchpad/desktop/LaunchpadProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/launchpad/desktop/LaunchpadRpc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/launchpad/desktop/MidiPortPool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/launchpad/desktop/PortClaim.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/launchpad/desktop/RtMidiPort.cpp)
target_link_libraries(retroplug-launchpad-desktop
    PUBLIC  retroplug-launchpad   # the include root, launchpad/SurfaceLink.hpp, the package id
            retroplug-node-sdk    # transport/SpscRing.hpp (the send ring), graph/rp_provider.h, reflect-cpp
    PRIVATE rtmidi                # deps/rtmidi, behind RtMidiPort.cpp alone (defined at root scope, cmake/rtmidi.cmake)
            Threads::Threads)     # ControllerLink's writer thread

# --- the pack's test binaries (cmake/test-binary.cmake), built by name; sources resolve against this directory.

# The transport, ported from feature/launchpad's Controller.test.cpp (docs/modular-graph-phase-f3-design.md section 8):
# the claimed pair on fixed slots, a generation per claim and the slots of an earlier one dropped by the audio thread,
# the span of messages one drain answers, a swap mid-drain driven from a producer thread and the audio thread (run under
# ThreadSanitizer by tools/run-sanitizer.sh's launchpad leg), the writer thread's ordering and the provider's per-frame
# wake, the close bytes replayed on disconnect and destruction, the audio path's two calls allocating nothing, the pool's
# inputs-held edge, PortClaim's one claim per machine, and the provider and the facet over a fake opener: no hardware,
# no MIDI system, no ALSA. The real RtMidi backend is linked (it is the desktop library's) but never opened.
retroplug_add_test_binary(
    NAME     retroplug-launchpad-test
    SOURCES  test/Controller.test.cpp
             test/SurfaceLink.test.cpp
             test/CountingAllocator.cpp       # the drain and the push allocate and free nothing
    LIBS     retroplug-launchpad-desktop   # the transport, the pool, the claim, the provider, the facet
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test              # FakePorts.hpp
    DEFS     RP_LAUNCHPAD_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/golden")  # the two types' descriptors
