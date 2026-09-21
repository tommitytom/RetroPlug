# packages/native — the C++ side of RetroPlug: two static libs, the hosts built on them, the DPF
# plugin, and the test binaries.
#
# This file holds the libraries and the straightforward executables. The parts that are large or
# self-contained are include()d from cmake/ so this stays readable top to bottom:
#
#   cmake/test-binary.cmake   retroplug_add_test_binary() — the shape every test binary shares
#   cmake/node-addon.cmake    retroplug-node, the N-API host (opt-in)
#   cmake/bundles.cmake       the embedded control-plane + React-UI bytecode
#   cmake/deps.cmake          rtmidi / serial / portaudio, and the retroplug-cli N8 facets
#   cmake/plugin.cmake        dpf_add_plugin + per-platform packaging
#   cmake/tests.cmake         the ten native test binaries
#   cmake/warnings.cmake      /W4 /WX on our own targets (MSVC)
#
# They are include()d, not add_subdirectory()'d, which matters: include() keeps the CURRENT
# directory scope, so ${CMAKE_CURRENT_SOURCE_DIR} still resolves to packages/native/ inside them and
# every relative source path reads as it did when it was inline. add_subdirectory would open a child
# scope, re-root that variable, and scope target properties differently.
#
# ORDER IS SEMANTIC. CMake runs top to bottom and a target must exist before anything modifies it —
# cmake/deps.cmake calls target_sources() on retroplug-cli, so it has to come after retroplug-cli is
# defined. Move an include() and the failure is a confusing "unknown target", not a clear one.

# quickjs.h's directory. Spelled out at six call sites before; it is deep enough that a typo in one
# of them would fail confusingly, and short enough here to read.
set(RP_QUICKJS_INCLUDE "${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/deps/quickjs")

# The shape shared by every native test binary (see the file for why).
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/test-binary.cmake)

# --- retroplug-core: the shared emulator/Project core, as a neutrally-named static lib. Extracted out of
# packages/native/cli (which also compiled the CLI-only HarnessRpcService) so the host links ONLY the
# shared core, never a CLI RPC surface it doesn't use. The legacy `retroplug-cli-core` is now a thin
# wrapper = HarnessRpcService.cpp + this lib. Deliberately txiki/QuickJS-free (harness-schema-dump links it
# without the JS runtime); PIC so it links into the plugin .so.
if(WIN32)
    # SameBoySystem.cpp pokes SameBoy internals cl.exe can't parse; on Windows it's compiled by clang-cl
    # into retroplug-samebsys (see the root CMakeLists) and linked below. Elsewhere it builds in place.
    set(_rpcore_sbs "")
else()
    set(_rpcore_sbs ${CMAKE_CURRENT_SOURCE_DIR}/src/system/sameboy/SameBoySystem.cpp)
endif()

add_library(retroplug-core STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/project/Project.cpp
    # Console-neutral kit pipeline: the async compiler + decoded-audio cache are
    # codec-agnostic (KitCodec.hpp). Per-console packing lives in a codec (LSDj
    # below; risa NES-DPCM planned).
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kit/SampleCache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kit/KitCompiler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/Effects.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/KitUtil.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/LsdjKitCodec.cpp
    # risa NES-DPCM (DMC) codec — reuses the generic kit pipeline (r8brain resample + enkiTS), swaps the
    # pack stage for 1-bit delta DPCM + an 8 KB kit bank.
    ${CMAKE_CURRENT_SOURCE_DIR}/src/risa/RisaDmcCodec.cpp
    # NOTE: the LSDj sav codec + model were removed — they are now pure TS
    # (packages/retroplug/src/lsdj), validated by frozen golden vectors. Only the
    # kit/sample pipeline (kit/* generic + lsdj/{Effects,KitUtil,LsdjKitCodec}) remains in C++.
    # processAlive only, so <windows.h> stays in one TU rather than in every includer of the header
    # (NesEverdriveFifo.hpp pulls it in, which is the whole NES core).
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util/ProcessId.cpp
    # Same rule: fsyncPath needs <windows.h> (FlushFileBuffers), so it stays out of line too.
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util/FileSync.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/SystemBase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/BlockRunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/OfflineRender.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/sameboy/LinkGroup.cpp
    ${_rpcore_sbs}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/MesenNesSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/MesenGlobalInit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/MesenNesDebugSession.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/MesenVideoDevice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/MesenGbaSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/MesenSmsSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/roles/NesN8FifoRole.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/roles/SmsSyncRole.cpp
    # lodepng (vendored under lv_binding_js): one PNG encoder, compiled directly so consumers don't pull
    # the full lvgl-js-native static lib for it.
    ${DPFJS_PATH}/deps/lv_binding_js/src/render/native/core/img/png/lodepng.cpp
)

