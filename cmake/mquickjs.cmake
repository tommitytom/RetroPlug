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
# Both are written to build/bin, where the toolchain looks for mqjs. It also defines what a host of the engine builds
# with (the MicroQuickJS script host, packages/graph-runtime): retroplug_mquickjs_stdlib(), a standard library's tables
# at both widths, and retroplug_mquickjs_engine(), the engine as a library at the desktop's width with one of them.
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
#
# Upstream bugs are fixed outside the tree, in graph-runtime's script/rp_mqjs_engine.c, which the host's engine
# compiles in place of mquickjs.c (retroplug_mquickjs_engine's ENGINE, below):
#
#   - JS_MakeUniqueString corrupts the engine's weak table of run-time names when a collection runs inside the table's
#     own growth (js_resize_value_array2 copies the size the table had before the collection shrank it, and the
#     insertion lands where it was found before): stale words in the table, a crash or a name resolving to another's
#     slot at the next lookup. It takes a dead name in the table and a collection at that one allocation, which a tight
#     heap makes likely (the device-pack toolchain's least-heap search met it). rp_mqjs_engine.c compiles upstream's
#     function under another name and routes every call to a copy that collects before the growth.
#   - A loaded bytecode's string constant, once used as a property name, is marked unique where it lies, and the
#     weak table drops it at the next collection, since the collector never marks a string outside the heap: a later
#     lookup of the same text from C makes a second string and finds nothing. rp_mqjs_keep_bytecode_names marks the
#     bytecode's strings kept after relocation.
#   - Three exceptions the engine drops: one a String.prototype.replace function throws (js_string_replace ignores
#     js_string_concat_subst's failure), an out-of-memory in string_buffer_puts, and one a hasOwnProperty key's
#     toString throws (js_object_hasOwnProperty ignores JS_ToPropertyKey's). rp_mqjs_engine.c routes each through a
#     wrapper that makes the engine's StringBuffer sticky, or makes the key first.
#
# RetroPlug also reaches into the engine in two ways these spans hold: rp_mqjs_limit_reentries caps a context's C
# re-entries by starting JS_Call's count above 0 (it rests on JS_Call being the count's only writer, which the count of
# its mentions holds), and the host's views are made by js_typed_array_constructor called with a C array of arguments
# (it rests on the constructor reading them before it allocates).
#
# The first is a copy of upstream's code that reads the context's internals, so a bump that changes upstream's function
# would still compile and the stale copy would win; the others rest on upstream's text as it stands. So the configure
# below hashes the text of each (JS_MakeUniqueString's definition, the sweep of unique_strings, replace and its
# substitution, the StringBuffer and its appends, hasOwnProperty, the typed array's constructor) and fails,
# naming rp_mqjs_engine.c, when any is not the text the fixes were written against: re-review the fixes against the
# new text, then record its hash here. retroplug-device-mode-test's [debug-gc] cases hold the second fix (its revert
# fails them); no test reaches the first's condition on demand (a collection at every allocation, DEBUG_GC, sweeps a
# dead name before any growth can meet it), so the hash is its guard.
set(MQUICKJS_DIR "${CMAKE_SOURCE_DIR}/deps/mquickjs")
file(READ "${MQUICKJS_DIR}/mquickjs.c" _mqjs_engine_text)
string(REPLACE "\r" "" _mqjs_engine_text "${_mqjs_engine_text}")
# Each span: what it is, the text it begins with, the text it ends before (neither may hold a semicolon, a list's
# separator), and its SHA-256.
set(_mqjs_span_0 "JS_MakeUniqueString" "static JSValue JS_MakeUniqueString(JSContext *ctx, JSValue val)" "\n}\n"
                 "80908cb5f38c4b25f53e1991b80c1880a97c095339fc21298ddedda470b04567")
set(_mqjs_span_1 "the sweep of unique_strings" "    /* update the unique string table (its elements are considered as"
                 "    /* update the weak references in the string position cache"
                 "0dcbcf210bd2a34941a7c0795e11f577932f5898251800e74c7a6fa509cc6c6d")
