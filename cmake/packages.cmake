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
# binary: retroplug_check_products() refuses one that links the engine's libraries and is not a product.

set(RP_PACKAGES_DIR "${CMAKE_SOURCE_DIR}/packages")
set(RP_PRODUCTS_DIR "${CMAKE_BINARY_DIR}/packages/products")

# retroplug_package(<name>): reads packages/<name>/package.json's manifest and, when the package has a
# native/CMakeLists.txt, adds it (its binary dir build/packages/<name>). Call it once per package, before any product
# that lists the package. A package whose targets are defined elsewhere (packages/native, the engine's own tree, whose
# CMakeLists the root adds) has no native/CMakeLists.txt, and this only reads its manifest.
function(retroplug_package name)
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
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _targets)

    set_property(GLOBAL APPEND PROPERTY RP_PACKAGES ${name})
    set_property(GLOBAL APPEND PROPERTY RP_PACKAGE_ENTRIES ${_entries})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_TARGETS ${_targets})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_ENTRIES ${_entries})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_DEVICE_ENTRIES ${_device})

    if(EXISTS "${_dir}/native/CMakeLists.txt")
        add_subdirectory("${_dir}/native" "${CMAKE_BINARY_DIR}/packages/${name}")
    endif()
endfunction()

# retroplug_package_library(<target> <sources>...): a package's static library, with the settings every one of them
# shares. Position-independent, as each is linked into the plugin's shared module, and /bigobj on MSVC: reflect-cpp's
# templates instantiate inside any translation unit that includes graph/node.hpp, enough COMDAT sections to pass COFF's
# 2^16 limit (C1128), which packages/native sets directory-wide for its own and a package's directory does not inherit.
function(retroplug_package_library target)
    add_library(${target} STATIC ${ARGN})
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/bigobj>)
    endif()
endfunction()

# retroplug_product(<product> PACKAGES <package>... [TARGETS <target>...]): declares a product and the packages it
# carries, in registration order. Generates build/packages/products/<product>.cpp, which defines the product's
# rp_register_packages(), and adds it to each of TARGETS (default: the target named <product>) with the packages'
# targets linked. TARGETS is for one product built as several binaries: the plugin's formats, each its own module.
function(retroplug_product product)
    cmake_parse_arguments(P "" "" "PACKAGES;TARGETS" ${ARGN})
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
    if(NOT P_TARGETS)
        set(P_TARGETS ${product})
    endif()
    get_property(_products GLOBAL PROPERTY RP_PRODUCTS)
    if(product IN_LIST _products)
        message(FATAL_ERROR "retroplug_product(${product}): the product is declared twice")
    endif()
    get_property(_known GLOBAL PROPERTY RP_PACKAGES)
    set(_entries "")
    set(_targets "")
    foreach(_p IN LISTS P_PACKAGES)
        if(NOT _p IN_LIST _known)
            message(FATAL_ERROR "retroplug_product(${product}): no package `${_p}` (retroplug_package() adds one)")
        endif()
        get_property(_e GLOBAL PROPERTY RP_PACKAGE_${_p}_ENTRIES)
        get_property(_t GLOBAL PROPERTY RP_PACKAGE_${_p}_TARGETS)
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
    else()
        set(_calls "")
        foreach(_e IN LISTS _entries)
            string(APPEND _calls "        ${_e}(),\n")
        endforeach()
        set(_body "    static const rp_package* const packages[] = {\n${_calls}    };\n")
        string(APPEND _body "    *count = ${_n};\n    return packages;\n")
    endif()
    list(JOIN P_PACKAGES " " _listed)
    set(_src "${RP_PRODUCTS_DIR}/${product}.cpp")
    file(GENERATE OUTPUT "${_src}" CONTENT
"// Generated by retroplug_product(${product}) in cmake/packages.cmake from the manifests of the packages it lists:
// ${_listed}. Do not edit; edit the product's PACKAGES or a manifest.

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
    set_property(GLOBAL APPEND PROPERTY RP_PRODUCTS ${product})
    set_property(GLOBAL APPEND PROPERTY RP_PRODUCT_TARGETS ${P_TARGETS})
    set_property(GLOBAL PROPERTY RP_PRODUCT_${product}_ENTRIES ${_entries})
endfunction()

# retroplug_check_products(LIBRARIES <library>... [EXEMPT <target>...]): refuses, at configure time, every executable,
# module or shared library in the build that links one of LIBRARIES directly and is neither a product's target nor
# EXEMPT. LIBRARIES are the ones that carry the engine; EXEMPT is for a binary that links one of them and is known never
# to pull the engine's object in. Call it after the last retroplug_product(). It stands in for the link error a Linux
# module never gives (this file's header says why); -Wl,--no-undefined would not, as it breaks the sanitizer
# builds and guards only the modules that are already products.
function(retroplug_check_products)
    cmake_parse_arguments(C "" "" "LIBRARIES;EXEMPT" ${ARGN})
    get_property(_products GLOBAL PROPERTY RP_PRODUCT_TARGETS)
    _retroplug_binaries(_binaries "${CMAKE_SOURCE_DIR}")
    set(_missing "")
    foreach(_t IN LISTS _binaries)
        if(_t IN_LIST _products OR _t IN_LIST C_EXEMPT)
            continue()
        endif()
        get_target_property(_links ${_t} LINK_LIBRARIES)
        foreach(_l IN LISTS C_LIBRARIES)
            if(_l IN_LIST _links)
                list(APPEND _missing "${_t} (links ${_l})")
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

# The executables, modules and shared libraries of `dir` and every directory under it.
function(_retroplug_binaries out dir)
    set(_found "")
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        if(_type STREQUAL "EXECUTABLE" OR _type STREQUAL "MODULE_LIBRARY" OR _type STREQUAL "SHARED_LIBRARY")
            list(APPEND _found ${_t})
        endif()
    endforeach()
    get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sub IN LISTS _subdirs)
        _retroplug_binaries(_more "${_sub}")
        list(APPEND _found ${_more})
    endforeach()
    set(${out} ${_found} PARENT_SCOPE)
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
