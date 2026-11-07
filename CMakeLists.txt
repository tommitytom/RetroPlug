# DISTRHO Plugin Framework (DPF)
# Copyright (C) 2021-2024 Filipe Coelho <falktx@falktx.com>
# SPDX-License-Identifier: ISC

cmake_minimum_required(VERSION 3.27...3.31)  # 3.27: deps/efsw's own minimum (the packages need 3.19)

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
# Activates the script host's audio-runtime profiling + the dsp* profiling RPCs (ScriptHost.cpp guards on the
# RETROPLUG_PROFILE macro), and adds -g globally so valgrind (DHAT/Callgrind) attributes into quickjs.c.
# Set here, before the dep subdirectories, so deps + src all get -g. The default build/ and the shipped
# plugin never define this, so they carry zero profiling overhead.
option(RETROPLUG_PROFILE "Build the DSP-thread profiling instrumentation + -g (use build-prof/)" OFF)
if(RETROPLUG_PROFILE)
    add_compile_definitions(RETROPLUG_PROFILE)
    add_compile_options(-g -fno-omit-frame-pointer)
endif()

# --- LTO on the Mesen NES core: ~10% faster on the Cortex-A53 handheld, but it costs the dev loop
# dearly, so it's opt-in and the release workflow turns it on (see deps/mesen/CMakeLists.txt).
#
# Mesen's ~430 objects become LTO bitcode, and EVERY binary that links the static lib re-runs codegen
# over all of them: retroplug{,-cli,-host,-ui-test,-sdl} + the four plugin variants. Changing a single
# .cpp measured 1m05s wall / 26m51s CPU with it on, and 2s wall / ~9s CPU with it off (32 threads) —
# the compile is one file either way, the rest is eight full LTO links. Off by default so `./build.sh`
# stays quick; release.yml
# passes -DRETROPLUG_MESEN_LTO=ON so shipped binaries keep the speedup.
option(RETROPLUG_MESEN_LTO "LTO the Mesen NES core (~10% faster, much slower links; on for releases)" OFF)

# tell LVGL where to find its config file. Set before add_subdirectory(dpf.js)
# so RetroPlug's lv_conf.h wins over the framework's default (CACHE no-FORCE).
set(LV_BUILD_CONF_PATH "${PROJECT_SOURCE_DIR}/packages/native/src/lv_conf.h" CACHE PATH "")

# <windows.h> defines min()/max() macros that break std::numeric_limits<T>::max()
# and std::min/max. NOMINMAX suppresses them - safe and standard; unlike
# WIN32_LEAN_AND_MEAN it removes no headers, so it's global.
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
# the framework forks are fixed at source. The deps RetroPlug pulls in ITSELF are
# quieted here instead - see retroplug_silence_dep_warnings below, plus the two
# vendored cores that need more than a flag (SameBoy: cmake/sameboy.cmake;
# Mesen: deps/mesen/CMakeLists.txt).
add_subdirectory("${DPFJS_PATH}" dpfjs)

# --- The UI's monospaced faces. lv_conf.h sets every LV_FONT_MONTSERRAT_* to 0, so LVGL
# compiled none of its own font objects; these supply those same seven symbol names built
# from DejaVu Sans Mono (tools/gen-mono-fonts.sh regenerates them).
#
# They go on the `lvgl` target rather than one of ours because that is where the objects
# they replace used to live. Both lvgl itself (LV_FONT_DEFAULT) and the binding's
# builtin_font_list reference these symbols, and a definition sitting in some other static
# library would leave the link order-dependent - fine here, a missing symbol on another
# platform's linker. Same archive, no ordering question.
file(GLOB RETROPLUG_MONO_FONTS "${PROJECT_SOURCE_DIR}/packages/native/src/fonts/lv_font_montserrat_*.c")
if(NOT RETROPLUG_MONO_FONTS)
    message(FATAL_ERROR "No fonts in packages/native/src/fonts/ — run tools/gen-mono-fonts.sh.")
endif()
target_sources(lvgl PRIVATE ${RETROPLUG_MONO_FONTS})

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

