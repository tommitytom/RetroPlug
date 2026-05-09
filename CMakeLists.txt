# DISTRHO Plugin Framework (DPF)
# Copyright (C) 2021-2024 Filipe Coelho <falktx@falktx.com>
# SPDX-License-Identifier: ISC

cmake_minimum_required(VERSION 3.14...3.31)

# setup project
set(CMAKE_CXX_STANDARD 20)
set(NAME retroplug)
project(${NAME})

# tell LVGL where to find its config file
set(LV_BUILD_CONF_PATH "${PROJECT_SOURCE_DIR}/src/lv_conf.h" CACHE PATH "")

# include DPF
add_subdirectory(deps/dpf)

# Use LVGL from lv_binding_js (single source of truth)
add_subdirectory(deps/lv_binding_js/deps/lvgl)

# --- txiki.js (QuickJS + libuv + libwebsockets + ffi runtime) ---
set(BUILD_WITH_MIMALLOC OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/lv_binding_js/deps/txiki EXCLUDE_FROM_ALL)

# --- rpcpp: header-only JSON-RPC 2.0 server, used by PluginJsBridge ---
add_subdirectory(deps/rpcpp)

# --- SameBoy: Game Boy emulator core, vendored under deps/sameboy ---
add_subdirectory(deps/sameboy)

# --- lvgl-js-native: the native C++ component bridge ---
set(LVGL_JS_NATIVE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/lv_binding_js/src/render/native)
add_library(lvgl-js-native STATIC
    ${LVGL_JS_NATIVE_DIR}/core/basic/comp.cpp
    ${LVGL_JS_NATIVE_DIR}/core/style/style.cpp
    ${LVGL_JS_NATIVE_DIR}/core/style/font/font.cpp
    ${LVGL_JS_NATIVE_DIR}/core/event/event.cpp
    ${LVGL_JS_NATIVE_DIR}/core/event/click/click.cpp
    ${LVGL_JS_NATIVE_DIR}/core/event/normal/normal.cpp
    ${LVGL_JS_NATIVE_DIR}/core/event/select/select.cpp
    ${LVGL_JS_NATIVE_DIR}/core/event/value_change/value_change.cpp
    ${LVGL_JS_NATIVE_DIR}/core/event/key/key.cpp
    ${LVGL_JS_NATIVE_DIR}/core/refresh/refresh.cpp
    ${LVGL_JS_NATIVE_DIR}/core/utils/utils.cpp
    ${LVGL_JS_NATIVE_DIR}/core/theme/theme.cpp
    ${LVGL_JS_NATIVE_DIR}/core/dimensions/dimensions.cpp
    ${LVGL_JS_NATIVE_DIR}/core/animate/animate.cpp
    ${LVGL_JS_NATIVE_DIR}/core/group/group.cpp
    ${LVGL_JS_NATIVE_DIR}/core/img/img.cpp
    ${LVGL_JS_NATIVE_DIR}/core/img/png/png.cpp
    ${LVGL_JS_NATIVE_DIR}/core/img/png/lodepng.cpp
    ${LVGL_JS_NATIVE_DIR}/core/img/gif/gif.cpp
    ${LVGL_JS_NATIVE_DIR}/components/component.cpp
    ${LVGL_JS_NATIVE_DIR}/components/view/view.cpp
    ${LVGL_JS_NATIVE_DIR}/components/view/view_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/text/text.cpp
    ${LVGL_JS_NATIVE_DIR}/components/text/text_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/button/button.cpp
    ${LVGL_JS_NATIVE_DIR}/components/button/button_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/image/image.cpp
    ${LVGL_JS_NATIVE_DIR}/components/image/image_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/slider/slider.cpp
    ${LVGL_JS_NATIVE_DIR}/components/slider/slider_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/switch/switch.cpp
    ${LVGL_JS_NATIVE_DIR}/components/switch/switch_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/arc/arc.cpp
    ${LVGL_JS_NATIVE_DIR}/components/arc/arc_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/checkbox/checkbox.cpp
    ${LVGL_JS_NATIVE_DIR}/components/checkbox/checkbox_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/textarea/textarea.cpp
    ${LVGL_JS_NATIVE_DIR}/components/textarea/textarea_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/keyboard/keyboard.cpp
    ${LVGL_JS_NATIVE_DIR}/components/keyboard/keyboard_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/dropdownlist/dropdownlist.cpp
    ${LVGL_JS_NATIVE_DIR}/components/dropdownlist/dropdownlist_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/progressbar/progressbar.cpp
    ${LVGL_JS_NATIVE_DIR}/components/progressbar/progressbar_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/roller/roller.cpp
    ${LVGL_JS_NATIVE_DIR}/components/roller/roller_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/line/line.cpp
    ${LVGL_JS_NATIVE_DIR}/components/line/line_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/calendar/calendar.cpp
    ${LVGL_JS_NATIVE_DIR}/components/calendar/calendar_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/chart/chart.cpp
    ${LVGL_JS_NATIVE_DIR}/components/chart/chart_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/tabview/tabview.cpp
    ${LVGL_JS_NATIVE_DIR}/components/tabview/tabview_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/gif/gif.cpp
    ${LVGL_JS_NATIVE_DIR}/components/gif/gif_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/mask/mask.cpp
    ${LVGL_JS_NATIVE_DIR}/components/mask/mask_wrap.cpp
    ${LVGL_JS_NATIVE_DIR}/components/window/window.cpp
    ${LVGL_JS_NATIVE_DIR}/bootstrap/render_bootstrap.cpp
    # Engine wrapper (replaces lv_binding_js/src/engine/engine.cpp)
    src/LvglJsEngine.cpp
)

