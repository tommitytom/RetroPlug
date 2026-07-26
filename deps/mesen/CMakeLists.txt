# Vendored Mesen2 emulator core, built as a static lib for RetroPlug.
#
# Per the porting plan: include the whole tree as-is. Mesen's internal coupling
# makes per-system pruning a project on its own; binary-size optimisation is
# deferred to a later pass. We just glob every .cpp/.c under the four roots
# (Core/, Utilities/, Lua/, SevenZip/) and let the linker drop unreferenced
# objects from the final plugin.

file(GLOB_RECURSE MESEN_CPP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/*.cpp")

file(GLOB_RECURSE MESEN_C_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Lua/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/SevenZip/*.c")

# Lua bundles its own `lua.c` (the standalone REPL) which has a `main()`.
# Drop it — we link Lua as a library only.
list(FILTER MESEN_C_SOURCES EXCLUDE REGEX "/lua\\.c$")
list(FILTER MESEN_C_SOURCES EXCLUDE REGEX "/luac\\.c$")

# luasocket's POSIX backends (unix-domain sockets, termios serial) need
# <sys/un.h> / termios and don't build on Windows — luasocket uses wsocket.c
# there instead. Drop the unix-only sources on Windows.
if(WIN32)
    list(FILTER MESEN_C_SOURCES EXCLUDE REGEX "/(unix|unixstream|unixdgram|usocket|serial)\\.c$")
endif()

# Mesen ships miniz v1.15 (2013); txiki vendors miniz v3.1.0. Linking both
# implementations causes mz_* symbol collisions. Drop Mesen's miniz.cpp and
# let txiki's libminiz provide the symbols. The two ABIs are roughly
# compatible at the API level used by Mesen's ZipReader/PNGHelper/etc., so
# Mesen sources that include "miniz.h" will still compile. ZIP-archived ROM
# loading hasn't been verified end-to-end on this path; n8-midi loads raw
# .nes files so it doesn't matter for the Phase A goal.
# NOTE: txiki vendors miniz v3.1.0 with different mz_zip_archive layout from
# Mesen's v1.15. Sharing one impl is UB. We instead prefix all Mesen miniz
# symbols at compile time via a `-include` of `mesen_miniz_renames.h`. The
# header is force-included on EVERY Mesen TU (a no-op preprocessor cost on
# files that don't touch miniz; saves us from hunting transitive includes
# like ZipReader.h / CompressionHelper.h / spng.h fan-out one-by-one).
set(MESEN_MINIZ_RENAMES_H "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/mesen_miniz_renames.h")
# MSVC cl.exe uses /FI for force-include; the GNU -include flag is silently
# mis-parsed (it ignored -include and treated the header path as a stray arg,
# so the rename never applied AND every TU compiled to an empty object).
if(MSVC)
    set(_mesen_force_inc "/FI${MESEN_MINIZ_RENAMES_H}")
else()
    set(_mesen_force_inc "-include;${MESEN_MINIZ_RENAMES_H}")
endif()
set_source_files_properties(${MESEN_CPP_SOURCES} ${MESEN_C_SOURCES} PROPERTIES
    COMPILE_OPTIONS "${_mesen_force_inc}")

add_library(mesen STATIC ${MESEN_CPP_SOURCES} ${MESEN_C_SOURCES})

target_include_directories(mesen PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"            # so Core/* and Utilities/* paths resolve
    "${CMAKE_CURRENT_SOURCE_DIR}/Core"       # so `#include "pch.h"` resolves
    "${CMAKE_CURRENT_SOURCE_DIR}/Utilities"  # sibling-file includes within Utilities/
    "${CMAKE_CURRENT_SOURCE_DIR}/Lua"        # Lua headers
)

set_target_properties(mesen PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    # LTO for optimized configs only (Debug stays fast to build). Intra-mesen cross-TU inlining across the
    # hot CPU/PPU/APU stepping loop, which is ~99% of the audio-thread cost. Measured ~10% faster NES core
    # on the Cortex-A53 handheld (nes-bench: xRT 0.83 -> 0.92 over plain -O3). The single biggest safe lever
    # for NES perf there — JS is ~0.5% of the block, the emulator core dominates. Free at runtime.
    INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
    INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON
)

if(WIN32)
    # Mesen's Win32 code assumes the UNICODE API (wchar_t buffers + GetComputerName,
    # WSAAddressToString, …). Without UNICODE these resolve to the ANSI variants and
    # fail to compile (UPnPPortMapper.cpp). Link the Win32 libs Mesen uses: winmm
    # (timeBeginPeriod), ws2_32 (sockets / WSAAddressToString), iphlpapi (UPnP).
    target_compile_definitions(mesen PRIVATE UNICODE _UNICODE)
    target_link_libraries(mesen PUBLIC winmm ws2_32 iphlpapi)
endif()

# Mesen is upstream-tracked; suppress its warnings rather than patching.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mesen PRIVATE
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-unused-but-set-variable
        -Wno-switch
        -Wno-sign-compare
        -Wno-reorder
        -Wno-deprecated-declarations
        -Wno-class-memaccess
        -Wno-missing-field-initializers
        -Wno-multichar
        -Wno-strict-aliasing
        -Wno-parentheses
        -Wno-format
        -Wno-implicit-fallthrough
    )
    # Lua's C sources in particular trigger these.
    set_source_files_properties(${MESEN_C_SOURCES} PROPERTIES
        COMPILE_FLAGS "-Wno-implicit-function-declaration -Wno-unused-result")
endif()

# Use Mesen's own precompiled header to keep build time down.
# pch.h is C++-only (includes <memory>, <thread>, ...); the generator
# expression keeps it off the Lua/SevenZip C sources.
# NOTE (Windows/MSVC): disabled — CMake's /Yu PCH left every Core TU's body
# uncompiled (1 KB objs, all definitions missing → unresolved externals at link).
# Sources #include "pch.h" directly, so dropping the CMake PCH is functionally a
# no-op (just slower to build).
if(NOT WIN32)
    target_precompile_headers(mesen PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/Core/pch.h>")
endif()
