# Workspace packages and the products that compose them (docs/modular-graph-phase-e1-design.md section 4).
#
# A package is packages/<name>/, a pnpm workspace member whose package.json carries a `retroplug` field: its manifest.
# The manifest's `native` member names the CMake target the package builds and, for a package that holds node types,
# the entry function returning its rp_package and whether those types build for the device:
#
#   "retroplug": { "native": { "target": "retroplug-core-nodes", "entry": "rp_core_package", "device": true } }
#
# `native` may also be a list of those, for a package whose types register as more than one rp_package: dsp holds
# rp.dsp and rp.dsp.resample, which is an rp_package of its own so that a build can leave r8brain out (its target
# links r8brain, its entry is not `device`). An object with no `entry` is a library and nothing more (node-sdk). The
# manifest is the ONE source of a package's targets and entries: retroplug_package() reads them with string(JSON), and
# nothing else in the build spells an entry.
#
# A product is an executable or a module that constructs an Engine. retroplug_product() generates its registration
# source, which defines rp_register_packages() (graph/Packages.hpp) as the entries of the packages it lists, in the
# order it lists them, and links those packages' targets. The engine registers what that function returns, so which
# node types a product carries is its CMake's choice. A binary that pulls the engine in without declaring itself a
# product has no definition of rp_register_packages: an executable (and a module on macOS or Windows) fails to link
# naming it, but a Linux module links and fails only when it is loaded. So the configure checks it instead, for every
# binary: retroplug_check_products() refuses one that reaches the engine through its libraries and is not a product.
#
# This file needs CMake 3.19: string(JSON) for the manifests and cmake_language(DEFER) for the product check, which the
# root's cmake_minimum_required states.

# The device build (docs/modular-graph-phase-e1-design.md section 7) includes this file too: the RP2350 firmware
# packages/graph-runtime/device-check, its own CMake project on the Pico SDK's toolchain, sets RP_DEVICE_BUILD before it
# does. Then a package's native/CMakeLists.txt defines its device targets alone, retroplug_package_library() compiles
# them as a device compiles graph/node.hpp, and a product is a DEVICE product (retroplug_product() says what that is).
# The packages are found from this file, not from the including project's source dir, so either project finds them.
get_filename_component(RP_PACKAGES_DIR "${CMAKE_CURRENT_LIST_DIR}/../packages" ABSOLUTE)
set(RP_PRODUCTS_DIR "${CMAKE_BINARY_DIR}/packages/products")

