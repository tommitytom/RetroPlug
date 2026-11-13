# BlastEm: the Sega Mega Drive core (vendored at deps/blastem, our mirror's `retroplug` branch), built as a static
# library for the blastem package. BlastEm's own Makefile is NOT used: this builds the Mega Drive file set only, on its
# portable interpreter (NEW_CORE) everywhere, never its x86 JIT, and generates the 68k and Z80 interpreters with
# cpu_dsl.py into the build tree (derived artifacts, never committed).
set(BLASTEM_DIR "${CMAKE_SOURCE_DIR}/deps/blastem")

if(NOT EXISTS "${BLASTEM_DIR}/cpu_dsl.py")
    message(FATAL_ERROR "deps/blastem is empty. Run: git submodule update --init --recursive")
endif()

# cpu_dsl.py is Python 3 and a hard configure requirement: m68k.c, z80.c and their headers exist only as its output,
# and the 32X's and LaserActive's headers are included by genesis.h and 32x.h even though their cores are not built.
find_package(Python3 3.6 REQUIRED COMPONENTS Interpreter)

set(_BLASTEM_GEN "${CMAKE_BINARY_DIR}/blastem-gen")
file(MAKE_DIRECTORY "${_BLASTEM_GEN}")

# One generator run per core, in the build tree: cpu_dsl.py writes the core's .h into its working directory (the build
# tree, so nothing lands in the source) and its .c to stdout (cpu_dsl.py's writeHeader + main). The dispatch is
# upstream's: `call` per 68k instruction (the handlers are too large for one function on MSVC) and `goto` per Z80
# instruction (Z80_DISPATCH:=goto, Makefile:13, 93). sh2 and upd78k2 are generated for their HEADERS only, so their
# dispatch value is irrelevant; `call` is what upstream's sed yields for any target but z80 (Makefile:435).
function(_blastem_generate_core name dispatch)
    add_custom_command(
        OUTPUT "${_BLASTEM_GEN}/${name}.c" "${_BLASTEM_GEN}/${name}.h"
        COMMAND "${Python3_EXECUTABLE}" "${BLASTEM_DIR}/cpu_dsl.py" -d ${dispatch} "${BLASTEM_DIR}/${name}.cpu"
                > "${_BLASTEM_GEN}/${name}.c"
        WORKING_DIRECTORY "${_BLASTEM_GEN}"
        DEPENDS "${BLASTEM_DIR}/cpu_dsl.py" "${BLASTEM_DIR}/${name}.cpu"
        COMMENT "cpu_dsl.py ${name}.cpu (${dispatch}) -> build/blastem-gen/${name}.c"
        VERBATIM)
endfunction()

_blastem_generate_core(m68k call)
_blastem_generate_core(z80 goto)
# The 32X and LaserActive cores are not compiled, but genesis.h includes 32x.h -> sh2.h and the file set's own sources
# include upd78k2.h, so their headers must exist.
_blastem_generate_core(sh2 call)
_blastem_generate_core(upd78k2 call)

# BlastEm's ROM database, which romdb.c parses through read_bundled_file() ("rom.db"). Upstream's Makefile turns it
# into a C string with sed (Makefile:437); this does the same with CMake's own string handling, escaping the one double
# quote the file carries (rom.db:868, in a comment) and appending the newline each line had.
file(STRINGS "${BLASTEM_DIR}/rom.db" _BLASTEM_ROMDB_LINES)
set(_BLASTEM_ROMDB "/* Generated from deps/blastem/rom.db by cmake/blastem.cmake. Do not edit. */\nconst char rom_db_data[] =\n")
foreach(_line IN LISTS _BLASTEM_ROMDB_LINES)
    string(REPLACE "\\" "\\\\" _line "${_line}")
    string(REPLACE "\"" "\\\"" _line "${_line}")
    string(APPEND _BLASTEM_ROMDB "    \"${_line}\\n\"\n")
endforeach()
string(APPEND _BLASTEM_ROMDB "    ;\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BLASTEM_DIR}/rom.db")
file(WRITE "${_BLASTEM_GEN}/rom.db.c" "${_BLASTEM_ROMDB}")

# The Mega Drive file set: the shell and bus (system, genesis, io, vdp, the m68k and z80 cores and their
# disassemblers), the ROM database and the cartridge mappers a Mega Drive cart can carry (Sega, J-Cart, multi-game,
# Sega 32X-cart mappers aside), the sound chips (the YM2612 and YSA2, the PSG, the MSM6258 in the RF5C164), the
# serialiser and state support, the tern config tree, and the bundled zlib chd.c and zip.c need. NOT compiled: 32X,
# Sega CD (segacd, cdd_*, cd_graphics, lc8951, cdimage, mediaplayer), SMS (sms, i8255, korean_sms_multi), Coleco,
# LaserActive, the Pico (pico_pcm, ymz263b), the VGM player (gen_player), the terminal, Nuklear, SDL, and the x86 JIT
# (m68k_core, m68k_core_x86, z80_to_x86, gen_x86, backend_x86). BlastemShim.cpp in packages/blastem provides the
# functions genesis.c and romdb.c still reference in the branches a Mega Drive never takes.
set(_BLASTEM_SOURCES
    system.c genesis.c vdp.c io.c romdb.c hash.c i2c.c nor.c
    sega_mapper.c multi_game.c jcart.c realtec.c xband.c radica.c megawifi.c sft_mapper.c
    serialize.c gst.c saves.c
    tern.c util.c paths.c config.c backend.c arena.c gen.c mem.c
    ym2612.c ymf262.c ym_common.c psg.c rf5c164.c wave.c flac.c vgm.c event_log.c blip_buf.c
    chd.c zip.c z80inst.c disasm.c 68kinst.c)

