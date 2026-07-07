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
    src/NesBackend.cpp            # NES builder (Mesen)
    src/GbaBackend.cpp            # GBA builder (Mesen, HLE BIOS)
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
)

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
