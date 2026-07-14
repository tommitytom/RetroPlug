# retroplug-host: the native host for the TS app.
#
# Exposes the Backend to a TS bundle over
# globalThis[Symbol.for("plugin")].__rpcSend, and runs the test harness. The
# fs/config/codec half is std::filesystem + miniz; the emulator half is a real SameBoySystem
# in a real Project. It links `retroplug-core` (Project + SystemBase + SameBoy/Mesen, plus
# transitive rpcpp + miniz and the packages/native/src include) — so no longer emulator-free.
# Output binary lands in build/bin/.

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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/Effects.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/KitUtil.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/SampleCache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/KitCompiler.cpp
    # NOTE: the LSDj sav codec + model were removed — they are now pure TS
    # (packages/retroplug/src/lsdj), validated by frozen golden vectors. Only the
    # kit/sample pipeline (Effects/KitUtil/SampleCache/KitCompiler) remains in C++.
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/SystemBase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/BlockRunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/OfflineRender.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/sameboy/LinkGroup.cpp
    ${_rpcore_sbs}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/MesenNesSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/MesenNesDebugSession.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/MesenVideoDevice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/MesenGbaSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/mesen/roles/NesN8MidiRole.cpp
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

# retroplug-backend: the Backend as a static library — everything except a host
# entry point, so both the test host (retroplug-host) and the DPF plugin compose the SAME
# Engine/Backend/DspRuntime. Includes + links are PUBLIC so consumers inherit them.
add_library(retroplug-backend STATIC
    src/host/rpc/HostRpcService.cpp         # fs / config / zip (pure, stateless)
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
    ${DPFJS_PATH}/src/dpfjs/host/TjsHostRuntime.cpp  # shared txiki host (dpf.js)
)

