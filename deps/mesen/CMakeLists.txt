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
set_source_files_properties(${MESEN_CPP_SOURCES} ${MESEN_C_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-include;${MESEN_MINIZ_RENAMES_H}")

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
)

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
target_precompile_headers(mesen PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/Core/pch.h>")
