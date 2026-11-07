# Included by packages/native/CMakeLists.txt, which is the only caller. `include()` keeps the
# CURRENT directory scope, so ${CMAKE_CURRENT_SOURCE_DIR} still means packages/native/ here and every
# relative source path below resolves exactly as it did inline. (add_subdirectory would NOT: it opens
# a child scope and re-roots that variable, which is the trap this split is avoiding.)
#
# --- native test binaries ---------------------------------------------------------------------------
# Eleven Catch2 binaries, all EXCLUDE_FROM_ALL and built BY NAME (nothing reads BUILD_TESTING), which
# `pnpm test:plugin` builds and runs. The N8 pack's three (`pnpm test:n8`'s two, and a harness that needs a
# real console on a USB port) are packages/n8/native/CMakeLists.txt's. The shared shape lives in
# retroplug_add_test_binary(): see cmake/test-binary.cmake at the root; what is written out here is only
# what differs per test.

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
# adapters, down to packages/n8/native/test/FakeN8.hpp's serial port), TS nodes run behind the C ABI in the script
# host, a script node pack loads beside the built-in one into runtimes of its own and reloads, the generation it
# replaced freed off the audio thread, the protocol script nodes send what the DSP kernel they replaced was recorded
# sending, every document in buildGraph's goldens, its role goldens among them, plans on it, and an
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
             test/engine-graph/Devices.test.cpp        # rp.host.devices/2: the links a hardware node binds by name
             test/engine-graph/DeviceAbi.test.cpp      # graph/rp_device.h, rp_jobs.h and rp_stage.h, as C++ and C:
             test/engine-graph/DeviceAbi.c             # the same three headers compiled as C, a C node's use of them
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
             test/engine-graph/Providers.test.cpp      # the providers a product starts, and its generated ones
             test/graph/CountingAllocator.cpp          # replaces operator new / delete for this binary
             ${SCRIPT_TEST_BUNDLE_C}                    # rp_script_test_bundle
    LIBS     retroplug-backend                         # Engine + retroplug-core (SystemBase + the cores)
             retroplug-dsp-kernels                     # OfflineJobs.test.cpp holds a job's buffers to the kernels
             retroplug-dsp-resample                    # and to resampledLength, which the resample's node wraps
             rpcpp                                     # the server Providers.test.cpp mounts the facets on (QjsRpc.hpp)
    PACKAGES ${RP_DESKTOP_PACKAGES}
    INCLUDES ${RP_QUICKJS_INCLUDE}
             ${CMAKE_SOURCE_DIR}/packages/n8/native/test   # FakeN8.hpp (N8Adapters.test.cpp), QjsRpc.hpp
             ${CMAKE_BINARY_DIR}/packages/test             # without/rp_product_providers.hpp (below)
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
# The providers header retroplug_product() would generate for a product listing no package with a provider, which
# Providers.test.cpp holds to starting and mounting nothing beside this binary's own (it lists the N8 pack).
retroplug_providers_header("${CMAKE_BINARY_DIR}/packages/test/without/rp_product_providers.hpp"
                           "packages/native/cmake/tests.cmake (a product listing no provider)"
                           core dsp graph-runtime native)