target_include_directories(lvgl-js-native PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/lv_binding_js/src/render  # for "native/..." includes
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/lv_binding_js/src/engine  # for "hal/hal.hpp" etc
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/lv_binding_js/src         # top-level src includes
)
target_link_libraries(lvgl-js-native PUBLIC tjs lvgl::lvgl)
set_target_properties(lvgl-js-native PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 14
)

# Ensure all static libs that end up in shared plugin binaries have PIC
set_target_properties(lvgl PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(tjs PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(qjs PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(uv_a PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(vmlib PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(websockets PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(mbedtls PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(mbedx509 PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(mbedcrypto PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(ada PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(reflectcpp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- UI bundle: esbuild-bundled TS/TSX, embedded into the plugin as a byte array ---
find_program(NODE_EXECUTABLE node REQUIRED)

set(UI_BUNDLE_JS  "${CMAKE_BINARY_DIR}/ui/bundle.js")
set(UI_BUNDLE_C   "${CMAKE_BINARY_DIR}/ui/bundle_data.c")
set(UI_BUNDLE_DEP "${CMAKE_BINARY_DIR}/ui/bundle.d")

file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/ui/*.ts"
    "${CMAKE_SOURCE_DIR}/ui/*.tsx"
    "${CMAKE_SOURCE_DIR}/runtime/*.ts"
    "${CMAKE_SOURCE_DIR}/runtime/*.tsx")

add_custom_command(
    OUTPUT  ${UI_BUNDLE_JS} ${UI_BUNDLE_C}
    COMMAND ${NODE_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/build-ui.js
            ${UI_BUNDLE_JS} ${UI_BUNDLE_C} ${UI_BUNDLE_DEP}
    DEPENDS ${UI_SOURCES} ${CMAKE_SOURCE_DIR}/tools/build-ui.js
    DEPFILE ${UI_BUNDLE_DEP}
    COMMENT "Building UI bundle"
    VERBATIM)

# create plugin build target
dpf_add_plugin(${NAME}
  TARGETS au clap jack lv2 vst2 vst3
  FILES_DSP
      src/PluginDSP.cpp
      src/project/Project.cpp
      src/system/sameboy/SameBoySystem.cpp
  FILES_UI
      src/PluginUI.cpp
      src/PluginJsBridge.cpp
      ${UI_BUNDLE_C}
      deps/dpf-widgets/generic/LVGL.cpp)

# setup build includes and libraries
target_include_directories(${NAME} PUBLIC src)
target_include_directories(${NAME} PUBLIC deps/dpf-widgets/generic)
target_link_libraries(${NAME} PUBLIC lvgl::lvgl $<$<BOOL:${WIN32}>:winmm>)
target_link_libraries(${NAME} PUBLIC lvgl-js-native)
target_link_libraries(${NAME} PUBLIC rpcpp)
target_link_libraries(${NAME} PUBLIC sameboy)

# NOTICE you probably do not want this on your final plugin
target_link_libraries(${NAME} PUBLIC lvgl::demos)
