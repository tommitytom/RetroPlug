# retroplug-cli: headless renderer + scripted-input runner.
#
# Reuses the DSP-side sources directly (Project.cpp + SameBoySystem.cpp);
# no DPF, no LVGL. Output binary lands in build/bin/.

# --- retroplug-cli-core: the emulator/Project + rpcpp HarnessRpcService surface.
# A static lib shared by retroplug-cli (the txiki host) and harness-schema-dump
# (the codegen schema dumper). Deliberately free of txiki/QuickJS so the schema
# dumper links without the JS runtime — that decoupling is what breaks the build
# cycle once retroplug-cli embeds a TS bundle compiled from this schema.
add_library(retroplug-cli-core STATIC
    HarnessRpcService.cpp
    ${PROJECT_SOURCE_DIR}/src/project/Project.cpp
    ${PROJECT_SOURCE_DIR}/src/lsdj/Effects.cpp
    ${PROJECT_SOURCE_DIR}/src/lsdj/KitUtil.cpp
    ${PROJECT_SOURCE_DIR}/src/lsdj/SampleCache.cpp
    ${PROJECT_SOURCE_DIR}/src/lsdj/KitCompiler.cpp
    ${PROJECT_SOURCE_DIR}/src/lsdj/codec/SongCodec.cpp
    ${PROJECT_SOURCE_DIR}/src/lsdj/codec/Compression.cpp
    ${PROJECT_SOURCE_DIR}/src/lsdj/codec/SavCodec.cpp
    ${PROJECT_SOURCE_DIR}/src/system/SystemBase.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/LinkGroup.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/RomSniffer.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/SameBoySystem.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/roles/ArduinoboyMaster.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/roles/LsdjSyncRole.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/roles/LsdjKitPatchRole.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/roles/MgbPassthroughRole.cpp
    ${PROJECT_SOURCE_DIR}/src/system/mesen/MesenNesSystem.cpp
    ${PROJECT_SOURCE_DIR}/src/system/mesen/MesenNesDebugSession.cpp
    ${PROJECT_SOURCE_DIR}/src/system/mesen/MesenVideoDevice.cpp
    ${PROJECT_SOURCE_DIR}/src/system/mesen/MesenGbaSystem.cpp
    ${PROJECT_SOURCE_DIR}/src/system/mesen/roles/NesN8MidiRole.cpp
    # lodepng (vendored under lv_binding_js). Compiled directly so the CLI
    # doesn't have to pull in the full lvgl-js-native static lib for one
    # PNG encoder.
    ${LVGL_JS_NATIVE_DIR}/core/img/png/lodepng.cpp
)

target_include_directories(retroplug-cli-core PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    # `native/core/img/png/lodepng.h` lives under this root.
    ${PROJECT_SOURCE_DIR}/deps/lv_binding_js/src/render
    # MinizZip.hpp -> miniz.h (txiki's v3.1.0; matches the linked `miniz` lib)
    ${PROJECT_SOURCE_DIR}/deps/lv_binding_js/deps/txiki/deps/miniz
)

target_link_libraries(retroplug-cli-core PUBLIC
    sameboy
    mesen
    rpcpp     # transitively pulls reflectcpp
    r8brain   # used by KitUtil for resampling
    enkiTS    # used by KitCompiler for per-sample parallel compile
    miniaudio # header-only audio decoder used by SampleCache
    miniz     # used by projectConfigToZip for --save-rplg
)

set_target_properties(retroplug-cli-core PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# --- retroplug-cli: the headless renderer + embedded QuickJS test harness.
add_executable(retroplug-cli
    main.cpp
    TestHarness.cpp  # embedded QuickJS TypeScript test harness (--test)
    ${PROJECT_SOURCE_DIR}/src/host/TjsHostRuntime.cpp  # shared txiki host
)

target_include_directories(retroplug-cli PRIVATE
    # txiki.js / QuickJS runtime headers for the embedded JS test harness.
    # `tjs.h` + internal `private.h` (TJS_EvalModuleContent / TJS_GetLoop /
    # tjs__execute_jobs); `quickjs.h` arrives transitively via the `tjs` link
    # but is listed for clarity.
    ${PROJECT_SOURCE_DIR}/deps/lv_binding_js/deps/txiki/src
    ${PROJECT_SOURCE_DIR}/deps/lv_binding_js/deps/txiki/deps/quickjs
)

target_link_libraries(retroplug-cli PRIVATE
    retroplug-cli-core
    tjs       # txiki.js/QuickJS runtime for the embedded TS test harness
              # (transitively brings qjs + libuv)
)

set_target_properties(retroplug-cli PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- harness-schema-dump: prints HarnessRpcService's OpenRPC schema for codegen.
# No txiki link — that's the whole point (see retroplug-cli-core above). Mirrors
# the plugin's rpc-schema-dump.
add_executable(harness-schema-dump HarnessSchemaDump.cpp)
target_link_libraries(harness-schema-dump PRIVATE retroplug-cli-core)
set_target_properties(harness-schema-dump PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