# What a manifest may not say, held at every configure: each scratch manifest under test/packages/refused must be
# refused by the reader retroplug_package() runs (cmake/packages.cmake's _retroplug_read_manifest, run here in script
# mode by cmake/check-manifest.cmake) with its own reason, so a rule that stopped firing fails the configure rather than
# going unnoticed; and the manifests that must read do: the N8 pack's, whose desktop entry names a provider and its
# facet, devtest's, with a native entry and a script pack, a scratch package that is a script pack alone, and one
# whose contributions are its app and protocol members, the TS half with no native part.
set(_rp_refused_manifests
    "ProviderOnDevice|entry rp_scratch_package is `device` and names a provider or an rpc"
    "RpcOnDevice|entry scratch-desktop is `device` and names a provider or an rpc"
    "RpcWithoutProvider|entry scratch-desktop names an rpc and no provider"
    "ProviderWithoutDevice|entry scratch-desktop needs \"device\": true or false"
    "NoContributionMember|has none of a retroplug.native, retroplug.script, retroplug.app or retroplug.protocol field"
    "ScriptNotObject|retroplug.script is a STRING, not an object"
    "ScriptWithoutEntry|retroplug.script needs an \"entry\""
    "ScriptEntryMissing|retroplug.script's entry nope.ts does not exist"
    "ScriptWithoutDevice|retroplug.script needs \"device\": true or false"
    "ScriptOnDesktop|retroplug.script is \"device\": false, a desktop script pack, which is reserved"
    "ScriptDeviceWithoutHeap|retroplug.script is a device pack, and needs \"heapBytes\""
    "ScriptHeapTooSmall|\"heapBytes\": a whole number of bytes from 4096 to 524288"
    "ScriptHeapTooBig|\"heapBytes\": a whole number of bytes from 4096 to 524288"
    "ScriptHeapFraction|\"heapBytes\": a whole number of bytes from 4096 to 524288"
    "ScriptHeapString|\"heapBytes\": a whole number of bytes from 4096 to 524288")
foreach(_rp_refused IN LISTS _rp_refused_manifests)
    string(REPLACE "|" ";" _rp_refused "${_rp_refused}")
    list(GET _rp_refused 0 _rp_manifest)
    list(GET _rp_refused 1 _rp_reason)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -DRP_MANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/test/packages/refused/${_rp_manifest}.json
                -P ${CMAKE_SOURCE_DIR}/cmake/check-manifest.cmake
        RESULT_VARIABLE _rp_result OUTPUT_VARIABLE _rp_output ERROR_VARIABLE _rp_output)
    if(_rp_result EQUAL 0)
        message(FATAL_ERROR "test/packages/refused/${_rp_manifest}.json was read; the configure must refuse it")
    endif()
    # CMake wraps a message's lines, so the reason is looked for with the output's whitespace folded.
    string(REGEX REPLACE "[ \t\r\n]+" " " _rp_folded "${_rp_output}")
    string(FIND "${_rp_folded}" "${_rp_reason}" _rp_at)
    if(_rp_at EQUAL -1)
        message(FATAL_ERROR "test/packages/refused/${_rp_manifest}.json was refused, but not with \"${_rp_reason}\":\n"
                            "${_rp_output}")
    endif()
endforeach()
foreach(_rp_read IN ITEMS "${CMAKE_SOURCE_DIR}/packages/n8/package.json"
                          "${CMAKE_SOURCE_DIR}/packages/devtest/package.json"
                          "${CMAKE_CURRENT_SOURCE_DIR}/test/packages/read/ScriptOnly.json"
                          "${CMAKE_CURRENT_SOURCE_DIR}/test/packages/read/AppAndProtocolOnly.json")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -DRP_MANIFEST=${_rp_read} -P ${CMAKE_SOURCE_DIR}/cmake/check-manifest.cmake
        RESULT_VARIABLE _rp_result OUTPUT_VARIABLE _rp_output ERROR_VARIABLE _rp_output)
    if(NOT _rp_result EQUAL 0)
        message(FATAL_ERROR "${_rp_read} did not read in script mode:\n${_rp_output}")
    endif()
