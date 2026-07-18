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

# Optional DSP-thread profiling instrumentation (spec/08-profiling.md). Like RETROPLUG_SANITIZE, use a
# SEPARATE build dir (build-prof/, via tools/run-profile.sh) — never the load-bearing build/.
# Activates the bare-QuickJS counting allocator + the dsp* profiling RPCs (DspRuntime.cpp guards on the
# RETROPLUG_PROFILE macro), and adds -g globally so valgrind (DHAT/Callgrind) attributes into quickjs.c.
# Set here, before the dep subdirectories, so deps + src all get -g. The default build/ and the shipped
# plugin never define this, so they carry zero profiling overhead.
option(RETROPLUG_PROFILE "Build the DSP-thread profiling instrumentation + -g (use build-prof/)" OFF)
if(RETROPLUG_PROFILE)
    add_compile_definitions(RETROPLUG_PROFILE)
    add_compile_options(-g -fno-omit-frame-pointer)
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
# WebAssembly (txiki's WAMR runtime) is unused by RetroPlug and blocks universal
# macOS builds: WAMR selects its SIMD target from a single configure-time arch,
# so it can't emit both arm64 (NEON) and x86_64 slices from one tree. Disable it
# for every build so dev / CI / release stay consistent. Set (non-FORCE) before
# dpf.js is added so txiki's option() adopts it; -DBUILD_WITH_WASM=ON still wins.
set(BUILD_WITH_WASM OFF CACHE BOOL "txiki WebAssembly/WAMR support")

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
# Only SDL_INIT_GAMECONTROLLER is initialised — see packages/native/src/input/GamepadManager.cpp.
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
    # Two ways to get SDL2 on macOS:
    #  - Default: Homebrew (`brew install sdl2`, which today installs the
    #    sdl2-compat shim over SDL3) ships an sdl2.pc, so pkg-config resolves
    #    it exactly like the Linux path. This is the dev default.
    #  - Distribution: drop the official universal SDL2.framework into
    #    ~/Library/Frameworks (or /Library/Frameworks) and configure with
    #    -DRETROPLUG_SDL2_FRAMEWORK=ON. find_library then picks the right arch
    #    slice for each CMAKE_OSX_ARCHITECTURES value automatically, so one fat
    #    framework satisfies single-arch and universal builds without per-arch wiring.
    if(RETROPLUG_SDL2_FRAMEWORK)
        find_library(SDL2_FRAMEWORK SDL2 REQUIRED)
        add_library(PkgConfig::SDL2 INTERFACE IMPORTED)
        target_link_libraries(PkgConfig::SDL2 INTERFACE ${SDL2_FRAMEWORK})
        target_include_directories(PkgConfig::SDL2 INTERFACE
            ${SDL2_FRAMEWORK}/Headers)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    endif()
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

# --- efsw: cross-platform filesystem watcher backing the native file watcher
# (NativeFileWatcher → HostRpcService::drainChangedPaths, spec/07). EXCLUDE_FROM_ALL so only the
# static lib the plugin links (efsw-static) compiles — a build that never links it pays nothing.
# efsw's own CMake links Threads::Threads to the shared lib only, so consumers of efsw-static add it
# themselves (done on retroplug-backend). BUILD_TEST_APP / EFSW_INSTALL default OFF as a subproject.
#
# efsw only defines the efsw-static target we link when BUILD_STATIC_LIBS is ON. Its option() defaults
# ON, but respects any pre-existing cache value — and this build dir persists across branch switches, so
# a stale/global BUILD_STATIC_LIBS=OFF can linger and silently drop the target. Linking a non-existent
# `efsw-static` then degrades to a bogus `-lefsw-static` link flag with NO include dir, so efsw/efsw.hpp
# isn't found. Force it ON here so efsw-static always exists (BUILD_SHARED_LIBS stays OFF — static build).
set(BUILD_STATIC_LIBS ON CACHE BOOL "Build efsw as a static library" FORCE)
add_subdirectory(deps/efsw "${CMAKE_BINARY_DIR}/deps/efsw" EXCLUDE_FROM_ALL)

# lvgl-js-native + the framework PIC settings now come from dpf.js (above).

# --- mgb-rom: the mGB Game Boy MIDI-synth ROM, embedded in the binary ---------
# bin2c turns resources/roms/mGB.gb into an external-linkage byte array; a small
# static lib so every consumer just links it (no per-source-list / cross-dir
# generated-source wiring). PluginRpcService::loadMgb builds a system from it,
# and Project::addSystem re-supplies it for saved projects (see EmbeddedRoms.hpp).
# Fixed, git-tracked asset → a timestamp-tracked rule (regen only when it changes).
set(MGB_ROM_C "${CMAKE_BINARY_DIR}/generated/roms/mgb_rom_data.c")
add_custom_command(
    OUTPUT  "${MGB_ROM_C}"
    COMMAND ${CMAKE_COMMAND}
            -DINPUT=${CMAKE_SOURCE_DIR}/resources/roms/mGB.gb
            -DSYMBOL=mgb_rom
            -DOUTPUT=${MGB_ROM_C}
            -P ${CMAKE_SOURCE_DIR}/cmake/bin2c.cmake
    DEPENDS "${CMAKE_SOURCE_DIR}/resources/roms/mGB.gb"
            "${CMAKE_SOURCE_DIR}/cmake/bin2c.cmake"
    COMMENT "bin2c mgb_rom_data.c"
    VERBATIM)
add_library(mgb-rom STATIC "${MGB_ROM_C}")
set_target_properties(mgb-rom PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Cross-compile knob for tjsc (bytecode compiler). Consumed by the
# cp-bundle / ui-bundle targets (packages/native) via _TJSC_COMMAND / _TJSC_DEP.
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

# --- Catch2: the C++ unit-test framework for the on-demand plugin test binaries
# (retroplug-plugin-test / retroplug-classid-test, built + run by `pnpm test:plugin`).
# From the deps/catch2 submodule; EXCLUDE_FROM_ALL so the library only compiles when a
# test target links it — a normal / plugin build never pays for it. dpf.js keeps its own
# host tests behind DPFJS_BUILD_TESTS (default OFF), so there's no duplicate Catch2 target.
set(CATCH_INSTALL_DOCS   OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/catch2 "${CMAKE_BINARY_DIR}/deps/catch2" EXCLUDE_FROM_ALL)

# --- the native package: retroplug-core (shared emulator/Project lib) + the Backend / txiki host
# + the DPF plugin + the test host / cli / ui-test.
add_subdirectory(packages/native native)

# SDL2 gamepad input: link onto the UI aggregator so every UI-bearing variant
# (clap/vst3/jack) gets the GamepadManager poll compiled into FILES_UI. DSP-only outputs never see SDL.
if(TARGET retroplug-ui)
    target_link_libraries(retroplug-ui PUBLIC PkgConfig::SDL2)
endif()

# The dpf.js seam probe (examples/minimal) now lives in the dpf.js repo and is
# built there with -DDPFJS_BUILD_EXAMPLE=ON.