target_include_directories(retroplug-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # `native/core/img/png/lodepng.h` lives under this root (in dpf.js).
    ${DPFJS_PATH}/deps/lv_binding_js/src/render
    # MinizZip.hpp -> miniz.h (txiki's v3.1.0; matches the linked `miniz` lib)
    ${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/deps/miniz
)

target_link_libraries(retroplug-core PUBLIC
    sameboy
    mesen
    rpcpp     # transitively pulls reflectcpp
    r8brain   # used by KitUtil for resampling
    enkiTS    # used by KitCompiler for per-sample parallel compile
    miniaudio # header-only audio decoder used by SampleCache
    miniz     # used by projectConfigToZip for --save-rplg
    mgb-rom   # embedded mGB ROM (Project.cpp re-supplies it on load)
    # Windows: SameBoySystem.cpp lives in this clang-cl-compiled lib (see root CMakeLists).
    $<$<BOOL:${WIN32}>:retroplug-samebsys>
)

set_target_properties(retroplug-core PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON   # linked into the plugin .so
)

# The render-worker bundle — src/render/worker.ts esbuilt to a global-code IIFE (NOT an ES module: the
# RenderHost loads it on a BARE QuickJS runtime with no module loader) + tjsc'd to bytecode C array,
# COMPILED INTO retroplug-backend so every consumer (plugin, test host, the parity test) can render on a
# worker thread. tjsc WITHOUT -m yields global-code bytecode (JS_ReadObject + JS_EvalFunction); the symbol
# name derives from the output stem → rp_render_worker_bundle[] + _size. Derived; never committed. Defined
# before retroplug-backend so its sources can embed the array.
set(RENDER_WORKER_BUNDLE_JS "${CMAKE_BINARY_DIR}/native/render-worker-bundle.js")
set(RENDER_WORKER_BUNDLE_C  "${CMAKE_BINARY_DIR}/native/render-worker-bundle_data.c")
set_source_files_properties(${RENDER_WORKER_BUNDLE_C} PROPERTIES GENERATED TRUE)
add_custom_target(retroplug-render-worker-bundle
    BYPRODUCTS ${RENDER_WORKER_BUNDLE_JS} ${RENDER_WORKER_BUNDLE_C} ${RENDER_WORKER_BUNDLE_C}.new
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${CMAKE_SOURCE_DIR}/packages/retroplug/src/render/worker.ts ${RENDER_WORKER_BUNDLE_JS} iife
    COMMAND ${_TJSC_COMMAND} -s -p rp_ -o ${RENDER_WORKER_BUNDLE_C}.new ${RENDER_WORKER_BUNDLE_JS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RENDER_WORKER_BUNDLE_C}.new ${RENDER_WORKER_BUNDLE_C}
    COMMENT "Bundling render worker (esbuild iife + global-code bytecode)"
    VERBATIM)
# Order against the in-tree tjsc target only; a cross-build's -DTJSC_EXECUTABLE is a host file
# path (not a target) — the custom command's DEPENDS already tracks it.
if(TARGET ${_TJSC_DEP})
  add_dependencies(retroplug-render-worker-bundle ${_TJSC_DEP})
endif()

# retroplug-backend: the Backend as a static library — everything except a host
# entry point, so both the test host (retroplug-host) and the DPF plugin compose the SAME
# Engine/Backend/DspRuntime. Includes + links are PUBLIC so consumers inherit them.
add_library(retroplug-backend STATIC
    src/host/rpc/HostRpcService.cpp         # fs / config / zip (+ opt-in efsw file watcher)
    src/host/rpc/NativeFileWatcher.cpp      # efsw watcher behind drainChangedPaths / setWatchedRoms
    src/host/rpc/EngineRpcService.cpp       # emulator surface over Engine + Invoker + Factory
    src/host/rpc/DebugRpcService.cpp        # live-core inspection over Engine (CLI-only facet)
    src/host/rpc/AudioDriverRpcService.cpp  # background audio thread + observation atomics
    src/host/engine/Engine.cpp              # single-threaded core: owns Project + DspRuntime, runs blocks
    src/host/engine/SnapshotRegistry.cpp    # owned id-keyed frame/state/SRAM store — the control plane's one read door
    src/host/engine/EngineInvoker.cpp       # Direct/Queued invokers — the only threading-aware layer
    src/system/SystemFactory.cpp            # backend-agnostic build registry (the one build path)
    src/system/CoreBackends.cpp             # registerCoreBackends: mounts sameboy + mesen on a factory
    src/system/sameboy/SameBoyBackend.cpp   # SameBoy builder — the only place SameBoyConfig is constructed
    src/system/mesen/MesenBackend.cpp       # Mesen builder — NES + GBA (HLE BIOS), dispatched on platform
    src/host/dsp/ScriptCompiler.cpp         # bare-QuickJS bytecode compiler (DSP-side JS runtime)
    src/host/dsp/DspRuntime.cpp             # second bare QuickJS context — runs translator scripts
    src/host/render/RenderHost.cpp          # bare-QuickJS render host (own Engine, runs the worker bundle)
    src/host/render/RenderJobRegistry.cpp   # background render jobs — one worker thread each, cancellable
    ${RENDER_WORKER_BUNDLE_C}               # embedded render-worker bytecode (rp_render_worker_bundle)
    ${DPFJS_PATH}/src/dpfjs/host/TjsHostRuntime.cpp  # shared txiki host (dpf.js)
)
add_dependencies(retroplug-backend retroplug-render-worker-bundle)  # the embedded worker bytecode must exist first

target_include_directories(retroplug-backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # dpf.js src root for the shared host header ("dpfjs/host/TjsHostRuntime.hpp").
    ${DPFJS_PATH}/src
    # txiki.js / QuickJS runtime headers (tjs.h + internal private.h; quickjs.h transitive).
    ${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/src
    ${RP_QUICKJS_INCLUDE}
    # (packages/native/src for the Project/SystemBase headers + util/MinizZip.hpp, and the
    #  miniz header dir, arrive PUBLIC from retroplug-core above.)
)

find_package(Threads REQUIRED)  # efsw-static needs pthreads on Linux (its CMake links it to the shared lib only);
                                # also the Launchpad scan thread and N8Link's serial thread, below.


target_link_libraries(retroplug-backend PUBLIC
    retroplug-core      # Project + SystemBase + SameBoy/Mesen; PUBLIC-brings rpcpp + miniz
    tjs                 # txiki.js/QuickJS runtime (transitively brings qjs + libuv)
    efsw-static         # NativeFileWatcher's filesystem watcher (PUBLIC-brings efsw/efsw.hpp include dir)
    Threads::Threads
)

set_target_properties(retroplug-backend PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON  # linked into the shared plugin module (below)
)

# retroplug-host: the test host — the backend lib + a main() that evals a TS bundle over the
# __rpcSend bridge. Output binary lands in build/bin/.
add_executable(retroplug-host src/main.cpp)

target_link_libraries(retroplug-host PRIVATE retroplug-backend)

set_target_properties(retroplug-host PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# retroplug-render-host-test: drives one RenderHost render from a JSON job spec, so a background/UI render
# can be proven byte-identical to `retroplug-cli render`. EXCLUDE_FROM_ALL — built by name for the parity
# check, not part of a normal build.
add_executable(retroplug-render-host-test EXCLUDE_FROM_ALL test/render-host-main.cpp)
target_link_libraries(retroplug-render-host-test PRIVATE retroplug-backend)
set_target_properties(retroplug-render-host-test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# The root CLI dispatcher bundle — cli/cli.ts → tjsc bytecode C array, COMPILED INTO retroplug-cli
# (mirrors the plugin's cp-/ui-bundle recipe). It owns command routing + all help; esbuild follows its
# imports (cli.ts → tools.ts → sessions/render.ts → session.ts). build-session.js injects
# __DSP_KERNEL_BUNDLE__; tjsc derives the array name from the output stem → rp_cli_bundle[] +
# rp_cli_bundle_size. Derived; never committed. Adding a command is pure TS (cli/tools.ts) — no change here.
set(CLI_BUNDLE_JS "${CMAKE_BINARY_DIR}/native/cli-bundle.js")
set(CLI_BUNDLE_C  "${CMAKE_BINARY_DIR}/native/cli-bundle_data.c")
set_source_files_properties(${CLI_BUNDLE_C} PROPERTIES GENERATED TRUE)  # produced before the CLI compiles
add_custom_target(retroplug-cli-bundle
    BYPRODUCTS ${CLI_BUNDLE_JS} ${CLI_BUNDLE_C} ${CLI_BUNDLE_C}.new
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${CMAKE_SOURCE_DIR}/packages/retroplug/cli/cli.ts ${CLI_BUNDLE_JS}
    COMMAND ${_TJSC_COMMAND} -m -s -p rp_ -o ${CLI_BUNDLE_C}.new ${CLI_BUNDLE_JS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CLI_BUNDLE_C}.new ${CLI_BUNDLE_C}
    COMMENT "Bundling retroplug-cli dispatcher (esbuild + bytecode)"
    VERBATIM)
# Order against the in-tree tjsc target only; a cross-build's -DTJSC_EXECUTABLE is a host file
# path (not a target) — the custom command's DEPENDS already tracks it.
if(TARGET ${_TJSC_DEP})
  add_dependencies(retroplug-cli-bundle ${_TJSC_DEP})
endif()

# The TypeScript type stripper — ts-blank-space + the TS parser → tjsc bytecode C array, COMPILED INTO
# retroplug-cli so `retroplug-cli test/run` can execute `.ts` directly with no Node, npm or bundler on
# the consumer's machine. GLOBAL CODE (tjsc WITHOUT -m, like the render worker): cli/main.cpp loads it
# with JS_ReadObject + JS_EvalFunction on demand, so the ~4 MB never touches the startup path of
# `render` / `n8-bridge` / any other command. Array name derives from the output stem →
# rp_ts_stripper_bundle[] + _size. Derived; never committed.
set(TS_STRIPPER_BUNDLE_JS "${CMAKE_BINARY_DIR}/native/ts-stripper-bundle.js")
set(TS_STRIPPER_BUNDLE_C  "${CMAKE_BINARY_DIR}/native/ts-stripper-bundle_data.c")
set_source_files_properties(${TS_STRIPPER_BUNDLE_C} PROPERTIES GENERATED TRUE)
add_custom_target(retroplug-ts-stripper-bundle
    BYPRODUCTS ${TS_STRIPPER_BUNDLE_JS} ${TS_STRIPPER_BUNDLE_C} ${TS_STRIPPER_BUNDLE_C}.new
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-ts-stripper.mjs ${TS_STRIPPER_BUNDLE_JS}
    COMMAND ${_TJSC_COMMAND} -s -p rp_ -o ${TS_STRIPPER_BUNDLE_C}.new ${TS_STRIPPER_BUNDLE_JS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TS_STRIPPER_BUNDLE_C}.new ${TS_STRIPPER_BUNDLE_C}
    COMMENT "Bundling the TypeScript type stripper (esbuild + global-code bytecode)"
    VERBATIM)
if(TARGET ${_TJSC_DEP})
  add_dependencies(retroplug-ts-stripper-bundle ${_TJSC_DEP})
endif()

# The test SDK, embedded twice, for two different consumers:
#
#   rp_retroplug_cli[]  MODULE bytecode, compiled with `tjsc -n retroplug-cli` so the module NAME is
#                       exactly `retroplug-cli`. cli/main.cpp JS_ReadObject's it before evaluating a
#                       session, which registers it in QuickJS's loaded-module table; an
#                       `import ... from "retroplug-cli"` then resolves from that table, because
#                       js_host_resolve_imported_module looks there BEFORE calling the module loader
#                       (quickjs.c: "first look at the loaded modules"). So the SDK never touches the
#                       filesystem - there is no .js to generate, sync, or let go stale.
#   rp_sdk_dts[]        the .d.ts as raw text. Still materialized next to a consumer's tests, because
#                       editors and `tsc` read declarations off disk; a bare specifier resolves to it
#                       via a tsconfig `paths` entry.
#
# The relative-import form could never work this way: tjs_module_normalizer only consults the import
# map for BARE specifiers (modules.c, `if (name[0] != '.')`), so "../sdk/x.js" always hits the disk.
set(SDK_ASSETS_C "${CMAKE_BINARY_DIR}/native/sdk-assets_data.c")
set(SDK_MODULE_C "${CMAKE_BINARY_DIR}/native/sdk-module_data.c")
set_source_files_properties(${SDK_ASSETS_C} ${SDK_MODULE_C} PROPERTIES GENERATED TRUE)
add_custom_target(retroplug-cli-sdk-assets
    BYPRODUCTS ${SDK_ASSETS_C} ${SDK_ASSETS_C}.new ${SDK_MODULE_C} ${SDK_MODULE_C}.new
    # Builds build/cli-sdk/{retroplug-cli.js,.d.ts}, then embeds the .d.ts + a content stamp.
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/embed-sdk.mjs
            ${CMAKE_BINARY_DIR}/cli-sdk ${SDK_ASSETS_C}.new
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDK_ASSETS_C}.new ${SDK_ASSETS_C}
    # ...and compiles the same bundle to module bytecode under the name a test imports.
    COMMAND ${_TJSC_COMMAND} -m -n retroplug-cli -s -p rp_ -o ${SDK_MODULE_C}.new
            ${CMAKE_BINARY_DIR}/cli-sdk/retroplug-cli.js
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDK_MODULE_C}.new ${SDK_MODULE_C}
    COMMENT "Embedding the retroplug-cli test SDK (module bytecode + .d.ts)"
    VERBATIM)
if(TARGET ${_TJSC_DEP})
  add_dependencies(retroplug-cli-sdk-assets ${_TJSC_DEP})
endif()

# retroplug-cli: the standalone runner — the backend lib + a main() that evals either the COMPILED-IN
# dispatcher bundle (commands + help) or, when argv[1] is a `.js`, a session file by path. No Node at
# runtime. Output binary lands in build/bin/.
add_executable(retroplug-cli cli/main.cpp ${CLI_BUNDLE_C} ${TS_STRIPPER_BUNDLE_C} ${SDK_ASSETS_C} ${SDK_MODULE_C})

target_link_libraries(retroplug-cli PRIVATE retroplug-backend)
add_dependencies(retroplug-cli retroplug-cli-bundle)             # the embedded dispatcher bytecode must exist first
add_dependencies(retroplug-cli retroplug-ts-stripper-bundle)     # ...and the stripper bytecode
add_dependencies(retroplug-cli retroplug-cli-sdk-assets)         # ...and the embedded SDK

set_target_properties(retroplug-cli PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# The Node-API addon host (opt-in: -DRETROPLUG_NODE_ADDON=ON).
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/node-addon.cmake)

# The example sessions, esbuild-bundled to runnable .js (with the DSP kernel injected as
# __DSP_KERNEL_BUNDLE__, mirroring the cp-bundle recipe). A normal build yields both the binary and demos
# the binary can run, e.g. build/bin/retroplug-cli build/cli/mgb-smoke.js.
set(CLI_SESSIONS "${CMAKE_SOURCE_DIR}/packages/retroplug/cli/sessions")
add_custom_target(retroplug-example-session ALL
    BYPRODUCTS ${CMAKE_BINARY_DIR}/cli/mgb-smoke.js
               ${CMAKE_BINARY_DIR}/cli/render-rom.js
               ${CMAKE_BINARY_DIR}/cli/render-song.js
               ${CMAKE_BINARY_DIR}/cli/rom-test.js
               ${CMAKE_BINARY_DIR}/cli/analyze-mgb.js
               ${CMAKE_BINARY_DIR}/cli/analyze-lsdj-sync.js
               ${CMAKE_BINARY_DIR}/cli/export-mgb-channels.js
               ${CMAKE_BINARY_DIR}/cli/export-nes-channels.js
               ${CMAKE_BINARY_DIR}/cli/export-nes-mono.js
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${CLI_SESSIONS}/mgb-smoke.ts   ${CMAKE_BINARY_DIR}/cli/mgb-smoke.js
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${CLI_SESSIONS}/render-rom.ts  ${CMAKE_BINARY_DIR}/cli/render-rom.js
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${CLI_SESSIONS}/render-song.ts ${CMAKE_BINARY_DIR}/cli/render-song.js
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${CLI_SESSIONS}/rom-test.ts    ${CMAKE_BINARY_DIR}/cli/rom-test.js
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${CLI_SESSIONS}/analyze-mgb.ts       ${CMAKE_BINARY_DIR}/cli/analyze-mgb.js
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${CLI_SESSIONS}/analyze-lsdj-sync.ts ${CMAKE_BINARY_DIR}/cli/analyze-lsdj-sync.js
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${CLI_SESSIONS}/export-mgb-channels.ts ${CMAKE_BINARY_DIR}/cli/export-mgb-channels.js
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${CLI_SESSIONS}/export-nes-channels.ts ${CMAKE_BINARY_DIR}/cli/export-nes-channels.js
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${CLI_SESSIONS}/export-nes-mono.ts ${CMAKE_BINARY_DIR}/cli/export-nes-mono.js
    COMMENT "Bundling CLI example sessions (esbuild)"
    VERBATIM)

# The embedded control-plane + React-UI bytecode bundles.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bundles.cmake)

# rtmidi / serial / portaudio, and the retroplug-cli N8 transport facets built on them.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps.cmake)

# --- retroplug-sdl: the SDL2 standalone for framebuffer handhelds (Anbernic / muOS) + plain Linux.
# muOS has no X11 (fbdev + Mali EGL only), so the DPF/pugl JACK standalone can't run there. This host
# reuses the WHOLE core — retroplug-backend (Engine + cores + txiki) + lvgl-js-native (the LVGL/React
# bridge) + the SAME embedded cp/ui bundles the plugin links — and provides its own SDL2 window / audio /
# Part of the default `all` target, so build.sh builds it and CI covers it. Its deps are in-tree submodules
# (rtmidi + portaudio) or already required (SDL2 at root scope for gamepad input); PortAudio adds no new
# system lib to build — ALSA is present via rtmidi, and libpipewire-0.3 (if installed) just upgrades it to the
# native PipeWire host API, else it falls back to ALSA.
add_executable(retroplug-sdl
    sdl/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/input/GamepadManager.cpp  # SDL gamepad poll (shared UI-thread input)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/input/MidiIo.cpp          # RtMidi in/out seam (virtual + hardware ports)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/n8/Edio.cpp               # N8 Pro serial client (Settings > N8)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/n8/WjwwoodSerialPort.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/n8/N8Link.cpp             # host serial thread -> physical N8 (MIDI forward)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/n8/N8Host.cpp             # shared N8 link + config + n8.cfg (also the plugin)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/n8/N8Hooks.cpp           # binds the __rp_*N8* config hooks (shared)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/n8/N8Menu.cpp             # on-device menu (*t/*n/*s) for SD ops (shared)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/n8/N8SdWorker.cpp         # background worker: ROM upload / SRAM dump+restore
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/launchpad/LaunchpadLink.cpp   # control-surface in/out pair (own rings)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/launchpad/LaunchpadHost.cpp   # link + config + launchpad.cfg
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/launchpad/LaunchpadScanner.cpp # device-inquiry port scan (worker thread)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/launchpad/LaunchpadHooks.cpp  # binds the __rp_*Launchpad* config hooks
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/launchpad/RtMidiPort.cpp      # the RtMidi backend behind IMidiPort
    ${CP_BUNDLE_C}                # the embedded control-plane bytecode (rp_cp_bundle)
    ${UI_BUNDLE_C})               # the embedded React UI bytecode (rp_ui_bundle)

target_link_libraries(retroplug-sdl PRIVATE
    lvgl-js-native                # LvglJsEngine + lvgl + lv_binding_js native + lodepng
    retroplug-backend             # Engine + cores + txiki host + rpcpp
    rtmidi                        # cross-platform MIDI in/out (ALSA/CoreMIDI/WinMM)
    portaudio                     # audio backend (PipeWire/ALSA on Linux); replaces SDL audio
    serial                        # wjwwood/serial -> Everdrive N8 Pro (cmake/serial.cmake)
    PkgConfig::SDL2)              # window + present + gamepad (audio now via PortAudio; found at root scope)

target_include_directories(retroplug-sdl PRIVATE
    # quickjs.h for the JS trampolines (lvgl-js-native PUBLIC-propagates the lv_binding_js render include
    # dir, so "native/..." resolves transitively; retroplug-backend brings "host/..." + "dpfjs/...").
    ${RP_QUICKJS_INCLUDE}
    # DistrhoUtils.hpp (header-only d_stderr logging) for GamepadManager.cpp — the one DPF header the
    # shared gamepad poller pulls in; no DPF linkage.
    ${DPFJS_PATH}/deps/dpf/distrho
    # portable-file-dialogs (header-only): the OS-native file picker backing __rp_openFileBrowser. No link
    # libs on Linux/macOS (it fork/execs zenity/kdialog/osascript); Windows pulls comdlg32/ole32 via its header.
    ${CMAKE_SOURCE_DIR}/deps/portable-file-dialogs
    # portaudio.h (the `portaudio` target doesn't export its include dir for a subdirectory build).
    ${CMAKE_SOURCE_DIR}/deps/portaudio/include)

add_dependencies(retroplug-sdl retroplug-cp-bundle retroplug-ui-bundle)  # embedded bundles must exist first

# Statically link libstdc++ on Linux so the binary doesn't depend on the target device's libstdc++
# version — handhelds (muOS/Buildroot) may ship an older one. But DO NOT static-link libgcc: the C++
# exception unwinder lives there, and a program that throws across a DSO boundary (we throw, and we load
# SDL2) needs ONE shared unwinder. Statically embedding libgcc gives a second unwinder whose .eh_frame
# registration collides with the system libgcc_s, corrupting unwinding into a SIGILL at startup on the
# device (observed on muOS/Cortex-A53). libgcc_s stays dynamic (present on the device, ABI-stable). glibc
# stays dynamic too (linked ≤ the build's version).
if(UNIX AND NOT APPLE)
    target_link_options(retroplug-sdl PRIVATE -static-libstdc++)
endif()

set_target_properties(retroplug-sdl PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# The DPF plugin (clap/vst3/vst2/au/jack) + per-platform packaging.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/plugin.cmake)

# --- UI test harness -------------------------------------------------------------------------------
# retroplug-ui-test boots the React UI on a headless software LVGL display
# (RenderCore) driven by the BackendFacade RPC (UiHarness) — the counterpart of the
# legacy retroplug-ui-test. Only built on demand (not ALL). Reuses UI_BUNDLE_C + the ui-bundle target
# defined above the plugin.

add_executable(retroplug-ui-test
    test/ui/UiRunner.cpp
    test/ui/UiHarness.cpp
    test/ui/RenderCore.cpp
    ${UI_BUNDLE_C})               # the embedded UI bytecode (rp_ui_bundle)

target_link_libraries(retroplug-ui-test PRIVATE
    lvgl-js-native                # LvglJsEngine + lvgl + lv_binding_js native + lodepng (+ tjs)
    retroplug-backend) # BackendFacade + rpcpp + the cores + the txiki host

target_include_directories(retroplug-ui-test PRIVATE
    # quickjs.h for the JS trampolines in RenderCore/UiRunner (lvgl-js-native
    # PUBLIC-propagates the lv_binding_js render include dir, so "native/..." resolves transitively).
    ${RP_QUICKJS_INCLUDE})

set_target_properties(retroplug-ui-test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# The embedded bundle must exist before the test binary compiles it.
add_dependencies(retroplug-ui-test retroplug-ui-bundle)

# The ten native test binaries (EXCLUDE_FROM_ALL; built by name).
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests.cmake)

# /W4 /WX on our own targets (MSVC), with per-number suppressions for what we do not own.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/warnings.cmake)