# --- Third-party warning suppression -----------------------------------------
# The deps RetroPlug adds itself (as opposed to the ones dpf.js owns and already quiets) are upstream
# code we don't author, so a warning in one tells us nothing we can act on and hides the ones in code we
# DO own. `-w` PRIVATE on the dep's own target: our sources are untouched and keep every warning.
#
# Call it AFTER the add_subdirectory that defines the target - the flag appends to whatever the dep set
# for itself, so it wins over a -Wall the dep raises. Targets are optional: several deps are
# EXCLUDE_FROM_ALL and only exist in some configurations, hence the per-target TARGET guard.
#
# GNU/Clang only, mirroring cmake/sameboy.cmake. On MSVC a second /W-family flag trips command-line
# warning D9025 on any dep that sets its own /W3 or /W4 - which is itself a warning, and not
# /wd-suppressible. dpf.js's CMakeLists has the escape hatch if Windows noise ever needs addressing: an
# explicit /wd#### list per observed code. Nothing here has been measured on MSVC, so nothing is guessed.
function(retroplug_silence_dep_warnings)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        return()
    endif()
    foreach(_tgt IN LISTS ARGN)
        if(TARGET ${_tgt})
            target_compile_options(${_tgt} PRIVATE -w)
        endif()
    endforeach()
endfunction()

# --- SameBoy: Game Boy emulator core (submodule at deps/sameboy) ---
include(cmake/sameboy.cmake)