# retroplug_package(<name>): reads packages/<name>/package.json's manifest and, when the package has a
# native/CMakeLists.txt, adds it (its binary dir build/packages/<name>). Call it once per package, before any product
# that lists the package. A package whose targets are defined elsewhere (packages/native, the engine's own tree, whose
# CMakeLists the root adds) has no native/CMakeLists.txt, and this only reads its manifest.
#
# A package's native/CMakeLists.txt is added from inside this function, so it runs in this function's variable scope:
# what it set() is visible to nothing after it, and a set(... PARENT_SCOPE) in it lands here and is lost rather than
# reaching the root. A package communicates through its targets (and cache or global properties), never variables.
function(retroplug_package name)
    # build/packages/<name> is the package's binary dir and build/packages/products holds the generated sources.
    if(name STREQUAL "products")
        message(FATAL_ERROR "retroplug_package(products): `products` is the directory of the generated product sources "
                            "(${RP_PRODUCTS_DIR}), which a package's build tree would sit on")
    endif()
    set(_dir "${RP_PACKAGES_DIR}/${name}")
    set(_manifest "${_dir}/package.json")
    if(NOT EXISTS "${_manifest}")
        message(FATAL_ERROR "retroplug_package(${name}): ${_manifest} does not exist")
    endif()
    get_property(_packages GLOBAL PROPERTY RP_PACKAGES)
    if(name IN_LIST _packages)
        message(FATAL_ERROR "retroplug_package(${name}): the package is added twice")
    endif()
    # A manifest is configure input, as a CMakeLists.txt is: editing one re-runs the configure.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_manifest}")
    file(READ "${_manifest}" _json)
    string(JSON _kind ERROR_VARIABLE _err TYPE "${_json}" retroplug native)
    if(_err)
        message(FATAL_ERROR "retroplug_package(${name}): ${_manifest} has no retroplug.native field (${_err})")
    endif()
    string(JSON _native GET "${_json}" retroplug native)
    if(_kind STREQUAL "OBJECT")
        set(_native "[${_native}]")
    elseif(NOT _kind STREQUAL "ARRAY")
        message(FATAL_ERROR "retroplug_package(${name}): retroplug.native is a ${_kind}, not an object or a list")
    endif()
    string(JSON _count LENGTH "${_native}")
    if(_count EQUAL 0)
        message(FATAL_ERROR "retroplug_package(${name}): retroplug.native is an empty list")
    endif()

    get_property(_taken GLOBAL PROPERTY RP_PACKAGE_ENTRIES)
    set(_targets "")
    set(_entries "")
    set(_device "")
    set(_deviceTargets "")
    math(EXPR _last "${_count} - 1")
    foreach(_i RANGE ${_last})
        string(JSON _target ERROR_VARIABLE _err GET "${_native}" ${_i} target)
        if(_err)
            message(FATAL_ERROR "retroplug_package(${name}): retroplug.native[${_i}] names no target")
        endif()
        list(APPEND _targets ${_target})
        string(JSON _entry ERROR_VARIABLE _noEntry GET "${_native}" ${_i} entry)
        if(_noEntry)
            continue()
        endif()
        # A boolean reads back as ON or OFF. Required wherever there is an entry, so no package's device standing is
        # a default nobody chose.
        string(JSON _isDevice ERROR_VARIABLE _err GET "${_native}" ${_i} device)
        if(_err OR NOT (_isDevice STREQUAL "ON" OR _isDevice STREQUAL "OFF"))
            message(FATAL_ERROR "retroplug_package(${name}): entry ${_entry} needs \"device\": true or false")
        endif()
        if(NOT _entry MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
            message(FATAL_ERROR "retroplug_package(${name}): entry \"${_entry}\" is not a C identifier")
        endif()
        # Every statically linked entry is an extern "C" symbol of the program, so two packages may not share one;
        # rp_package_entry is a downloaded pack's, never a linked package's.
        if(_entry IN_LIST _taken OR _entry STREQUAL "rp_package_entry")
            message(FATAL_ERROR "retroplug_package(${name}): entry ${_entry} is taken")
        endif()
        list(APPEND _taken ${_entry})
        list(APPEND _entries ${_entry})
        if(_isDevice)
            list(APPEND _device ${_entry})
            list(APPEND _deviceTargets ${_target})
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _targets)
    list(REMOVE_DUPLICATES _deviceTargets)

    set_property(GLOBAL APPEND PROPERTY RP_PACKAGES ${name})
    set_property(GLOBAL APPEND PROPERTY RP_PACKAGE_ENTRIES ${_entries})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_TARGETS ${_targets})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_ENTRIES ${_entries})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_DEVICE_ENTRIES ${_device})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_DEVICE_TARGETS ${_deviceTargets})

    if(EXISTS "${_dir}/native/CMakeLists.txt")
        add_subdirectory("${_dir}/native" "${CMAKE_BINARY_DIR}/packages/${name}")
        # What its manifest names it defines now, so a typo is refused here, whether or not a product lists the package
        # (node-sdk, which none does). A device build defines the device targets alone. A package without a
        # native/CMakeLists.txt (native) has its targets defined later, and retroplug_product() checks those.
        if(RP_DEVICE_BUILD)
            set(_defined ${_deviceTargets})
        else()
            set(_defined ${_targets})
        endif()
        foreach(_target IN LISTS _defined)
            if(NOT TARGET ${_target})
                message(FATAL_ERROR "retroplug_package(${name}): ${_manifest} names `${_target}`, which "
                                    "${_dir}/native/CMakeLists.txt does not define")
            endif()
        endforeach()
    endif()
    # A device build compiles every target as a device does already, so it has nothing to check twice.
    if(NOT RP_DEVICE_BUILD)
        foreach(_target IN LISTS _deviceTargets)
            _retroplug_device_check(${name} ${_target})
        endforeach()
    endif()
