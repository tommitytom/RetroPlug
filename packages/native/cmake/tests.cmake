# Included by packages/native/CMakeLists.txt, which is the only caller. `include()` keeps the
# CURRENT directory scope, so ${CMAKE_CURRENT_SOURCE_DIR} still means packages/native/ here and every
# relative source path below resolves exactly as it did inline. (add_subdirectory would NOT: it opens
# a child scope and re-roots that variable, which is the trap this split is avoiding.)
#
# --- native test binaries ---------------------------------------------------------------------------
# Ten Catch2 binaries, all EXCLUDE_FROM_ALL and built BY NAME (nothing reads BUILD_TESTING):
# `pnpm test:plugin` builds and runs eight of them, `pnpm test:n8` the ninth, and the tenth needs a
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

# Guards the per-channel audio path: the host seam (runUnit stream loop + AudioRouter::streamCount +
# lane-counted finishBlock, over a fake SystemBase) AND the real SameBoy per-channel tap
# (ChannelStreams + SameBoyStems), plus the ChannelSplit plugin router + its Engine gating
# (spec/10 step 4). Links retroplug-backend, which PUBLIC-brings retroplug-core → BlockRunner +
# routers + the sameboy core, so the Engine gating test can build an Engine.
#
# The ROM defines: mGB's boot chime supplies real signal for the SameBoyStems + ChannelSplit fidelity
# tests, and a ch1 note in the BlipToaster ROM does the same for the NES pin test. smsggdj ships in
# both cartridge shapes because the SMS and GG builds differ in visible geometry and video config, so
# both are needed to guard the overscan setup. resources/ never ships (release.yml packages build/bin
# plus the license bundle), so these are test-only.
retroplug_add_test_binary(
    NAME     retroplug-audio-test
    SOURCES  test/audio/ChannelStreams.test.cpp
             test/audio/SameBoyStems.test.cpp
             test/audio/ChannelSplit.test.cpp
             test/audio/EngineChannelSplit.test.cpp
             test/audio/EngineSampleRate.test.cpp      # host sample-rate change re-rates live cores
             test/audio/SameBoySerialTiming.test.cpp   # host MIDI-in keeps its intra-block sample offset over serial
             test/audio/NesApuLatency.test.cpp         # runtime NES APU flush window (live apuLatencyMs knob)
             test/audio/NesN8FifoTiming.test.cpp       # host MIDI-in keeps its intra-block offset into the N8 FIFO
             test/audio/NesEverdriveFifo.test.cpp      # Edio status protocol leaves no stale byte to desync MIDI parsing
             test/audio/NesStems.test.cpp              # NES stereo-mod pins re-sum to the mix (spec/10 step 5)
             test/audio/NesSplitRouting.test.cpp       # NES-in-plugin: PinSplit/ChannelSplit arm the tap live + mono-pack
             test/audio/SmsAudio.test.cpp              # SMS/GG boot, visible geometry, non-silent PSG, teardown survival
             test/audio/GbaFirmware.test.cpp           # each GBA system loads ITS bios, not whichever landed last
             test/audio/GbaAudio.test.cpp              # the GBA block triad terminates and fills a block
             test/audio/MesenBlockOps.test.cpp         # the shared drain tail blanks a short ring
             test/audio/NesTeardown.test.cpp           # the NES twin of SMS's construct/destruct guard
             test/audio/SameBoyModels.test.cpp         # every SameBoyModel (incl. SGB variants) boots + renders
    LIBS     retroplug-backend                         # Engine + retroplug-core (SystemBase + BlockRunner + routers + sameboy)
    INCLUDES ${RP_QUICKJS_INCLUDE}                     # quickjs.h (via Engine.hpp → DspRuntime.hpp)
    DEFS     RP_MGB_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/mGB.gb"
             RP_BLIPTOASTER_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/bliptoaster.nes"
             RP_SMS_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.sms"
             RP_GG_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.gg")

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
             src/host/launchpad/LaunchpadLink.cpp
             src/host/launchpad/LaunchpadHost.cpp
             src/host/launchpad/LaunchpadScanner.cpp
    LIBS     Threads::Threads                          # LaunchpadScanner's scan thread
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)          # host/launchpad/*.hpp + transport/SpscRing.hpp

# Guards the Everdrive N8 Pro protocol framing (Edio): that fifoWR emits the exact krikzz CMD_MEM_WR
# byte stream to ADDR_FIFO and the connect handshake accepts / rejects the 0xA5 status word; plus the
# N8Link forward path (the SDL standalone's realtime serial thread) and the N8SdWorker SD / menu
# control ops (ROM upload / SRAM dump+restore + the connection manager that pauses streaming).
# Compiles those against a capturing FakeSerialPort — no `serial` lib, no `rtmidi`, no hardware. The
# CLI's risa-sync translation lives in TS (packages/retroplug src/n8, `pnpm test n8`); the C++
# Edio/N8Link/N8Menu twin backs the realtime + UI paths (the native worker can't run the
# single-threaded TS stack off the UI thread). Run by `pnpm test:n8`.
retroplug_add_test_binary(
    NAME     retroplug-n8-test
    SOURCES  test/n8/Edio.test.cpp
             test/n8/N8Host.test.cpp
             test/n8/N8SdWorker.test.cpp
             src/host/n8/Edio.cpp
             src/host/n8/N8Link.cpp
             src/host/n8/N8Host.cpp
             src/host/n8/N8Menu.cpp
             src/host/n8/N8SdWorker.cpp
    LIBS     reflectcpp::reflectcpp                    # rfl::json - parse the shared Edio golden (twins edio.test.ts)
             Threads::Threads                          # N8Link's serial thread
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src           # host/n8/Edio.hpp
    # The shared Edio framing golden both this and edio.test.ts assert against (single source, no drift).
    DEFS     EDIO_GOLDEN_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/test/n8/edio-golden.json")

# A manual hardware harness (NOT CI) that drives the real N8 SD/menu ops through the same native
# N8Host + N8SdWorker the Settings > N8 Pro menu uses, over a real /dev/ttyACM0 (adds the real
# WjwwoodSerialPort + `serial`). Brings its own main(), hence NO_CATCH2.
# Build by name: cmake --build build --target retroplug-n8-hwtest.
retroplug_add_test_binary(
    NAME     retroplug-n8-hwtest
    NO_CATCH2
    SOURCES  test/n8/n8-sd-hwtest.cpp
             src/host/n8/Edio.cpp
             src/host/n8/N8Link.cpp
             src/host/n8/N8Host.cpp
             src/host/n8/N8Menu.cpp
             src/host/n8/N8SdWorker.cpp
             src/host/n8/WjwwoodSerialPort.cpp
    LIBS     serial
             Threads::Threads
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Proves the ThorVG-backed <Lottie> capability: that lv_lottie rasterizes animated vector frames on a
# headless software display (LV_USE_LOTTIE → ThorVG). Links bare lvgl (which PRIVATE-brings
# lvgl_thorvg) + Catch2 — no JS engine needed.
retroplug_add_test_binary(
    NAME     retroplug-lottie-test
    SOURCES  test/ui/LottieRender.test.cpp
    LIBS     lvgl                                      # lv_lottie + lvgl_thorvg (LV_USE_LOTTIE in the active lv_conf.h)
    # The Lottie animation the test rasterizes (a real ~5 KB LVGL example asset).
    DEFS     RP_LOTTIE_JSON_PATH="${DPFJS_PATH}/deps/lv_binding_js/deps/lvgl/examples/widgets/lottie/lv_example_lottie_approve.json")
