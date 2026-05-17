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
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

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

# --- SameBoy: Game Boy emulator core, vendored under deps/sameboy ---
add_subdirectory(deps/sameboy)

# --- Mesen: NES emulator core (multi-system upstream; we use the NES path) ---
add_subdirectory(deps/mesen)

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
    src/project/Project.cpp
    src/system/sameboy/LinkGroup.cpp
    src/system/sameboy/RomSniffer.cpp
    src/system/sameboy/SameBoySystem.cpp
    src/system/sameboy/roles/ArduinoboyMaster.cpp
    src/system/sameboy/roles/LsdjSyncRole.cpp
    src/system/sameboy/roles/MgbPassthroughRole.cpp
    src/system/mesen/MesenSystem.cpp
    src/system/mesen/MesenVideoDevice.cpp
    src/system/mesen/GbaSystem.cpp
    src/system/mesen/roles/NesN8MidiRole.cpp)
target_include_directories(rpc-schema-dump PRIVATE src)
target_link_libraries(rpc-schema-dump PRIVATE rpcpp sameboy mesen efsw)

# --- UI bundle: esbuild-bundled TS/TSX, embedded into the plugin as a byte array ---
find_program(NODE_EXECUTABLE node REQUIRED)

set(UI_BUNDLE_JS  "${CMAKE_BINARY_DIR}/ui/bundle.js")
set(UI_BUNDLE_C   "${CMAKE_BINARY_DIR}/ui/bundle_data.c")
set(UI_BUNDLE_DEP "${CMAKE_BINARY_DIR}/ui/bundle.d")
set(UI_RPC_TS     "${CMAKE_BINARY_DIR}/ui/generated/PluginService.ts")

