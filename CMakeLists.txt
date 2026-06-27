# DISTRHO Plugin Framework (DPF)
# Copyright (C) 2021-2024 Filipe Coelho <falktx@falktx.com>
# SPDX-License-Identifier: ISC

cmake_minimum_required(VERSION 3.14...3.31)

# Windows/MSVC: pin the static CRT (/MT) across the whole tree. The framework
# (txiki, mbedtls, libwebsockets) forces MultiThreaded for its own targets, so
# everything that links into the plugin must agree or the final link fails with
# LNK4098 (mismatched CRT). CMP0091 NEW makes CMAKE_MSVC_RUNTIME_LIBRARY the
# mechanism; CMAKE_POLICY_DEFAULT_CMP0091 forces it into add_subdirectory'd deps
# regardless of their own cmake_minimum_required. Requires vcpkg's
# x64-windows-static triplet for SDL2/libffi. (Matches lvgl-template-plugin.)
cmake_policy(SET CMP0091 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)

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

# Force the static MSVC runtime AFTER project() so it wins over anything the
# vcpkg toolchain set during project init. All static deps must agree on /MT.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        CACHE STRING "MSVC runtime library" FORCE)
endif()

# Force a non-executable stack on Linux ELF outputs. Without this, one
# unmarked assembly object in WAMR (txiki's wasm runtime —
# invokeNative_em64_simd.s) leaks the "needs executable stack" attribute
# into every plugin .so we link. Renoise refuses to dlopen such modules
# with "cannot enable executable stack as shared object requires". The
# linker warning we've been seeing on every build is the same root cause.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_link_options(-Wl,-z,noexecstack)
endif()

# Optional sanitizer instrumentation for the test binaries. Always use a
# SEPARATE build dir (build-tsan/, build-asan/) — never the load-bearing
# build/. e.g. cmake -B build-tsan -DRETROPLUG_SANITIZE=thread. Set here,
# before the dep subdirectories, so deps + src + test all get instrumented.
set(RETROPLUG_SANITIZE "" CACHE STRING "Sanitizer for test builds: thread | address | <empty>")
if(RETROPLUG_SANITIZE)
    add_compile_options(-fsanitize=${RETROPLUG_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${RETROPLUG_SANITIZE})
endif()

# tell LVGL where to find its config file. Set before add_subdirectory(dpf.js)
# so RetroPlug's lv_conf.h wins over the framework's default (CACHE no-FORCE).
set(LV_BUILD_CONF_PATH "${PROJECT_SOURCE_DIR}/packages/native/src/lv_conf.h" CACHE PATH "")

# <windows.h> defines min()/max() macros that break std::numeric_limits<T>::max()
# and std::min/max (e.g. lsdj/KitUtil.cpp). NOMINMAX suppresses them — safe and
# standard; unlike WIN32_LEAN_AND_MEAN it removes no headers, so it's global.
# (The earlier /FI cpp_stdlib_preinclude.h workaround for the extern "C" / STL
# C2894 issue is gone — tommitytom/txiki.js's self-guarding headers + dropping
# lv_binding_js's external extern "C" wraps fix it at the root.)
if(WIN32)
    add_compile_definitions(NOMINMAX)
endif()

# NOTE: the NOGDI / WIN32_LEAN_AND_MEAN fix for the GDI `Arc` name collision is
# applied PER-TARGET on lvgl-js-native (in dpf.js's CMakeLists), not globally —
# WIN32_LEAN_AND_MEAN drops winioctl.h which wamr's win_file.c needs, so it must
# not leak into the framework's C deps. Matches the lvgl-template-plugin build.

# MSVC reports __cplusplus as 199711L regardless of /std:c++NN unless told
# otherwise; dpf-widgets' LVGL.hpp #errors when it sees < 201103L.
if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>)
endif()

