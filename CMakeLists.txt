# DISTRHO Plugin Framework (DPF)
# Copyright (C) 2021-2024 Filipe Coelho <falktx@falktx.com>
# SPDX-License-Identifier: ISC

cmake_minimum_required(VERSION 3.14...3.31)

# Default to Release when the user hasn't asked for a specific build type.
# Without this the project compiles at -O0 (no opt flags), which makes the
# DSP path 3-4x slower than necessary and the standalone visibly chug. Set
# before project() so the cache value is in scope by the time subprojects
# pick up CMAKE_BUILD_TYPE.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type (Release, Debug, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

# setup project
set(CMAKE_CXX_STANDARD 20)
set(NAME retroplug)
project(${NAME})

# Force a non-executable stack on Linux ELF outputs. Without this, one
# unmarked assembly object in WAMR (txiki's wasm runtime —
# invokeNative_em64_simd.s) leaks the "needs executable stack" attribute
# into every plugin .so we link. Renoise refuses to dlopen such modules
# with "cannot enable executable stack as shared object requires". The
# linker warning we've been seeing on every build is the same root cause.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_link_options(-Wl,-z,noexecstack)
endif()

# tell LVGL where to find its config file
set(LV_BUILD_CONF_PATH "${PROJECT_SOURCE_DIR}/src/lv_conf.h" CACHE PATH "")

# include DPF
add_subdirectory(deps/dpf)

# --- SDL2: used for gamepad/game-controller input across every plugin
# variant (and the standalone). DPF's own SDL2 detection is gated to the
# JACK target only (audio backend fallback), so we find SDL2 ourselves at
# the parent scope and link it into every variant after dpf_add_plugin.
# Only SDL_INIT_GAMECONTROLLER is initialised — see src/GamepadManager.cpp.
find_package(PkgConfig REQUIRED)
if(APPLE)
    # SDL2.framework from /Library/Frameworks. find_library picks the right
    # arch slice for each value of CMAKE_OSX_ARCHITECTURES automatically,
    # so the same universal framework satisfies both single-arch and
    # universal builds without per-arch wiring.
    find_library(SDL2_FRAMEWORK SDL2 REQUIRED)
    add_library(PkgConfig::SDL2 INTERFACE IMPORTED)
    target_link_libraries(PkgConfig::SDL2 INTERFACE ${SDL2_FRAMEWORK})
    target_include_directories(PkgConfig::SDL2 INTERFACE
        ${SDL2_FRAMEWORK}/Headers)
else()
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
endif()

# Use LVGL from lv_binding_js (single source of truth)
add_subdirectory(deps/lv_binding_js/deps/lvgl)

# --- txiki.js (QuickJS + libuv + libwebsockets + ffi runtime) ---
set(BUILD_WITH_MIMALLOC OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/lv_binding_js/deps/txiki EXCLUDE_FROM_ALL)

