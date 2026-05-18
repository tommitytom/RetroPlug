# retroplug-cli: headless renderer + scripted-input runner.
#
# Reuses the DSP-side sources directly (Project.cpp + SameBoySystem.cpp);
# no DPF, no LVGL, no JS. Output binary lands in build/bin/.

add_executable(retroplug-cli
    main.cpp
    ${PROJECT_SOURCE_DIR}/src/project/Project.cpp
    ${PROJECT_SOURCE_DIR}/src/lsdj/Effects.cpp
    ${PROJECT_SOURCE_DIR}/src/lsdj/KitUtil.cpp
    ${PROJECT_SOURCE_DIR}/src/lsdj/SampleCache.cpp
    ${PROJECT_SOURCE_DIR}/src/lsdj/KitCompiler.cpp
    ${PROJECT_SOURCE_DIR}/src/system/SystemBase.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/LinkGroup.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/RomSniffer.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/SameBoySystem.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/roles/ArduinoboyMaster.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/roles/LsdjSyncRole.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/roles/LsdjKitPatchRole.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/roles/MgbPassthroughRole.cpp
    ${PROJECT_SOURCE_DIR}/src/system/mesen/MesenSystem.cpp
    ${PROJECT_SOURCE_DIR}/src/system/mesen/MesenVideoDevice.cpp
    ${PROJECT_SOURCE_DIR}/src/system/mesen/GbaSystem.cpp
    ${PROJECT_SOURCE_DIR}/src/system/mesen/roles/NesN8MidiRole.cpp
    # lodepng (vendored under lv_binding_js). Compiled directly so the CLI
    # doesn't have to pull in the full lvgl-js-native static lib for one
    # PNG encoder.
    ${LVGL_JS_NATIVE_DIR}/core/img/png/lodepng.cpp
)

target_include_directories(retroplug-cli PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    # `native/core/img/png/lodepng.h` lives under this root.
    ${PROJECT_SOURCE_DIR}/deps/lv_binding_js/src/render
)

target_link_libraries(retroplug-cli PRIVATE
    sameboy
    mesen
    rpcpp     # transitively pulls reflectcpp
    r8brain   # used by KitUtil for resampling
    enkiTS    # used by KitCompiler for per-sample parallel compile
    miniaudio # header-only audio decoder used by SampleCache
)

set_target_properties(retroplug-cli PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