endfunction()

# _retroplug_device_check(<package> <target>): the desktop build's check of a `device` entry. An entry the manifest
# marks `device` is one a device product compiles, so the desktop's default build compiles its target's sources a
# second time, as the device does (graph/node.hpp under RP_DEVICE, no exceptions, no RTTI), into an object library
# nothing links: <target>-device. A node type whose settings a device cannot hold, one declaring heap or threads, and
# a description hook left outside `#ifndef RP_DEVICE` (node.hpp's header comment) then fail the desktop build, on
# every platform CI builds, not only a device toolchain's. The manifest's flag is what keys it, rather than a constant
# on each node type, because only a device-mode compile can see a hook that is not guarded, and the flag already says
# which targets a device compiles. It copies the target's sources, definitions, options and include directories; a
# source a generator expression picks, or a C source, is refused here rather than half-copied, and a device entry's
# target must exist when retroplug_package() runs (a package's own native/CMakeLists.txt defines it).
function(_retroplug_device_check package target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "retroplug_package(${package}): ${target}, a device entry's target, is not a target")
    endif()
    get_target_property(_sources ${target} SOURCES)
    get_target_property(_sourceDir ${target} SOURCE_DIR)
    set(_absolute "")
    foreach(_source IN LISTS _sources)
        # A source the device compile could not take as it stands: one a generator expression chooses (it would need
        # evaluating per configuration), or C (graph/node.hpp is C++, and a C file is no node source).
        if(_source MATCHES "^\\$<")
            message(FATAL_ERROR "retroplug_package(${package}): ${target}'s source ${_source} is a generator "
                                "expression, which its device check (_retroplug_device_check) does not take")
        endif()
        if(_source MATCHES "\\.c$")
            message(FATAL_ERROR "retroplug_package(${package}): ${target}'s source ${_source} is C, which its device "
                                "check (_retroplug_device_check) does not take")
        endif()
        if(NOT IS_ABSOLUTE "${_source}")
            set(_source "${_sourceDir}/${_source}")
        endif()
        list(APPEND _absolute "${_source}")
    endforeach()
    add_library(${target}-device OBJECT ${_absolute})
    set_target_properties(${target}-device PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    # The target's own definitions, options (/bigobj on MSVC among them, retroplug_package_library's) and include
    # directories, read when the build is generated, so a package that adds one after this ran is still copied; then
    # the device mode on top.
    target_compile_definitions(${target}-device PRIVATE $<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS> RP_DEVICE)
    target_compile_options(${target}-device PRIVATE $<TARGET_PROPERTY:${target},COMPILE_OPTIONS>)
    if(NOT MSVC)
        target_compile_options(${target}-device PRIVATE -fno-exceptions -fno-rtti)
    endif()
    target_include_directories(${target}-device PRIVATE $<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>)
    target_link_libraries(${target}-device PRIVATE retroplug-node-sdk-headers retroplug-reflect-headers)
    set_property(GLOBAL APPEND PROPERTY RP_DEVICE_CHECK_TARGETS ${target}-device)
endfunction()

# retroplug_package_library(<target> <sources>...): a package's static library, with the settings every one of them
# shares. Position-independent, as each is linked into the plugin's shared module, and /bigobj on MSVC: reflect-cpp's
# templates instantiate inside any translation unit that includes graph/node.hpp, enough COMDAT sections to pass COFF's
# 2^16 limit (C1128), which packages/native sets directory-wide for its own and a package's directory does not inherit.
#
# In a device build it is the device's library instead: graph/node.hpp's device mode (RP_DEVICE), no exceptions and no
# RTTI, and the code-generation options the device project names in RP_DEVICE_COMPILE_OPTIONS (the Pico SDK's, which
# its own sources compile with), on top of what the device's toolchain adds to every compile; not
# position-independent, as a firmware image links it statically.
function(retroplug_package_library target)
    add_library(${target} STATIC ${ARGN})
    set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    if(RP_DEVICE_BUILD)
        target_compile_definitions(${target} PRIVATE RP_DEVICE)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>"
                               ${RP_DEVICE_COMPILE_OPTIONS})
        return()
    endif()
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/bigobj>)
    endif()