# --- msgpack-c: C library for the msgpack codec used by rpcpp/reflect-cpp.
# Vendored + statically linked so plugin binaries don't depend on a system
# libmsgpackc.so / msgpack-c.dll the host won't have. Upstream's CMakeLists
# creates a single `msgpack-c` target whose type follows BUILD_SHARED_LIBS;
# we're a STATIC build, so this lands as a static archive. Must be added
# before deps/rpcpp so reflect-cpp's `if (NOT TARGET msgpack-c)` short-
# circuits its find_package call and links our vendored target instead.
set(MSGPACK_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(MSGPACK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MSGPACK_ENABLE_SHARED  OFF CACHE BOOL "" FORCE)
set(MSGPACK_ENABLE_STATIC  ON  CACHE BOOL "" FORCE)
add_subdirectory(deps/msgpack-c EXCLUDE_FROM_ALL)
# Static lib ends up inside shared plugin .so / .dylib outputs — needs PIC.
# No-op on Windows (PE format is always position-independent).
set_target_properties(msgpack-c PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- rpcpp: header-only JSON-RPC 2.0 server, used by PluginJsBridge ---
set(RPCPP_BUILD_MSGPACK ON CACHE BOOL "" FORCE)
add_subdirectory(deps/rpcpp)

# --- SameBoy: Game Boy emulator core (submodule at deps/sameboy) ---
include(cmake/sameboy.cmake)

# --- Mesen: NES emulator core (multi-system upstream; we use the NES path) ---
add_subdirectory(deps/mesen)

# --- r8brain-free: high-quality SRC for LSDJ kit compilation.
# Also used for non-44.1 kHz host-rate sync accuracy.
add_subdirectory(deps/r8brain)

# --- enkiTS: task scheduler. Drives per-sample parallel work in the LSDJ
# kit compiler so a 16-sample kit can recompile across cores instead of
# blocking the UI rpcpp thread on a serial loop.
add_subdirectory(deps/enkiTS)

# --- miniaudio: single-header audio file decoder (WAV / MP3 / FLAC).
# Used by SampleCache to load source samples for kit compilation.
# Header-only — no library, just an include path.
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/deps/miniaudio)

# --- efsw: cross-platform file watcher (inotify / FSEvents / Win32) used by
# src/config/UserConfig to live-reload key/pad bindings JSON. Built as a single
# static lib (BUILD_SHARED_LIBS=OFF) and the duplicate efsw-static target is
# suppressed (BUILD_STATIC_LIBS=OFF). EFSW_INSTALL / BUILD_TEST_APP default to
# OFF when efsw is added as a subdirectory (not the main project).
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build efsw as static" FORCE)
set(BUILD_STATIC_LIBS OFF CACHE BOOL "Skip duplicate efsw-static target" FORCE)
add_subdirectory(deps/efsw EXCLUDE_FROM_ALL)

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
    ${LVGL_JS_NATIVE_DIR}/components/canvas/canvas.cpp
    ${LVGL_JS_NATIVE_DIR}/components/canvas/canvas_wrap.cpp
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
set_target_properties(efsw PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- rpc-schema-dump: prints PluginRpcService's OpenRPC schema for codegen ---
# Standalone executable so the codegen step doesn't need to start the plugin
# (or wire up the rest of its dependency graph). Constructs the service with
# all-null state; the schema generator only inspects reflect-cpp's
# compile-time type info, no methods are actually called.
add_executable(rpc-schema-dump
    src/RpcSchemaDump.cpp
    src/PluginRpcService.cpp
    src/config/UserConfig.cpp
    src/config/UserConfigPaths.cpp
    src/config/RecentFiles.cpp
    src/project/Project.cpp
    src/lsdj/Effects.cpp
    src/lsdj/KitUtil.cpp
    src/lsdj/SampleCache.cpp
    src/lsdj/KitCompiler.cpp
    src/system/SystemBase.cpp
    src/system/sameboy/LinkGroup.cpp
    src/system/sameboy/RomSniffer.cpp
    src/system/sameboy/SameBoySystem.cpp
    src/system/sameboy/roles/ArduinoboyMaster.cpp
    src/system/sameboy/roles/LsdjSyncRole.cpp
    src/system/sameboy/roles/LsdjKitPatchRole.cpp
    src/system/sameboy/roles/MgbPassthroughRole.cpp
    src/system/mesen/MesenNesSystem.cpp
    src/system/mesen/MesenNesDebugSession.cpp
    src/system/mesen/MesenVideoDevice.cpp
    src/system/mesen/MesenGbaSystem.cpp
    src/system/mesen/roles/NesN8MidiRole.cpp)
target_include_directories(rpc-schema-dump PRIVATE src)
target_include_directories(rpc-schema-dump PRIVATE deps/lv_binding_js/deps/txiki/deps/miniz)
target_link_libraries(rpc-schema-dump PRIVATE rpcpp sameboy mesen r8brain enkiTS miniaudio efsw miniz)

# --- sav-schema-dump: prints the LSDj Sav model's JSON Schema for zod/TS gen ---
# Header-only model + reflect-cpp (via rpcpp); no codec sources needed since
# to_schema only inspects the struct definitions.
add_executable(sav-schema-dump tools/SavSchemaDump.cpp)
target_include_directories(sav-schema-dump PRIVATE src)
target_link_libraries(sav-schema-dump PRIVATE rpcpp)
set_target_properties(sav-schema-dump PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# --- UI bundle: esbuild-bundled TS/TSX, embedded into the plugin as a byte array ---
find_program(NODE_EXECUTABLE node REQUIRED)

# --- sav-regenerate: emit zod + TS for the LSDj Sav model (on demand) ---
# Derived artifacts (build/ui/generated/SavSchema.ts + SavTypes.ts); never
# committed. Not part of ALL — invoked via `make sav-regenerate` (and wired into
# the UI build later, when the UI consumes the schema).
add_custom_target(sav-regenerate
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/gen-sav-ts.js
            $<TARGET_FILE:sav-schema-dump> ${CMAKE_BINARY_DIR}/ui/generated
    DEPENDS sav-schema-dump
    BYPRODUCTS ${CMAKE_BINARY_DIR}/ui/generated/SavSchema.ts
               ${CMAKE_BINARY_DIR}/ui/generated/SavTypes.ts
    COMMENT "Regenerating LSDj Sav zod/TS schema")

set(UI_BUNDLE_JS  "${CMAKE_BINARY_DIR}/ui/bundle.js")
set(UI_BUNDLE_C   "${CMAKE_BINARY_DIR}/ui/bundle_data.c")
set(UI_BUNDLE_DEP "${CMAKE_BINARY_DIR}/ui/bundle.d")
set(UI_RPC_TS     "${CMAKE_BINARY_DIR}/ui/generated/PluginService.ts")

# Cross-compile knob for tjsc (bytecode compiler). Set BEFORE the
# ui-regenerate target so the COMMAND substitution resolves at the right
# expansion time.
set(TJSC_EXECUTABLE "" CACHE FILEPATH
    "Path to a host-built tjsc/qjsc for bytecode compilation. \
Leave empty to use the in-tree tjsc target.")
if(CMAKE_CROSSCOMPILING AND NOT TJSC_EXECUTABLE)
    message(FATAL_ERROR "Cross-compiling: set -DTJSC_EXECUTABLE=/path/to/host-tjsc")
endif()
if(TJSC_EXECUTABLE)
    set(_TJSC_COMMAND "${TJSC_EXECUTABLE}")
    set(_TJSC_DEP "${TJSC_EXECUTABLE}")
else()
    set(_TJSC_COMMAND "$<TARGET_FILE:tjsc>")
    set(_TJSC_DEP tjsc)
endif()

# UI regeneration chain — always runs, every build.
#
# We deliberately don't use timestamp-tracked add_custom_command(OUTPUT ...)
# rules here. The three steps below (schema → TS client, TSX → bundle.js,
# bundle.js → bytecode) have a long history of going stale: new TSX files
# need a reconfigure to enter the glob, the rpc-schema-dump dependency
# doesn't always propagate through CMake's timestamp comparison, and
# even when bundle_data.c does refresh the per-variant .o sometimes lags.
# Symptoms: builds linking against last week's RPC surface.
#
# Trading those failure modes for ~1–2 s of unconditional regen each
# build is the user-stated preference. Incrementalism is preserved at
# the C compile step via `copy_if_different`: tjsc writes to .new, we
# only refresh bundle_data.c if the bytes actually changed, so the .o
# only rebuilds on real content changes.
#
# tools/gen-rpc-ts.js     → ${UI_RPC_TS}
# tools/build-ui.js       → ${UI_BUNDLE_JS}   (esbuild)
# tjsc                    → ${UI_BUNDLE_C}.new
# copy_if_different       → ${UI_BUNDLE_C}    (guards .o rebuild)
add_custom_target(ui-regenerate ALL
    BYPRODUCTS ${UI_RPC_TS}
               ${UI_BUNDLE_JS}
               ${UI_BUNDLE_DEP}
               ${UI_BUNDLE_C}
               ${UI_BUNDLE_C}.new
    COMMAND ${NODE_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/gen-rpc-ts.js
            $<TARGET_FILE:rpc-schema-dump>
            ${UI_RPC_TS}
    COMMAND ${NODE_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/build-ui.js
            ${UI_BUNDLE_JS} ${UI_BUNDLE_DEP}
    COMMAND ${_TJSC_COMMAND}
            -m -s -p ui_
            -o ${UI_BUNDLE_C}.new
            ${UI_BUNDLE_JS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${UI_BUNDLE_C}.new ${UI_BUNDLE_C}
    COMMENT "Regenerating UI artifacts (RPC client + bundle + bytecode)"
    VERBATIM)
add_dependencies(ui-regenerate rpc-schema-dump ${_TJSC_DEP})

# Declare bundle_data.c as a generated source so dpf_add_plugin doesn't
# error out when the file is missing on a from-scratch build. The
# ui-regenerate target produces it; the GENERATED property tells CMake
# "trust me, this file will exist before compile time".
set_source_files_properties(${UI_BUNDLE_C} PROPERTIES GENERATED TRUE)

# create plugin build target
dpf_add_plugin(${NAME}
  TARGETS au clap jack lv2 vst2 vst3
  USE_FILE_BROWSER
  FILES_DSP
      src/PluginDSP.cpp
      src/PluginRpcService.cpp
      src/config/UserConfig.cpp
      src/config/UserConfigPaths.cpp
      src/config/RecentFiles.cpp
      src/project/Project.cpp
      src/lsdj/Effects.cpp
      src/lsdj/KitUtil.cpp
      src/lsdj/SampleCache.cpp
      src/lsdj/KitCompiler.cpp
      src/system/SystemBase.cpp
      src/system/sameboy/LinkGroup.cpp
      src/system/sameboy/RomSniffer.cpp
      src/system/sameboy/SameBoySystem.cpp
      src/system/sameboy/roles/ArduinoboyMaster.cpp
      src/system/sameboy/roles/LsdjSyncRole.cpp
      src/system/sameboy/roles/LsdjKitPatchRole.cpp
      src/system/sameboy/roles/MgbPassthroughRole.cpp
      src/system/mesen/MesenNesSystem.cpp
      src/system/mesen/MesenNesDebugSession.cpp
      src/system/mesen/MesenVideoDevice.cpp
      src/system/mesen/MesenGbaSystem.cpp
      src/system/mesen/roles/NesN8MidiRole.cpp
  FILES_UI
      src/PluginUI.cpp
      src/PluginJsBridge.cpp
      src/PluginRpcService.cpp
      src/config/UserConfig.cpp
      src/config/UserConfigPaths.cpp
      src/config/RecentFiles.cpp
      src/GamepadManager.cpp
      # SameBoySystem is constructed on the UI thread (loadRomFromPath) and
      # snapshotConfig is referenced by the bridge's Save project path; the
      # symbols must be linked into the UI binary as well, not just the DSP.
      # In in-process formats (VST3/CLAP/...) DSP+UI share a binary so this
      # is a no-op duplicate; for LV2 the UI binary needs its own copy
      # (though at runtime the LV2-UI never actually constructs one because
      # shared DSP state is unreachable).
      src/project/Project.cpp
      src/lsdj/Effects.cpp
      src/lsdj/KitUtil.cpp
      src/lsdj/SampleCache.cpp
      src/lsdj/KitCompiler.cpp
      src/system/SystemBase.cpp
      src/system/sameboy/LinkGroup.cpp
      src/system/sameboy/RomSniffer.cpp
      src/system/sameboy/SameBoySystem.cpp
      src/system/sameboy/roles/ArduinoboyMaster.cpp
      src/system/sameboy/roles/LsdjSyncRole.cpp
      src/system/sameboy/roles/LsdjKitPatchRole.cpp
      src/system/sameboy/roles/MgbPassthroughRole.cpp
      src/system/mesen/MesenNesSystem.cpp
      src/system/mesen/MesenNesDebugSession.cpp
      src/system/mesen/MesenVideoDevice.cpp
      src/system/mesen/MesenGbaSystem.cpp
      src/system/mesen/roles/NesN8MidiRole.cpp
      ${UI_BUNDLE_C}
      deps/dpf-widgets/generic/LVGL.cpp)

# setup build includes and libraries
target_include_directories(${NAME} PUBLIC src)
target_include_directories(${NAME} PUBLIC deps/dpf-widgets/generic)
target_link_libraries(${NAME} PUBLIC lvgl::lvgl $<$<BOOL:${WIN32}>:winmm>)
target_link_libraries(${NAME} PUBLIC lvgl-js-native)
target_link_libraries(${NAME} PUBLIC rpcpp)
target_link_libraries(${NAME} PUBLIC sameboy)
target_link_libraries(${NAME} PUBLIC mesen)
target_link_libraries(${NAME} PUBLIC r8brain)
target_link_libraries(${NAME} PUBLIC enkiTS)
target_link_libraries(${NAME} PUBLIC miniaudio)
# efsw lands on the umbrella target so both DSP and UI sub-targets get the
# static lib. The DSP doesn't actually use the watcher at runtime, but
# PluginRpcService.cpp (shared between FILES_DSP and FILES_UI) references
# UserConfig symbols and we don't split the .cpp.
target_link_libraries(${NAME} PUBLIC efsw)
# Project files are PKZIP blobs (project.json + raw binary entries); miniz
# provides the in-memory writer/reader. The target is built as part of txiki
# but we link + include directly so src/util/MinizZip.hpp can #include "miniz.h".
target_link_libraries(${NAME} PUBLIC miniz)
target_include_directories(${NAME} PUBLIC deps/lv_binding_js/deps/txiki/deps/miniz)

# Force UI artifacts to refresh before any consumer of bundle_data.c
# builds. Wiring only ${NAME}-ui (DPF's static-lib aggregator) is not
# enough: CMake doesn't propagate add_dependencies through library link
# edges, so `make retroplug-jack` / `make screenshot` / `make validate`
# were observed skipping ui-regenerate entirely. Add a direct edge to
# every UI-bearing variant and the umbrella aggregator.
#
# copy_if_different on bundle_data.c (see the ui-regenerate target above)
# keeps the C compile + plugin link incremental when content is unchanged,
# so this is free on no-op rebuilds and ~1–2 s otherwise.
set(_RP_UI_VARIANTS ui jack clap vst2 vst3 lv2-ui au)
foreach(_v IN LISTS _RP_UI_VARIANTS)
    if(TARGET ${NAME}-${_v})
        add_dependencies(${NAME}-${_v} ui-regenerate)
    endif()
endforeach()
# The umbrella ${NAME} target is what convenience targets like screenshot
# and validate DEPENDS on; wire it directly so the regen fires even when
# they don't transit through a specific variant.
if(TARGET ${NAME})
    add_dependencies(${NAME} ui-regenerate)
endif()

# NOTICE you probably do not want this on your final plugin
target_link_libraries(${NAME} PUBLIC lvgl::demos)

# SDL2 lands on ${NAME}-ui — the aggregator DPF creates for every UI-
# bearing variant (retroplug-vst3, -clap, -au, -jack, -lv2-ui, etc. all
# link this PUBLIC/PRIVATE, so SDL2 propagates to each .so/.vst3/.clap
# without needing a per-variant loop). DSP-only outputs (the LV2 split's
# -dsp half) never link UI sources and thus never see SDL.
target_link_libraries(${NAME}-ui PUBLIC PkgConfig::SDL2)

# --- Tests (Catch2) ----------------------------------------------------------
# Gated so the default build stays small; opt in with -DBUILD_TESTING=ON.
option(BUILD_TESTING "Build the Catch2 unit tests" OFF)
if(BUILD_TESTING)
    add_subdirectory(deps/catch2)
    enable_testing()
    add_subdirectory(test)
endif()

# --- CLI tool ---------------------------------------------------------------
# Headless renderer + scripted-input runner. Useful for fast LSDJ renders
# and agent-driven workflows.
option(BUILD_CLI "Build the retroplug-cli headless renderer" ON)
if(BUILD_CLI)
    add_subdirectory(cli)
endif()

# --- Convenience targets for headless agent workflows -----------------------
# `make screenshot` -> launch standalone under Xvfb + dummy jackd, capture
# /tmp/retroplug.png after a few seconds.
add_custom_target(screenshot
    COMMAND ${CMAKE_SOURCE_DIR}/tools/run-standalone.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS retroplug
    USES_TERMINAL
    COMMENT "Capturing standalone screenshot via Xvfb + dummy jackd"
)

# `make validate` -> run pluginval (VST3) + clap-validator (CLAP) against the
# built artifacts. Useful for CI / agent-driven regression checks.
add_custom_target(validate
    COMMAND ${CMAKE_SOURCE_DIR}/tools/validate-plugins.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS retroplug-clap retroplug-vst3
    USES_TERMINAL
    COMMENT "Running plugin-format validators (clap-validator + pluginval)"
)

# `make reaper-headless` -> launch Reaper under Xvfb + dummy jackd. Useful
# for driving Reaper via ReaScript / xdotool when the MCP server's static
# analysis isn't enough. Long-running: Ctrl-C to exit.
add_custom_target(reaper-headless
    COMMAND ${CMAKE_SOURCE_DIR}/tools/run-reaper-headless.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    USES_TERMINAL
    COMMENT "Launching Reaper headless (Xvfb + dummy jackd)"
)

# `make cli-smoke` -> render the example LSDJ script to ./out.wav.
if(BUILD_CLI)
    add_custom_target(cli-smoke
        COMMAND ${CMAKE_BINARY_DIR}/bin/retroplug-cli
                --script ${CMAKE_SOURCE_DIR}/examples/scripts/lsdj_smoke.json
                --out ${CMAKE_BINARY_DIR}/cli-smoke.wav
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-cli
        USES_TERMINAL
        COMMENT "Rendering examples/scripts/lsdj_smoke.json -> build/cli-smoke.wav"
    )

    # `make cli-ts-test` -> transpile every test/ts/*.test.ts with esbuild and
    # run each as its OWN `retroplug-cli --test` process (one QuickJS runtime
    # per file = test isolation). TAP on stdout; the build stops on the first
    # nonzero exit. Adding a new *.test.ts requires a reconfigure (CONFIGURE_
    # DEPENDS re-globs), matching the repo's existing glob convention.
    # Recursive glob: tests are organised by platform — test/ts/{gb,nes,gba}/...
    # (e.g. gb/cpu.test.ts, gb/lsdj/sav.test.ts). The path under test/ts becomes
    # a slug ("gb/lsdj/sav") used for the bundle path and (with / -> -) the
    # per-test target name (cli-ts-test-gb-lsdj-sav), so same-named tests in
    # different platforms don't collide.
    file(GLOB_RECURSE TS_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/test/ts/*.test.ts)
    add_custom_target(cli-ts-test
        COMMENT "Running TypeScript harness tests (TAP)")
    foreach(_ts ${TS_TEST_SOURCES})
        file(RELATIVE_PATH _rel ${CMAKE_SOURCE_DIR}/test/ts ${_ts})
        string(REGEX REPLACE "\\.test\\.ts$" "" _slug ${_rel})  # e.g. gb/lsdj/sav
        string(REPLACE "/" "-" _tgt ${_slug})                   # target-name-safe
        set(_js  ${CMAKE_BINARY_DIR}/test-js/${_slug}.js)
        set(_dep ${CMAKE_BINARY_DIR}/test-js/${_slug}.js.d)
        add_custom_command(
            OUTPUT  ${_js}
            COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-test.js
                    ${_ts} ${_js} ${_dep}
            DEPENDS ${_ts}
                    ${CMAKE_SOURCE_DIR}/test/harness/index.ts
                    ${CMAKE_SOURCE_DIR}/tools/build-test.js
            DEPFILE ${_dep}
            COMMENT "esbuild ${_rel} -> test-js/${_slug}.js"
            VERBATIM)
        add_custom_target(cli-ts-test-${_tgt}
            COMMAND ${CMAKE_BINARY_DIR}/bin/retroplug-cli --test ${_js}
            DEPENDS ${_js} retroplug-cli
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            USES_TERMINAL
            COMMENT "retroplug-cli --test test-js/${_slug}.js")
        add_dependencies(cli-ts-test cli-ts-test-${_tgt})
    endforeach()

    # `make cli-nes-smoke` -> boot the n8-midi NES ROM under MesenNesSystem,
    # render 5 s, capture a final screenshot. End-to-end verification of
    # the Mesen backend + NES ROM dispatch.
    add_custom_target(cli-nes-smoke
        COMMAND ${CMAKE_BINARY_DIR}/bin/retroplug-cli
                --script ${CMAKE_SOURCE_DIR}/examples/scripts/nes_smoke.json
                --screenshot-dir ${CMAKE_BINARY_DIR}/nes-smoke
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-cli
        USES_TERMINAL
        COMMENT "Rendering examples/scripts/nes_smoke.json -> /tmp/nes-smoke.wav + boot.png"
    )

    # `make cli-gba-smoke` -> boot the nanoloop demo GBA ROM under MesenGbaSystem,
    # render 5 s, capture two screenshots. End-to-end verification of the
    # Mesen-backed GBA dispatch (audio + video + boot).
    add_custom_target(cli-gba-smoke
        COMMAND ${CMAKE_BINARY_DIR}/bin/retroplug-cli
                --script ${CMAKE_SOURCE_DIR}/examples/scripts/gba_smoke.json
                --screenshot-dir ${CMAKE_BINARY_DIR}/gba-smoke
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-cli
        USES_TERMINAL
        COMMENT "Rendering examples/scripts/gba_smoke.json -> /tmp/gba-smoke.wav + boot.png"
    )

    # `make cli-n8-midi` -> end-to-end MIDI-input test for the n8-midi ROM.
    # Pumps note-ons across the four standard NES channels (pulse1=ch0,
    # pulse2=ch1, triangle=ch2, noise=ch3) into the FIFO and captures the
    # WAV. Audio should contain audible notes (the smoke target produces
    # silence by comparison since n8-midi only emits sound on MIDI input).
    add_custom_target(cli-n8-midi
        COMMAND ${CMAKE_BINARY_DIR}/bin/retroplug-cli
                --script ${CMAKE_SOURCE_DIR}/examples/scripts/n8_midi_input.json
                --screenshot-dir ${CMAKE_BINARY_DIR}/n8-midi
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-cli
        USES_TERMINAL
        COMMENT "Running the n8-midi MIDI-input demo (host MIDI -> FIFO -> NES audio)"
    )

    # Two-instance link-cable sync is covered by cli-ts-test:
    #   test/ts/gb/lsdj/sync_pattern.test.ts  — positive (follower plays, synced)
    #   test/ts/gb/lsdj/sync_negative.test.ts — control (follower silent, no sync)
    #   test/ts/gb/lsdj/sync_smoke.test.ts    — two-instance boot + audio plumbing
    # sync_pattern also writes /tmp/lsdj-sync-pattern_sys{0,1}.wav for the
    # reaper-analyze-lsdj-sync MCP workflow below.

    # Host-MIDI-sync (SYNC=MIDI, MidiSync role) is covered by
    # test/ts/gb/lsdj/midi_sync.test.ts; the functional host-clock->playback path
    # is covered by test/ts/gb/lsdj/arduinoboy_input.test.ts (cli-ts-test).

    # MidiMap mode is covered by test/ts/gb/lsdj/midimap.test.ts (cli-ts-test).

    # KeyboardMidi mode is covered by test/ts/gb/lsdj/keyboardmidi.test.ts (cli-ts-test).

    # MidiPassthrough is covered by test/ts/gb/lsdj/passthrough.test.ts (cli-ts-test).

    # The aboy SYNC cycle is no longer discovered via UI navigation — the sav
    # codec authors the SYNC byte directly. test/ts/gb/lsdj/sync_modes.test.ts
    # authors every model SyncMode and asserts the byte; arduinoboy_master.test.ts
    # covers the ArduinoboyMaster serial-out capture path (both cli-ts-test).

    # `make cli-lsdj-kit-patch` -> end-to-end kit-patching smoke. Loads LSDJ,
    # patches kit slot 0 with the mule drum samples (kick/snare/hat) at +100
    # ms, then screenshots LSDJ's boot screen and the post-patch state. The
    # boot screen confirms LSDJ is up; the post_patch screenshot verifies the
    # patched kit name appears in the kit selector.
    add_custom_target(cli-lsdj-kit-patch
        COMMAND ${CMAKE_BINARY_DIR}/bin/retroplug-cli
                --script ${CMAKE_SOURCE_DIR}/examples/scripts/lsdj_kit_patch_smoke.json
                --screenshot-dir ${CMAKE_BINARY_DIR}/lsdj-kit-patch
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-cli
        USES_TERMINAL
        COMMENT "Running the LSDJ kit-patching smoke (loads ROM + drum samples; PNGs + WAV)"
    )

    # `make reaper-analyze-smoke` -> render cli-smoke and stage the WAV in
    # the reaper-mcp-server's projects dir. The MCP server's audio-analysis
    # tools (loudness/frequency/dynamics/stereo) can then be invoked
    # against cli-smoke.wav by name.
    add_custom_target(reaper-analyze-smoke
        COMMAND ${CMAKE_SOURCE_DIR}/tools/reaper-stage.sh
                ${CMAKE_BINARY_DIR}/cli-smoke.wav
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS cli-smoke
        USES_TERMINAL
        COMMENT "Staging cli-smoke.wav for reaper-mcp-server analysis"
    )

    # `make reaper-analyze-lsdj-sync` -> run the TS link-sync test (which writes
    # per-system WAVs to /tmp via emu.writeWav) and stage both for analysis.
    # Useful for asking the MCP server to compare loudness / dynamics across the
    # two synced instances.
    add_custom_target(reaper-analyze-lsdj-sync
        COMMAND ${CMAKE_SOURCE_DIR}/tools/reaper-stage.sh
                /tmp/lsdj-sync-pattern_sys0.wav
                /tmp/lsdj-sync-pattern_sys1.wav
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS cli-ts-test-gb-lsdj-sync_pattern
        USES_TERMINAL
        COMMENT "Staging LSDJ-sync per-system WAVs for reaper-mcp-server analysis"
    )

    # `make reaper-mgb-smoke` -> render the committed Reaper project that
    # loads RetroPlug VST3i + mGB and plays a C-major chord. First
    # end-to-end test that the plugin works inside a real DAW host.
    # Output: build/reaper-mgb-smoke.wav.
    add_custom_target(reaper-mgb-smoke
        COMMAND ${CMAKE_SOURCE_DIR}/tools/run-reaper-render.sh
                ${CMAKE_SOURCE_DIR}/examples/reaper/mgb_smoke.rpp
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-vst3
        USES_TERMINAL
        COMMENT "Reaper headless render: mGB + MIDI -> build/reaper-mgb-smoke.wav"
    )

    # `make reaper-mgb-author` -> regenerate the committed .RPP fixture
    # from scratch via a ReaScript that builds the project + bakes the
    # mGB ROM into the plugin chunk. Only re-run when mgb_smoke.json or
    # the author script change.
    add_custom_target(reaper-mgb-author
        COMMAND ${CMAKE_SOURCE_DIR}/tools/run-reaper-author.sh
                ${CMAKE_SOURCE_DIR}/examples/reaper/mgb_smoke.rpp
                ${CMAKE_BINARY_DIR}
                ${CMAKE_SOURCE_DIR}/tools/reaper-mgb-author.lua
                ${CMAKE_SOURCE_DIR}/examples/scripts/mgb_smoke.json
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-vst3 retroplug-cli
        USES_TERMINAL
        COMMENT "Re-author examples/reaper/mgb_smoke.rpp via headless Reaper"
    )

    # `make reaper-lsdj-arduinoboy-author` -> regenerate the committed
    # LSDj-Arduinoboy fixture. Bootstrap script navigates LSDj into a
    # known SONG/CHAIN/PHRASE state with SYNC=LSDJ; the resulting
    # savestate is baked into the .RPP plugin chunk.
    add_custom_target(reaper-lsdj-arduinoboy-author
        COMMAND ${CMAKE_SOURCE_DIR}/tools/run-reaper-author.sh
                ${CMAKE_SOURCE_DIR}/examples/reaper/lsdj_arduinoboy_metro.rpp
                ${CMAKE_BINARY_DIR}
                ${CMAKE_SOURCE_DIR}/tools/reaper-lsdj-arduinoboy-author.lua
                ${CMAKE_SOURCE_DIR}/examples/scripts/lsdj_arduinoboy_metro_setup.json
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-vst3 retroplug-cli
        USES_TERMINAL
        COMMENT "Re-author examples/reaper/lsdj_arduinoboy_metro.rpp via headless Reaper"
    )

    # `make reaper-lsdj-arduinoboy-metro` -> render the committed
    # Arduinoboy-timing project and analyze sync drift. The LSDj track
    # (panned L) should land its notes on the same sample positions as
    # the Click track (panned R) generated by ReaSynth at host BPM.
    add_custom_target(reaper-lsdj-arduinoboy-metro
        COMMAND ${CMAKE_SOURCE_DIR}/tools/run-reaper-render.sh
                ${CMAKE_SOURCE_DIR}/examples/reaper/lsdj_arduinoboy_metro.rpp
        COMMAND ${CMAKE_SOURCE_DIR}/tools/reaper-timing-analyze.py
                ${CMAKE_BINARY_DIR}/reaper-arduinoboy-metro.wav
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-vst3
        USES_TERMINAL
        COMMENT "Reaper timing test: LSDj vs metronome (Arduinoboy slave)"
    )

    # `make reaper-lsdj-midi-author` -> regenerate the stock-LSDj
    # MidiSync fixture. Bootstrap script boots stock LSDj, sets
    # SYNC=MIDI, builds a chain+phrase+note, then presses START to arm
    # LSDj's "WAIT for clock" state — captured in the savestate so the
    # .RPP is self-contained.
    add_custom_target(reaper-lsdj-midi-author
        COMMAND ${CMAKE_SOURCE_DIR}/tools/run-reaper-author.sh
                ${CMAKE_SOURCE_DIR}/examples/reaper/lsdj_midi_metro.rpp
                ${CMAKE_BINARY_DIR}
                ${CMAKE_SOURCE_DIR}/tools/reaper-lsdj-midi-author.lua
                ${CMAKE_SOURCE_DIR}/examples/scripts/lsdj_midi_metro_setup.json
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-vst3 retroplug-cli
        USES_TERMINAL
        COMMENT "Re-author examples/reaper/lsdj_midi_metro.rpp via headless Reaper"
    )

    # `make reaper-lsdj-midi-metro` -> render + analyze the stock-LSDj
    # MidiSync timing project. Counterpart to reaper-lsdj-arduinoboy-metro;
    # comparing the two startup-latency numbers tells us whether the
    # observed delay is in the Arduinoboy-slave emulation path or in
    # LSDj itself (which is also reachable via the simpler MidiSync mode).
    add_custom_target(reaper-lsdj-midi-metro
        COMMAND ${CMAKE_SOURCE_DIR}/tools/run-reaper-render.sh
                ${CMAKE_SOURCE_DIR}/examples/reaper/lsdj_midi_metro.rpp
        COMMAND ${CMAKE_SOURCE_DIR}/tools/reaper-timing-analyze.py
                ${CMAKE_BINARY_DIR}/reaper-lsdj-midi-metro.wav
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-vst3
        USES_TERMINAL
        COMMENT "Reaper timing test: LSDj vs metronome (stock MidiSync)"
    )
endif()