endforeach()

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
             test/graph/MidiFilter.test.cpp           # rp.core.midi-filter through real plans, and its golden
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
# keeps JSON's leniency and refusals. The desktop's nodes read msgpack too. The device check runs its types and its
# plan as the image does (DeviceCheck.test.cpp). And a plan exported for the RP2350
# (docs/modular-graph-phase-e3-design.md section 4), loaded by the loader over the device-mode builds in an arena,
# renders what GraphHost renders from the same document, allocating nothing, and each of the loader's and the
# exporter's refusals says why (DevicePlan.test.cpp, which runs retroplug-export-plan too); rp.device.midi-in takes
# what a device host stages, each build alike (DeviceIo.test.cpp), and the box's bridge plan feeds a fake cart link what
# its filter keeps. The Pico box's portable pieces run here too, over its own plan.json (BoxHost.test.cpp: the block
# driver, the staging, the cart link and a whole pass, and plan.json's blob golden), and its Edio link over a fake
# TinyUSB host (UsbCdcTransport.test.cpp). Device script packs run on the MicroQuickJS host, both its builds
# (ScriptPack.test.cpp, docs/modular-graph-phase-e4-design.md section 7): the block API, settings and hooks, each fault
# and a heap that runs out silencing its own pack alone, a warmed block allocating nothing (native or JS), and a plan
# with a script node exported and loaded over the device-mode builds rendering what GraphHost renders, its pack's
# memory sized, placed and refused as the loader does.
#
# Both builds of one source sit in this one binary: the device-mode objects (retroplug_device_mode_objects,
# cmake/packages.cmake, which says how the rename lets them) are the device's build of every package with a `device`
# entry, beside the echo node's own (test/device-mode/DeviceModeNodes.cpp), and the binary links the desktop's build of
# the same entries. Which sources and entries are the device's is the manifests' word (cmake/packages.cmake): the
# objects compile the sources of every `device` entry's target (each one's <target>-device check holds the same list),
# and each device entry is renamed <entry>_device_mode. device-entries.cpp, generated below, lists every device entry
# with both its builds, which is what the test holds a device registration to.
get_property(_rp_packages GLOBAL PROPERTY RP_PACKAGES)
set(_rp_device_packages "")
foreach(_p IN LISTS _rp_packages)
    get_property(_entries GLOBAL PROPERTY RP_PACKAGE_${_p}_DEVICE_ENTRIES)
    get_property(_scriptFn GLOBAL PROPERTY RP_PACKAGE_${_p}_SCRIPT_FN)
    if(_entries OR _scriptFn)  # a device entry, or a script pack (every one a device's): bliptoaster's
        list(APPEND _rp_device_packages ${_p})
    endif()
endforeach()
# The Pico box's portable pieces (docs/modular-graph-phase-e3-design.md sections 5 and 8), built for the host: the
# generic box host's sources (pico/host/host.cmake) and the N8 box's cart link, desktop-built here, and the link again
# in the device-mode objects, where it derives the device-mode build's CartLink as the box's derives the device's; the
# box's plan.json and what it is exported for (pico/n8-host/plan.cmake), which BoxHost.test.cpp's golden pins.
include("${CMAKE_SOURCE_DIR}/pico/host/host.cmake")
include("${CMAKE_SOURCE_DIR}/pico/n8-host/plan.cmake")
set(_rp_box_dir "${CMAKE_SOURCE_DIR}/pico/n8-host")

# The test's own device script packs (test/device-mode/script-packs, docs/modular-graph-phase-e4-design.md section 7),
# compiled by the device-pack toolchain as a package's pack is: scripttest holds the MicroQuickJS host to its contract
# (the block API, settings, the hooks, a fault of each kind), hog runs its pack's heap out and slowload's load runs
# past a short deadline. Both builds of each registration are in the binary, as devtest's script pack's are
# (ScriptPack.test.cpp).
set(_rp_script_packs ${CMAKE_CURRENT_SOURCE_DIR}/test/device-mode/script-packs)
retroplug_device_script_pack(scripttest ENTRY ${_rp_script_packs}/scripttest/index.ts HEAP_BYTES 262144
                             PACK_DIR ${_rp_script_packs}/scripttest
                             OUT_DIR ${CMAKE_BINARY_DIR}/device-mode/script-packs/scripttest)
retroplug_device_script_pack(hog ENTRY ${_rp_script_packs}/hog/index.ts HEAP_BYTES 40960
                             PACK_DIR ${_rp_script_packs}/hog OUT_DIR ${CMAKE_BINARY_DIR}/device-mode/script-packs/hog)
