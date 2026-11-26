# Packs: a package's native half built to load at run time (docs/downloadable-packs-loading-design.md section 2), beside
# the static build every product links. The root CMakeLists.txt includes it after cmake/packages.cmake and before the
# first retroplug_package(), so a package's own native/CMakeLists.txt can call retroplug_pack() once its targets exist.
#
# A pack module is a MODULE library: the package's static targets linked in privately with every static dependency
# they carry (its own copies of node-sdk, system, reflect-cpp, a vendored core), plus a generated entry translation unit
# defining rp_pack_entry (graph/rp_pack.h). Every symbol but the entry is local to the module: a version script on
# Linux, an exported-symbols list on macOS, and on Windows a DLL exports only what is dllexport'd, which is the entry
# alone. So two packs' copies of a library, or a pack's and the host's, never bind to each other; and Linux links it
# with --no-undefined, so a module that imports a symbol from RetroPlug (rather than reaching the host through rp_host
# interfaces and vtables) fails to link instead of failing to load.
#
# Static libraries linked into a module must be position-independent. Every package library is
# (retroplug_package_library), and so is every vendored library a package links today, because the plugin, itself a
# shared module, links them all; _retroplug_pack_check_pic refuses at configure any static library in a module's link
# closure that does not say so, naming it, rather than leaving a relocation error to the linker.
#
# The module is staged where a development pack is loaded from (a version folder's layout):
#
#   build/packs/<id>/<version>/pack.json                      the package's manifest template, apiLevel stamped
#   build/packs/<id>/<version>/native/<platform>/<stem><ext>  the module (halves.native names the stem)

# The API level, read from the header a pack's entry compiles against, so the level a pack.json is stamped with and the
# level its entry reports cannot disagree.
set(_rp_pack_header "${CMAKE_CURRENT_LIST_DIR}/../packages/node-sdk/native/src/graph/rp_pack.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_rp_pack_header}")
file(STRINGS "${_rp_pack_header}" _rp_pack_level_line REGEX "^#define RP_API_LEVEL [0-9]+u$")
string(REGEX REPLACE "^#define RP_API_LEVEL ([0-9]+)u$" "\\1" RP_API_LEVEL "${_rp_pack_level_line}")
if(NOT RP_API_LEVEL MATCHES "^[0-9]+$")
    message(FATAL_ERROR "cmake/packs.cmake: ${_rp_pack_header} has no `#define RP_API_LEVEL <n>u` line")
endif()

# The platform folder this build's modules go in, as host/packs/PackDirs.cpp's currentPlatform() names it (the packs
# test holds the two to each other). Empty on a platform no pack is built for.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
    set(RP_PACK_PLATFORM "windows-x64")
