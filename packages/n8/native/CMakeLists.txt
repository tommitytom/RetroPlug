# The Everdrive N8 Pro pack (docs/modular-graph-phase-e2-design.md section 3): rp.n8.nes, the cart as a node, and the
# link interface it drives the cart through (n8/CartLink.hpp), with the desktop's half beside it: the Edio protocol, the
# serial thread that streams to the cart, the host that owns the link and runs the cart's jobs, and the serial port
# (n8/desktop/). BlipToaster's reliable link (transport/N8ReliableLink.hpp) comes from the n8-wire package, which this
# pack declares: N8Link speaks it to the cart, and the mesen pack's emulated NES FIFO role speaks it from the cart's
# side. Added by retroplug_package(n8) (cmake/packages.cmake). Like any pack, it depends on the node SDK and on the wire
# it shares with mesen, and on nothing of the shell; nothing here links a packages/native target.

# --- retroplug-n8: the rp_n8_package entry, rp.n8.nes (src/n8/N8Node.cpp), and the Edio protocol written once for both
# targets: Edio (the commands, over the abstract n8/Transport.hpp), Menu (the on-device menu's commands) and Ops (a file
# read or written whole, a ROM booted). The node reaches the cart only through the rp::n8::CartLink a host binds by
# name (rp.host.devices/2), so it holds no serial port, no thread and no protocol; the protocol is what drives the cart
# on the other side of that link (the desktop's N8Host below, the Pico box). A `device` entry: the desktop build
# compiles all of it as a device does too (retroplug-n8-device), and the device check
# (packages/graph-runtime/device-check) links it into the RP2350 image under the link trap.
retroplug_package_library(retroplug-n8
    ${CMAKE_CURRENT_SOURCE_DIR}/src/n8/N8Node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/n8/Edio.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/n8/Menu.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/n8/Ops.cpp)
# N8Node.cpp includes n8wire/ExpansionVolume.hpp, whose root reaches this target, and the device twin that compiles its
# sources again and copies its INCLUDE_DIRECTORIES, through the link below: that property carries a linked INTERFACE
# target's roots.
target_include_directories(retroplug-n8 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-n8 PUBLIC retroplug-node-sdk retroplug-n8-wire)

# The device build (RP_DEVICE_BUILD, cmake/packages.cmake) stops here: the desktop's half below is a serial port and a
# thread, which a device has neither of, and its manifest entry is not `device`.
if(RP_DEVICE_BUILD)
    return()
endif()

# Threads: N8Link's serial thread. Found here as well as in packages/native, as an imported target is visible only in
# the directory that finds it and below.
find_package(Threads REQUIRED)

# --- retroplug-n8-desktop: the cart's desktop link (src/n8/desktop/), over the portable protocol above: N8Link, the
# serial thread streaming MIDI to the cart's FIFO; N8Host, which owns that link, its config and n8.cfg, and runs the
# cart's capability jobs one at a time (graph/desktop/DeviceJobSlot.hpp); SerialTransport, the n8/Transport.hpp over
# deps/serial (PortTransport.hpp, the desktop's owned transports and the text of a port's failure). N8CartLink.hpp, the
# rp::n8::CartLink over an N8Host that a host hands rp.n8.nes, is a header. No entry of its own: its manifest entry
# names the provider (rp_n8_provider, N8Provider.cpp: the N8Host, the cart link installed as `n8`, the facet's service)
# and the facet (N8Rpc.hpp: the streaming link's methods and the CLI's sessions, implemented in N8Rpc.cpp; only its
# registerRpc is a template, which a product's server instantiates, so the pack itself needs neither rpcpp nor QuickJS,
# only reflect-cpp's byte string). A product starts the provider through its generated rp_start_providers
# (cmake/packages.cmake) and names nothing here; one that never does (the render host, most tests) pulls none of this
# library's objects in.
retroplug_package_library(retroplug-n8-desktop
    ${CMAKE_CURRENT_SOURCE_DIR}/src/n8/desktop/N8Link.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/n8/desktop/N8Host.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/n8/desktop/N8Provider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/n8/desktop/N8Rpc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/n8/desktop/SerialTransport.cpp)