# --- dpf.js: the generic DPF + LVGL + txiki/QuickJS + rpcpp framework, consumed
# as an npm source package. require.resolve gives its on-disk path; the
# add_subdirectory pulls in DPF (which defines dpf_add_plugin globally), LVGL,
# txiki, msgpack-c, rpcpp, efsw, plus the lvgl-js-native + dpfjs::core targets.
# Requires `pnpm install` before the first configure (node + the dpf.js link).
find_program(NODE_EXECUTABLE node REQUIRED)
execute_process(
    COMMAND ${NODE_EXECUTABLE} -e "console.log(require.resolve('dpf.js/package.json'))"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE DPFJS_PACKAGE_JSON
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _DPFJS_RESOLVE_RC)
if(NOT _DPFJS_RESOLVE_RC EQUAL 0)
    message(FATAL_ERROR "Could not resolve the dpf.js package — run `pnpm install` first.")
endif()
get_filename_component(DPFJS_PATH "${DPFJS_PACKAGE_JSON}" DIRECTORY)
message(STATUS "dpf.js framework: ${DPFJS_PATH}")

# Third-party dep warnings are silenced inside dpf.js's own CMake (it owns the
# lvgl / txiki / quickjs / libuv / mbedtls subtrees), and the genuine warnings in
# the framework forks are fixed at source — so RetroPlug needs no dep-warning
# handling here beyond the vendored bits it pulls in itself (SameBoy: cmake/
# sameboy.cmake; liblsdj: packages/native/test/CMakeLists.txt).
add_subdirectory("${DPFJS_PATH}" dpfjs)

# --- SDL2: used for gamepad/game-controller input across every plugin
# variant (and the standalone). DPF's own SDL2 detection is gated to the
# JACK target only (audio backend fallback), so we find SDL2 ourselves at
# the parent scope and link it into every variant after dpf_add_plugin.
# Only SDL_INIT_GAMECONTROLLER is initialised — see packages/native/src/GamepadManager.cpp.
if(WIN32)
    # Windows has no pkg-config; SDL2 comes from vcpkg's CONFIG package, which
    # exposes the SDL2::SDL2 imported target. Wrap it in a PkgConfig::SDL2
    # INTERFACE target so the rest of the file links the same name on every
    # platform. Requires configuring with the vcpkg toolchain file (or a
    # CMAKE_PREFIX_PATH pointing at the vcpkg-installed tree).
    find_package(SDL2 CONFIG REQUIRED)
    add_library(PkgConfig::SDL2 INTERFACE IMPORTED)
    target_link_libraries(PkgConfig::SDL2 INTERFACE SDL2::SDL2)
elseif(APPLE)
    find_package(PkgConfig REQUIRED)
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
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
endif()

# LVGL, txiki.js, msgpack-c, and rpcpp now come from dpf.js (above).

# --- SameBoy: Game Boy emulator core (submodule at deps/sameboy) ---
include(cmake/sameboy.cmake)

# --- SameBoySystem.cpp: clang-cl isolation (Windows) ------------------------
# SameBoySystem.cpp #defines GB_INTERNAL to reach SameBoy's internal struct view
# and GNU-extension headers, which MSVC cl.exe cannot parse. Like the sameboy
# core, compile it with clang-cl (MSVC-ABI) into a static lib and link THAT into
# the plugin / tools rather than compiling the .cpp in the MSVC targets. It is
# the only RetroPlug C++ that needs the internal view (verified via rpc-schema-dump
# -k 0). Everything else compiles fine under MSVC because GB_INTERNAL is PRIVATE
# to the sameboy target (gb.h then exposes GB_gameboy_t as an opaque same-size blob).
if(WIN32)
    find_program(RP_CLANG_CL clang-cl
        HINTS "$ENV{VCINSTALLDIR}Tools/Llvm/x64/bin"
              "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin"
        REQUIRED)
    set(_sbs_src "${CMAKE_SOURCE_DIR}/packages/native/src/system/sameboy/SameBoySystem.cpp")
    set(_sbs_obj "${CMAKE_BINARY_DIR}/sameboy-clang/SameBoySystem.cpp.obj")
    add_custom_command(
        OUTPUT "${_sbs_obj}"
        COMMAND "${RP_CLANG_CL}" /nologo /c /MT /O2 /Ob2 /DNDEBUG /EHsc /std:c++20 /Zc:__cplusplus
            "/FI${CMAKE_BINARY_DIR}/sameboy-clang/sameboy_version.h"
            # GB_DISABLE_* MUST match the sameboy core or GB_gameboy_t's layout diverges.
            -DGB_DISABLE_TIMEKEEPING -DGB_DISABLE_DEBUGGER
            -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DNOGDI
            "-I${CMAKE_SOURCE_DIR}/packages/native/src"
            "-I${CMAKE_SOURCE_DIR}/deps/sameboy/Core"
            "-I${CMAKE_BINARY_DIR}/generated"
            "-I${CMAKE_BINARY_DIR}/_deps/reflectcpp-src/include"
            "/Fo${_sbs_obj}" "${_sbs_src}"
        DEPENDS "${_sbs_src}" sameboy_bootroms
        COMMENT "clang-cl (sameboy) SameBoySystem.cpp"
        VERBATIM)
    add_library(retroplug-samebsys STATIC "${_sbs_obj}")
    set_target_properties(retroplug-samebsys PROPERTIES LINKER_LANGUAGE CXX)
    # On Windows the source is built above; elsewhere it compiles in-target.
    set(RP_SAMEBOYSYSTEM_SRC "")