elseif(APPLE)
    set(RP_PACK_PLATFORM "macos-universal")
    if(NOT "arm64" IN_LIST CMAKE_OSX_ARCHITECTURES OR NOT "x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES)
        message(WARNING "cmake/packs.cmake: macOS pack modules go in a folder named macos-universal, but "
                        "CMAKE_OSX_ARCHITECTURES (${CMAKE_OSX_ARCHITECTURES}) is not both arm64 and x86_64")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(RP_PACK_PLATFORM "linux-x64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(RP_PACK_PLATFORM "linux-arm64")
else()
    set(RP_PACK_PLATFORM "")
endif()
set(RP_PACKS_DIR "${CMAKE_BINARY_DIR}/packs")

# _retroplug_pack_check_pic(<module> <target>...): every static library the targets link, directly or through
# INTERFACE_LINK_LIBRARIES, is position-independent. An imported or interface target is not judged (it has no objects
# of ours), nor is a plain library name or path. retroplug_pack_module defers the call to the end of the configure, so
# a library defined after the module (or given its PIC property late) is judged as the build will see it.
function(_retroplug_pack_check_pic module)
    set(_queue ${ARGN})
    set(_seen "")
    while(_queue)
        list(POP_FRONT _queue _item)
        # $<LINK_ONLY:x>, $<BUILD_INTERFACE:x> and their kin name x; a configuration expression names nothing here.
        string(REGEX REPLACE "^\\$<(LINK_ONLY|BUILD_INTERFACE|BUILD_LOCAL_INTERFACE):(.*)>$" "\\2" _item "${_item}")
        if(_item MATCHES "^\\$<" OR NOT TARGET "${_item}" OR _item IN_LIST _seen)
            continue()
        endif()
        list(APPEND _seen "${_item}")
        get_target_property(_aliased "${_item}" ALIASED_TARGET)
        if(_aliased)
            list(APPEND _queue "${_aliased}")
            continue()
        endif()
        get_target_property(_imported "${_item}" IMPORTED)
        if(_imported)
            continue()
        endif()
        get_target_property(_type "${_item}" TYPE)
        if(_type STREQUAL "STATIC_LIBRARY" OR _type STREQUAL "OBJECT_LIBRARY")
            get_target_property(_pic "${_item}" POSITION_INDEPENDENT_CODE)
            if(NOT _pic)
                message(FATAL_ERROR "retroplug_pack_module: ${module} links the static or object library "
                                    "${_item}, which is not position-independent: set its POSITION_INDEPENDENT_CODE "
                                    "ON (a pack module is a shared object, and a static or object library linked "
                                    "into one must be built -fPIC)")
            endif()
        endif()
        if(NOT _type STREQUAL "INTERFACE_LIBRARY")
            get_target_property(_links "${_item}" LINK_LIBRARIES)
            if(_links)
                list(APPEND _queue ${_links})
            endif()
        endif()
        get_target_property(_iface "${_item}" INTERFACE_LINK_LIBRARIES)
        if(_iface)
            list(APPEND _queue ${_iface})
        endif()
    endwhile()
endfunction()

# retroplug_pack_module(<target> MANIFEST <pack.json template> [LIBS <target>...] [SOURCES <file>...]
#                       [DEFINES <name=value>...] [ENTRIES <C function>...] [EXPORTS <C function>...] [API_LEVEL <n>] [ENTRY_API_LEVEL <n>]
#                       [NO_ENTRY] [THROWING_ENTRY] [NO_LIBRARY] [TS_HALVES <package dir>])
#
# Stages build/packs/<id>/<version>/pack.json from MANIFEST (its `id`, `version` and `halves.native` read from it, its
# `apiLevel` set to API_LEVEL, RP_API_LEVEL by default), and defines <target>, the MODULE library built from SOURCES and
# LIBS with an entry returning ENTRIES' rp_packages in order and reporting ENTRY_API_LEVEL (API_LEVEL by default).
# EXPORTS are exported beside the entry: a test variant's hooks, which the SOURCES define `extern "C" RP_EXPORT` (on
# Windows a DLL exports what is dllexport'd, so the list matters only on Linux and macOS).
# EXCLUDE_FROM_ALL: what loads a pack depends on its target. The fixture pack's broken variants use the rest:
# NO_ENTRY builds a module whose one export is rp_pack_entry_withheld (an exported-symbols list is never empty, which
# macOS's linker would refuse); THROWING_ENTRY one whose entry first calls rp_pack_entry_prelude(), a C++ function the
# variant's SOURCES define (and throw from: a throw written in the extern "C", dllexport'ed entry itself is MSVC's
# C4297); and NO_LIBRARY stages the manifest alone, defining no target.
# TS_HALVES builds the package's TS halves into the folder with tools/build-pack-halves.mjs, which then writes the
# folder's pack.json from the stamped template (the folder's one writer of it, so configure writes none); the module
# target depends on that build, with a depfile from the esbuild metafiles so editing a bundled source rebuilds it.
function(retroplug_pack_module target)
    cmake_parse_arguments(M "NO_ENTRY;THROWING_ENTRY;NO_LIBRARY" "MANIFEST;API_LEVEL;ENTRY_API_LEVEL;TS_HALVES"
                          "LIBS;SOURCES;DEFINES;ENTRIES;EXPORTS" ${ARGN})
    if(M_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "retroplug_pack_module(${target}): unrecognised argument(s): ${M_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT M_MANIFEST OR NOT EXISTS "${M_MANIFEST}")
        message(FATAL_ERROR "retroplug_pack_module(${target}): MANIFEST names no file (${M_MANIFEST})")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${M_MANIFEST}")
    file(READ "${M_MANIFEST}" _json)
    foreach(_member id version)
        string(JSON _${_member} ERROR_VARIABLE _err GET "${_json}" ${_member})
        if(_err OR NOT _${_member} MATCHES "^[a-z0-9][a-z0-9.-]*$")
            message(FATAL_ERROR "retroplug_pack_module(${target}): ${M_MANIFEST} has no plain `${_member}`")
        endif()
    endforeach()
    if(NOT DEFINED M_API_LEVEL)
        set(M_API_LEVEL ${RP_API_LEVEL})
    endif()
    if(NOT DEFINED M_ENTRY_API_LEVEL)
        set(M_ENTRY_API_LEVEL ${M_API_LEVEL})
    endif()
    string(JSON _json SET "${_json}" apiLevel ${M_API_LEVEL})
    set(_dir "${RP_PACKS_DIR}/${_id}/${_version}")
    get_property(_staged GLOBAL PROPERTY RP_PACK_STAGED)
    if("${_id}/${_version}" IN_LIST _staged)
        message(FATAL_ERROR "retroplug_pack_module(${target}): ${_id}/${_version} is already staged by another target")
    endif()
    set_property(GLOBAL APPEND PROPERTY RP_PACK_STAGED "${_id}/${_version}")
    # Written at configure, and copied only when its content changed, so a pack's folder exists before anything builds
    # and a reconfigure leaves an unchanged manifest's timestamp alone.
    file(MAKE_DIRECTORY "${_dir}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/packs/${target}.pack.json" "${_json}\n")
    if(NOT M_TS_HALVES)
        file(COPY_FILE "${CMAKE_CURRENT_BINARY_DIR}/packs/${target}.pack.json" "${_dir}/pack.json" ONLY_IF_DIFFERENT)
    endif()
    if(M_TS_HALVES AND RP_PACK_PLATFORM)
        file(READ "${M_TS_HALVES}/package.json" _pkg)
        set(_halves_out "${_dir}/pack.json")
        foreach(_half app consoles ui views cli)
            string(JSON _entry ERROR_VARIABLE _err GET "${_pkg}" retroplug ${_half})
            if(NOT _err)
                list(APPEND _halves_out "${_dir}/js/${_half}.js")
            endif()
        endforeach()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${M_TS_HALVES}/package.json")
        set(_depfile "${CMAKE_CURRENT_BINARY_DIR}/packs/${target}-halves.d")
        add_custom_command(
            OUTPUT ${_halves_out}
            COMMAND ${NODE_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/build-pack-halves.mjs" "${M_TS_HALVES}" "${_dir}"
                    "${CMAKE_CURRENT_BINARY_DIR}/packs/${target}.pack.json" --depfile "${_depfile}"
            DEPFILE "${_depfile}"
            DEPENDS "${CMAKE_SOURCE_DIR}/tools/build-pack-halves.mjs"
                    "${CMAKE_CURRENT_BINARY_DIR}/packs/${target}.pack.json"
                    "${M_TS_HALVES}/package.json"
                    "${CMAKE_SOURCE_DIR}/packages/retroplug/scripts/lib/contracts.mjs"
                    "${CMAKE_SOURCE_DIR}/packages/retroplug/host-modules.json"
                    "${_rp_pack_header}"
            COMMENT "Building ${target}'s TS halves into ${_dir}"
            VERBATIM)
        add_custom_target(${target}-halves DEPENDS ${_halves_out})
    endif()
    if(M_NO_LIBRARY)
        return()
    endif()
    string(JSON _stem ERROR_VARIABLE _err GET "${_json}" halves native)
    if(_err OR NOT _stem MATCHES "^[a-z][a-z0-9-]*$")
        message(FATAL_ERROR "retroplug_pack_module(${target}): ${M_MANIFEST} has no `halves.native` library stem")
    endif()
    if(NOT RP_PACK_PLATFORM)
        message(STATUS "retroplug_pack_module(${target}): this platform (${CMAKE_SYSTEM_NAME} "
                       "${CMAKE_SYSTEM_PROCESSOR}) has no pack platform folder, so no pack is built")
        return()
    endif()
    if(NOT M_ENTRIES AND NOT M_NO_ENTRY)
        message(FATAL_ERROR "retroplug_pack_module(${target}): ENTRIES names no package entry")
    endif()

    # The entry, generated as a product's registration is: the packages in the order ENTRIES names them.
    set(_decls "")
    set(_calls "")
    list(LENGTH M_ENTRIES _count)
    foreach(_e IN LISTS M_ENTRIES)
        string(APPEND _decls "extern \"C\" const rp_package* ${_e}(void);\n")
        string(APPEND _calls "        ${_e}(),\n")
    endforeach()
    set(_name "rp_pack_entry")
    set(_linkage "")
    if(M_NO_ENTRY)
        # The same body under another name, exported in the entry's place: a module that links and has no entry.
        set(_name "rp_pack_entry_withheld")
        set(_linkage "RP_EXPORT ")
    endif()
    set(_throw "")
    set(_prelude "")
    if(M_THROWING_ENTRY)
        # Defined in another translation unit, so the compiler cannot see that it always throws: the rest of the body
        # stays reachable (MSVC's C4702 is an error under /WX).
        set(_prelude "void rp_pack_entry_prelude();\n")
        set(_throw "    rp_pack_entry_prelude();\n")
    endif()
    # A zero-length array is ill-formed C++ (MSVC C2466), so a module with no entries (NO_ENTRY alone) returns none.
    if(_count EQUAL 0)
        set(_body "    *count = 0;\n    return nullptr;\n")
    else()
        set(_body "    static const rp_package* const packages[] = {\n${_calls}    };\n")
        string(APPEND _body "    *count = ${_count};\n    return packages;\n")
    endif()
    set(_entry "${CMAKE_CURRENT_BINARY_DIR}/packs/${target}-entry.cpp")
    file(GENERATE OUTPUT "${_entry}" CONTENT
"// Generated by retroplug_pack_module(${target}) in cmake/packs.cmake from ${M_MANIFEST}. Do not edit.

#include \"graph/rp_pack.h\"

${_decls}${_prelude}
extern \"C\" ${_linkage}const rp_package* const* ${_name}(uint32_t* api_level, size_t* count) {
${_throw}    *api_level = ${M_ENTRY_API_LEVEL}u;
${_body}}
")
    add_library(${target} MODULE EXCLUDE_FROM_ALL "${_entry}" ${M_SOURCES})
    target_link_libraries(${target} PRIVATE retroplug-node-sdk-headers ${M_LIBS})
    if(M_TS_HALVES)
        add_dependencies(${target} ${target}-halves)
    endif()
    # The linker makes no folder: one removed since the configure (a clean of the pack) is made again by the build.
    add_custom_command(TARGET ${target} PRE_LINK
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_dir}/native/${RP_PACK_PLATFORM}" VERBATIM)
    if(M_DEFINES)
        target_compile_definitions(${target} PRIVATE ${M_DEFINES})
    endif()
    if(WIN32)
        set(_suffix ".dll")
    elseif(APPLE)
        set(_suffix ".dylib")
    else()
        set(_suffix ".so")
    endif()
    # $<1:...> keeps a multi-config generator from appending a per-configuration folder: the loader looks in
    # native/<platform>/ and nowhere else.
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        PREFIX ""
        OUTPUT_NAME "${_stem}"
        SUFFIX "${_suffix}"
        LIBRARY_OUTPUT_DIRECTORY "$<1:${_dir}/native/${RP_PACK_PLATFORM}>"
        RUNTIME_OUTPUT_DIRECTORY "$<1:${_dir}/native/${RP_PACK_PLATFORM}>"
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
    set(_exported "${_name}" ${M_EXPORTS})
    if(APPLE)
        set(_exports "${CMAKE_CURRENT_BINARY_DIR}/packs/${target}.exports")
        list(TRANSFORM _exported PREPEND "_" OUTPUT_VARIABLE _apple)
        list(JOIN _apple "\n" _apple)
        file(CONFIGURE OUTPUT "${_exports}" CONTENT "${_apple}\n")
        target_link_options(${target} PRIVATE "LINKER:-exported_symbols_list,${_exports}")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${_exports}")
    elseif(NOT WIN32)
        set(_script "${CMAKE_CURRENT_BINARY_DIR}/packs/${target}.map")
        list(JOIN _exported "; " _globals)
        file(CONFIGURE OUTPUT "${_script}" CONTENT "{ global: ${_globals}; local: *; };\n")
        target_link_options(${target} PRIVATE "LINKER:--version-script=${_script}" "LINKER:--no-undefined")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${_script}")
    endif()
    # EVAL expands the variables now: a deferred call's arguments would be read at the end of the configure, when this
    # function's variables are gone.
    cmake_language(EVAL CODE "cmake_language(DEFER DIRECTORY [[${CMAKE_SOURCE_DIR}]] "
                             "CALL _retroplug_pack_check_pic ${target} ${M_LIBS})")
    set_property(GLOBAL APPEND PROPERTY RP_PACK_TARGETS ${target})
endfunction()

# retroplug_pack(<package> [MANIFEST <file>] [TS_HALVES]): the package's native entries built as a pack, retroplug-<package>-pack,
# from its manifest's desktop targets and entries (the same ones a product links), its pack.json template MANIFEST
# (packages/<package>/pack.json by default); a valueless TS_HALVES also builds the package's TS halves into the folder
# (retroplug_pack_module's TS_HALVES). Called from the package's own native/CMakeLists.txt, after its targets,
# so the module is the package's own target and links nothing its manifest does not declare. Part 1 loads a pack's
# native node types alone, so a package with a provider, an audio backend, a script pack or an embed is refused here
# rather than built into a pack that would silently lose them.
function(retroplug_pack package)
    cmake_parse_arguments(P "TS_HALVES" "MANIFEST" "" ${ARGN})
    if(P_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "retroplug_pack(${package}): unrecognised argument(s): ${P_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT P_MANIFEST)
        set(P_MANIFEST "${RP_PACKAGES_DIR}/${package}/pack.json")
    endif()
    get_property(_entries GLOBAL PROPERTY RP_PACKAGE_${package}_ENTRIES)
    get_property(_targets GLOBAL PROPERTY RP_PACKAGE_${package}_TARGETS)
    foreach(_part PROVIDERS AUDIO_BACKEND_TARGETS SCRIPT_ENTRY EMBED_IDS)
        get_property(_has GLOBAL PROPERTY RP_PACKAGE_${package}_${_part})
        if(_has)
            message(FATAL_ERROR "retroplug_pack(${package}): the package has ${_part} (${_has}), which a pack does not "
                                "load yet (docs/downloadable-packs-loading-design.md): build it into a product")
        endif()
    endforeach()
    if(NOT _entries)
        message(FATAL_ERROR "retroplug_pack(${package}): the package's manifest names no native entry, so its pack "
                            "would register nothing")
    endif()
    set(_halves "")
    if(P_TS_HALVES)
        set(_halves TS_HALVES "${RP_PACKAGES_DIR}/${package}")
    endif()
    retroplug_pack_module(retroplug-${package}-pack MANIFEST "${P_MANIFEST}" LIBS ${_targets} ENTRIES ${_entries}
                          ${_halves})
endfunction()