target_include_directories(retroplug-backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # dpf.js src root for the shared host header ("dpfjs/host/TjsHostRuntime.hpp").
    ${DPFJS_PATH}/src
    # txiki.js / QuickJS runtime headers (tjs.h + internal private.h; quickjs.h transitive).
    ${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/src
    ${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/deps/quickjs
    # (packages/native/src for the Project/SystemBase headers + util/MinizZip.hpp, and the
    #  miniz header dir, arrive PUBLIC from retroplug-core above.)
)

target_link_libraries(retroplug-backend PUBLIC
    retroplug-core      # Project + SystemBase + SameBoy/Mesen; PUBLIC-brings rpcpp + miniz
    tjs                 # txiki.js/QuickJS runtime (transitively brings qjs + libuv)
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

# The `render` subcommand bundle — cli/sessions/render.ts → tjsc bytecode C array, COMPILED INTO
# retroplug-cli (mirrors the plugin's cp-/ui-bundle recipe). This is what lets `retroplug-cli render …`
# run with no loose .js and no Node. build-session.js injects __DSP_KERNEL_BUNDLE__; tjsc emits
# rp_render_bundle[] + rp_render_bundle_size. Derived; never committed.
set(RENDER_BUNDLE_JS "${CMAKE_BINARY_DIR}/native/render-bundle.js")
set(RENDER_BUNDLE_C  "${CMAKE_BINARY_DIR}/native/render-bundle_data.c")
set_source_files_properties(${RENDER_BUNDLE_C} PROPERTIES GENERATED TRUE)  # produced before the CLI compiles
add_custom_target(retroplug-render-bundle
    BYPRODUCTS ${RENDER_BUNDLE_JS} ${RENDER_BUNDLE_C} ${RENDER_BUNDLE_C}.new
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${CMAKE_SOURCE_DIR}/packages/retroplug/cli/sessions/render.ts ${RENDER_BUNDLE_JS}
    COMMAND ${_TJSC_COMMAND} -m -s -p rp_ -o ${RENDER_BUNDLE_C}.new ${RENDER_BUNDLE_JS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RENDER_BUNDLE_C}.new ${RENDER_BUNDLE_C}
    COMMENT "Bundling retroplug-cli render subcommand (esbuild + bytecode)"
    VERBATIM)
add_dependencies(retroplug-render-bundle ${_TJSC_DEP})

# retroplug-cli: the standalone session runner — the same backend lib + a main() that runs either a
# COMPILED-IN subcommand bundle (e.g. `render`) or, as a fallback, one pre-bundled session .js given by
# path over the __rpcSend bridge. No Node at runtime. Output binary lands in build/bin/.
add_executable(retroplug-cli cli/main.cpp ${RENDER_BUNDLE_C})

target_link_libraries(retroplug-cli PRIVATE retroplug-backend)
add_dependencies(retroplug-cli retroplug-render-bundle)  # the embedded render bytecode must exist first

set_target_properties(retroplug-cli PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

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

# --- control-plane bundle: pluginControlPlane.ts → bytecode C array (mirrors ui-regenerate). The DPF
# plugin evals this at construct to compose the stores + DSP kernel. Derived; never committed.
set(CP_BUNDLE_JS "${CMAKE_BINARY_DIR}/native/cp-bundle.js")
set(CP_BUNDLE_C  "${CMAKE_BINARY_DIR}/native/cp-bundle_data.c")
set_source_files_properties(${CP_BUNDLE_C} PROPERTIES GENERATED TRUE)  # produced before the plugin compiles

add_custom_target(retroplug-cp-bundle ALL
    BYPRODUCTS ${CP_BUNDLE_JS} ${CP_BUNDLE_C} ${CP_BUNDLE_C}.new
    COMMAND ${NODE_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/build-controlplane.js ${CP_BUNDLE_JS}
    COMMAND ${_TJSC_COMMAND} -m -s -p rp_ -o ${CP_BUNDLE_C}.new ${CP_BUNDLE_JS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CP_BUNDLE_C}.new ${CP_BUNDLE_C}
    COMMENT "Regenerating control-plane bundle (esbuild + bytecode)"
    VERBATIM)
add_dependencies(retroplug-cp-bundle ${_TJSC_DEP})

# The UI bundle: main.tsx → bytecode C array (React entry via build-ui.js). Derived; never
# committed. Defined BEFORE the plugin so its FILES_UI can embed it; the UI-test binary below reuses the
# same vars/target.
set(UI_BUNDLE_JS "${CMAKE_BINARY_DIR}/native/ui-bundle.js")
set(UI_BUNDLE_C  "${CMAKE_BINARY_DIR}/native/ui-bundle_data.c")
set_source_files_properties(${UI_BUNDLE_C} PROPERTIES GENERATED TRUE)

add_custom_target(retroplug-ui-bundle
    BYPRODUCTS ${UI_BUNDLE_JS} ${UI_BUNDLE_C} ${UI_BUNDLE_C}.new
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-ui.js
            ${UI_BUNDLE_JS} ${UI_BUNDLE_JS}.d
            ${CMAKE_SOURCE_DIR}/packages/retroplug/ui/main.tsx
    COMMAND ${_TJSC_COMMAND} -m -s -p rp_ -o ${UI_BUNDLE_C}.new ${UI_BUNDLE_JS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${UI_BUNDLE_C}.new ${UI_BUNDLE_C}
    COMMENT "Regenerating UI bundle (esbuild + bytecode)"
    VERBATIM)
add_dependencies(retroplug-ui-bundle ${_TJSC_DEP})

# --- retroplug.{vst3,clap} + the jack standalone: the DPF plugin hosting the
# Engine + control plane, with the React UI on the shared LVGL editor widget. Distinct identity from the
# legacy `retroplug` (they coexist).
dpf_add_plugin(retroplug
    TARGETS clap vst3 vst2 au jack  # au is macOS-only (DPF gates it to APPLE; a no-op elsewhere)
    USE_FILE_BROWSER                    # native OS file dialog (matches DISTRHO_UI_FILE_BROWSER)
    FILES_DSP
        plugin/PluginDSP.cpp
        ${CP_BUNDLE_C}                # the embedded control-plane bytecode (rp_cp_bundle)
    FILES_UI
        plugin/PluginUI.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/host/input/GamepadManager.cpp  # SDL gamepad poll (shared UI-thread input)
        ${UI_BUNDLE_C}                # the embedded React UI bytecode (rp_ui_bundle)
        ${DPFJS_PATH}/deps/dpf-widgets/generic/LVGL.cpp)  # the generic DPF↔LVGL base widget

# plugin/ FIRST so DPF resolves OUR DistrhoPluginInfo.h ahead of DPF's own default. The `src` root
# (for "host/input/GamepadManager.hpp") arrives transitively PUBLIC via retroplug-backend.
# dpf-widgets/generic resolves "LVGL.hpp".
target_include_directories(retroplug BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/plugin)
target_include_directories(retroplug PUBLIC ${DPFJS_PATH}/deps/dpf-widgets/generic)

target_link_libraries(retroplug PUBLIC
    retroplug-backend  # Engine/Backend + txiki host + (transitively) the cores + rpcpp
    lvgl-js-native                # LVGL + LvglJsEngine + lv_binding_js + lodepng (the editor)
    dpfjs::core)                  # DPF Plugin + UI base

target_compile_definitions(retroplug PUBLIC DPFJS_ENV_PREFIX="RETROPLUG_")

# The embedded bundles must exist before the plugin compiles them.
add_dependencies(retroplug retroplug-cp-bundle retroplug-ui-bundle)

# Windows only: the standalone (retroplug-jack, OUTPUT_NAME "retroplug") exports
# symbols, so link.exe emits an import library "retroplug.lib" — the same name,
# in the same dir, as DPF's base plugin library "retroplug.lib" that the exe also
# links, tripping LNK1149 ("output filename matches input filename"). Executables
# never emit import libraries on Linux/macOS, so this is Windows-specific. Give
# the standalone's implib a distinct name; nothing links against it.
if(WIN32 AND TARGET retroplug-jack)
    set_target_properties(retroplug-jack PROPERTIES ARCHIVE_OUTPUT_NAME retroplug-jack)

    # No console window when the standalone is launched directly. DPF's main() already attaches to a
    # parent console when there is one (AttachConsole(ATTACH_PARENT_PROCESS) in DistrhoPluginJACK.cpp),
    # but the exe links as a CONSOLE-subsystem app, so Windows always spawns a console. Build it as a
    # GUI-subsystem app instead: double-clicking shows no console, while running it from a terminal still
    # prints there (DPF's attach picks up the launching shell) — so debugging via `retroplug.exe` in a
    # console still works. main() (not WinMain) stays the entry via /ENTRY:mainCRTStartup.
    if(MSVC)
        target_link_options(retroplug-jack PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    endif()
elseif(APPLE AND TARGET retroplug-jack)
    # No terminal window when the standalone is launched from Finder. DPF builds the JACK standalone as a
    # bare Mach-O executable (bin/retroplug); double-clicking a bare executable in Finder opens it inside
    # Terminal.app — the macOS analogue of the Windows console popup. Package it as a .app bundle instead:
    # Finder / `open` launches it as a proper GUI app with no terminal, while running the binary inside the
    # bundle (bin/retroplug.app/Contents/MacOS/retroplug) from a shell still prints stdout/stderr there — so
    # console debugging still works. Same trade-off as the Windows /SUBSYSTEM:WINDOWS build above. The UI is
    # compiled into the binary (rp_ui_bundle), so the bundle needs no external Resources.
    set_target_properties(retroplug-jack PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "RetroPlug"
        MACOSX_BUNDLE_GUI_IDENTIFIER "co.tommitytom.retroplug")
endif()

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
    ${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/deps/quickjs)

set_target_properties(retroplug-ui-test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# The embedded bundle must exist before the test binary compiles it.
add_dependencies(retroplug-ui-test retroplug-ui-bundle)

# --- plugin unit checks (retroplug-plugin-test) ----------------------------------------------------
# Pure-C++ Catch2 checks for plugin/ mechanisms that need real JSContexts but no LVGL/txiki host —
# currently the per-context window-hook routing (ContextTargets.hpp). Links bare quickjs (qjs) + Catch2.
# Run via `pnpm test:plugin` (builds + runs the binary; the exit code is pass/fail). On demand only.
add_executable(retroplug-plugin-test EXCLUDE_FROM_ALL
    test/plugin/ContextTargets.test.cpp)

target_link_libraries(retroplug-plugin-test PRIVATE
    qjs                        # bare quickjs: JS_* + ArrayBuffer / CFunctionData
    Catch2::Catch2WithMain)

target_include_directories(retroplug-plugin-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin                                        # ContextTargets.hpp
    ${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/deps/quickjs)                 # quickjs.h

set_target_properties(retroplug-plugin-test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# retroplug-classid-test guards TjsHostRuntime::init's class-id counter sync (the plugin-editor blank-UI
# fix): it needs the real txiki host, so it links retroplug-backend. Also `pnpm test:plugin`, on demand.
add_executable(retroplug-classid-test EXCLUDE_FROM_ALL
    test/plugin/ClassIdReserve.test.cpp)

target_link_libraries(retroplug-classid-test PRIVATE
    retroplug-backend          # TjsHostRuntime (+ txiki, quickjs) + ClassIdSpace.hpp
    Catch2::Catch2WithMain)

target_include_directories(retroplug-classid-test PRIVATE
    ${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/deps/quickjs)             # quickjs.h

set_target_properties(retroplug-classid-test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# retroplug-audio-test guards the per-channel audio path: the host seam (runUnit stream loop +
# AudioRouter::streamCount + lane-counted finishBlock, over a fake SystemBase) AND the real SameBoy
# per-channel tap (ChannelStreams + SameBoyStems), plus the ChannelSplit plugin router + its Engine
# gating (ChannelSplit + EngineChannelSplit, spec/10 step 4). Links retroplug-backend (which PUBLIC-brings
# retroplug-core → BlockRunner + routers + the sameboy core) so the Engine gating test can build an
# Engine. Also `pnpm test:plugin`, on demand.
add_executable(retroplug-audio-test EXCLUDE_FROM_ALL
    test/audio/ChannelStreams.test.cpp
    test/audio/SameBoyStems.test.cpp
    test/audio/ChannelSplit.test.cpp
    test/audio/EngineChannelSplit.test.cpp
    test/audio/EngineSampleRate.test.cpp   # host sample-rate change re-rates live cores
    test/audio/NesStems.test.cpp)    # NES stereo-mod pins re-sum to the mix (spec/10 step 5)

target_link_libraries(retroplug-audio-test PRIVATE
    retroplug-backend          # Engine + retroplug-core (SystemBase + BlockRunner + routers + sameboy)
    Catch2::Catch2WithMain)

target_include_directories(retroplug-audio-test PRIVATE
    ${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/deps/quickjs)             # quickjs.h (via Engine.hpp → DspRuntime.hpp)

# The mGB ROM the SameBoyStems + ChannelSplit fidelity tests drive (the boot chime supplies real signal);
# the n8-midi NES ROM the NesStems pin test drives (a ch1 note supplies real APU signal).
target_compile_definitions(retroplug-audio-test PRIVATE
    RP_MGB_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/mGB.gb"
    RP_N8_MIDI_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/n8-midi.nes")

set_target_properties(retroplug-audio-test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
