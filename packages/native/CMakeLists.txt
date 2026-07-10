# retroplug-host: the native host for the greenfield TS app.
#
# Exposes the greenfield Backend to a TS bundle over
# globalThis[Symbol.for("plugin")].__rpcSend, and runs the greenfield test harness. The
# fs/config/codec half is std::filesystem + miniz; the emulator half is a real SameBoySystem
# in a real Project. It links `retroplug-core` (Project + SystemBase + SameBoy/Mesen, plus
# transitive rpcpp + miniz and the packages/native/src include) — so no longer emulator-free.
# Output binary lands in build/bin/.

# --- retroplug-core: the shared emulator/Project core, as a neutrally-named static lib. Extracted out of
# packages/native/cli (which also compiled the CLI-only HarnessRpcService) so greenfield links ONLY the
# shared core, never a CLI RPC surface it doesn't use. The legacy `retroplug-cli-core` is now a thin
# wrapper = HarnessRpcService.cpp + this lib. Deliberately txiki/QuickJS-free (harness-schema-dump links it
# without the JS runtime); PIC so it links into the greenfield plugin .so.
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/codec/SongCodec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/codec/Compression.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/codec/SavCodec.cpp
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
    POSITION_INDEPENDENT_CODE ON   # linked into the greenfield plugin .so
)

