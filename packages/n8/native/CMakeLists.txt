# The Everdrive N8 Pro pack (docs/modular-graph-phase-e2-design.md section 3): rp.n8.nes, the cart as a node, and the
# link interface it drives the cart through (n8/CartLink.hpp), with the desktop's half beside it: the Edio protocol, the
# serial thread that streams to the cart, the host that owns the link and runs the cart's jobs, and the serial port
# (n8/desktop/). BlipToaster's reliable link (transport/N8ReliableLink.hpp) is the pack's as well: N8Link speaks it to
# the cart, and the emulated NES FIFO role in packages/native speaks it from the cart's side. Added by
# retroplug_package(n8) (cmake/packages.cmake). Like any pack, it depends on node-sdk alone; nothing here links a
# packages/native target.

# --- retroplug-n8: the rp_n8_package entry, rp.n8.nes (src/n8/N8Node.cpp). The node reaches the cart only through the
# rp::n8::CartLink a host binds by name (rp.host.devices/2), so it holds no serial port, no thread and no protocol. A
# `device` entry: the desktop build compiles it as a device does too (retroplug-n8-device), and the device check
# (packages/graph-runtime/device-check) links it into the RP2350 image under the link trap.
retroplug_package_library(retroplug-n8
    ${CMAKE_CURRENT_SOURCE_DIR}/src/n8/N8Node.cpp)
target_include_directories(retroplug-n8 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-n8 PUBLIC retroplug-node-sdk)

# The device build (RP_DEVICE_BUILD, cmake/packages.cmake) stops here: the desktop's half below is a serial port and a
# thread, which a device has neither of, and its manifest entry is not `device`.
if(RP_DEVICE_BUILD)
    return()
endif()

# Threads: N8Link's serial thread. Found here as well as in packages/native, as an imported target is visible only in
# the directory that finds it and below.
find_package(Threads REQUIRED)

# --- retroplug-n8-desktop: the cart's desktop link (src/n8/desktop/): Edio, the krikzz protocol over an ISerialPort;
# N8Link, the serial thread streaming MIDI to the cart's FIFO; N8Host, which owns that link, its config and n8.cfg, and
# runs the cart's capability jobs one at a time (graph/desktop/DeviceJobSlot.hpp); N8Menu, the on-device menu's
# commands a ROM job drives; WjwwoodSerialPort, the ISerialPort over deps/serial. N8CartLink.hpp, the rp::n8::CartLink
# over an N8Host that a host hands rp.n8.nes, is a header. No entry of its own: its manifest entry names the provider
# (rp_n8_provider, N8Provider.cpp: the N8Host, the cart link installed as `n8`, the facet's service) and the facet
# (N8Rpc.hpp, header-only, so the pack itself needs neither rpcpp nor QuickJS: a product's server instantiates it). A
# product starts the provider through its generated rp_start_providers (cmake/packages.cmake) and names nothing here;
# one that never does (the render host, most tests) pulls none of this library's objects in.
retroplug_package_library(retroplug-n8-desktop
    ${CMAKE_CURRENT_SOURCE_DIR}/src/n8/desktop/Edio.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/n8/desktop/N8Link.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/n8/desktop/N8Host.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/n8/desktop/N8Menu.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/n8/desktop/N8Provider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/n8/desktop/WjwwoodSerialPort.cpp)
target_link_libraries(retroplug-n8-desktop
    PUBLIC  retroplug-n8           # n8/CartLink.hpp, which N8CartLink.hpp implements, and the include root
            retroplug-node-sdk     # graph/desktop/DeviceJobSlot.hpp, transport/SpscRing.hpp (N8Link's rings), jobs
    PRIVATE serial                 # wjwwood/serial, behind WjwwoodSerialPort.cpp alone (cmake/serial.cmake)
            Threads::Threads)      # N8Link's serial thread

# --- the pack's test binaries (cmake/test-binary.cmake), built by name; sources resolve against this directory.

# Guards the Everdrive N8 Pro protocol framing (Edio): that fifoWR emits the exact krikzz CMD_MEM_WR byte stream to
# ADDR_FIFO and the connect handshake accepts / rejects the 0xA5 status word; plus the N8Link forward path (the SDL
# standalone's realtime serial thread) and the cart's capability jobs (SRAM dump and restore, the ROM upload and its
# boot), which pause and resume streaming around the transfer; and the provider and the facet over them. Drives those
# against a capturing FakeSerialPort - no serial port is opened, no `rtmidi`, no hardware - and runs the jobs into a
# record of its own (test/FakeJobs.hpp), so like the pack's libraries it reaches nothing of packages/native. The CLI's
# risa-sync translation lives in TS (packages/retroplug src/n8, `pnpm test n8`); the C++ Edio/N8Link/N8Menu twin backs
# the realtime + job paths. Run by `pnpm test:n8`.
retroplug_add_test_binary(
    NAME     retroplug-n8-test
    SOURCES  test/Edio.test.cpp
             test/N8Host.test.cpp
             test/N8CartLink.test.cpp      # the cart device link over N8Link + N8Host
             test/N8CartJobs.test.cpp      # the cart's capability jobs, run with streaming paused
             test/N8Provider.test.cpp      # the provider and the facet's link methods, over a JSON server
    LIBS     retroplug-n8-desktop                      # Edio, N8Link, N8Host, N8Menu, the provider; the job ABI
             reflectcpp::reflectcpp                    # rfl::json - parse the shared Edio golden (twins edio.test.ts)
             rpcpp                                     # the server the facet is mounted on (JSON codec)
    # The shared Edio framing golden both this and edio.test.ts assert against (single source, no drift).
    DEFS     EDIO_GOLDEN_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/test/n8/edio-golden.json")

# WjwwoodSerialPort against a pseudo-terminal: that a read's timeout does not leak into the next write (the bug
# that left a real N8 mid-command behind N8Link's 0 ms back-channel polls). The one N8 test that opens a real serial
# port, so it is its own binary - retroplug-n8-test stays on fakes. Run by `pnpm test:n8`.
retroplug_add_test_binary(
    NAME     retroplug-serial-test
    SOURCES  test/WjwwoodSerialPort.test.cpp
    LIBS     retroplug-n8-desktop)                     # WjwwoodSerialPort, over deps/serial

# A manual hardware harness (NOT CI) that drives the real N8 card's jobs through the same native
# N8Host the cart's node hands them to, over a real /dev/ttyACM0 (the real WjwwoodSerialPort +
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
