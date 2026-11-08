# Vendored Mesen2 emulator core, built as a static lib for RetroPlug.
#
# We glob every .cpp/.c under the two roots (Core/, Utilities/) plus Lua's and SevenZip's C sources, and drop the
# consoles RetroPlug never runs through the MESEN_ENABLE_* options below: RetroPlug emulates the NES, the GBA and the
# SMS/GG, so the root CMakeLists sets the Super Nintendo (with the Game Boy), PC Engine and WonderSwan options OFF.
# An object the linker never pulls would not have cost the build anything, but an archive member IS pulled whole:
# mesen's consoles reference each other (Super Game Boy, the ST018 coprocessor), so the cut is a compile-out and every
# console costs its own translation units until it is switched off. The filter's table is the record of what each
# switch drops, so a further console is one more row.

file(GLOB_RECURSE MESEN_CPP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/*.cpp")

file(GLOB_RECURSE MESEN_C_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Lua/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/SevenZip/*.c")

# Mesen's console compile-outs: one option per console RetroPlug may drop, ON by default, which is how upstream Mesen
# builds (the vendored directory still builds as Mesen). Each is a PUBLIC compile definition, so a consumer including
# Mesen's headers sees the same guards the library was built with (Emulator.cpp's ROM detection among them).
#
# SNES covers the Game Boy too: the two are coupled through Super Game Boy (a SNES constructs a Game Boy, and the Game
# Boy's SGB support calls back into SNES), so one switch is honest about both. NES, GBA and SMS are always built: this
# filter has no switch for them, and nothing in this tree would turn one off.
option(MESEN_ENABLE_SNES "Build Mesen's Super Nintendo and Game Boy cores" ON)
option(MESEN_ENABLE_PCE "Build Mesen's PC Engine core" ON)
option(MESEN_ENABLE_WS "Build Mesen's WonderSwan core" ON)

# The filter's table: one row per switch, `|`-separated as option | directories | evaluators | units kept, each list
# comma-separated so a row stays one list item (a `;` would split the row into another item, since CMake lists are
# `;`-separated). A disabled console drops every .cpp under its directories (nested ones included) and its CPUs'
# ExpressionEvaluator, and keeps the units listed in the fourth field. Only .cpp files are filtered: the headers stay on
# disk, so every #include keeps compiling and only a call or a construction into a dropped unit needs a guard (the
# `// RetroPlug:` blocks in Core).
set(_mesen_console_filter
    # The SNES's own debugger evaluators are its CPUs', the SPC among them; `/Core/SNES/Input/SnesController.cpp` is
    # the input hub every console's control manager builds (NES included) and `/Core/SNES/SnesDefaultVideoFilter.cpp`
    # is Emulator::GetVideoFilter's fallback, so both stay.
    "MESEN_ENABLE_SNES|Core/SNES,Core/Gameboy|Snes,Spc,Gsu,Cx4,NecDsp,St018,Gameboy|Core/SNES/Input/SnesController.cpp,Core/SNES/SnesDefaultVideoFilter.cpp"
    "MESEN_ENABLE_PCE|Core/PCE|Pce|"
    "MESEN_ENABLE_WS|Core/WS|Ws|")
# A directory is matched at the start of each path, so a checkout whose own path happens to contain `/Core/SNES/` does
# not lose unrelated units. The root is escaped first, so it is a literal rather than a pattern.
string(REGEX REPLACE "([][+.*()^$?\\\\])" "\\\\\\1" _mesen_source_root "${CMAKE_CURRENT_SOURCE_DIR}")
foreach(_row IN LISTS _mesen_console_filter)
    string(REPLACE "|" ";" _row "${_row}")
    list(GET _row 0 _mesen_option)
    list(GET _row 1 _mesen_dirs)
    list(GET _row 2 _mesen_evaluators)
    list(GET _row 3 _mesen_kept)
    string(REPLACE "," ";" _mesen_dirs "${_mesen_dirs}")
    string(REPLACE "," ";" _mesen_evaluators "${_mesen_evaluators}")
    string(REPLACE "," ";" _mesen_kept "${_mesen_kept}")
    if(${_mesen_option})
        continue()
    endif()
    foreach(_dir IN LISTS _mesen_dirs)
        list(FILTER MESEN_CPP_SOURCES EXCLUDE REGEX "^${_mesen_source_root}/${_dir}/")
    endforeach()
    foreach(_evaluator IN LISTS _mesen_evaluators)
        list(FILTER MESEN_CPP_SOURCES EXCLUDE REGEX "/ExpressionEvaluator\\.${_evaluator}\\.cpp$")
    endforeach()
    foreach(_unit IN LISTS _mesen_kept)
        list(APPEND MESEN_CPP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${_unit}")
    endforeach()
endforeach()

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

# PUBLIC, so every consumer's TUs - MesenNesSystem.cpp and the rest - compile against the same console set as the
# library they link, and `#if MESEN_ENABLE_*` in a Core header means the same thing on both sides.
target_compile_definitions(mesen PUBLIC
    MESEN_ENABLE_SNES=$<BOOL:${MESEN_ENABLE_SNES}>
    MESEN_ENABLE_PCE=$<BOOL:${MESEN_ENABLE_PCE}>
    MESEN_ENABLE_WS=$<BOOL:${MESEN_ENABLE_WS}>
)

# LTO for optimized configs only (Debug stays fast to build). Intra-mesen cross-TU inlining across the
# hot CPU/PPU/APU stepping loop, which is ~99% of the audio-thread cost. Measured ~10% faster NES core
# on the Cortex-A53 handheld (nes-bench: xRT 0.83 -> 0.92 over plain -O3). The single biggest safe lever
# for NES perf there — JS is ~0.5% of the block, the emulator core dominates. Free at runtime.
#
# Not free at BUILD time, though: it makes every mesen object LTO bitcode, so each of the thirteen binaries
# that link this lib re-runs codegen over its objects (288 with the consoles RetroPlug does not run compiled
# out, 426 with MESEN_ENABLE_SNES/-PCE/-WS all on). That's why it's opt-in (RETROPLUG_MESEN_LTO, declared in
# the root CMakeLists) rather than always-on - release.yml turns it on so shipped binaries get the speedup,
# and the dev loop doesn't pay for it. Toggling the option recompiles mesen.
if(RETROPLUG_MESEN_LTO)
    set_target_properties(mesen PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
        INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON
    )
    # Under LTO the codegen happens at LINK time, in a process that takes its warning flags from the LINK
    # command line - so the -w further down, which is a per-TU compile option, does not reach it. Two of
    # Mesen's warnings come back at every one of the thirteen links, and one of them (-Wodr on
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
