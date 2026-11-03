# Included by packages/native/CMakeLists.txt, which is the only caller. `include()` keeps the
# CURRENT directory scope, so ${CMAKE_CURRENT_SOURCE_DIR} still means packages/native/ here and every
# relative source path below resolves exactly as it did inline. (add_subdirectory would NOT: it opens
# a child scope and re-roots that variable, which is the trap this split is avoiding.)
#
# --- native test binaries ---------------------------------------------------------------------------
# Thirteen Catch2 binaries, all EXCLUDE_FROM_ALL and built BY NAME (nothing reads BUILD_TESTING):
# `pnpm test:plugin` builds and runs ten of them, `pnpm test:n8` the next two, and the thirteenth needs a
# real console on a USB port. The shared shape lives in retroplug_add_test_binary() — see
# cmake/test-binary.cmake; what is written out here is only what differs per test.

# Pure-C++ Catch2 checks for plugin/ mechanisms that need real JSContexts but no LVGL/txiki host —
# currently the per-context window-hook routing (ContextTargets.hpp) and the last-good project-state
# cache (LastGoodState.hpp). Links bare quickjs (qjs) + Catch2.
retroplug_add_test_binary(
    NAME     retroplug-plugin-test
    SOURCES  test/plugin/ContextTargets.test.cpp
             test/plugin/LastGoodState.test.cpp
    LIBS     qjs                                        # bare quickjs: JS_* + ArrayBuffer / CFunctionData
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/plugin         # ContextTargets.hpp
             ${RP_QUICKJS_INCLUDE})                     # quickjs.h

# Guards the dynamic-parameter diff classifier in the DPF fork (PluginExporter::reinitParameters —
# spec/12-dynamic-parameters.md): which parameter fields a LIVE plugin may re-declare, and that
# `hints`/`ranges` are never written behind the audio thread's back. It builds NO plugin format — just
# DPF's format-neutral core (DistrhoPlugin.cpp; the rest of what it needs is header-only) plus a toy
# plugin in the test — so its own test/plugin/dynparams/DistrhoPluginInfo.h is ordered first on the
# include path to shadow the shipped one.
retroplug_add_test_binary(
    NAME     retroplug-dynparams-test
    SOURCES  test/plugin/DynamicParameters.test.cpp
             ${DPFJS_PATH}/deps/dpf/distrho/src/DistrhoPlugin.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test/plugin/dynparams   # the test's DistrhoPluginInfo.h — must come first
             ${DPFJS_PATH}/deps/dpf/distrho)                     # DistrhoPlugin.hpp + src/DistrhoPluginInternal.hpp

# Guards TjsHostRuntime::init's class-id counter sync (the plugin-editor blank-UI fix); it needs the
# real txiki host, so it links retroplug-backend.
retroplug_add_test_binary(
    NAME     retroplug-classid-test
    SOURCES  test/plugin/ClassIdReserve.test.cpp
    LIBS     retroplug-backend                          # TjsHostRuntime (+ txiki, quickjs) + ClassIdSpace.hpp
    INCLUDES ${RP_QUICKJS_INCLUDE})                     # quickjs.h

