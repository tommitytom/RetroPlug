# SameBoy: upstream emulator core (submodule at deps/sameboy), built as a
# static library for RetroPlug. Compiles every Core/*.c except the
# disassembler / debugger / symbol-hash files, with the same compile defs.

set(SAMEBOY_DIR "${CMAKE_SOURCE_DIR}/deps/sameboy")

if(NOT EXISTS "${SAMEBOY_DIR}/Core/gb.h")
    message(FATAL_ERROR
        "deps/sameboy is empty. Run: git submodule update --init --recursive")
endif()

file(STRINGS "${SAMEBOY_DIR}/version.mk" _SAMEBOY_VERSION_RAW
     REGEX "^VERSION[ \t]*:=[ \t]*")
string(REGEX REPLACE "^VERSION[ \t]*:=[ \t]*" "" SAMEBOY_VERSION "${_SAMEBOY_VERSION_RAW}")
string(STRIP "${SAMEBOY_VERSION}" SAMEBOY_VERSION)

file(GLOB SAMEBOY_SOURCES CONFIGURE_DEPENDS "${SAMEBOY_DIR}/Core/*.c")
list(FILTER SAMEBOY_SOURCES EXCLUDE REGEX "/sm83_disassembler\\.c$")
list(FILTER SAMEBOY_SOURCES EXCLUDE REGEX "/symbol_hash\\.c$")
list(FILTER SAMEBOY_SOURCES EXCLUDE REGEX "/debugger\\.c$")
# cheat_search.c references struct fields that gb.h removes when
# GB_DISABLE_DEBUGGER is set; gb.h auto-defines GB_DISABLE_CHEAT_SEARCH in
# that case, and gb.c's call sites are correctly guarded — but the .c file
# itself isn't, so exclude it from the build.
list(FILTER SAMEBOY_SOURCES EXCLUDE REGEX "/cheat_search\\.c$")

# Boot ROMs are assembled from the submodule's BootROMs/*.asm sources via RGBDS
# at build time. Include this FIRST so the generated headers + sameboy_bootroms
# target exist before the sameboy library — the Windows clang-cl object commands
# below depend on sameboy_bootroms. (pb12 + RGBDS run fine under the MSVC build.)
include("${CMAKE_SOURCE_DIR}/cmake/sameboy_bootroms.cmake")
set(_SAMEBOY_GEN_INC "${CMAKE_BINARY_DIR}/generated")

if(WIN32)
    # SameBoy's Core is built on GNU/Clang extensions (statement-expressions,
    # typeof, __attribute__, __builtin_*) that MSVC cl.exe cannot compile. On
    # Windows we therefore compile the core with clang-cl (MSVC-ABI compatible —
    # the resulting objects link with the rest of the MSVC build) into the static
    # `sameboy` library. This is the SameBoy half of the "MSVC main + clang-isolate
    # SameBoy" toolchain split; SameBoySystem.cpp (the only RetroPlug C++ needing
    # the internal view) is isolated the same way in the top-level CMakeLists.
    find_program(SAMEBOY_CLANG_CL clang-cl
        HINTS "$ENV{VCINSTALLDIR}Tools/Llvm/x64/bin"
              "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin"
        REQUIRED)

    # GB_VERSION must expand to a string literal; bake it into a generated header
    # /FI'd ahead of every TU to avoid command-line quote-escaping pain.
    set(_sb_objdir "${CMAKE_BINARY_DIR}/sameboy-clang")
    file(MAKE_DIRECTORY "${_sb_objdir}")
    file(WRITE "${_sb_objdir}/sameboy_version.h" "#define GB_VERSION \"${SAMEBOY_VERSION}\"\n")

    # /MT matches the project-wide static CRT. GB_INTERNAL is PRIVATE to these
    # core compilations (it must not leak to MSVC-compiled consumers — see the
    # opaque-struct note above). The win shim bridges the UCRT gaps.
    set(_sb_flags
        /nologo /c /MT /O2 /Ob2 /DNDEBUG /std:c11
        "/FI${_sb_objdir}/sameboy_version.h"
        "/FI${CMAKE_SOURCE_DIR}/cmake/sameboy_win_shim.h"
        -DGB_INTERNAL -DGB_DISABLE_TIMEKEEPING -DGB_DISABLE_DEBUGGER
        "-I${SAMEBOY_DIR}/Core" "-I${_SAMEBOY_GEN_INC}"
        -Wno-unused-variable -Wno-unused-function -Wno-missing-braces -Wno-switch
        -Wno-int-in-bool-context -Wno-implicit-function-declaration -Wno-multichar
        -Wno-strict-aliasing -Wno-deprecated-non-prototype -Wno-unused-but-set-variable)

    set(_sb_objs "")
    foreach(src ${SAMEBOY_SOURCES})
        get_filename_component(_n "${src}" NAME)
        set(_obj "${_sb_objdir}/${_n}.obj")
        add_custom_command(
            OUTPUT "${_obj}"
            COMMAND "${SAMEBOY_CLANG_CL}" ${_sb_flags} "/Fo${_obj}" "${src}"
            DEPENDS "${src}" sameboy_bootroms
                    "${CMAKE_SOURCE_DIR}/cmake/sameboy_win_shim.h"
            COMMENT "clang-cl (sameboy) ${_n}"
            VERBATIM)
        list(APPEND _sb_objs "${_obj}")
    endforeach()

    add_library(sameboy STATIC ${_sb_objs})
    set_target_properties(sameboy PROPERTIES LINKER_LANGUAGE C)
else()
    add_library(sameboy STATIC ${SAMEBOY_SOURCES})
    add_dependencies(sameboy sameboy_bootroms)
    target_compile_definitions(sameboy PRIVATE GB_INTERNAL)
    set_target_properties(sameboy PROPERTIES C_STANDARD 11)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(sameboy PRIVATE
            -Wno-unused-variable -Wno-unused-function -Wno-missing-braces
            -Wno-switch -Wno-int-in-bool-context -Wno-implicit-function-declaration
            -Wno-multichar -Wno-strict-aliasing)
    endif()
endif()

# Consumer-facing surface (both toolchains). GB_INTERNAL is deliberately NOT here
# — consumers get the public opaque-struct view. The DISABLE_* defs affect the
# struct layout, so consumers MUST agree on them; GB_VERSION is convenience.
target_include_directories(sameboy PUBLIC "${SAMEBOY_DIR}/Core" "${_SAMEBOY_GEN_INC}")
target_compile_definitions(sameboy PUBLIC
    GB_DISABLE_TIMEKEEPING
    GB_DISABLE_DEBUGGER
    GB_VERSION="${SAMEBOY_VERSION}"
)
set_target_properties(sameboy PROPERTIES POSITION_INDEPENDENT_CODE ON)