set(_mqjs_span_2 "String.prototype.replace and its substitution"
                 "static int js_string_concat_subst(JSContext *ctx, StringBuffer *b," "\n// split(sep, limit)"
                 "5f8263e6c8cc87ca1366b0cd17b234ff773fe57fb8abd162cdb89a444636d84b")
set(_mqjs_span_3 "the StringBuffer and its appends"
                 "typedef struct {\n    JSGCRef buffer_ref" "/* val1 and val2 must be strings or exception */"
                 "f9567d2729edcf582c7d45b70ff9e7b418db65022641dbb25dd438635b528f6a")
set(_mqjs_span_4 "js_object_hasOwnProperty" "JSValue js_object_hasOwnProperty(JSContext *ctx" "\n}\n"
                 "0046d19ecb2384776eda8f8344d8ce7fd87a612307a657278f1de8b7c8e5e2ec")
set(_mqjs_span_5 "js_typed_array_constructor" "JSValue js_typed_array_constructor(JSContext *ctx, JSValue *this_val,"
                 "\n}\n" "973db0412518887f46e5b757f768229e43658dc6922f07d7bfd07193b38ce11a")
string(REGEX MATCHALL "js_call_rec_count" _mqjs_mentions "${_mqjs_engine_text}")
list(LENGTH _mqjs_mentions _mqjs_mentions)
if(NOT _mqjs_mentions EQUAL 4)
    message(FATAL_ERROR "deps/mquickjs/mquickjs.c mentions js_call_rec_count ${_mqjs_mentions} times, not the 4 "
                        "(its field, JS_Call's check, increment and decrement) rp_mqjs_limit_reentries was written "
                        "against (cmake/mquickjs.cmake's header says why); re-review it")
endif()
foreach(_mqjs_span IN ITEMS _mqjs_span_0 _mqjs_span_1 _mqjs_span_2 _mqjs_span_3 _mqjs_span_4 _mqjs_span_5)
    list(GET ${_mqjs_span} 0 _what)
    list(GET ${_mqjs_span} 1 _begin)
    list(GET ${_mqjs_span} 2 _end)
    list(GET ${_mqjs_span} 3 _expected)
    string(FIND "${_mqjs_engine_text}" "${_begin}" _at)
    set(_hash "")
    if(NOT _at EQUAL -1)
        string(SUBSTRING "${_mqjs_engine_text}" ${_at} -1 _rest)
        string(FIND "${_rest}" "${_end}" _length)
        if(NOT _length EQUAL -1)
            string(SUBSTRING "${_rest}" 0 ${_length} _rest)
            string(SHA256 _hash "${_rest}")
        endif()
    endif()
    if(NOT _hash STREQUAL _expected)
        message(FATAL_ERROR "deps/mquickjs/mquickjs.c: ${_what} is not the text "
                            "packages/graph-runtime/native/src/script/rp_mqjs_engine.c's fixes were written against "
                            "(cmake/mquickjs.cmake's header says which and why); re-review them against it, then "
                            "record its hash (${_hash}) here")
    endif()
endforeach()
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