# render_audio.c is NOT in that list, and blip_buf.c is: the shell replaces BlastEm's audio sink whole
# (packages/blastem/.../BlastemAudio.cpp), because render_audio.c is a process-global source registry capped at 8 and
# resamples linearly, and blip_buf (our mirror's copy of 1.1.0) is what that sink synthesises with.

set(_BLASTEM_ABS "")
foreach(_src IN LISTS _BLASTEM_SOURCES)
    list(APPEND _BLASTEM_ABS "${BLASTEM_DIR}/${_src}")
endforeach()

file(GLOB _BLASTEM_ZLIB CONFIGURE_DEPENDS "${BLASTEM_DIR}/zlib/*.c")

add_library(blastem STATIC
    ${_BLASTEM_ABS}
    ${_BLASTEM_ZLIB}
    "${_BLASTEM_GEN}/m68k.c" "${_BLASTEM_GEN}/z80.c" "${_BLASTEM_GEN}/rom.db.c"
    # The four generated HEADERS too, and not as an afterthought: a custom command's OUTPUT that no target consumes is
    # never built, so a build that names only m68k.c and z80.c leaves sh2.h and upd78k2.h (which genesis.h reaches
    # through 32x.h, and which upd78k2-using sources include) missing. A .h in a target's sources costs no compile and
    # makes CMake own the rule.
    "${_BLASTEM_GEN}/m68k.h" "${_BLASTEM_GEN}/z80.h" "${_BLASTEM_GEN}/sh2.h" "${_BLASTEM_GEN}/upd78k2.h")

# The chips' maths (the synthesised sine and log tables) is libm's. MSVC keeps it in the CRT; the GNU and Apple
# toolchains need -lm, which a static library propagates to its consumers.
if(NOT WIN32)
    target_link_libraries(blastem PUBLIC m)
endif()

target_include_directories(blastem SYSTEM PUBLIC "${BLASTEM_DIR}" "${_BLASTEM_GEN}")
# NEW_CORE selects the generated interpreters. IS_LIB is BlastEm's own library-build switch: it drops the debugger, the
# oscilloscope and the threaded VDP renderer, keeps the sound-chip chunk size small, and compiles out romdb.c's
# `device MENU` mapper and system.c's load_media_zip with the same switch (a Mega Drive takes neither: the ROM DB's one
# `device MENU` row is the GUI image's). DISABLE_NUKLEAR keeps SDL's headers out of config.c.
# PUBLIC, not PRIVATE: the package's own translation units include BlastEm's headers, and without NEW_CORE genesis.h
# takes its JIT branch (m68k_core.h) while BlastemCore.hpp includes the generated m68k.h: two declarations of
# m68k_context in one unit, which does not compile.
# The core is GNU C99 and is not ours to fix: warnings are silenced for it alone, never globally (on MSVC it is off
# /W4 /WX in packages/native/cmake/warnings.cmake, and /wd suppressions belong there with a reason).
target_compile_definitions(blastem PUBLIC NEW_CORE IS_LIB DISABLE_NUKLEAR)

# BlastEm's copies of two vendored APIs get a package-private spelling, because the names are already taken by another
# emulator in the same binary. Mesen sits beside BlastEm in every product, and it carries its own blip_buf
# (deps/mesen/Utilities/Audio/blip_buf.cpp) and LuaSocket's usocket.c (its own socket_close). The eleven blip entry
# points and socket_close are defined by both archives, and a static link binds an undefined symbol to whichever of the
# two objects was pulled first. The two blip_bufs are the same source today: deps/blastem's is a copy of Mesen's with
# only the precompiled header and EXPORT removed, both say 1.1.0, blip_new and blip_read_samples have the same arity in
# each and blip_t is the same five-field struct. So that collision is silent and order-dependent (either archive would
# do until one copy is edited, and then neither the link nor a test would say so), while only socket_close is a hard
# multiple-definition error, its signature differing (Mesen's takes no argument, BlastEm's takes the socket). The rename
# is right either way: it stops the binary depending on that luck and on the two copies staying the same source.
#
# So BlastEm's file set is compiled against these renames, and the blastem package's own units that reach the two seams
# take the same definitions (packages/blastem/native/CMakeLists.txt); nothing else in the tree ever spells the names.
# Only entry points are renamed, never the blip_t typedef, so a header's declaration and its caller's call always agree.
set(BLASTEM_SEAM_RENAMES
    blip_add_delta=rp_blastem_blip_add_delta
    blip_add_delta_fast=rp_blastem_blip_add_delta_fast
    blip_clear=rp_blastem_blip_clear
    blip_clocks_needed=rp_blastem_blip_clocks_needed
    blip_delete=rp_blastem_blip_delete
    blip_end_frame=rp_blastem_blip_end_frame
    blip_new=rp_blastem_blip_new
    blip_read_samples=rp_blastem_blip_read_samples
    blip_reset=rp_blastem_blip_reset
    blip_samples_avail=rp_blastem_blip_samples_avail
    blip_set_rates=rp_blastem_blip_set_rates
    socket_init=rp_blastem_socket_init
    socket_blocking=rp_blastem_socket_blocking
    socket_close=rp_blastem_socket_close
    socket_last_error=rp_blastem_socket_last_error
    socket_error_is_wouldblock=rp_blastem_socket_error_is_wouldblock)
target_compile_definitions(blastem PRIVATE ${BLASTEM_SEAM_RENAMES})
set_target_properties(blastem PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(blastem PRIVATE -w)
endif()
# The interpreter is a single 1.94-million-line translation unit: MSVC needs /bigobj for the section count it produces.
if(MSVC)
    target_compile_options(blastem PRIVATE /bigobj)
endif()
