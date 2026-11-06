# MicroQuickJS (mquickjs), Fabrice Bellard's JavaScript engine for microcontrollers, vendored at upstream commit
# 6d4d7eb6b99011d5aab6e88a9b99e8fd4d86c6e9 under deps/mquickjs (MIT: deps/mquickjs/LICENSE). The tree is upstream's
# tracked files at that commit, unpatched; what RetroPlug needs on top is a file of its own, here or beside it, never an
# edit in deps/mquickjs. Vendored rather than a submodule so that a fresh clone and CI build it with no fetch of their
# own, and so that the pin cannot drift: the bytecode a device pack ships is only as stable as the engine that compiled
# it (docs/modular-graph-phase-e4-design.md section 3's table and section 11.1).
#
# This file builds the engine's two host tools for the desktop, both EXCLUDE_FROM_ALL (a target that needs one builds
# it):
#
#   mqjs_stdlib  upstream's stdlib build tool (mquickjs_build.c over mqjs_stdlib.c): writes the C tables of the standard
#                library (mqjs_stdlib.h) and the predefined atoms' offsets (mquickjs_atom.h, `-a`), for the host's word
#                width, or for a 32-bit target's with `-m32`.
#   mqjs         upstream's REPL and compiler (mqjs.c), the tool the device-pack toolchain
#                (packages/retroplug/scripts/build-device-pack.mjs) compiles a pack to bytecode with (`-o`, `-m32` for
#                the box) and runs it under (`-b`).
#
# Both are written to build/bin, where the toolchain looks for mqjs.
#
# The tables are generated into build/deps/mquickjs/host, never into deps/mquickjs: mquickjs.c includes
# "mquickjs_atom.h" by a quoted name, which the compiler looks for beside mquickjs.c first, so a generated header left
# in the vendored tree (a `make` run there) would shadow this build's, and dirty the tree besides.
#
# The flags are upstream Makefile's for these files (-D_GNU_SOURCE -fno-math-errno -fno-trapping-math, and -lm), its
# warnings silenced as every vendored dep's are (retroplug_silence_dep_warnings, in the root CMakeLists).
#
# Windows: MSVC's cl cannot compile the engine (GNU attributes and __builtin_*_overflow), so on Windows the tools are
# compiled with clang-cl, MSVC-ABI compatible, as SameBoy's core is (cmake/sameboy.cmake), and mqjs.c gets the one
# header MSVC's C runtime lacks from cmake/mquickjs-win-shim (its gettimeofday). That half has not been built yet: it is
# written to the SameBoy pattern, and the first Windows build is its test.
set(MQUICKJS_DIR "${CMAKE_SOURCE_DIR}/deps/mquickjs")
set(MQUICKJS_HOST_GEN "${CMAKE_BINARY_DIR}/deps/mquickjs/host")
file(MAKE_DIRECTORY "${MQUICKJS_HOST_GEN}")

set(_mqjs_stdlib_sources "${MQUICKJS_DIR}/mqjs_stdlib.c" "${MQUICKJS_DIR}/mquickjs_build.c")
set(_mqjs_sources
    "${MQUICKJS_DIR}/mqjs.c" "${MQUICKJS_DIR}/readline_tty.c" "${MQUICKJS_DIR}/readline.c"
    "${MQUICKJS_DIR}/mquickjs.c" "${MQUICKJS_DIR}/dtoa.c" "${MQUICKJS_DIR}/libm.c" "${MQUICKJS_DIR}/cutils.c")