else()
    set(RP_SAMEBOYSYSTEM_SRC packages/native/src/system/sameboy/SameBoySystem.cpp)
endif()

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
# packages/native/src/config/UserConfig to live-reload bindings + recent-files JSON (and the ROM
# watcher in PluginUI). RetroPlug-specific — the dpf.js framework doesn't watch
# files. Built as a single static lib; the duplicate efsw-static target is
# suppressed.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build efsw as static" FORCE)
set(BUILD_STATIC_LIBS OFF CACHE BOOL "Skip duplicate efsw-static target" FORCE)
add_subdirectory(deps/efsw EXCLUDE_FROM_ALL)
set_target_properties(efsw PROPERTIES POSITION_INDEPENDENT_CODE ON)

# lvgl-js-native + the framework PIC settings now come from dpf.js (above).

# --- rpc-schema-dump: prints PluginRpcService's OpenRPC schema for codegen ---
# Standalone executable so the codegen step doesn't need to start the plugin
# (or wire up the rest of its dependency graph). Constructs the service with
# all-null state; the schema generator only inspects reflect-cpp's
# compile-time type info, no methods are actually called.
add_executable(rpc-schema-dump
    packages/native/src/RpcSchemaDump.cpp
    packages/native/src/PluginRpcService.cpp
    packages/native/src/config/UserConfig.cpp
    packages/native/src/config/UserConfigPaths.cpp
    packages/native/src/config/RecentFiles.cpp
    packages/native/src/project/Project.cpp
    packages/native/src/lsdj/Effects.cpp
    packages/native/src/lsdj/KitUtil.cpp
    packages/native/src/lsdj/SampleCache.cpp
    packages/native/src/lsdj/KitCompiler.cpp
    packages/native/src/system/SystemBase.cpp
    packages/native/src/system/sameboy/LinkGroup.cpp
    packages/native/src/system/sameboy/RomSniffer.cpp
    ${RP_SAMEBOYSYSTEM_SRC}
    packages/native/src/system/sameboy/roles/ArduinoboyMaster.cpp
    packages/native/src/system/sameboy/roles/LsdjSyncRole.cpp
    packages/native/src/system/sameboy/roles/LsdjKitPatchRole.cpp
    packages/native/src/system/sameboy/roles/MgbPassthroughRole.cpp
    packages/native/src/system/mesen/MesenNesSystem.cpp
    packages/native/src/system/mesen/MesenNesDebugSession.cpp
    packages/native/src/system/mesen/MesenVideoDevice.cpp
    packages/native/src/system/mesen/MesenGbaSystem.cpp
    packages/native/src/system/mesen/roles/NesN8MidiRole.cpp)
target_include_directories(rpc-schema-dump PRIVATE packages/native/src)
target_include_directories(rpc-schema-dump PRIVATE ${DPFJS_PATH}/deps/lv_binding_js/deps/txiki/deps/miniz)
target_link_libraries(rpc-schema-dump PRIVATE rpcpp sameboy mesen r8brain enkiTS miniaudio efsw miniz
    $<$<BOOL:${WIN32}>:retroplug-samebsys>)

