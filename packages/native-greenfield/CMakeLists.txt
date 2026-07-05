# native-greenfield-host: the native host for the greenfield TS app.
#
# Exposes the greenfield Backend to a TS bundle over
# globalThis[Symbol.for("plugin")].__rpcSend, and runs the greenfield test harness. The
# fs/config/codec half is std::filesystem + miniz; the emulator half is a real SameBoySystem
# in a real Project. It links `retroplug-cli-core` (Project + SystemBase + SameBoy/Mesen, plus
# transitive rpcpp + miniz and the packages/native/src include) — so no longer emulator-free.
# Output binary lands in build/bin/.

add_executable(native-greenfield-host
    src/main.cpp
    src/BackendRpcService.cpp
    src/ScriptCompiler.cpp   # bare-QuickJS bytecode compiler (DSP-side JS runtime)
    src/DspRuntime.cpp       # second bare QuickJS context — runs translator scripts
    ${DPFJS_PATH}/src/dpfjs/host/TjsHostRuntime.cpp  # shared txiki host (dpf.js)
)

target_include_directories(native-greenfield-host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # dpf.js src root for the shared host header ("dpfjs/host/TjsHostRuntime.hpp").
    ${DPFJS_PATH}/src
    # txiki.js / QuickJS runtime headers (tjs.h + internal private.h; quickjs.h transitive).
    ${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/src
    ${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/deps/quickjs
    # (packages/native/src for the Project/SystemBase headers + util/MinizZip.hpp, and the
    #  miniz header dir, arrive PUBLIC from retroplug-cli-core below.)
)

target_link_libraries(native-greenfield-host PRIVATE
    retroplug-cli-core  # Project + SystemBase + SameBoy/Mesen; PUBLIC-brings rpcpp + miniz
    tjs                 # txiki.js/QuickJS runtime (transitively brings qjs + libuv)
)

set_target_properties(native-greenfield-host PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