# Guards the cores' audio paths: the real SameBoy and NES per-channel taps (lane-counted finishBlock),
# the NES split routings through a real Engine, and each backend's block triad. Links retroplug-backend,
# which PUBLIC-brings retroplug-core (the cores) and the Engine.
#
# The ROM defines: mGB's boot chime supplies real signal for the SameBoyStems fidelity tests, and a ch1
# note in the BlipToaster ROM does the same for the NES pin test. smsggdj ships in both cartridge shapes
# because the SMS and GG builds differ in visible geometry and video config, so both are needed to guard
# the overscan setup. resources/ never ships (release.yml packages build/bin
# plus the license bundle), so these are test-only.
retroplug_add_test_binary(
    NAME     retroplug-audio-test
    SOURCES  test/audio/SameBoyStems.test.cpp
             test/audio/EngineSampleRate.test.cpp      # host sample-rate change re-rates live cores
             test/audio/SameBoySerialTiming.test.cpp   # host MIDI-in keeps its intra-block sample offset over serial
             test/audio/NesApuLatency.test.cpp         # runtime NES APU flush window (live apuLatencyMs knob)
             test/audio/NesN8FifoTiming.test.cpp       # host MIDI-in keeps its intra-block offset into the N8 FIFO
             test/audio/NesEverdriveFifo.test.cpp      # Edio status protocol leaves no stale byte to desync MIDI parsing
             test/audio/N8ReliableLink.test.cpp        # BlipToaster reliable link: framing, acks, re-sends (sender + emulated FIFO role)
             test/audio/NesStems.test.cpp              # NES stereo-mod pins re-sum to the mix (spec/10 step 5)
             test/audio/NesSplitRouting.test.cpp       # NES-in-plugin: PinSplit/ChannelSplit arm the tap live + mono-pack
             test/audio/SmsAudio.test.cpp              # SMS/GG boot, visible geometry, non-silent PSG, teardown survival
             test/audio/GbaFirmware.test.cpp           # each GBA system loads ITS bios, not whichever landed last
             test/audio/GbaAudio.test.cpp              # the GBA block triad terminates and fills a block
             test/audio/MesenBlockOps.test.cpp         # the shared drain tail blanks a short ring
             test/audio/NesTeardown.test.cpp           # the NES twin of SMS's construct/destruct guard
             test/audio/SameBoyModels.test.cpp         # every SameBoyModel (incl. SGB variants) boots + renders
             test/audio/EngineInvoker.test.cpp         # the teardown discards a staged-message payload
             test/audio/Dither.test.cpp                # the kit TPDF dithers are seeded from their input
             test/graph/CountingAllocator.cpp          # replaces operator new / delete: the N8 FIFO's [alloc] case
             # The kit path's effect wrappers, so the dither cases are held to what rp::lsdj::processEffect does to a
             # buffer, not only to the kernels underneath it (retroplug-graph-test compiles it too, for the two
             # buffer-reading nodes). It needs the kernels, named in LIBS below.
             ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/Effects.cpp
    LIBS     retroplug-backend                         # Engine + retroplug-core (SystemBase + the cores)
             retroplug-dsp-kernels                     # Effects.cpp's wrappers call these
    PACKAGES ${RP_DESKTOP_PACKAGES}
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test          # graph/CountingAllocator.hpp
    DEFS     RP_MGB_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/mGB.gb"
             RP_BLIPTOASTER_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/bliptoaster.nes"
             RP_SMS_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.sms"
             RP_GG_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.gg")

# Guards the engine's graph (docs/modular-graph-phase-b1-design.md, -b2-design.md, -b3a-design.md, -b3b-design.md,
# -c2-design.md): each system node renders exactly what its core renders driven directly, its inputs and its settings'
# knobs included, the engine places, re-arms and frees what the documents TS builds say, the host MIDI I/O nodes move
# what the hosts stage and drain and the hardware nodes drive their fake device links (and the cart's node its real
# adapters, down to test/n8/FakeN8.hpp's serial port), TS nodes run behind the C ABI in the script host, a script
# node pack loads beside the built-in one into runtimes of its own and reloads, the generation it replaced freed off
# the audio thread, the protocol script nodes send what the DSP kernel they replaced
# was recorded sending, every document in buildGraph's goldens, its role goldens among them, plans on it, and an
# offline job (docs/modular-graph-phase-d2-design.md section 3) runs a document to completion on a worker host of its
# own, apart from the live plan, as an engine job (docs/modular-graph-phase-d3-design.md section 3) runs a render with a
# runtime and an Engine of its own. A hidden bench times a script node per block, another an offline job.
# Every core in it powers on in a fixed state (test/engine-graph/Deterministic.cpp), so two identical cores agree.
# CountingAllocator.cpp is shared with retroplug-graph-test, for the no-allocation checks. The script tests' bundle
# (the authoring layer for their toy nodes) is embedded here alone: cmake/bundles.cmake, retroplug-script-test-bundle.
retroplug_add_test_binary(
    NAME     retroplug-engine-graph-test
    SOURCES  test/engine-graph/Deterministic.cpp
             test/engine-graph/SystemNode.test.cpp
             test/engine-graph/MesenShared.test.cpp    # ClearState's empty state, and the notification dispatch's copy
             test/engine-graph/EngineGraph.test.cpp
             test/engine-graph/SlurpFile.test.cpp     # the file reader putResource reads a `path` resource with
             test/engine-graph/HostIo.test.cpp
             test/engine-graph/Devices.test.cpp        # rp.host.devices/1: the links a hardware node binds by name
             test/engine-graph/Capabilities.test.cpp   # rp.system.memory/1, sram/1, rom/1 as asynchronous jobs
             test/engine-graph/OfflineJobs.test.cpp    # a document run to completion on a worker host of its own
             test/engine-graph/EngineJobs.test.cpp     # a render run on the same pool by a RenderHost of its own
             test/engine-graph/N8Node.test.cpp         # rp.n8.nes: the real cart as a node, its jobs on the link
             test/engine-graph/N8Adapters.test.cpp     # rp.n8.nes through N8CartLink / N8Host to a fake serial port
             test/engine-graph/Surface.test.cpp        # rp.launchpad.surface-in / -out: the control surface as nodes
             test/engine-graph/StageNode.test.cpp      # staging a message at one node by id, through rp.stage/1
             test/engine-graph/ScriptNodes.test.cpp
             test/engine-graph/ScriptPacks.test.cpp
             test/engine-graph/ProtocolNodes.test.cpp
             test/engine-graph/ScriptBench.test.cpp    # hidden: run with "[bench]"
             test/engine-graph/BuildGraphGoldens.test.cpp
             test/engine-graph/Packages.test.cpp       # each product registers the node types its golden row holds
             test/graph/CountingAllocator.cpp          # replaces operator new / delete for this binary
             src/host/n8/Edio.cpp                      # the cart's adapters N8Adapters.test.cpp composes: serial-free,
             src/host/n8/N8Link.cpp                    # as retroplug-n8-test compiles them (the backend holds none of
             src/host/n8/N8Host.cpp                    # them; the hosts that own an N8Host compile their own)
             src/host/n8/N8Menu.cpp
             ${SCRIPT_TEST_BUNDLE_C}                    # rp_script_test_bundle
    LIBS     retroplug-backend                         # Engine + retroplug-core (SystemBase + the cores)
             retroplug-dsp-kernels                     # OfflineJobs.test.cpp holds a job's buffers to the kernels
             retroplug-dsp-resample                    # and to resampledLength, which the resample's node wraps
    PACKAGES ${RP_DESKTOP_PACKAGES}
    INCLUDES ${RP_QUICKJS_INCLUDE}
    DEFS     RP_MGB_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/mGB.gb"
             RP_BLIPTOASTER_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/bliptoaster.nes"
             RP_SMS_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.sms"
             RP_GG_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.gg"
             RP_SMS_SAV_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.sav"
             RP_BUILD_GRAPH_GOLDENS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/test/graph/golden/build-graph.json"
             RP_BUILD_GRAPH_ROLE_GOLDENS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/test/graph/golden/build-graph-roles.json"
             RP_SETTINGS_ENUMS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/src/settingsEnums.ts"  # the labels a knob takes
             RP_BUILD_GRAPH_TS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/src/graph/buildGraph.ts"  # the knob ranges
             RP_CORE_ROLES_TS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/src/coreRoles.ts"  # their ranges
             RP_SYSTEM_SETTINGS_TS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/src/systemSettings.ts"  # gain's range
             RP_PORTS_TS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/src/script/nodes/ports.ts"  # the queue capacities
             RP_PRODUCT_REGISTRATIONS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test/engine-graph/golden/product-registrations.json"
             RP_BUILD_TYPE="$<CONFIG>")                 # the bench prints the configuration it was built in
add_dependencies(retroplug-engine-graph-test retroplug-script-test-bundle)  # the embedded bytecode first

# Guards the native file watcher (NativeFileWatcher over efsw): config.json + bindings/ recursive
# detection, registered-ROM parent-dir watches, and the unregistered-file filter.
retroplug_add_test_binary(
    NAME     retroplug-watcher-test
    SOURCES  test/watcher/NativeFileWatcher.test.cpp
    LIBS     retroplug-backend)                        # NativeFileWatcher + efsw-static

# Guards the MIDI device-selection policy (Settings > MIDI): the pure, RtMidi-free helpers in
# MidiIo.hpp (hardwarePortIndices / matchPortIndex) that decide which hardware ports to open for a
# given selection. Header-only helpers → links just Catch2 (no RtMidi, no backend).
retroplug_add_test_binary(
    NAME     retroplug-midi-test
    SOURCES  test/midi/MidiPortSelect.test.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)          # host/input/MidiIo.hpp (pure helpers; RtMidi only forward-declared)

# Guards the control-surface device link (the instance menu's Launchpad submenu): LaunchpadLink's
# connect/disconnect lifecycle, the two rings (a message received reaching the audio-thread drain; an
# oversized LED message dropped rather than overrunning its slot), the FAREWELL replayed on both
# disconnect and destruct — Programmer mode locks the device's front panel, so skipping that strands
# the user's hardware — and LaunchpadHost's launchpad.cfg round-trip + reserved-port reporting. It
# also guards LaunchpadScanner, the device-inquiry port scan that decides whether the submenu appears
# at all: the probe reaching every output, a reply tagged with the PAIR that produced it, and a port
# that will not open being stepped over rather than failing the scan. Compiles all three against a
# capturing fake IMidiPort: no rtmidi, no MIDI system, no hardware.
retroplug_add_test_binary(
    NAME     retroplug-launchpad-test
    SOURCES  test/launchpad/LaunchpadLink.test.cpp
             test/launchpad/LaunchpadSurfaceLink.test.cpp  # the surface device link over LaunchpadLink
             src/host/launchpad/LaunchpadLink.cpp
             src/host/launchpad/LaunchpadHost.cpp
             src/host/launchpad/LaunchpadScanner.cpp
             test/graph/CountingAllocator.cpp          # the block thread's drain allocates and frees nothing
    LIBS     Threads::Threads                          # LaunchpadScanner's scan thread
             retroplug-node-sdk-headers                # transport/SpscRing.hpp, the rings' type
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test          # graph/CountingAllocator.hpp
             ${CMAKE_CURRENT_SOURCE_DIR}/src)          # host/launchpad/*.hpp

# Guards the Everdrive N8 Pro protocol framing (Edio): that fifoWR emits the exact krikzz CMD_MEM_WR
# byte stream to ADDR_FIFO and the connect handshake accepts / rejects the 0xA5 status word; plus the
# N8Link forward path (the SDL standalone's realtime serial thread) and the cart's capability jobs
# (SRAM dump and restore, the ROM upload and its boot), which pause and resume streaming around the
# transfer. Compiles those against a capturing FakeSerialPort - no `serial` lib, no `rtmidi`, no
# hardware. The CLI's risa-sync translation lives in TS (packages/retroplug src/n8, `pnpm test n8`); the
# C++ Edio/N8Link/N8Menu twin backs the realtime + job paths. Run by `pnpm test:n8`.
retroplug_add_test_binary(
    NAME     retroplug-n8-test
    SOURCES  test/n8/Edio.test.cpp
             test/n8/N8Host.test.cpp
             test/n8/N8CartLink.test.cpp   # the cart device link over N8Link + N8Host
             test/n8/N8CartJobs.test.cpp   # the cart's capability jobs, run with streaming paused
             src/host/n8/Edio.cpp
             src/host/n8/N8Link.cpp
             src/host/n8/N8Host.cpp
             src/host/n8/N8Menu.cpp
    LIBS     reflectcpp::reflectcpp                    # rfl::json - parse the shared Edio golden (twins edio.test.ts)
             Threads::Threads                          # N8Link's serial thread
             retroplug-node-sdk-headers                # transport/SpscRing.hpp, N8Link's rings
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src           # host/n8/Edio.hpp
    # The shared Edio framing golden both this and edio.test.ts assert against (single source, no drift).
    DEFS     EDIO_GOLDEN_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/test/n8/edio-golden.json")

# WjwwoodSerialPort against a pseudo-terminal: that a read's timeout does not leak into the next write (the bug
# that left a real N8 mid-command behind N8Link's 0 ms back-channel polls). The one N8 test that needs the `serial`
# lib, so it is its own binary - retroplug-n8-test stays serial-free. Run by `pnpm test:n8`.
retroplug_add_test_binary(
    NAME     retroplug-serial-test
    SOURCES  test/n8/WjwwoodSerialPort.test.cpp
             src/host/n8/WjwwoodSerialPort.cpp
    LIBS     serial
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)

# A manual hardware harness (NOT CI) that drives the real N8 card's jobs through the same native
# N8Host the cart's node hands them to, over a real /dev/ttyACM0 (adds the real WjwwoodSerialPort +
# `serial`). Brings its own main(), hence NO_CATCH2.
# Build by name: cmake --build build --target retroplug-n8-hwtest.
retroplug_add_test_binary(
    NAME     retroplug-n8-hwtest
    NO_CATCH2
    SOURCES  test/n8/n8-sd-hwtest.cpp
             src/host/n8/Edio.cpp
             src/host/n8/N8Link.cpp
             src/host/n8/N8Host.cpp
             src/host/n8/N8Menu.cpp
             src/host/n8/WjwwoodSerialPort.cpp
    LIBS     serial
             Threads::Threads
             retroplug-node-sdk-headers                # transport/SpscRing.hpp, N8Link's rings
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
# `timing` times against an ALSA capture of the console's audio, so it exists only where ALSA does. Linking
# ALSA::ALSA unconditionally failed the GENERATE step on macOS/Windows - EXCLUDE_FROM_ALL does not spare a target
# from resolving its link libraries.
find_package(ALSA)
if(ALSA_FOUND)
    target_sources(retroplug-n8-hwtest PRIVATE test/n8/n8-timing.cpp)
    target_link_libraries(retroplug-n8-hwtest PRIVATE ALSA::ALSA)
    target_compile_definitions(retroplug-n8-hwtest PRIVATE RP_N8_TIMING=1)
endif()

# Proves the ThorVG-backed <Lottie> capability: that lv_lottie rasterizes animated vector frames on a
# headless software display (LV_USE_LOTTIE → ThorVG). Links bare lvgl (which PRIVATE-brings
# lvgl_thorvg) + Catch2 — no JS engine needed.
retroplug_add_test_binary(
    NAME     retroplug-lottie-test
    SOURCES  test/ui/LottieRender.test.cpp
    LIBS     lvgl                                      # lv_lottie + lvgl_thorvg (LV_USE_LOTTIE in the active lv_conf.h)
    # The Lottie animation the test rasterizes (a real ~5 KB LVGL example asset).
    DEFS     RP_LOTTIE_JSON_PATH="${DPFJS_PATH}/deps/lv_binding_js/deps/lvgl/examples/widgets/lottie/lv_example_lottie_approve.json")

# Guards the node-graph engine (docs/modular-graph-design.md, Phase A): the executor over raw C-ABI
# nodes, the canonical JSON C++ and TS share, descriptors, the C++ authoring layer, the planner's
# validation codes, plan swaps that keep node state, a node reshaped across a layout change, queues sized
# by their ports and the drops they report, the rp.core nodes, a no-heap check of the block path, and the rp::dsp
# kernels. Self-contained: no core, no txiki, no backend.
retroplug_add_test_binary(
    NAME     retroplug-graph-test
    SOURCES  test/graph/Executor.test.cpp
             test/graph/CanonicalJson.test.cpp
             test/graph/Descriptor.test.cpp
             test/graph/NodeLayer.test.cpp
             test/graph/FixedQueue.test.cpp              # the fixed ingress storage: refusal, compaction and wrap
             test/graph/Registry.test.cpp
             test/graph/Planner.test.cpp
             test/graph/Execution.test.cpp
             test/graph/CoreNodes.test.cpp
             test/graph/NoAlloc.test.cpp
             test/graph/CountingAllocator.cpp         # replaces operator new / delete for this binary
             test/graph/SwapStress.test.cpp
             test/graph/HostCommands.test.cpp
             test/graph/Reshape.test.cpp
             test/graph/Resources.test.cpp            # a node that reads a resource its settings name
             test/graph/SettingsTypes.test.cpp         # rp::text, rp::list and the numeric resource_ref
             test/graph/Fields.test.cpp                # the field walk's names held to reflect-cpp's own
             test/graph/QueueCapacity.test.cpp
             test/graph/PlannerBench.test.cpp         # hidden: run with "[bench]"
             test/graph/Dsp.test.cpp                  # the rp::dsp kernels: streaming, no allocation, the kit's math
             test/graph/DspNodes.test.cpp             # the rp.dsp nodes, through real plans, against those kernels
             test/graph/DspBufferNodes.test.cpp       # the rp.dsp buffer-reading nodes: normalize, DPCM, seedFrom
             test/graph/DspResample.test.cpp          # rp.dsp.resample, through real plans, against its kernel
             # The kit path's effect wrappers, so the two buffer-reading nodes are held to what
             # rp::lsdj::processEffect does to a buffer, not only to the kernels underneath it. They need the kernels,
             # named in LIBS below, and nothing of the core.
             ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/Effects.cpp
    LIBS     retroplug-graph
             retroplug-core-nodes                     # the rp.core package under test
             retroplug-dsp-nodes                      # the rp.dsp package under test
             retroplug-dsp-resample-nodes             # and the rp.dsp.resample package
             retroplug-dsp-kernels                    # core-free: the standard library
             retroplug-dsp-resample                   # Dsp.test.cpp's resampler
             Threads::Threads                          # SwapStress runs a real audio thread
    DEFS     RP_GRAPH_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/graph/golden"
    # Dsp.test.cpp includes dsp/R8bAllocator.hpp first, and that header includes r8brain's CDSPResampler.h. The dep's
    # include directory is private to the resampler target, so the one test that reads it asks for it here.
    INCLUDES ${CMAKE_SOURCE_DIR}/deps/r8brain
             # The engine tree's headers it reads beside the packages: lsdj/Effects.hpp, system/InputCapacity.hpp.
             ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Guards the device build of the node SDK (docs/modular-graph-phase-e1-design.md sections 6 and 8): rp.core and rp.dsp
# compiled as a device compiles them (RP_DEVICE, no exceptions, no RTTI) render, through real plans and over ragged
# blocks, what the desktop's builds of the same sources render, bit for bit, created from msgpack where the desktop's
# are created from JSON; their create, activate, process and destroy allocate nothing; and the msgpack settings decode
# keeps JSON's leniency and refusals. The desktop's nodes read msgpack too.
#
# Both builds of one source sit in this one binary. The device-mode objects are an object library of their own, with
# the device flags, and their C++ names are in rp::device (graph/sdk_namespace.hpp), so no name has two definitions as
# long as a source keeps its own names under rp::graph, rp::detail or an anonymous namespace (that header's
# convention); their packages' C entries, which no namespace reaches, are renamed by a compile definition. Catch2 is the shared
# library every test binary links, built with exceptions, so the test's own translation unit keeps them and reaches the
# device-mode code only through the C ABI, as a device's host would. MSVC keeps its /EHsc there: a second /EH option on
# one command line is the D9025 warning AGENTS.md describes, and the GCC and Clang builds are where -fno-exceptions is
# proved.
add_library(retroplug-device-mode-objects OBJECT EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/packages/core/native/src/graph/nodes/CoreNodes.cpp
    ${CMAKE_SOURCE_DIR}/packages/dsp/native/src/graph/nodes/DspNodes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/device-mode/DeviceModeNodes.cpp)
set_target_properties(retroplug-device-mode-objects PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(retroplug-device-mode-objects PRIVATE
    RP_DEVICE
    rp_core_package=rp_core_package_device_mode
    rp_dsp_package=rp_dsp_package_device_mode)
if(NOT MSVC)
    target_compile_options(retroplug-device-mode-objects PRIVATE -fno-exceptions -fno-rtti)
endif()
# The SDK's headers and reflect-cpp's (the field reflection, header-only and SYSTEM: nothing of reflect-cpp is linked),
# and the packages' own source roots for their headers and the kernels'.
target_link_libraries(retroplug-device-mode-objects PRIVATE retroplug-node-sdk-headers retroplug-reflect-headers)
target_include_directories(retroplug-device-mode-objects PRIVATE
    ${CMAKE_SOURCE_DIR}/packages/core/native/src
    ${CMAKE_SOURCE_DIR}/packages/dsp/native/src
    ${CMAKE_CURRENT_SOURCE_DIR}/test/device-mode)

# What a device-mode build refuses, held at every configure (GCC and Clang): each source under
# test/device-mode/refused must fail to compile as a device compiles it, and fail with its own reason on one of the
# compiler's error lines, so a check that stopped firing (a static_assert gone, the heap-header #error gone) fails the
# configure rather than going unnoticed. HeapHeader is checked only where the standard library is libstdc++: the
# heap-header #error in graph/node.hpp reads libstdc++'s include guards (the device toolchain's library), so under
# libc++ (macOS's AppleClang, a Linux Clang with -stdlib=libc++) that source compiles, the other two are still held,
# and the device check there is the link trap's and graph/node.hpp's static_asserts.
if(NOT MSVC)
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("#include <cstddef>
#ifndef __GLIBCXX__
#error not libstdc++
#endif
int main() { return 0; }" RP_STDLIB_IS_LIBSTDCXX)
    set(_rp_refused_sources
        "HeapType|is never built for the device"
        "PointerSettings|a device node's Settings holds only")
    if(RP_STDLIB_IS_LIBSTDCXX)
        list(APPEND _rp_refused_sources "HeapHeader|a heap or exception header")
    endif()
    get_target_property(_rp_reflect_dir reflectcpp SOURCE_DIR)
    foreach(_rp_refused IN LISTS _rp_refused_sources)
        string(REPLACE "|" ";" _rp_refused "${_rp_refused}")
        list(GET _rp_refused 0 _rp_source)
        list(GET _rp_refused 1 _rp_reason)
        unset(RP_DEVICE_REFUSES_${_rp_source} CACHE)
        try_compile(RP_DEVICE_REFUSES_${_rp_source}
            "${CMAKE_BINARY_DIR}/device-mode-refused/${_rp_source}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test/device-mode/refused/${_rp_source}.cpp"
            CMAKE_FLAGS
                "-DINCLUDE_DIRECTORIES=${CMAKE_SOURCE_DIR}/packages/node-sdk/native/src;${_rp_reflect_dir}/include"
            COMPILE_DEFINITIONS -DRP_DEVICE -fno-exceptions -fno-rtti
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
            OUTPUT_VARIABLE _rp_output)
        if(RP_DEVICE_REFUSES_${_rp_source})
            message(FATAL_ERROR "test/device-mode/refused/${_rp_source}.cpp compiled in device mode; it must not")
        endif()
        # The reason on a diagnostic line ("error: static assertion failed: ...", "error: #error ..."), not anywhere in
        # the output, which also echoes the source lines a diagnostic quotes.
        string(REGEX MATCHALL "[^\n]*error:[^\n]*" _rp_errors "${_rp_output}")
        string(FIND "${_rp_errors}" "${_rp_reason}" _rp_at)
        if(_rp_at EQUAL -1)
            message(FATAL_ERROR "test/device-mode/refused/${_rp_source}.cpp failed in device mode, but not with "
                                "\"${_rp_reason}\":\n${_rp_output}")
        endif()
        unset(RP_DEVICE_REFUSES_${_rp_source} CACHE)
    endforeach()
endif()

retroplug_add_test_binary(
    NAME     retroplug-device-mode-test
    SOURCES  test/device-mode/DeviceMode.test.cpp
             test/graph/CountingAllocator.cpp          # replaces operator new / delete for this binary
             $<TARGET_OBJECTS:retroplug-device-mode-objects>
    LIBS     retroplug-graph
             retroplug-core-nodes                      # the desktop's rp.core, the device's twin
             retroplug-dsp-nodes                       # and rp.dsp
             retroplug-dsp-kernels                     # the kernels, which both builds of rp.dsp call
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test          # graph/CountingAllocator.hpp
             ${CMAKE_CURRENT_SOURCE_DIR}/test/device-mode)