retroplug_device_script_pack(slowload ENTRY ${_rp_script_packs}/slowload/index.ts HEAP_BYTES 65536
                             PACK_DIR ${_rp_script_packs}/slowload
                             OUT_DIR ${CMAKE_BINARY_DIR}/device-mode/script-packs/slowload)

retroplug_device_mode_objects(retroplug-device-mode-objects
    PACKAGES ${_rp_device_packages}
    SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/test/device-mode/DeviceModeNodes.cpp
             ${_rp_box_dir}/BoxCartLink.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test/device-mode
             ${_rp_box_dir}
    SCRIPTS  retroplug-scripttest-script retroplug-hog-script retroplug-slowload-script
    OWNERS   _rp_device_owners)
# The device-mode host's fixed tables (script/MqjsHost.cpp) hold a box's few packs; this binary registers every
# package's and the tests' own (devtest, bliptoaster, scripttest, hog, slowload), and the device check's cases register
# the devtest pack again at each heap they run it at (DeviceCheck.test.cpp).
target_compile_definitions(retroplug-device-mode-objects PRIVATE RP_MQJS_MAX_PACKS=16)

# The box's Edio link (pico/n8-host/UsbCdcTransport.cpp) over a fake TinyUSB host: its own objects, so the fake
# tusb.h and pico/time.h (test/device-mode/fake-tinyusb) are on no other source's include path.
add_library(retroplug-box-usb-cdc OBJECT EXCLUDE_FROM_ALL ${_rp_box_dir}/UsbCdcTransport.cpp)
set_target_properties(retroplug-box-usb-cdc PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(retroplug-box-usb-cdc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test/device-mode/fake-tinyusb
                                                         ${_rp_box_dir})
target_link_libraries(retroplug-box-usb-cdc PRIVATE retroplug-n8)

# The test's own scripttest pack defines more node types than a box's few (script/MqjsHost.cpp's RP_MQJS_MAX_TYPES, 16
# on a device), which the device-mode host's fixed tables are sized for.
target_compile_definitions(retroplug-device-mode-objects PRIVATE RP_MQJS_MAX_TYPES=48)

set(_rp_device_rows "")
set(_rp_device_decls "")
foreach(_p IN LISTS _rp_packages)
    get_property(_entries GLOBAL PROPERTY RP_PACKAGE_${_p}_DEVICE_ENTRIES)
    foreach(_e IN LISTS _entries)
        string(APPEND _rp_device_decls
               "extern \"C\" const rp_package* ${_e}(void);\nextern \"C\" const rp_package* ${_e}_device_mode(void);\n")
        string(APPEND _rp_device_rows "        {\"${_p}\", \"${_e}\", &${_e}, &${_e}_device_mode},\n")
    endforeach()
endforeach()
# And every package's script pack: its registration with both builds, and its 64-bit source, which a test registers
# again with another heap (the device check's image figure).
set(_rp_script_rows "")
foreach(_p IN LISTS _rp_packages)
    get_property(_scriptFn GLOBAL PROPERTY RP_PACKAGE_${_p}_SCRIPT_FN)
    if(_scriptFn)
        string(MAKE_C_IDENTIFIER "${_p}" _id)
        string(APPEND _rp_device_decls
               "extern \"C\" const rp_package* ${_scriptFn}(void);\n"
               "extern \"C\" const rp_package* ${_scriptFn}_device_mode(void);\n"
               "extern \"C\" const rp_mqjs_pack_source rp_script_${_id};\n")
        string(APPEND _rp_script_rows
               "        {\"${_p}\", \"${_scriptFn}\", &${_scriptFn}, &${_scriptFn}_device_mode, &rp_script_${_id}},\n")
    endif()
endforeach()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/device-mode/device-entries.cpp" CONTENT
"// Generated by packages/native/cmake/tests.cmake from the packages' manifests: every `device` entry, with the
// desktop's build of it and the device-mode build retroplug-device-mode-objects renames, and every script pack's
// registration likewise, with its 64-bit source. Do not edit.

#include <vector>

#include \"DeviceModeEntries.hpp\"

${_rp_device_decls}
std::vector<DeviceEntry> deviceEntries() {
    return {
${_rp_device_rows}    };
}

std::vector<ScriptEntry> scriptEntries() {
    return {
${_rp_script_rows}    };
}
")

# The device check (packages/graph-runtime/device-check) run on the desktop over the device-mode objects, against the
# header the case generator writes for the RP2350 image and the plans exported for it as the image's build exports them
# (its plan.cmake: plan.json and script-plan.json): the same cases, the same plans, the same code, the same arena size,
# and the image's script pack heap (its packages.cmake's RP_DEVICE_CHECK_SCRIPT_HEAP_BYTES).
set(_rp_device_check_dir ${CMAKE_SOURCE_DIR}/packages/graph-runtime/device-check)
include("${_rp_device_check_dir}/plan.cmake")
include("${_rp_device_check_dir}/packages.cmake")  # RP_DEVICE_CHECK_PACKAGES, which the export is held to
add_custom_command(
    OUTPUT  ${CMAKE_BINARY_DIR}/device-check/DeviceCheckCases.hpp
    COMMAND rp-device-check-cases ${CMAKE_BINARY_DIR}/device-check/DeviceCheckCases.hpp
    DEPENDS rp-device-check-cases
    COMMENT "rp-device-check-cases: the device check's cases"
    VERBATIM)
string(REPLACE ";" "," _rp_device_check_list "${RP_DEVICE_CHECK_PACKAGES}")
string(REPLACE ";" "," _rp_box_list "${N8_HOST_PACKAGES}")
add_custom_command(
    OUTPUT  ${CMAKE_BINARY_DIR}/device-check/DeviceCheckPlan.hpp
    COMMAND retroplug-export-plan "${RP_DEVICE_CHECK_PLAN}" ${CMAKE_BINARY_DIR}/device-check/DeviceCheckPlan.hpp
            --arena ${RP_DEVICE_CHECK_ARENA_BYTES} --sample-rate ${RP_DEVICE_CHECK_PLAN_SAMPLE_RATE}
            --max-frames ${RP_DEVICE_CHECK_PLAN_MAX_FRAMES} --packages ${_rp_device_check_list}
            --header kDeviceCheckPlan
    DEPENDS retroplug-export-plan "${RP_DEVICE_CHECK_PLAN}" "${_rp_device_check_dir}/plan.cmake"
            "${_rp_device_check_dir}/packages.cmake"
    COMMENT "retroplug-export-plan: the device check's plan (plan.json)"
    VERBATIM)
add_custom_command(
    OUTPUT  ${CMAKE_BINARY_DIR}/device-check/DeviceCheckScriptPlan.hpp
    COMMAND retroplug-export-plan "${RP_DEVICE_CHECK_SCRIPT_PLAN}"
            ${CMAKE_BINARY_DIR}/device-check/DeviceCheckScriptPlan.hpp
            --arena ${RP_DEVICE_CHECK_ARENA_BYTES} --sample-rate ${RP_DEVICE_CHECK_PLAN_SAMPLE_RATE}
            --max-frames ${RP_DEVICE_CHECK_PLAN_MAX_FRAMES} --packages ${_rp_device_check_list}
            --header kDeviceCheckScriptPlan
    DEPENDS retroplug-export-plan "${RP_DEVICE_CHECK_SCRIPT_PLAN}" "${_rp_device_check_dir}/plan.cmake"
            "${_rp_device_check_dir}/packages.cmake"
    COMMENT "retroplug-export-plan: the device check's script plan (script-plan.json)"
    VERBATIM)

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
        "PointerSettings|a device node's Settings holds only"
        "UnguardedValidate|validate and adjust_layout are the desktop's"
        "UnguardedLayout|incomplete type")
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
             test/device-mode/DeviceCheck.test.cpp
             test/device-mode/DevicePlan.test.cpp
             test/device-mode/DeviceIo.test.cpp
             test/device-mode/BoxHost.test.cpp
             test/device-mode/UsbCdcTransport.test.cpp
             test/device-mode/ScriptPack.test.cpp
             test/device-mode/BlipToaster.test.cpp
             test/device-mode/FakeTinyUsb.cpp
             ${RP_BOX_HOST_PORTABLE_SOURCES}          # the box host, desktop-built
             ${_rp_box_dir}/BoxCartLink.cpp           # the N8 box's cart link, desktop-built
             ${_rp_box_dir}/CartVolume.cpp            # and its expansion-volume write
             $<TARGET_OBJECTS:retroplug-box-usb-cdc>
             test/graph/CountingAllocator.cpp          # replaces operator new / delete for this binary
             ${_rp_device_check_dir}/DeviceCheck.cpp   # the device check, as a host: desktop-built, C ABI only
             ${_rp_device_check_dir}/N8Protocol.cpp    # its N8 protocol run, over the desktop's build of the pack
             ${CMAKE_BINARY_DIR}/device-check/DeviceCheckCases.hpp
             ${CMAKE_BINARY_DIR}/device-check/DeviceCheckPlan.hpp
             ${CMAKE_BINARY_DIR}/device-check/DeviceCheckScriptPlan.hpp
             ${CMAKE_BINARY_DIR}/device-mode/device-entries.cpp
             $<TARGET_OBJECTS:retroplug-device-mode-objects>
    LIBS     retroplug-graph
             ${_rp_device_owners}                      # the desktop's build of every device entry, the device's twin
             retroplug-dsp-kernels                     # the desktop rp.dsp's kernels (the objects build theirs)
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test          # graph/CountingAllocator.hpp
             ${CMAKE_CURRENT_SOURCE_DIR}/test/device-mode
             ${_rp_device_check_dir}                   # DeviceCheck.hpp
             ${CMAKE_BINARY_DIR}/device-check          # DeviceCheckCases.hpp, DeviceCheckPlan.hpp
             ${RP_BOX_HOST_INCLUDES}                   # the box host and midi.h
             ${_rp_box_dir}                            # BoxCartLink.hpp, UsbCdcTransport.hpp
             ${CMAKE_CURRENT_SOURCE_DIR}/src/util      # BuildTarget.hpp: the reference host, for float figures
    DEFS     RP_DEVICE_MODE_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/device-mode/golden"
             RP_EXPORT_PLAN_TOOL="$<TARGET_FILE:retroplug-export-plan>"
             RP_BOX_PLAN="${N8_HOST_PLAN}"
             RP_BOX_SAMPLE_RATE=${N8_HOST_PLAN_SAMPLE_RATE}
             RP_BOX_MAX_FRAMES=${N8_HOST_PLAN_MAX_FRAMES}
             RP_BOX_ARENA_BYTES=${N8_HOST_ARENA_BYTES}
             RP_BOX_PACKAGES="${_rp_box_list}"
             RP_DEVICE_CHECK_PLAN="${RP_DEVICE_CHECK_PLAN}"
             RP_DEVICE_CHECK_SCRIPT_PLAN="${RP_DEVICE_CHECK_SCRIPT_PLAN}"
             RP_DEVICE_CHECK_ARENA_BYTES=${RP_DEVICE_CHECK_ARENA_BYTES}
             RP_DEVICE_CHECK_SCRIPT_HEAP_BYTES=${RP_DEVICE_CHECK_SCRIPT_HEAP_BYTES}
             RP_BUILD_DIR="${CMAKE_BINARY_DIR}"
             RP_MQJS_RUN="$<TARGET_FILE:retroplug-mqjs-run>"
             RP_MQJS_RUN_GC="$<TARGET_FILE:retroplug-mqjs-run-gc>")
# The plan cases run the exporter's tool in a process of its own (DevicePlan.test.cpp), and the [debug-gc] case the
# toolchain's runner and its DEBUG_GC build (ScriptPack.test.cpp).
add_dependencies(retroplug-device-mode-test retroplug-export-plan retroplug-mqjs-run retroplug-mqjs-run-gc)