endfunction()

# retroplug_product(<product> PACKAGES <package>... [TARGETS <target>...] [DEVICE]): declares a product and the packages
# it carries, in registration order. Generates build/packages/products/<product>.cpp, which defines the product's
# rp_register_packages(), and adds it to each of TARGETS (default: the target named <product>) with the packages'
# targets linked. TARGETS is for one product built as several binaries: the plugin's formats, each its own module.
#
# DEVICE makes it a device product: it takes each package's `device` entries alone, and their targets, and a package
# with none is refused, since a device product that names one would carry a node type the device cannot run (as the
# planner refuses one on the Pico). A device build's products are all device products: the RP2350 check, whose targets
# are device-mode libraries. A desktop build may declare one too, which links the desktop's builds of the same entries:
# the device check's case generator, which lists what the device registers. A device product constructs no engine, so
# it is not in the engine's registration golden (retroplug_product_table); retroplug-device-mode-test holds what a
# device registers instead.
function(retroplug_product product)
    cmake_parse_arguments(P "DEVICE" "" "PACKAGES;TARGETS" ${ARGN})
    if(P_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "retroplug_product(${product}): unrecognised argument(s): ${P_UNPARSED_ARGUMENTS}")
    endif()
    # TARGETS given but empty (a list that resolved to nothing) is an error, not the default: for the plugin the default
    # would be DPF's static `retroplug` library, the one place the registration must not go.
    if("TARGETS" IN_LIST ARGN AND NOT P_TARGETS)
        message(FATAL_ERROR "retroplug_product(${product}): TARGETS names no target")
    endif()
    if(NOT P_PACKAGES)
        message(FATAL_ERROR "retroplug_product(${product}): PACKAGES is required")
    endif()
    if(RP_DEVICE_BUILD AND NOT P_DEVICE)
        message(FATAL_ERROR "retroplug_product(${product}): a device build declares DEVICE products alone")
    endif()
    if(NOT P_TARGETS)
        set(P_TARGETS ${product})
    endif()
    get_property(_products GLOBAL PROPERTY RP_PRODUCTS)
    get_property(_deviceProducts GLOBAL PROPERTY RP_DEVICE_PRODUCTS)
    if(product IN_LIST _products OR product IN_LIST _deviceProducts)
        message(FATAL_ERROR "retroplug_product(${product}): the product is declared twice")
    endif()
    set(_unique ${P_PACKAGES})
    list(REMOVE_DUPLICATES _unique)
    list(LENGTH _unique _nUnique)
    list(LENGTH P_PACKAGES _nListed)
    if(NOT _nUnique EQUAL _nListed)
        # Its entry would be called twice: the engine refuses the second offer with a line on stderr and carries on.
        set(_seen "")
        foreach(_p IN LISTS P_PACKAGES)
            if(_p IN_LIST _seen)
                message(FATAL_ERROR "retroplug_product(${product}): package `${_p}` is listed twice in PACKAGES")
            endif()
            list(APPEND _seen ${_p})
        endforeach()
    endif()
    get_property(_known GLOBAL PROPERTY RP_PACKAGES)
    set(_entries "")
    set(_targets "")
    set(_leftOut "")
    foreach(_p IN LISTS P_PACKAGES)
        if(NOT _p IN_LIST _known)
            message(FATAL_ERROR "retroplug_product(${product}): no package `${_p}` (retroplug_package() adds one)")
        endif()
        get_property(_e GLOBAL PROPERTY RP_PACKAGE_${_p}_ENTRIES)
        get_property(_t GLOBAL PROPERTY RP_PACKAGE_${_p}_TARGETS)
        if(P_DEVICE)
            set(_all ${_e})
            get_property(_e GLOBAL PROPERTY RP_PACKAGE_${_p}_DEVICE_ENTRIES)
            get_property(_t GLOBAL PROPERTY RP_PACKAGE_${_p}_DEVICE_TARGETS)
            if(NOT _e)
                message(FATAL_ERROR "retroplug_product(${product}): package `${_p}` has no `device` entry "
                                    "(${RP_PACKAGES_DIR}/${_p}/package.json), so a device product cannot carry it")
            endif()
            list(REMOVE_ITEM _all ${_e})
            list(APPEND _leftOut ${_all})
        endif()
        # Every package's targets exist by the first product (the packages are added before packages/native, whose
        # retroplug-backend comes before its first product), so a name that is not one is a manifest typo, refused here
        # rather than left to the linker as a bare -l flag.
        foreach(_target IN LISTS _t)
            if(NOT TARGET ${_target})
                message(FATAL_ERROR "retroplug_product(${product}): ${RP_PACKAGES_DIR}/${_p}/package.json names "
                                    "`${_target}`, which is not a target")
            endif()
        endforeach()
        list(APPEND _entries ${_e})
        list(APPEND _targets ${_t})
    endforeach()
    list(REMOVE_DUPLICATES _targets)

    _retroplug_entry_declarations(_decls ${_entries})
    list(LENGTH _entries _n)
    if(_n EQUAL 0)
        set(_body "    *count = 0;\n    return nullptr;\n")
    elseif(P_DEVICE)
        # Filled on every call rather than initialised: a static initialised by calls needs a guard
        # (__cxa_guard_acquire), which a device image does not link (the device check's link trap refuses one).
        # Every call writes the same pointers, and a device product calls it once.
        set(_body "    static const rp_package* packages[${_n}];\n")
        set(_i 0)
        foreach(_e IN LISTS _entries)
            string(APPEND _body "    packages[${_i}] = ${_e}();\n")
            math(EXPR _i "${_i} + 1")
        endforeach()
        string(APPEND _body "    *count = ${_n};\n    return packages;\n")
    else()
        set(_calls "")
        foreach(_e IN LISTS _entries)
            string(APPEND _calls "        ${_e}(),\n")
        endforeach()
        set(_body "    static const rp_package* const packages[] = {\n${_calls}    };\n")
        string(APPEND _body "    *count = ${_n};\n    return packages;\n")
    endif()
    list(JOIN P_PACKAGES " " _listed)
    set(_kind "")
    if(P_DEVICE)
        set(_kind ", a device product,")
        if(_leftOut)
            list(JOIN _leftOut " " _leftOut)
            string(APPEND _listed ";\n// their entries that are not `device`, left out: ${_leftOut}")
        endif()
    endif()
    set(_src "${RP_PRODUCTS_DIR}/${product}.cpp")
    file(GENERATE OUTPUT "${_src}" CONTENT
"// Generated by retroplug_product(${product})${_kind} in cmake/packages.cmake from the manifests of the packages it
// lists: ${_listed}. Do not edit; edit the product's PACKAGES or a manifest.

#include \"graph/Packages.hpp\"

${_decls}
const rp_package* const* rp_register_packages(size_t* count) {
${_body}}
")
    foreach(_t IN LISTS P_TARGETS)
        if(NOT TARGET ${_t})
            message(FATAL_ERROR "retroplug_product(${product}): no target `${_t}`")
        endif()
        target_sources(${_t} PRIVATE "${_src}")
        target_link_libraries(${_t} PRIVATE ${_targets})
    endforeach()
    if(P_DEVICE)
        set_property(GLOBAL APPEND PROPERTY RP_DEVICE_PRODUCTS ${product})
        set_property(GLOBAL PROPERTY RP_DEVICE_PRODUCT_${product}_ENTRIES ${_entries})
        return()
    endif()
    set_property(GLOBAL APPEND PROPERTY RP_PRODUCTS ${product})
    set_property(GLOBAL APPEND PROPERTY RP_PRODUCT_TARGETS ${P_TARGETS})
    set_property(GLOBAL PROPERTY RP_PRODUCT_${product}_ENTRIES ${_entries})
endfunction()

# retroplug_check_products(LIBRARIES <library>... [EXEMPT <target>...]): refuses, at configure time, every executable,
# module or shared library in the build that reaches the engine and is neither a product's target nor EXEMPT. LIBRARIES
# are the seeds, the libraries that hold the engine (retroplug-backend: Engine.o); the set of libraries that carry it is
# computed outward from them, every static, object or interface library of the build whose LINK_LIBRARIES or
# INTERFACE_LINK_LIBRARIES name a carrier (a generator expression's operands included, so `$<LINK_ONLY:x>` and
# `$<$<BOOL:..>:x>` count), repeated until nothing joins, so a library of ours that links the backend makes whatever
# links it a suspect without anyone listing it. A binary that links a carrier is then refused. EXEMPT is for a binary
# that does and is known never to pull the engine's object in; each name must be a target, so a renamed test cannot leave
# a stale exemption. It stands in for the link error a Linux module never gives (this file's header says why);
# -Wl,--no-undefined would not, as it breaks the sanitizer builds and guards only the modules that are already
# products.
#
# It must see every binary of the build, so it runs when the root's CMakeLists is done:
#
#   cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL retroplug_check_products LIBRARIES ... EXEMPT ...)
#
# (3.19; its arguments are expanded where it is deferred). A binary defined after the call, anywhere in the tree, is
# checked as one before it.
function(retroplug_check_products)
    cmake_parse_arguments(C "" "" "LIBRARIES;EXEMPT" ${ARGN})
    if(NOT C_LIBRARIES)
        message(FATAL_ERROR "retroplug_check_products: LIBRARIES is required")
    endif()
    foreach(_t IN LISTS C_LIBRARIES C_EXEMPT)
        if(NOT TARGET ${_t})
            message(FATAL_ERROR "retroplug_check_products: `${_t}`, named in LIBRARIES or EXEMPT, is not a target")
        endif()
    endforeach()
    get_property(_products GLOBAL PROPERTY RP_PRODUCT_TARGETS)
    _retroplug_build_targets(_targets "${CMAKE_SOURCE_DIR}")

    set(_libraries "")
    set(_binaries "")
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        if(_type STREQUAL "STATIC_LIBRARY" OR _type STREQUAL "OBJECT_LIBRARY" OR _type STREQUAL "INTERFACE_LIBRARY")
            list(APPEND _libraries ${_t})
        elseif(_type STREQUAL "EXECUTABLE" OR _type STREQUAL "MODULE_LIBRARY" OR _type STREQUAL "SHARED_LIBRARY")
            list(APPEND _binaries ${_t})
        endif()
    endforeach()

    set(_carriers ${C_LIBRARIES})
    foreach(_t IN LISTS _libraries)
        _retroplug_linked_names(_names_${_t} ${_t} LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    endforeach()
    set(_grew TRUE)
    while(_grew)
        set(_grew FALSE)
        foreach(_t IN LISTS _libraries)
            if(_t IN_LIST _carriers)
                continue()
            endif()
            foreach(_c IN LISTS _carriers)
                if(_c IN_LIST _names_${_t})
                    list(APPEND _carriers ${_t})
                    set(_grew TRUE)
                    break()
                endif()
            endforeach()
        endforeach()
    endwhile()
    message(VERBOSE "retroplug_check_products: the libraries that carry the engine: ${_carriers}")

    set(_missing "")
    foreach(_t IN LISTS _binaries)
        if(_t IN_LIST _products OR _t IN_LIST C_EXEMPT)
            continue()
        endif()
        _retroplug_linked_names(_linked ${_t} LINK_LIBRARIES)
        foreach(_c IN LISTS _carriers)
            if(_c IN_LIST _linked)
                list(APPEND _missing "${_t} (links ${_c})")
                break()
            endif()
        endforeach()
    endforeach()
    if(_missing)
        list(JOIN _missing ", " _missing)
        message(FATAL_ERROR "retroplug_check_products: these binaries link the engine and are not products, so none "
                            "defines rp_register_packages: ${_missing}. Declare each with retroplug_product() (or "
                            "PACKAGES in retroplug_add_test_binary), or exempt one that never pulls the engine in.")
    endif()
endfunction()

# Every target `dir` and the directories under it define, imported and alias targets excepted. A directory still being
# configured counts, with what it defined so far.
function(_retroplug_build_targets out dir)
    get_property(_found DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sub IN LISTS _subdirs)
        _retroplug_build_targets(_more "${_sub}")
        list(APPEND _found ${_more})
    endforeach()
    set(${out} ${_found} PARENT_SCOPE)
endfunction()

# The names a target's link properties (<property>...) mention, as a list: every word of the property's value, so the
# operands of a generator expression are in it, with its keywords.
function(_retroplug_linked_names out target)
    set(_names "")
    foreach(_property IN LISTS ARGN)
        get_target_property(_value ${target} ${_property})
        if(_value)
            string(REGEX MATCHALL "[A-Za-z0-9_.+-]+" _words "${_value}")
            list(APPEND _names ${_words})
        endif()
    endforeach()
    set(${out} ${_names} PARENT_SCOPE)
endfunction()

# retroplug_product_table(<target>): generates build/packages/products/product-table.cpp, every product this configure
# declared with the entries its rp_register_packages() returns, as rp_product_registrations(), and adds it to <target>,
# which must link every package those name. The registration golden reads it (test/engine-graph/Packages.test.cpp), so
# call this after the last retroplug_product().
function(retroplug_product_table target)
    get_property(_products GLOBAL PROPERTY RP_PRODUCTS)
    set(_all "")
    set(_rows "")
    foreach(_p IN LISTS _products)
        get_property(_e GLOBAL PROPERTY RP_PRODUCT_${_p}_ENTRIES)
        list(APPEND _all ${_e})
        list(JOIN _e ", " _list)
        string(APPEND _rows "        {\"${_p}\", {${_list}}},\n")
    endforeach()
    list(REMOVE_DUPLICATES _all)
    _retroplug_entry_declarations(_decls ${_all})
    set(_src "${RP_PRODUCTS_DIR}/product-table.cpp")
    file(GENERATE OUTPUT "${_src}" CONTENT
"// Generated by retroplug_product_table() in cmake/packages.cmake: every product the configure declared, with the
// package entries its rp_register_packages() returns, in order. Do not edit.

#include <string>
#include <utility>
#include <vector>

#include \"graph/rp_node.h\"

${_decls}
std::vector<std::pair<std::string, std::vector<rp_package_entry_fn>>> rp_product_registrations() {
    return {
${_rows}    };
}
")
    target_sources(${target} PRIVATE "${_src}")
endfunction()

# The extern "C" declarations of a list of package entries, for a generated source.
function(_retroplug_entry_declarations out)
    set(_decls "")
    foreach(_e IN LISTS ARGN)
        string(APPEND _decls "extern \"C\" const rp_package* ${_e}(void);\n")
    endforeach()
    set(${out} "${_decls}" PARENT_SCOPE)
endfunction()