# retroplug-backend: the greenfield Backend as a static library — everything except a host
# entry point, so both the test host (retroplug-host) and the DPF plugin compose the SAME
# Engine/Backend/DspRuntime. Includes + links are PUBLIC so consumers inherit them.
add_library(retroplug-backend STATIC
    src/BackendFacade.cpp         # thin facade: owns the shared state, forwards to the 3 services
    src/HostRpcService.cpp        # fs / config / codec / sav (pure, stateless)
    src/EngineRpcService.cpp      # emulator surface over Engine + Invoker + Factory
    src/AudioDriverRpcService.cpp # background audio thread + observation atomics
    src/Engine.cpp                # single-threaded core: owns Project + DspRuntime, runs blocks
    src/SnapshotRegistry.cpp      # owned id-keyed frame/state/SRAM store — the control plane's one read door
    src/EngineInvoker.cpp         # Direct/Queued invokers — the only threading-aware layer
    src/SystemFactory.cpp         # backend-agnostic build registry (the one build path)
    src/SameBoyBackend.cpp        # SameBoy builder — the only place SameBoyConfig is constructed
    src/MesenBackend.cpp          # Mesen builder — NES + GBA (HLE BIOS), dispatched on platform
    src/ScriptCompiler.cpp        # bare-QuickJS bytecode compiler (DSP-side JS runtime)
    src/DspRuntime.cpp            # second bare QuickJS context — runs translator scripts
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

# retroplug-cli: the standalone session runner — the same backend lib + a main() that evals
# one pre-bundled session .js (authored in TS, esbuild-bundled) over the __rpcSend bridge. No Node at
# runtime. Output binary lands in build/bin/.
add_executable(retroplug-cli cli/main.cpp)

target_link_libraries(retroplug-cli PRIVATE retroplug-backend)

set_target_properties(retroplug-cli PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# The example sessions, esbuild-bundled to runnable .js (with the DSP kernel injected as
# __DSP_KERNEL_BUNDLE__, mirroring the cp-bundle recipe). A normal build yields both the binary and demos
# the binary can run, e.g. build/bin/retroplug-cli build/greenfield-cli/mgb-smoke.js.
set(GFCLI_SESSIONS "${CMAKE_SOURCE_DIR}/packages/retroplug/cli/sessions")
add_custom_target(retroplug-example-session ALL
    BYPRODUCTS ${CMAKE_BINARY_DIR}/greenfield-cli/mgb-smoke.js
               ${CMAKE_BINARY_DIR}/greenfield-cli/render-rom.js
               ${CMAKE_BINARY_DIR}/greenfield-cli/render-song.js
               ${CMAKE_BINARY_DIR}/greenfield-cli/rom-test.js
               ${CMAKE_BINARY_DIR}/greenfield-cli/analyze-mgb.js
               ${CMAKE_BINARY_DIR}/greenfield-cli/analyze-lsdj-sync.js
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${GFCLI_SESSIONS}/mgb-smoke.ts   ${CMAKE_BINARY_DIR}/greenfield-cli/mgb-smoke.js
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${GFCLI_SESSIONS}/render-rom.ts  ${CMAKE_BINARY_DIR}/greenfield-cli/render-rom.js
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${GFCLI_SESSIONS}/render-song.ts ${CMAKE_BINARY_DIR}/greenfield-cli/render-song.js
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${GFCLI_SESSIONS}/rom-test.ts    ${CMAKE_BINARY_DIR}/greenfield-cli/rom-test.js
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${GFCLI_SESSIONS}/analyze-mgb.ts       ${CMAKE_BINARY_DIR}/greenfield-cli/analyze-mgb.js
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${GFCLI_SESSIONS}/analyze-lsdj-sync.ts ${CMAKE_BINARY_DIR}/greenfield-cli/analyze-lsdj-sync.js
    COMMENT "Bundling greenfield CLI example sessions (esbuild)"
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
    COMMENT "Regenerating greenfield control-plane bundle (esbuild + bytecode)"
    VERBATIM)
add_dependencies(retroplug-cp-bundle ${_TJSC_DEP})

# The greenfield UI bundle: main.tsx → bytecode C array (React entry via build-ui.js). Derived; never
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
    COMMENT "Regenerating greenfield UI bundle (esbuild + bytecode)"
    VERBATIM)
add_dependencies(retroplug-ui-bundle ${_TJSC_DEP})

# --- retroplug.{vst3,clap} + the jack standalone: the DPF plugin hosting the greenfield
# Engine + control plane, with the React UI on the shared LVGL editor widget. Distinct identity from the
# legacy `retroplug` (they coexist).
dpf_add_plugin(retroplug
    TARGETS clap vst3 vst2 jack
    USE_FILE_BROWSER                    # native OS file dialog (matches DISTRHO_UI_FILE_BROWSER)
    FILES_DSP
        plugin/PluginDSP.cpp
        ${CP_BUNDLE_C}                # the embedded control-plane bytecode (rp_cp_bundle)
    FILES_UI
        plugin/PluginUI.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/input/GamepadManager.cpp  # SDL gamepad poll (shared UI-thread input; re-homed here from packages/native/src)
        ${UI_BUNDLE_C}                # the embedded React UI bytecode (rp_ui_bundle)
        ${DPFJS_PATH}/deps/dpf-widgets/generic/LVGL.cpp)  # the generic DPF↔LVGL base widget

# plugin/ FIRST so DPF resolves OUR DistrhoPluginInfo.h, not the legacy one that arrives transitively
# via retroplug-core's packages/native/src include. dpf-widgets/generic resolves "LVGL.hpp".
target_include_directories(retroplug BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/plugin)
target_include_directories(retroplug PUBLIC ${DPFJS_PATH}/deps/dpf-widgets/generic)
# GamepadManager.hpp lives in src/input/ (shared UI-thread input, re-homed from packages/native/src).
target_include_directories(retroplug PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/input)

target_link_libraries(retroplug PUBLIC
    retroplug-backend  # Engine/Backend + txiki host + (transitively) the cores + rpcpp
    lvgl-js-native                # LVGL + LvglJsEngine + lv_binding_js + lodepng (the editor)
    dpfjs::core)                  # DPF Plugin + UI base

target_compile_definitions(retroplug PUBLIC DPFJS_ENV_PREFIX="RETROPLUG_")

# The embedded bundles must exist before the plugin compiles them.
add_dependencies(retroplug retroplug-cp-bundle retroplug-ui-bundle)

# --- greenfield UI test harness --------------------------------------------------------------------
# retroplug-ui-test boots the greenfield React UI on a headless software LVGL display
# (RenderCore) driven by the BackendFacade RPC (UiHarness) — the greenfield twin of the
# legacy retroplug-ui-test. Only built on demand (not ALL). Reuses UI_BUNDLE_C + the ui-bundle target
# defined above the plugin.

add_executable(retroplug-ui-test
    test/ui/UiRunner.cpp
    test/ui/UiHarness.cpp
    test/ui/RenderCore.cpp
    ${UI_BUNDLE_C})               # the embedded greenfield UI bytecode (rp_ui_bundle)

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