# retroplug_mquickjs_stdlib(<name> SOURCE <file.c> [INCLUDES <dir>...]): a standard library of the engine's
# (docs/modular-graph-phase-e4-design.md section 4), its tables generated at both widths a script pack runs at. The
# build tool <name>-tool is SOURCE (a list of the library's globals, classes and C functions by name, whose main calls
# build_atoms) with upstream's mquickjs_build.c, built for the host; it prints the tables, <name>.h, and the
# predefined atoms' offsets, mquickjs_atom.h, for the host's width and, with -m32, for a 32-bit target's (the box's).
# Written into build/mquickjs/<name>/64 and build/mquickjs/<name>/32: a compile that runs against the library at a
# width puts that directory on its include path, before deps/mquickjs, and finds both there. Sets <name>_DIR_64 and
# <name>_DIR_32 (the directories) and <name>_HEADERS_64 and <name>_HEADERS_32 (the files, for a dependency) in the
# caller's scope. A table is only as good as the width it was built for: mquickjs.c compiled at 32 bits takes the
# 32-bit atoms, and a context at a width takes that width's tables.
function(retroplug_mquickjs_stdlib name)
    cmake_parse_arguments(P "" "SOURCE" "INCLUDES" ${ARGN})
    if(NOT P_SOURCE OR P_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "retroplug_mquickjs_stdlib(${name}): SOURCE is required, and nothing else is unnamed")
    endif()
    set(_tool ${name}-tool)
    if(MSVC)
        # The tool is upstream C, which cl cannot compile: clang-cl, as the other tools above, into objects of its own.
        set(_mqjs_objdir "${CMAKE_BINARY_DIR}/deps/mquickjs/${_tool}")
        file(MAKE_DIRECTORY "${_mqjs_objdir}")
        _mqjs_clang_objects(_objs "" "${P_SOURCE}" "${MQUICKJS_DIR}/mquickjs_build.c")
        add_executable(${_tool} EXCLUDE_FROM_ALL ${_objs})
        set_target_properties(${_tool} PROPERTIES LINKER_LANGUAGE C)
    else()
        add_executable(${_tool} EXCLUDE_FROM_ALL "${P_SOURCE}" "${MQUICKJS_DIR}/mquickjs_build.c")
        target_compile_definitions(${_tool} PRIVATE _GNU_SOURCE)
        target_include_directories(${_tool} PRIVATE "${MQUICKJS_DIR}" ${P_INCLUDES})
        retroplug_silence_dep_warnings(${_tool})
    endif()
    set_target_properties(${_tool} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(_root "${CMAKE_BINARY_DIR}/mquickjs/${name}")
    foreach(_width 64 32)
        set(_dir "${_root}/${_width}")
        file(MAKE_DIRECTORY "${_dir}")
        if(_width EQUAL 32)
            set(_m "-m32")
        else()
            set(_m "-m64")
        endif()
        set(_headers "${_dir}/${name}.h" "${_dir}/mquickjs_atom.h")
        add_custom_command(
            OUTPUT ${_headers}
            COMMAND ${CMAKE_COMMAND} "-DTOOL=$<TARGET_FILE:${_tool}>" "-DARGS=${_m}" "-DOUTPUT=${_dir}/${name}.h"
                    -P "${CMAKE_SOURCE_DIR}/cmake/mquickjs-table.cmake"
            COMMAND ${CMAKE_COMMAND} "-DTOOL=$<TARGET_FILE:${_tool}>" "-DARGS=${_m};-a"
                    "-DOUTPUT=${_dir}/mquickjs_atom.h" -P "${CMAKE_SOURCE_DIR}/cmake/mquickjs-table.cmake"
            DEPENDS ${_tool} "${CMAKE_SOURCE_DIR}/cmake/mquickjs-table.cmake"
            COMMENT "mquickjs: ${name}'s tables, ${_width}-bit"
            VERBATIM)
        set(${name}_DIR_${_width} "${_dir}" PARENT_SCOPE)
        set(${name}_HEADERS_${_width} ${_headers} PARENT_SCOPE)
    endforeach()
endfunction()

# retroplug_mquickjs_engine(<target> STDLIB <name> SOURCES <c>... [ENGINE <c>] [FIRST <h>]): the engine as a static
# library for the desktop's own width, with the standard library <name> (retroplug_mquickjs_stdlib) whose run-time half
# SOURCES are (the C file that compiles the generated tables with the functions they name). ENGINE, if given, is
# compiled in place of mquickjs.c: a file that includes mquickjs.c whole and defines after it what must read the
# engine's own structures. The engine's sources are upstream's, unpatched, at upstream Makefile's flags, their warnings
# silenced as a dep's are; position-independent, as a product module links it. Its users get deps/mquickjs's headers
# (mquickjs.h). A device builds the engine its own way, at its own width, and must compile the same ENGINE file the
# host's build names (graph-runtime's script/rp_mqjs_engine.c) in place of mquickjs.c: the host's device build calls
# what it defines. FIRST, if given, is a header every source is compiled with first (graph-runtime's
# script/rp_mqjs_names.h, which renames the engine's external names), and the built library is then held to defining
# no external name outside rp_mqjs_ (cmake/mquickjs-names.cmake), with the toolchain's nm on Linux, and on macOS with
# Xcode's (its `-g --defined-only` and Mach-O's leading underscore are handled, but no macOS build has run it yet). Not
# on MSVC: CMAKE_NM is empty there, and though clang-cl's install carries llvm-nm, which reads its objects, the Windows
# build has not linked the renamed engine yet, so the names are held there only by the products' link.
function(retroplug_mquickjs_engine target)
    cmake_parse_arguments(P "" "STDLIB;ENGINE;FIRST" "SOURCES;INCLUDES" ${ARGN})
    if(NOT P_STDLIB OR NOT P_SOURCES OR P_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "retroplug_mquickjs_engine(${target}): STDLIB and SOURCES are required")
    endif()
    if(NOT P_ENGINE)
        set(P_ENGINE "${MQUICKJS_DIR}/mquickjs.c")
    endif()
    set(_engine "${P_ENGINE}" "${MQUICKJS_DIR}/dtoa.c" "${MQUICKJS_DIR}/libm.c" "${MQUICKJS_DIR}/cutils.c")
    set(_dir "${${P_STDLIB}_DIR_64}")
    set(_headers ${${P_STDLIB}_HEADERS_64})
    if(NOT _dir)
        message(FATAL_ERROR "retroplug_mquickjs_engine(${target}): no standard library `${P_STDLIB}` "
                            "(retroplug_mquickjs_stdlib)")
    endif()
    if(MSVC)
        # clang-cl, as the tools above; the generated tables and the sources' own directories first.
        set(_saved_flags ${_mqjs_cl_flags})
        set(_mqjs_cl_flags ${_mqjs_cl_flags} "-I${_dir}")
        if(P_FIRST)
            list(APPEND _mqjs_cl_flags "/FI${P_FIRST}")
        endif()
        foreach(_inc IN LISTS P_INCLUDES)
            list(APPEND _mqjs_cl_flags "-I${_inc}")
        endforeach()
        set(_mqjs_objdir "${CMAKE_BINARY_DIR}/deps/mquickjs/${target}")
        file(MAKE_DIRECTORY "${_mqjs_objdir}")
        _mqjs_clang_objects(_objs "${_headers}" ${_engine} ${P_SOURCES})
        set(_mqjs_cl_flags ${_saved_flags})
        add_library(${target} STATIC ${_objs})
        set_target_properties(${target} PROPERTIES LINKER_LANGUAGE C)
    else()
        add_library(${target} STATIC ${_engine} ${P_SOURCES} ${_headers})
        target_compile_definitions(${target} PRIVATE _GNU_SOURCE)
        target_compile_options(${target} PRIVATE -fno-math-errno -fno-trapping-math)
        # The generated tables before the vendored tree: mquickjs.c includes "mquickjs_atom.h" by a quoted name.
        target_include_directories(${target} PRIVATE "${_dir}" ${P_INCLUDES})
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        retroplug_silence_dep_warnings(${target})
        target_link_libraries(${target} PRIVATE m)
        if(P_FIRST)
            target_compile_options(${target} PRIVATE "SHELL:-include ${P_FIRST}")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DLIBRARY=$<TARGET_FILE:${target}>
                        -P "${CMAKE_SOURCE_DIR}/cmake/mquickjs-names.cmake"
                VERBATIM)
        endif()
    endif()
    target_include_directories(${target} PUBLIC "${MQUICKJS_DIR}")
endfunction()