# --- sav-schema-dump: prints the LSDj Sav model's JSON Schema for zod/TS gen ---
# Header-only model + reflect-cpp (via rpcpp); no codec sources needed since
# to_schema only inspects the struct definitions.
add_executable(sav-schema-dump tools/SavSchemaDump.cpp)
target_include_directories(sav-schema-dump PRIVATE packages/native/src)
target_link_libraries(sav-schema-dump PRIVATE rpcpp)
set_target_properties(sav-schema-dump PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# --- UI bundle: esbuild-bundled TS/TSX, embedded into the plugin as a byte array ---
find_program(NODE_EXECUTABLE node REQUIRED)

# --- sav-regenerate: emit zod + TS for the LSDj Sav model ---
# Derived artifacts (build/ui/generated/SavSchema.ts + SavTypes.ts); never
# committed. Not ALL on its own, but pulled in by cli-regenerate below so the
# TS CLI tests (gb/lsdj/sav) that import SavSchema can't go stale.
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
    # Service name + UI entry are consumer-owned build inputs (the framework
    # tools default to them but RetroPlug passes them explicitly here).
    COMMAND ${NODE_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/gen-rpc-ts.js
            $<TARGET_FILE:rpc-schema-dump>
            ${UI_RPC_TS}
            PluginService
    COMMAND ${NODE_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/build-ui.js
            ${UI_BUNDLE_JS} ${UI_BUNDLE_DEP}
            ${CMAKE_SOURCE_DIR}/packages/ui/src/PluginUI.tsx
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
      packages/native/src/PluginDSP.cpp
      packages/native/src/PluginRpcService.cpp
      packages/native/src/transport/CommandApply.cpp
      packages/native/src/config/UserConfig.cpp
      packages/native/src/config/UserConfigPaths.cpp
      packages/native/src/config/RecentFiles.cpp
      packages/native/src/project/Project.cpp
      packages/native/src/lsdj/Effects.cpp
      packages/native/src/lsdj/KitUtil.cpp
      packages/native/src/lsdj/SampleCache.cpp
      packages/native/src/lsdj/KitCompiler.cpp
      packages/native/src/system/SystemBase.cpp
      packages/native/src/system/sameboy/LinkGroup.cpp
      packages/native/src/system/sameboy/RomSniffer.cpp
      ${RP_SAMEBOYSYSTEM_SRC}
      packages/native/src/system/sameboy/roles/ArduinoboyMaster.cpp
      packages/native/src/system/sameboy/roles/LsdjSyncRole.cpp
      packages/native/src/system/sameboy/roles/LsdjKitPatchRole.cpp
      packages/native/src/system/sameboy/roles/MgbPassthroughRole.cpp
      packages/native/src/system/mesen/MesenNesSystem.cpp
      packages/native/src/system/mesen/MesenNesDebugSession.cpp
      packages/native/src/system/mesen/MesenVideoDevice.cpp
      packages/native/src/system/mesen/MesenGbaSystem.cpp
      packages/native/src/system/mesen/roles/NesN8MidiRole.cpp
  FILES_UI
      packages/native/src/PluginUI.cpp
      packages/native/src/PluginJsBridge.cpp
      packages/native/src/PluginRpcService.cpp
      packages/native/src/config/UserConfig.cpp
      packages/native/src/config/UserConfigPaths.cpp
      packages/native/src/config/RecentFiles.cpp
      packages/native/src/GamepadManager.cpp
      # SameBoySystem is constructed on the UI thread (loadRomFromPath) and
      # snapshotConfig is referenced by the bridge's Save project path; the
      # symbols must be linked into the UI binary as well, not just the DSP.
      # In in-process formats (VST3/CLAP/...) DSP+UI share a binary so this
      # is a no-op duplicate; for LV2 the UI binary needs its own copy
      # (though at runtime the LV2-UI never actually constructs one because
      # shared DSP state is unreachable).
      packages/native/src/project/Project.cpp
      packages/native/src/lsdj/Effects.cpp
      packages/native/src/lsdj/KitUtil.cpp
      packages/native/src/lsdj/SampleCache.cpp
      packages/native/src/lsdj/KitCompiler.cpp
      packages/native/src/system/SystemBase.cpp
      packages/native/src/system/sameboy/LinkGroup.cpp
      packages/native/src/system/sameboy/RomSniffer.cpp
      ${RP_SAMEBOYSYSTEM_SRC}
      packages/native/src/system/sameboy/roles/ArduinoboyMaster.cpp
      packages/native/src/system/sameboy/roles/LsdjSyncRole.cpp
      packages/native/src/system/sameboy/roles/LsdjKitPatchRole.cpp
      packages/native/src/system/sameboy/roles/MgbPassthroughRole.cpp
      packages/native/src/system/mesen/MesenNesSystem.cpp
      packages/native/src/system/mesen/MesenNesDebugSession.cpp
      packages/native/src/system/mesen/MesenVideoDevice.cpp
      packages/native/src/system/mesen/MesenGbaSystem.cpp
      packages/native/src/system/mesen/roles/NesN8MidiRole.cpp
      ${UI_BUNDLE_C}
      ${DPFJS_PATH}/deps/dpf-widgets/generic/LVGL.cpp)

# setup build includes and libraries
target_include_directories(${NAME} PUBLIC packages/native/src)
target_include_directories(${NAME} PUBLIC ${DPFJS_PATH}/deps/dpf-widgets/generic)
# dpf.js framework env-var prefix (PluginUI's screenshot + dev-bundle reads use
# dpfjs::getenvWithPrefix). The consumer owns the prefix; RetroPlug = RETROPLUG_.
target_compile_definitions(${NAME} PUBLIC DPFJS_ENV_PREFIX="RETROPLUG_")
# dpfjs::core carries the framework links (lvgl-js-native, lvgl, tjs, rpcpp,
# miniz) + their include dirs (incl. txiki's miniz for MinizZip.hpp).
target_link_libraries(${NAME} PUBLIC dpfjs::core $<$<BOOL:${WIN32}>:winmm>)
target_link_libraries(${NAME} PUBLIC sameboy)
# Windows: SameBoySystem.cpp is compiled by clang-cl into this static lib (see the
# clang-cl isolation block above) instead of in the MSVC plugin sources.
target_link_libraries(${NAME} PUBLIC $<$<BOOL:${WIN32}>:retroplug-samebsys>)
target_link_libraries(${NAME} PUBLIC mesen)
target_link_libraries(${NAME} PUBLIC r8brain)
target_link_libraries(${NAME} PUBLIC enkiTS)
target_link_libraries(${NAME} PUBLIC miniaudio)
# efsw lands on the umbrella target so both DSP and UI sub-targets get the static
# lib (PluginRpcService.cpp, shared between FILES_DSP/FILES_UI, references the
# UserConfig watcher symbols). RetroPlug-specific; not part of dpfjs::core.
target_link_libraries(${NAME} PUBLIC efsw)

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

# (The dgl WebViewWin32 /std D9025 is now fixed in DPF itself — dpf_add_plugin
# only forces /std:c++17 on that file when the project builds below c++17.)

# --- Windows link-stage fixes (from the lvgl-template-plugin Windows build) ---
if(WIN32)
    # The JACK standalone's import lib (${NAME}.lib) and the umbrella static
    # library share an output name → LNK1149 when both are built. Rename the
    # umbrella so they no longer collide.
    if(TARGET ${NAME})
        set_target_properties(${NAME} PROPERTIES OUTPUT_NAME "${NAME}-common")
    endif()
    # DPF only exports the legacy VST3 entry points (InitDll/ExitDll) on Windows,
    # but VST3 SDK 3.7+ hosts (incl. FL Studio) look for ModuleEntry/ModuleExit.
    # Alias both names to the same functions.
    if(MSVC AND TARGET ${NAME}-vst3)
        target_link_options(${NAME}-vst3 PRIVATE
            "/EXPORT:ModuleEntry=InitDll"
            "/EXPORT:ModuleExit=ExitDll")
    endif()
endif()

# --- Tests (Catch2) ----------------------------------------------------------
# Gated so the default build stays small; opt in with -DBUILD_TESTING=ON.
option(BUILD_TESTING "Build the Catch2 unit tests" OFF)
if(BUILD_TESTING)
    add_subdirectory(deps/catch2)
    enable_testing()
    add_subdirectory(packages/native/test test)

endif()

# --- CLI tool ---------------------------------------------------------------
# Headless renderer + scripted-input runner. Useful for fast LSDJ renders
# and agent-driven workflows.
option(BUILD_CLI "Build the retroplug-cli headless renderer" ON)

# CLI bundle: packages/cli -> esbuild -> tjsc bytecode -> embedded in
# retroplug-cli, run by default (the no-Node end-user path). Mirrors the UI
# bundle. The vars must exist before add_subdirectory(cli) so retroplug-cli can
# list ${CLI_BUNDLE_C} as a (generated) source.
if(BUILD_CLI AND NODE_EXECUTABLE)
    set(CLI_BUNDLE_JS  "${CMAKE_BINARY_DIR}/cli/bundle.js")
    set(CLI_BUNDLE_C   "${CMAKE_BINARY_DIR}/cli/bundle_data.c")
    set(CLI_BUNDLE_DEP "${CMAKE_BINARY_DIR}/cli/bundle.d")
    set_source_files_properties(${CLI_BUNDLE_C} PROPERTIES GENERATED TRUE)
endif()

if(BUILD_CLI)
    add_subdirectory(packages/native/cli cli)
endif()

# --- cli-regenerate: typed TS client for the harness RPC surface ------------
# Derived artifact (build/generated/HarnessService.ts); never committed. Kept
# separate from ui-regenerate so the cli test client doesn't drag in the
# LVGL/UI build. Always runs (anti-stale, like ui-regenerate). The schema is
# dumped by the standalone harness-schema-dump exe (NOT retroplug-cli) so this
# stays decoupled from the binary that embeds the CLI bundle built off it.
if(BUILD_CLI AND NODE_EXECUTABLE)
    add_custom_target(cli-regenerate ALL
        BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/HarnessService.ts
        COMMAND ${NODE_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/gen-rpc-ts.js
                $<TARGET_FILE:harness-schema-dump>
                ${CMAKE_BINARY_DIR}/generated/HarnessService.ts
                HarnessService
        DEPENDS harness-schema-dump
        COMMENT "Regenerating harness RPC client (HarnessService.ts)"
        VERBATIM)
    # The TS CLI tests also consume the generated LSDj Sav zod/TS schema
    # (gb/lsdj/sav imports build/ui/generated/SavSchema.ts). Fold its regen in
    # here so every CLI-test path (pnpm test:cli, smoke, reaper authors) and a
    # plain full build produce it — same anti-stale contract as HarnessService.
    add_dependencies(cli-regenerate sav-regenerate)
endif()

# --- cli-bundle-regenerate: the embedded end-user CLI app bytecode -----------
# packages/cli -> esbuild (build-cli.js) -> tjsc bytecode (cli_bundle). Depends
# on cli-regenerate (HarnessService.ts) but NOT on retroplug-cli, so retroplug-cli
# can embed the result without a build cycle. Always runs (anti-stale, like
# ui-regenerate); copy_if_different guards the .o rebuild.
if(BUILD_CLI AND NODE_EXECUTABLE)
    add_custom_target(cli-bundle-regenerate ALL
        BYPRODUCTS ${CLI_BUNDLE_JS}
                   ${CLI_BUNDLE_DEP}
                   ${CLI_BUNDLE_C}
                   ${CLI_BUNDLE_C}.new
        COMMAND ${NODE_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/build-cli.js
                ${CLI_BUNDLE_JS} ${CLI_BUNDLE_DEP}
        COMMAND ${_TJSC_COMMAND}
                -m -s -p cli_
                -o ${CLI_BUNDLE_C}.new
                ${CLI_BUNDLE_JS}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CLI_BUNDLE_C}.new ${CLI_BUNDLE_C}
        COMMENT "Regenerating CLI bundle (esbuild + bytecode)"
        VERBATIM)
    add_dependencies(cli-bundle-regenerate cli-regenerate ${_TJSC_DEP})
    # retroplug-cli compiles ${CLI_BUNDLE_C}; make sure it's generated first.
    add_dependencies(retroplug-cli cli-bundle-regenerate)
endif()

# The dpf.js seam probe (examples/minimal) now lives in the dpf.js repo and is
# built there with -DDPFJS_BUILD_EXAMPLE=ON.

