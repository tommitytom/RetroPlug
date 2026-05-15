# retroplug-cli: headless renderer + scripted-input runner.
#
# Reuses the DSP-side sources directly (Project.cpp + SameBoySystem.cpp);
# no DPF, no LVGL, no JS. Output binary lands in build/bin/.

add_executable(retroplug-cli
    main.cpp
    ${PROJECT_SOURCE_DIR}/src/project/Project.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/LinkGroup.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/RomSniffer.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/SameBoySystem.cpp
    ${PROJECT_SOURCE_DIR}/src/system/sameboy/roles/MgbPassthroughRole.cpp
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
    rpcpp     # transitively pulls reflectcpp
)

set_target_properties(retroplug-cli PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