# Generated TypeScript client for PluginRpcService. The rpc-schema-dump
# executable prints the OpenRPC document; gen-rpc-ts.js bundles
# @rpcpp/codegen via esbuild and runs writeService on it. Both sit under
# build/ — never committed.
add_custom_command(
    OUTPUT  ${UI_RPC_TS}
    COMMAND ${NODE_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/gen-rpc-ts.js
            $<TARGET_FILE:rpc-schema-dump>
            ${UI_RPC_TS}
    DEPENDS rpc-schema-dump
            ${CMAKE_SOURCE_DIR}/tools/gen-rpc-ts.js
    COMMENT "Generating typed RPC client from OpenRPC schema"
    VERBATIM)

file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/ui/*.ts"
    "${CMAKE_SOURCE_DIR}/ui/*.tsx"
    "${CMAKE_SOURCE_DIR}/runtime/*.ts"
    "${CMAKE_SOURCE_DIR}/runtime/*.tsx")

add_custom_command(
    OUTPUT  ${UI_BUNDLE_JS}
    COMMAND ${NODE_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/build-ui.js
            ${UI_BUNDLE_JS} ${UI_BUNDLE_DEP}
    DEPENDS ${UI_SOURCES} ${UI_RPC_TS} ${CMAKE_SOURCE_DIR}/tools/build-ui.js
    DEPFILE ${UI_BUNDLE_DEP}
    COMMENT "Building UI bundle"
    VERBATIM)

# Compile the JS bundle to QuickJS bytecode and embed it as a C byte array.
# We reuse txiki's `tjsc` binary (added with EXCLUDE_FROM_ALL in
# deps/lv_binding_js/deps/txiki/CMakeLists.txt — the DEPENDS below forces
# it to be built when the UI bundle rule fires).
#
# Output symbols are `ui_bundle[N]` / `ui_bundle_size` (uint8_t / uint32_t).
# Flags:
#   -m  force module mode — the runtime loads the bundle as a module.
#   -s  strip source (keeps debug line info; -ss would also drop debug).
#   -p  prefix for emitted C identifiers.
#
# Cross-compile note: tjsc is built with the project toolchain. For
# host != target builds the user must point TJSC_EXECUTABLE at a host-built
# tjsc binary. The bytecode itself is consumed by the same vendored QuickJS-ng
# tree, so version skew can't happen unless someone runs a different tjsc.
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

add_custom_command(
    OUTPUT  ${UI_BUNDLE_C}
    COMMAND ${_TJSC_COMMAND}
            -m -s -p ui_
            -o ${UI_BUNDLE_C}
            ${UI_BUNDLE_JS}
    DEPENDS ${UI_BUNDLE_JS} ${_TJSC_DEP}
    COMMENT "Compiling UI bundle to QuickJS bytecode"
    VERBATIM)

# create plugin build target
dpf_add_plugin(${NAME}
  TARGETS au clap jack lv2 vst2 vst3
  USE_FILE_BROWSER
  FILES_DSP
      src/PluginDSP.cpp
      src/PluginRpcService.cpp
      src/config/UserConfig.cpp
      src/config/UserConfigPaths.cpp
      src/project/Project.cpp
      src/system/sameboy/LinkGroup.cpp
      src/system/sameboy/RomSniffer.cpp
      src/system/sameboy/SameBoySystem.cpp
      src/system/sameboy/roles/ArduinoboyMaster.cpp
      src/system/sameboy/roles/LsdjSyncRole.cpp
      src/system/sameboy/roles/MgbPassthroughRole.cpp
      src/system/mesen/MesenSystem.cpp
      src/system/mesen/MesenVideoDevice.cpp
      src/system/mesen/GbaSystem.cpp
      src/system/mesen/roles/NesN8MidiRole.cpp
  FILES_UI
      src/PluginUI.cpp
      src/PluginJsBridge.cpp
      src/PluginRpcService.cpp
      src/config/UserConfig.cpp
      src/config/UserConfigPaths.cpp
      src/GamepadManager.cpp
      # SameBoySystem is constructed on the UI thread (loadRomFromPath) and
      # snapshotConfig is referenced by the bridge's Save project path; the
      # symbols must be linked into the UI binary as well, not just the DSP.
      # In in-process formats (VST3/CLAP/...) DSP+UI share a binary so this
      # is a no-op duplicate; for LV2 the UI binary needs its own copy
      # (though at runtime the LV2-UI never actually constructs one because
      # shared DSP state is unreachable).
      src/project/Project.cpp
      src/system/sameboy/LinkGroup.cpp
      src/system/sameboy/RomSniffer.cpp
      src/system/sameboy/SameBoySystem.cpp
      src/system/sameboy/roles/ArduinoboyMaster.cpp
      src/system/sameboy/roles/LsdjSyncRole.cpp
      src/system/sameboy/roles/MgbPassthroughRole.cpp
      src/system/mesen/MesenSystem.cpp
      src/system/mesen/MesenVideoDevice.cpp
      src/system/mesen/GbaSystem.cpp
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
# efsw lands on the umbrella target so both DSP and UI sub-targets get the
# static lib. The DSP doesn't actually use the watcher at runtime, but
# PluginRpcService.cpp (shared between FILES_DSP and FILES_UI) references
# UserConfig symbols and we don't split the .cpp.
target_link_libraries(${NAME} PUBLIC efsw)

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

    # `make cli-nes-smoke` -> boot the n8-midi NES ROM under MesenSystem,
    # render 5 s, capture a final screenshot. First end-to-end verification of
    # the Mesen backend + NES ROM dispatch (step 17 / Phase B).
    add_custom_target(cli-nes-smoke
        COMMAND ${CMAKE_BINARY_DIR}/bin/retroplug-cli
                --script ${CMAKE_SOURCE_DIR}/examples/scripts/nes_smoke.json
                --screenshot-dir ${CMAKE_BINARY_DIR}/nes-smoke
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-cli
        USES_TERMINAL
        COMMENT "Rendering examples/scripts/nes_smoke.json -> /tmp/nes-smoke.wav + boot.png"
    )

    # `make cli-gba-smoke` -> boot the nanoloop demo GBA ROM under GbaSystem,
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

    # `make cli-sync-smoke` -> boot two LSDJ instances on link_group 1 and
    # write per-instance PNGs + a stereo WAV. Exists so agents can verify the
    # multi-instance + GB-framebuffer screenshot path end-to-end without
    # touching the plugin formats.
    add_custom_target(cli-sync-smoke
        COMMAND ${CMAKE_BINARY_DIR}/bin/retroplug-cli
                --script ${CMAKE_SOURCE_DIR}/examples/scripts/lsdj_sync_smoke.json
                --out ${CMAKE_BINARY_DIR}/lsdj-sync-smoke.wav
                --screenshot-dir ${CMAKE_BINARY_DIR}
                --final-screenshot
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-cli
        USES_TERMINAL
        COMMENT "Rendering examples/scripts/lsdj_sync_smoke.json -> build/lsdj-sync-smoke.wav + PNGs"
    )

    # `make cli-lsdj-sync` -> the full LSDJ link-cable sync demo: two
    # instances, both configured SYNC=LSDJ in the PROJECT screen, a one-note
    # pattern entered on each, then START on instance 0. Writes per-system
    # WAVs so audible sync can be cross-correlated. Use this as a regression
    # test for the LinkGroup serial-bit ferrying and as the reference for
    # how to drive LSDJ from a script.
    add_custom_target(cli-lsdj-sync
        COMMAND ${CMAKE_BINARY_DIR}/bin/retroplug-cli
                --script ${CMAKE_SOURCE_DIR}/examples/scripts/lsdj_sync_pattern.json
                --screenshot-dir ${CMAKE_BINARY_DIR}/lsdj-sync
                --per-system-wav
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-cli
        USES_TERMINAL
        COMMENT "Running the LSDJ two-instance sync demo (PNGs + per-system WAVs)"
    )

    # `make cli-lsdj-midi-sync` -> drive a single LSDJ instance from a
    # simulated host transport. Sets SYNC=MIDI in PROJECT, builds a one-note
    # chain/phrase, then toggles `set_transport: true` so AudioBlockInfo
    # carries a running transport. LsdjSyncRole generates 0xF8 clock bytes
    # from the host PPQ position; LSDJ syncs against them just as it would
    # against a DAW's transport.
    add_custom_target(cli-lsdj-midi-sync
        COMMAND ${CMAKE_BINARY_DIR}/bin/retroplug-cli
                --script ${CMAKE_SOURCE_DIR}/examples/scripts/lsdj_midi_sync.json
                --screenshot-dir ${CMAKE_BINARY_DIR}/lsdj-midi-sync
                --per-system-wav
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-cli
        USES_TERMINAL
        COMMENT "Running the LSDJ host-MIDI-sync demo (PNGs + per-system WAV)"
    )

    # `make cli-lsdj-arduinoboy` -> exercises LsdjSyncRole's Arduinoboy slave
    # mode. Pre-sets `lsdj_sync_mode: MidiSyncArduinoboy` on the system so the
    # role's onMidi handles Arduinoboy note commands (24/25 play toggle,
    # 26-29 tempo divisor, 30+ row trigger). Uses the Arduinoboy-build ROM.
    add_custom_target(cli-lsdj-arduinoboy
        COMMAND ${CMAKE_BINARY_DIR}/bin/retroplug-cli
                --script ${CMAKE_SOURCE_DIR}/examples/scripts/lsdj_arduinoboy_input.json
                --screenshot-dir ${CMAKE_BINARY_DIR}/lsdj-arduinoboy
                --per-system-wav
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-cli
        USES_TERMINAL
        COMMENT "Running the LSDJ Arduinoboy slave-mode input demo"
    )

    # `make cli-lsdj-midimap` -> exercises MidiMap mode (channel + note → row
    # index byte + 0xFE NoteOff handshake). Uses the Arduinoboy-build ROM.
    add_custom_target(cli-lsdj-midimap
        COMMAND ${CMAKE_BINARY_DIR}/bin/retroplug-cli
                --script ${CMAKE_SOURCE_DIR}/examples/scripts/lsdj_midimap.json
                --screenshot-dir ${CMAKE_BINARY_DIR}/lsdj-midimap
                --per-system-wav
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-cli
        USES_TERMINAL
        COMMENT "Running the LSDJ MidiMap-mode demo"
    )

    # `make cli-lsdj-keyboardmidi` -> exercises KeyboardMidi mode (MIDI notes
    # mapped to PS/2 scancodes via LSDJ's keyboard map).
    add_custom_target(cli-lsdj-keyboardmidi
        COMMAND ${CMAKE_BINARY_DIR}/bin/retroplug-cli
                --script ${CMAKE_SOURCE_DIR}/examples/scripts/lsdj_keyboardmidi.json
                --screenshot-dir ${CMAKE_BINARY_DIR}/lsdj-keyboardmidi
                --per-system-wav
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-cli
        USES_TERMINAL
        COMMENT "Running the LSDJ KeyboardMidi-mode demo"
    )

    # `make cli-lsdj-passthrough` -> exercises MidiPassthrough mode (raw
    # 3-byte MIDI bytes forwarded to LSDJ's serial port, MGB-style).
    add_custom_target(cli-lsdj-passthrough
        COMMAND ${CMAKE_BINARY_DIR}/bin/retroplug-cli
                --script ${CMAKE_SOURCE_DIR}/examples/scripts/lsdj_passthrough.json
                --screenshot-dir ${CMAKE_BINARY_DIR}/lsdj-passthrough
                --per-system-wav
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-cli
        USES_TERMINAL
        COMMENT "Running the LSDJ MidiPassthrough-mode demo"
    )

    # `make cli-lsdj-aboy-sync-discovery` -> diagnostic that cycles the
    # PROJECT-screen SYNC value 8 times with a screenshot after each press,
    # so an agent can empirically map the aboy build's SYNC value order.
    # Run once whenever the bundled aboy LSDJ ROM changes, then update the
    # AGENTS.md cycle table. Not part of the normal verification loop.
    add_custom_target(cli-lsdj-aboy-sync-discovery
        COMMAND ${CMAKE_BINARY_DIR}/bin/retroplug-cli
                --script ${CMAKE_SOURCE_DIR}/examples/scripts/lsdj_aboy_sync_discovery.json
                --screenshot-dir ${CMAKE_BINARY_DIR}/lsdj-aboy-sync-discovery
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-cli
        USES_TERMINAL
        COMMENT "Running the aboy-LSDJ SYNC value cycle discovery (screenshots only)"
    )

    # `make cli-lsdj-arduinoboy-master` -> the master-out path. LSDJ's MI.OUT
    # mode emits serial bytes that the role's ArduinoboyMaster decoder
    # translates to host MIDI events. The per-system MIDI log (enabled via
    # `--event-logs`) captures the decoded stream (look for 0xF8 clock +
    # 0xFA/0xFC transport).
    add_custom_target(cli-lsdj-arduinoboy-master
        COMMAND ${CMAKE_BINARY_DIR}/bin/retroplug-cli
                --script ${CMAKE_SOURCE_DIR}/examples/scripts/lsdj_arduinoboy_master.json
                --screenshot-dir ${CMAKE_BINARY_DIR}/lsdj-arduinoboy-master
                --event-logs ${CMAKE_BINARY_DIR}/lsdj-arduinoboy-master
                --per-system-wav
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-cli
        USES_TERMINAL
        COMMENT "Running the LSDJ Arduinoboy-master demo (MI.OUT → host MIDI)"
    )

    # `make cli-lsdj-sync-negative` -> control test (no SYNC=LSDJ set). Used
    # to confirm that LEAD/SYNC indicators only appear when sync is properly
    # configured — pair with cli-lsdj-sync when investigating sync issues.
    add_custom_target(cli-lsdj-sync-negative
        COMMAND ${CMAKE_BINARY_DIR}/bin/retroplug-cli
                --script ${CMAKE_SOURCE_DIR}/examples/scripts/lsdj_sync_negative.json
                --screenshot-dir ${CMAKE_BINARY_DIR}/lsdj-sync-negative
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS retroplug-cli
        USES_TERMINAL
        COMMENT "Running the LSDJ no-sync control test (PNGs)"
    )
endif()