target_link_libraries(retroplug-n8-desktop
    PUBLIC  retroplug-n8           # the protocol (n8/Edio.hpp, Menu.hpp, Ops.hpp), n8/CartLink.hpp, the include root
            retroplug-node-sdk     # graph/desktop/DeviceJobSlot.hpp, transport/SpscRing.hpp (N8Link's rings), jobs;
                                   # reflect-cpp, whose rfl::Bytestring the facet's methods take and answer
    PRIVATE serial                 # wjwwood/serial, behind SerialTransport.cpp alone (cmake/serial.cmake)
            Threads::Threads)      # N8Link's serial thread

# --- the pack's test binaries (cmake/test-binary.cmake), built by name; sources resolve against this directory.

# Guards the Everdrive N8 Pro protocol (Edio, Menu, Ops) byte for byte against its golden (test/edio-golden.json),
# every failure each primitive can meet, the menu's replies and a boot over a model of the cart (test/FakeN8.hpp); the
# N8Link forward path (the standalone's and the plugin's realtime serial thread) and the cart's capability jobs (SRAM
# dump and restore, the ROM upload and its boot), which pause and resume streaming around the transfer; and the
# provider and the facet over them (the streaming link's methods, the CLI's sessions). Drives those against fake ports
# - no serial port is opened, no `rtmidi`, no hardware - and runs the jobs into a record of its own
# (test/FakeJobs.hpp), so like the pack's libraries it reaches nothing of packages/native. Run by `pnpm test:n8`.
retroplug_add_test_binary(
    NAME     retroplug-n8-test
    SOURCES  test/Edio.test.cpp
             test/Ops.test.cpp             # the menu's commands and the operations over FakeN8
             test/N8ReliableLink.test.cpp  # the reliable link's sender half, over LinkRomModel.hpp's ROM rules
             test/N8Host.test.cpp
             test/N8CartLink.test.cpp      # the cart device link over N8Link + N8Host
             test/N8CartJobs.test.cpp      # the cart's capability jobs, run with streaming paused
             test/N8Provider.test.cpp      # the provider and the facet's two halves, over QuickJS
    LIBS     retroplug-n8-desktop                      # the protocol, N8Link, N8Host, the provider; the job ABI
             retroplug-testing                         # LinkRomModel.hpp alone: the ROM's link rules, both halves' oracle
             reflectcpp::reflectcpp                    # rfl::json - parse the Edio golden
             rpcpp                                     # the server the facet is mounted on (QjsRpc.hpp)
             qjs                                       # QuickJS, the products' codec that server runs over
    DEFS     EDIO_GOLDEN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test/edio-golden.json")

# SerialTransport against a pseudo-terminal: that a read's timeout does not leak into the next write (the bug that left
# a real N8 mid-command behind N8Link's 0 ms back-channel polls). The one N8 test that opens a real serial
# port, so it is its own binary - retroplug-n8-test stays on fakes. Run by `pnpm test:n8`.
retroplug_add_test_binary(
    NAME     retroplug-serial-test
    SOURCES  test/SerialTransport.test.cpp
    LIBS     retroplug-n8-desktop)                     # SerialTransport, over deps/serial

# A manual hardware harness (NOT CI) that drives the real N8 card's jobs through the same native
# N8Host the cart's node hands them to, over a real /dev/ttyACM0 (the real SerialTransport +
# `serial`). Brings its own main(), hence NO_CATCH2.
# Build by name: cmake --build build --target retroplug-n8-hwtest.
retroplug_add_test_binary(
    NAME     retroplug-n8-hwtest
    NO_CATCH2
    SOURCES  test/n8-sd-hwtest.cpp                     # its jobs run into test/FakeJobs.hpp's record
    LIBS     retroplug-n8-desktop)
# `timing` times against an ALSA capture of the console's audio, so it exists only where ALSA does. Linking
# ALSA::ALSA unconditionally failed the GENERATE step on macOS/Windows - EXCLUDE_FROM_ALL does not spare a target
# from resolving its link libraries.
find_package(ALSA)
if(ALSA_FOUND)
    target_sources(retroplug-n8-hwtest PRIVATE test/n8-timing.cpp)
    target_link_libraries(retroplug-n8-hwtest PRIVATE ALSA::ALSA)
    target_compile_definitions(retroplug-n8-hwtest PRIVATE RP_N8_TIMING=1)
endif()