# --- wjwwood/serial: USB-serial for the Everdrive N8 Pro bridge (submodule at deps/serial) ---
include(cmake/serial.cmake)

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
    # A custom command gets no header scan, so it rebuilds on the headers listed here and nothing else. A
    # stale object is not a slow build but a crash: change a class layout or a vtable (SystemBase's virtuals)
    # and the MSVC objects agree on the new one while this one keeps the old, every slot after the change
    # off by one. These are the directories its includes reach (the boot-ROM headers come with
    # sameboy_bootroms); one it starts including from belongs here too.
    file(GLOB _sbs_headers CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/packages/native/src/system/*.hpp"
        "${CMAKE_SOURCE_DIR}/packages/native/src/system/sameboy/*.hpp"
        "${CMAKE_SOURCE_DIR}/packages/native/src/transport/*.hpp"
        "${CMAKE_SOURCE_DIR}/packages/native/src/util/*.hpp"
        "${CMAKE_SOURCE_DIR}/packages/node-sdk/native/src/util/*.hpp"
        "${CMAKE_SOURCE_DIR}/deps/sameboy/Core/*.h")
    add_custom_command(
        OUTPUT "${_sbs_obj}"
        COMMAND "${RP_CLANG_CL}" /nologo /c /MT /O2 /Ob2 /DNDEBUG /EHsc /std:c++20 /Zc:__cplusplus
            "/FI${CMAKE_BINARY_DIR}/sameboy-clang/sameboy_version.h"
            # GB_DISABLE_* MUST match the sameboy core or GB_gameboy_t's layout diverges.
            -DGB_DISABLE_TIMEKEEPING -DGB_DISABLE_DEBUGGER
            -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DNOGDI -D_CRT_SECURE_NO_WARNINGS
            "-I${CMAKE_SOURCE_DIR}/packages/native/src"
            "-I${CMAKE_SOURCE_DIR}/packages/node-sdk/native/src"  # util/FixedQueue.hpp
            "-I${CMAKE_SOURCE_DIR}/deps/sameboy/Core"
            "-I${CMAKE_BINARY_DIR}/generated"
            "-I${CMAKE_BINARY_DIR}/_deps/reflectcpp-src/include"
            "/Fo${_sbs_obj}" "${_sbs_src}"
        DEPENDS "${_sbs_src}" ${_sbs_headers} sameboy_bootroms
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
# Headers only: packages/dsp/native/src/dsp/R8bBase.cpp compiles its one source (r8bbase.cpp) on the throwing
# allocator, so the tree declares no target for the vendored library and there is none to link.

# --- enkiTS: a task scheduler, vendored under deps/enkiTS but not built. No RetroPlug code uses it, so the tree adds
# no target for it and links none; the directory stays for a later cleanup to judge.

# --- MicroQuickJS: the JavaScript engine a device script pack runs on (docs/modular-graph-phase-e4-design.md), vendored
# under deps/mquickjs. Its host tools, mqjs_stdlib and mqjs (which the device-pack toolchain compiles a pack with), are
# EXCLUDE_FROM_ALL; cmake/mquickjs.cmake says how they are built.
include(cmake/mquickjs.cmake)

# --- miniaudio: single-header audio file decoder (WAV / MP3 / FLAC).
# Used by SampleCache, the decode an offline job's file input takes.
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
retroplug_silence_dep_warnings(efsw-static efsw)

# lvgl-js-native + the framework PIC settings now come from dpf.js (above).

# --- mgb-rom: the mGB Game Boy MIDI-synth ROM, embedded in the binary ---------
# bin2c turns resources/roms/mGB.gb into an external-linkage byte array; a small
# static lib so every consumer just links it (no per-source-list / cross-dir
# generated-source wiring). A construct spec carries `embeddedRom: "mgb"` instead of a
# path, and SameBoyBackend resolves the marker to these bytes (see EmbeddedRoms.hpp),
# so a saved project reloads mGB with no file on disk.
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
retroplug_silence_dep_warnings(Catch2 Catch2WithMain)

# --- the workspace packages (cmake/packages.cmake, docs/modular-graph-phase-e1-design.md section 4): each reads its
# manifest from its package.json and adds its native/ tree.
include(cmake/packages.cmake)
# The shape every native test binary shares (see the file for why), for packages/native's and the packs' alike.
include(cmake/test-binary.cmake)

# The packages a desktop product carries (retroplug_product), every one today; a product that differs lists its own.
# The LIST is the root package.json's retroplug.products.desktop, read here and by the TS generator
# (packages/retroplug/scripts/lib/packages.mjs), so the two languages cannot register different products, and a package
# follows the ones it depends on (the generator refuses a list that does not).
# The ORDER is load-bearing in both languages that read it. In CMake it is the order a product's node types register,
# which the registration golden holds (packages/native/test/engine-graph/golden/product-registrations.json): the carved
# packages come first, in the order they were carved - rp.core, rp.dsp, rp.dsp.resample, rp.host (graph-runtime, after
# dsp for that reason alone), then rp.n8 (the n8 pack's rp.n8.nes, after rp.host.audio-out and before rp.sameboy.gb) -
# then the types still in packages/native, then the device script packs, rp.bliptoaster last, so a pack's types move
# none of the others. In the generator it is the order that product's TS contributions concatenate in. There it is
# behaviour for the ROM providers alone (the first to match a ROM fixes a system's role-list order); roles, kit formats
# and tracker integrations are looked up by kind, id or an exclusive marker role, so a duplicate of one of those is
# what the generated module refuses, not a reordering.
set(_rp_root_manifest "${CMAKE_SOURCE_DIR}/package.json")
# A configure input, so editing the list re-runs the configure retroplug_product() needs before it reads it. The whole
# file is the input (CMake cannot depend on one JSON member), so an edit to its scripts reconfigures too; that is the
# price of the list living where both languages read it, and a reconfigure of this tree is cheap.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_rp_root_manifest}")
file(READ "${_rp_root_manifest}" _rp_root_json)
string(JSON _rp_products_kind ERROR_VARIABLE _rp_products_error TYPE "${_rp_root_json}" retroplug products desktop)
if(_rp_products_error)
    message(FATAL_ERROR "${_rp_root_manifest} has no retroplug.products.desktop list (${_rp_products_error})")
endif()
if(NOT _rp_products_kind STREQUAL "ARRAY")
    message(FATAL_ERROR "retroplug.products.desktop in ${_rp_root_manifest} is a ${_rp_products_kind}, not a list")
endif()
string(JSON _rp_products_count LENGTH "${_rp_root_json}" retroplug products desktop)
if(_rp_products_count EQUAL 0)
    message(FATAL_ERROR "retroplug.products.desktop in ${_rp_root_manifest} lists no package")
endif()
set(RP_DESKTOP_PACKAGES "")
math(EXPR _rp_products_last "${_rp_products_count} - 1")
foreach(_rp_i RANGE ${_rp_products_last})
    string(JSON _rp_package GET "${_rp_root_json}" retroplug products desktop ${_rp_i})
    # Read as JSON, so a number or an object would arrive as its JSON text and name no package directory.
    string(JSON _rp_package_kind ERROR_VARIABLE _rp_package_error TYPE "${_rp_root_json}" retroplug products desktop ${_rp_i})
    if(_rp_package_error OR NOT _rp_package_kind STREQUAL "STRING")
        message(FATAL_ERROR "retroplug.products.desktop's entry ${_rp_i} in ${_rp_root_manifest} is a "
                            "${_rp_package_kind}, not a package name")
    endif()
    list(APPEND RP_DESKTOP_PACKAGES "${_rp_package}")
endforeach()

# The shell imports every workspace package it declares BY PACKAGE NAME (its sources, and the modules the generator
# writes into packages/retroplug/src/generated), and pnpm links a workspace package into the shell's node_modules only
# when the shell declares it. A missing link is esbuild's bare "Could not resolve" with no hint of the cause, so
# configure says what to do: the declaration and the link must both be there, for every @retroplug/* the shell names -
# including a package that contributes no registration and so never appears in a product list, which the generator's
# own refusal only covers for packages that contribute.
file(READ "${CMAKE_SOURCE_DIR}/packages/retroplug/package.json" _rp_shell_manifest_json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_SOURCE_DIR}/packages/retroplug/package.json")
string(JSON _rp_shell_deps_kind ERROR_VARIABLE _rp_shell_error TYPE "${_rp_shell_manifest_json}" dependencies)
if(_rp_shell_error)
    message(FATAL_ERROR "packages/retroplug/package.json has no dependencies object (${_rp_shell_error}): every "
                        "@retroplug/* it imports must be declared, or this configure cannot check the links")
endif()
# Read from the packages' side, because CMake's JSON reader enumerates an array's members and not an object's keys:
# every workspace package, kept when the shell declares it. A declaration of a name that is no package here is the
# boundary check's to refuse.
file(GLOB _rp_workspace_dirs LIST_DIRECTORIES TRUE "${CMAKE_SOURCE_DIR}/packages/*")
foreach(_rp_dir IN LISTS _rp_workspace_dirs)
    if(NOT EXISTS "${_rp_dir}/package.json")
        continue()
    endif()
    get_filename_component(_rp_declared "${_rp_dir}" NAME)
    string(JSON _rp_dep_kind ERROR_VARIABLE _rp_dep_error TYPE "${_rp_shell_manifest_json}"
           dependencies "@retroplug/${_rp_declared}")
    if(_rp_dep_error)
        continue()
    endif()
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/packages/retroplug/node_modules/@retroplug/${_rp_declared}")
        message(FATAL_ERROR "packages/retroplug/node_modules/@retroplug/${_rp_declared} is missing: the shell "
                            "declares and imports @retroplug/${_rp_declared} by name, so run `pnpm install`")
    endif()
endforeach()

# Every package a desktop product lists is registered by iterating that list, so a new package's registration is its
# manifest plus the list entry and no CMake edit a third-party pack can forget. The two lists around it are the packages
# no product lists and a device build needs, placed where the loop's own ordering requires them: the node SDK first,
# because every other package's library is built on it; a device host's own I/O (rp.device.midi-in, the plan exporter's
# and the devices') and the device check's test types (rp.devtest.plan) last, because registering a script pack needs
# the MicroQuickJS host the graph-runtime package adds. packages/native is in the product list and registers the node
# types
# not yet carved into packages of their own (sameboy, mesen, host.io, launchpad); its targets are defined below, where
# the root adds the engine's own tree. @retroplug/tracker is in neither list: it contributes no registration of any
# kind, only modules the shell and the cart packages import, so CMake is never told about it.
set(_rp_first_packages node-sdk)          # first: every other package's library is built on it
set(_rp_last_packages device-io devtest)  # after the list: a script pack needs graph-runtime's host
foreach(_rp_package IN LISTS _rp_first_packages RP_DESKTOP_PACKAGES _rp_last_packages)
    retroplug_package(${_rp_package})
endforeach()

# The device check's case generator, a desktop tool listing what the RP2350 check registers (a device product of the
# packages above); the check itself is packages/graph-runtime/device-check, a Pico SDK project of its own.
add_subdirectory(packages/graph-runtime/device-check/cases "${CMAKE_BINARY_DIR}/device-check/cases")
# The plan exporter, the desktop tool a device image's build runs to embed its built-in plan (a device product of the
# packages above, with their device builds beside it to size the plan as the device will).
add_subdirectory(packages/graph-runtime/export-plan "${CMAKE_BINARY_DIR}/export-plan")
# Every package's native targets, held to what its manifest declares, deferred to the end of this directory as the
# product check is: a check run while a package is configured sees only the targets defined before it, and links to a
# later package or to the shell below would pass unseen. Every target is attributed to the package whose directory holds
# its SOURCE_DIR, so a helper no manifest names is still that package's.
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL retroplug_check_package_boundaries)

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

