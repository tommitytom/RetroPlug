# native-greenfield-host: the native host for the greenfield TS app.
#
# Exposes the greenfield Backend to a TS bundle over
# globalThis[Symbol.for("plugin")].__rpcSend, and runs the greenfield test harness. The
# fs/config/codec half is std::filesystem + miniz; the emulator half is a real SameBoySystem
# in a real Project. It links `retroplug-cli-core` (Project + SystemBase + SameBoy/Mesen, plus
# transitive rpcpp + miniz and the packages/native/src include) — so no longer emulator-free.
# Output binary lands in build/bin/.

# retroplug-greenfield-backend: the greenfield Backend as a static library — everything except a host
# entry point, so both the test host (native-greenfield-host) and the DPF plugin compose the SAME
# Engine/Backend/DspRuntime. Includes + links are PUBLIC so consumers inherit them.
add_library(retroplug-greenfield-backend STATIC
    src/BackendFacade.cpp         # thin facade: owns the shared state, forwards to the 3 services
    src/HostRpcService.cpp        # fs / config / codec / sav (pure, stateless)
    src/EngineRpcService.cpp      # emulator surface over Engine + Invoker + Factory
    src/AudioDriverRpcService.cpp # background audio thread + observation atomics
    src/Engine.cpp                # single-threaded core: owns Project + DspRuntime, runs blocks
    src/EngineInvoker.cpp         # Direct/Queued invokers — the only threading-aware layer
    src/SystemFactory.cpp         # backend-agnostic build registry (the one build path)
    src/SameBoyBackend.cpp        # SameBoy builder — the only place SameBoyConfig is constructed
    src/MesenBackend.cpp          # Mesen builder — NES + GBA (HLE BIOS), dispatched on platform
    src/ScriptCompiler.cpp        # bare-QuickJS bytecode compiler (DSP-side JS runtime)
    src/DspRuntime.cpp            # second bare QuickJS context — runs translator scripts
    ${DPFJS_PATH}/src/dpfjs/host/TjsHostRuntime.cpp  # shared txiki host (dpf.js)
)

