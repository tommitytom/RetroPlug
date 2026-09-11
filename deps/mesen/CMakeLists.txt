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

# Lua picks `tmpnam` over `mkstemp` for os.tmpname unless it is told the platform is POSIX
# (Lua/loslib.c:111-135, gated on LUA_USE_POSIX). glibc marks `tmpnam` with a .gnu.warning section, so
# every binary that links libmesen.a got "the use of `tmpnam' is dangerous, better use `mkstemp'" from
# the LINKER - eight times in a full build, and once more on every incremental relink. That one is not
# suppressible with a compiler flag; the fix is to stop referencing the symbol, which is what the
# warning asks for and what any POSIX Lua build does.
#
# LUA_USE_POSIX and not LUA_USE_LINUX/MACOSX: those two add LUA_USE_DLOPEN (and a -ldl dependency) on
# top, which this build has no use for. luaconf.h never reads the macro itself - only the .c files do -
# so the Lua sources are the whole blast radius, and Mesen's C++ Lua consumers are unaffected.
if(NOT WIN32)
    set(MESEN_LUA_SOURCES ${MESEN_C_SOURCES})
    list(FILTER MESEN_LUA_SOURCES INCLUDE REGEX "/Lua/")
    set_source_files_properties(${MESEN_LUA_SOURCES} PROPERTIES COMPILE_DEFINITIONS LUA_USE_POSIX)
endif()

add_library(mesen STATIC ${MESEN_CPP_SOURCES} ${MESEN_C_SOURCES})

# SYSTEM: the -w below is PRIVATE, so it does nothing for the RetroPlug sources that include Mesen's
# headers (MesenNesSystem.cpp and the rest of src/system/mesen). Those headers are upstream code too, so
# a consumer gets them as -isystem and is not held responsible for what they warn about - while its own
# warnings stay exactly as loud as they were.
target_include_directories(mesen SYSTEM PUBLIC
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

# LTO for optimized configs only (Debug stays fast to build). Intra-mesen cross-TU inlining across the
# hot CPU/PPU/APU stepping loop, which is ~99% of the audio-thread cost. Measured ~10% faster NES core
# on the Cortex-A53 handheld (nes-bench: xRT 0.83 -> 0.92 over plain -O3). The single biggest safe lever
# for NES perf there — JS is ~0.5% of the block, the emulator core dominates. Free at runtime.
#
# Not free at BUILD time, though: it makes every mesen object LTO bitcode, so each of the nine binaries
# that link this lib re-runs codegen over all ~430 of them. That's why it's opt-in (RETROPLUG_MESEN_LTO,
# declared in the root CMakeLists) rather than always-on — release.yml turns it on so shipped binaries
# get the speedup, and the dev loop doesn't pay for it. Toggling the option recompiles mesen.
if(RETROPLUG_MESEN_LTO)
    set_target_properties(mesen PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
        INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON
    )
    # Under LTO the codegen happens at LINK time, in a process that takes its warning flags from the LINK
    # command line - so the -w further down, which is a per-TU compile option, does not reach it. Two of
    # Mesen's warnings come back at every one of the nine links, and one of them (-Wodr on
    # Utilities/NTSC/nes_ntsc_impl.h) is a whole-program check that exists ONLY at link time and has no
    # compile-time form at all. That pair is what "mesen scrolls past on every build" has always been.
    #
    # INTERFACE, so it lands on the link line of each consumer rather than on mesen's own (a static lib
    # is archived, not linked). It reaches lto1 only: real linker diagnostics - ld's own warnings, the
    # .gnu.warning sections - are the linker's and stay visible, on these binaries and every other.
    #
    # Scoped inside this if(): with LTO off there is no link-time codegen to quiet, and leaving the
    # default dev-loop link lines untouched keeps any future link diagnostic loud.
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(mesen INTERFACE -w)
    endif()
endif()

if(WIN32)
    # Mesen's Win32 code assumes the UNICODE API (wchar_t buffers + GetComputerName,
    # WSAAddressToString, …). Without UNICODE these resolve to the ANSI variants and
    # fail to compile (UPnPPortMapper.cpp). Link the Win32 libs Mesen uses: winmm
    # (timeBeginPeriod), ws2_32 (sockets / WSAAddressToString), iphlpapi (UPnP).
    target_compile_definitions(mesen PRIVATE UNICODE _UNICODE)
    target_link_libraries(mesen PUBLIC winmm ws2_32 iphlpapi)
endif()

# Mesen is upstream-tracked; suppress its warnings rather than patching. `-w` rather than the curated
# -Wno-* list this used to carry, for two reasons the list could not fix:
#
#   - It could not silence the loudest ones. `NesPpu.h`'s "inline function ... used but never defined"
#     (16 of them, from HdNesPpu.cpp and friends) is emitted with NO [-W...] tag, so GCC offers no flag
#     that turns it off. Only -w does.
#   - Two of its entries (-Wno-reorder, -Wno-class-memaccess) are C++/ObjC++-only, and the list was
#     applied to the whole target - including the ~73 Lua / SevenZip / Utilities C sources. cc1 then
#     warned about the flags themselves, twice per C file: 146 warning lines, the bulk of a clean
#     build's noise, all of it produced by the warning suppression.
#
# Same treatment as the other vendored cores (cmake/sameboy.cmake, cmake/serial.cmake) and the same
# rationale: we do not own this code, and a clean build should be quiet enough that a warning in code we
# DO own is visible. Our own sources keep every warning they had - this is PRIVATE to the mesen target.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mesen PRIVATE -w)
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