if(MSVC)
    find_program(MQUICKJS_CLANG_CL clang-cl
        HINTS "$ENV{VCINSTALLDIR}Tools/Llvm/x64/bin"
              "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin"
        REQUIRED)
    set(_mqjs_objdir "${CMAKE_BINARY_DIR}/deps/mquickjs/clang")
    file(MAKE_DIRECTORY "${_mqjs_objdir}")
    # /MT: the project's static CRT. -w: upstream's warnings are not ours to act on.
    set(_mqjs_cl_flags /nologo /c /MT /O2 /DNDEBUG -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_WARNINGS
        "-I${CMAKE_SOURCE_DIR}/cmake/mquickjs-win-shim" "-I${MQUICKJS_HOST_GEN}" "-I${MQUICKJS_DIR}" -w)
    # _mqjs_clang_objects(<var> <generated headers the sources include> <sources>...)
    function(_mqjs_clang_objects var headers)
        set(_objs "")
        foreach(_src IN LISTS ARGN)
            get_filename_component(_n "${_src}" NAME)
            set(_obj "${_mqjs_objdir}/${_n}.obj")
            add_custom_command(
                OUTPUT "${_obj}"
                COMMAND "${MQUICKJS_CLANG_CL}" ${_mqjs_cl_flags} "/Fo${_obj}" "${_src}"
                DEPENDS "${_src}" ${headers} "${CMAKE_SOURCE_DIR}/cmake/mquickjs-win-shim/sys/time.h"
                COMMENT "clang-cl (mquickjs) ${_n}"
                VERBATIM)
            list(APPEND _objs "${_obj}")
        endforeach()
        set(${var} ${_objs} PARENT_SCOPE)
    endfunction()
    _mqjs_clang_objects(_mqjs_stdlib_objects "" ${_mqjs_stdlib_sources})
    add_executable(mqjs_stdlib EXCLUDE_FROM_ALL ${_mqjs_stdlib_objects})
    set_target_properties(mqjs_stdlib PROPERTIES LINKER_LANGUAGE C)
else()
    add_executable(mqjs_stdlib EXCLUDE_FROM_ALL ${_mqjs_stdlib_sources})
    target_compile_definitions(mqjs_stdlib PRIVATE _GNU_SOURCE)
    retroplug_silence_dep_warnings(mqjs_stdlib)
endif()

# The host width's tables, which mqjs is compiled with. A command writes each, through cmake/mquickjs-table.cmake (the
# tool prints the table on stdout).
set(_mqjs_host_headers "${MQUICKJS_HOST_GEN}/mqjs_stdlib.h" "${MQUICKJS_HOST_GEN}/mquickjs_atom.h")
add_custom_command(
    OUTPUT ${_mqjs_host_headers}
    COMMAND ${CMAKE_COMMAND} "-DTOOL=$<TARGET_FILE:mqjs_stdlib>" "-DOUTPUT=${MQUICKJS_HOST_GEN}/mqjs_stdlib.h"
            -P "${CMAKE_SOURCE_DIR}/cmake/mquickjs-table.cmake"
    COMMAND ${CMAKE_COMMAND} "-DTOOL=$<TARGET_FILE:mqjs_stdlib>" "-DARGS=-a"
            "-DOUTPUT=${MQUICKJS_HOST_GEN}/mquickjs_atom.h" -P "${CMAKE_SOURCE_DIR}/cmake/mquickjs-table.cmake"
    DEPENDS mqjs_stdlib "${CMAKE_SOURCE_DIR}/cmake/mquickjs-table.cmake"
    COMMENT "mquickjs: the standard library's tables, host width"
    VERBATIM)

if(MSVC)
    _mqjs_clang_objects(_mqjs_objects "${_mqjs_host_headers}" ${_mqjs_sources})
    add_executable(mqjs EXCLUDE_FROM_ALL ${_mqjs_objects})
    set_target_properties(mqjs PROPERTIES LINKER_LANGUAGE C)
else()
    add_executable(mqjs EXCLUDE_FROM_ALL ${_mqjs_sources} ${_mqjs_host_headers})
    target_compile_definitions(mqjs PRIVATE _GNU_SOURCE)
    target_compile_options(mqjs PRIVATE -fno-math-errno -fno-trapping-math)
    # The generated tables include the engine's private header by a quoted name, from the generated directory.
    target_include_directories(mqjs PRIVATE "${MQUICKJS_HOST_GEN}" "${MQUICKJS_DIR}")
    target_link_libraries(mqjs PRIVATE m)
    retroplug_silence_dep_warnings(mqjs)
endif()
set_target_properties(mqjs mqjs_stdlib PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