target_include_directories(retroplug-greenfield-backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # dpf.js src root for the shared host header ("dpfjs/host/TjsHostRuntime.hpp").
    ${DPFJS_PATH}/src
    # txiki.js / QuickJS runtime headers (tjs.h + internal private.h; quickjs.h transitive).
    ${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/src
    ${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/deps/quickjs
    # (packages/native/src for the Project/SystemBase headers + util/MinizZip.hpp, and the
    #  miniz header dir, arrive PUBLIC from retroplug-cli-core below.)
)

target_link_libraries(retroplug-greenfield-backend PUBLIC
    retroplug-cli-core  # Project + SystemBase + SameBoy/Mesen; PUBLIC-brings rpcpp + miniz
    tjs                 # txiki.js/QuickJS runtime (transitively brings qjs + libuv)
)

set_target_properties(retroplug-greenfield-backend PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON  # linked into the shared plugin module (below)
)

# The plugin is a shared module, so its static deps must be PIC. The CLI core lib is shared with the
# test host + CLI (executables that don't need PIC); make it position-independent so it can also link
# into the plugin. The emulator cores (sameboy/mesen/…) already set PIC themselves.
set_target_properties(retroplug-cli-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# native-greenfield-host: the test host — the backend lib + a main() that evals a TS bundle over the
# __rpcSend bridge. Output binary lands in build/bin/.
add_executable(native-greenfield-host src/main.cpp)

target_link_libraries(native-greenfield-host PRIVATE retroplug-greenfield-backend)

set_target_properties(native-greenfield-host PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- control-plane bundle: pluginControlPlane.ts → bytecode C array (mirrors ui-regenerate). The DPF
# plugin evals this at construct to compose the stores + DSP kernel. Derived; never committed.
set(GFCP_BUNDLE_JS "${CMAKE_BINARY_DIR}/native-greenfield/cp-bundle.js")
set(GFCP_BUNDLE_C  "${CMAKE_BINARY_DIR}/native-greenfield/cp-bundle_data.c")
set_source_files_properties(${GFCP_BUNDLE_C} PROPERTIES GENERATED TRUE)  # produced before the plugin compiles

add_custom_target(retroplug-greenfield-cp-bundle ALL
    BYPRODUCTS ${GFCP_BUNDLE_JS} ${GFCP_BUNDLE_C} ${GFCP_BUNDLE_C}.new
    COMMAND ${NODE_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/build-greenfield-controlplane.js ${GFCP_BUNDLE_JS}
    COMMAND ${_TJSC_COMMAND} -m -s -p gfcp_ -o ${GFCP_BUNDLE_C}.new ${GFCP_BUNDLE_JS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GFCP_BUNDLE_C}.new ${GFCP_BUNDLE_C}
    COMMENT "Regenerating greenfield control-plane bundle (esbuild + bytecode)"
    VERBATIM)
add_dependencies(retroplug-greenfield-cp-bundle ${_TJSC_DEP})

# --- retroplug-greenfield.{vst3,clap} + the jack standalone: the UI-less DPF plugin hosting the
# greenfield Engine + control plane. Distinct identity from the legacy `retroplug` (they coexist).
dpf_add_plugin(retroplug-greenfield
    TARGETS clap vst3 jack
    FILES_DSP
        plugin/PluginGreenfieldDSP.cpp
        ${GFCP_BUNDLE_C})               # the embedded control-plane bytecode (gfcp_cp_bundle)

# plugin/ FIRST so DPF resolves OUR DistrhoPluginInfo.h, not the legacy one that arrives transitively
# via retroplug-cli-core's packages/native/src include.
target_include_directories(retroplug-greenfield BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/plugin)

target_link_libraries(retroplug-greenfield PUBLIC
    retroplug-greenfield-backend  # Engine/Backend + txiki host + (transitively) the cores + rpcpp
    dpfjs::core)                   # DPF Plugin base

target_compile_definitions(retroplug-greenfield PUBLIC DPFJS_ENV_PREFIX="RETROPLUG_")

# The embedded bundle must exist before the plugin compiles it.
add_dependencies(retroplug-greenfield retroplug-greenfield-cp-bundle)

# --- greenfield UI test harness --------------------------------------------------------------------
# retroplug-greenfield-ui-test boots the greenfield React UI on a headless software LVGL display
# (RenderCore) driven by the BackendFacade RPC (GreenfieldUiHarness) — the greenfield twin of the
# legacy retroplug-ui-test. Only built on demand (not ALL).

# The greenfield UI bundle: main.tsx → bytecode C array (mirrors retroplug-greenfield-cp-bundle, but
# a React entry via build-ui.js). Derived; never committed.
set(GFUI_BUNDLE_JS "${CMAKE_BINARY_DIR}/native-greenfield/ui-bundle.js")
set(GFUI_BUNDLE_C  "${CMAKE_BINARY_DIR}/native-greenfield/ui-bundle_data.c")
set_source_files_properties(${GFUI_BUNDLE_C} PROPERTIES GENERATED TRUE)

add_custom_target(retroplug-greenfield-ui-bundle
    BYPRODUCTS ${GFUI_BUNDLE_JS} ${GFUI_BUNDLE_C} ${GFUI_BUNDLE_C}.new
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-ui.js
            ${GFUI_BUNDLE_JS} ${GFUI_BUNDLE_JS}.d
            ${CMAKE_SOURCE_DIR}/packages/retroplug-greenfield/ui/main.tsx
    COMMAND ${_TJSC_COMMAND} -m -s -p gfui_ -o ${GFUI_BUNDLE_C}.new ${GFUI_BUNDLE_JS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GFUI_BUNDLE_C}.new ${GFUI_BUNDLE_C}
    COMMENT "Regenerating greenfield UI bundle (esbuild + bytecode)"
    VERBATIM)
add_dependencies(retroplug-greenfield-ui-bundle ${_TJSC_DEP})

add_executable(retroplug-greenfield-ui-test
    test/ui/GreenfieldUiRunner.cpp
    test/ui/GreenfieldUiHarness.cpp
    test/ui/RenderCore.cpp
    ${GFUI_BUNDLE_C})               # the embedded greenfield UI bytecode (gfui_ui_bundle)

target_link_libraries(retroplug-greenfield-ui-test PRIVATE
    lvgl-js-native                # LvglJsEngine + lvgl + lv_binding_js native + lodepng (+ tjs)
    retroplug-greenfield-backend) # BackendFacade + rpcpp + the cores + the txiki host

target_include_directories(retroplug-greenfield-ui-test PRIVATE
    # quickjs.h for the JS trampolines in RenderCore/GreenfieldUiRunner (lvgl-js-native
    # PUBLIC-propagates the lv_binding_js render include dir, so "native/..." resolves transitively).
    ${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/deps/quickjs)

set_target_properties(retroplug-greenfield-ui-test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# The embedded bundle must exist before the test binary compiles it.
add_dependencies(retroplug-greenfield-ui-test retroplug-greenfield-ui-bundle)
