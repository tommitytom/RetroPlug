# native-greenfield-host: the clean, emulator-free native host for the greenfield TS app.
#
# Exposes the greenfield Backend (fs/config/codec) to a TS bundle over
# globalThis[Symbol.for("plugin")].__rpcSend, and runs the greenfield test harness. Links
# only the shared txiki host + rpcpp + miniz — deliberately NO Project/SystemBase/emulator
# (that arrives in increment 2). Output binary lands in build/bin/.

add_executable(native-greenfield-host
    src/main.cpp
    src/BackendRpcService.cpp
    ${DPFJS_PATH}/src/dpfjs/host/TjsHostRuntime.cpp  # shared txiki host (dpf.js)
)

target_include_directories(native-greenfield-host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # dpf.js src root for the shared host header ("dpfjs/host/TjsHostRuntime.hpp").
    ${DPFJS_PATH}/src
    # txiki.js / QuickJS runtime headers (tjs.h + internal private.h; quickjs.h transitive).
    ${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/src
    ${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/deps/quickjs
    # native src root for util/MinizZip.hpp
    ${PROJECT_SOURCE_DIR}/packages/native/src
    # MinizZip.hpp -> miniz.h (txiki's vendored copy; matches the linked `miniz` lib)
    ${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/deps/miniz
)

target_link_libraries(native-greenfield-host PRIVATE
    tjs     # txiki.js/QuickJS runtime (transitively brings qjs + libuv)
    rpcpp   # TypedRpcServer + QuickJS codec/transport (transitively pulls reflectcpp)
    miniz   # zip/unzip (BackendRpcService::zip/unzip)
)

set_target_properties(native-greenfield-host PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
