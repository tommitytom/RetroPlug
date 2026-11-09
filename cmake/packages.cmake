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
# A package's TypeScript node types are its `script` member, a script pack (docs/modular-graph-phase-e4-design.md
# section 6): the module that defines and registers them, whether the pack runs on the device, and, for one that does,
# the heap its MicroQuickJS context declares, in bytes:
#
#   "retroplug": { "script": { "entry": "ts/index.ts", "device": true, "heapBytes": 16384 } }
#
# `entry` is a path from the package's directory, as a manifest's other paths are. `device` is required, as an entry's
# is, so no pack's standing is a default nobody chose: a device pack runs on MicroQuickJS wherever it runs, its
# definitions held to what the box can run, and retroplug_package() gives it the targets that compile it to bytecode
# and register it (_retroplug_script_targets). `device: false`, a desktop script pack, is reserved and refused, as
# nothing builds one yet. `heapBytes` is a JSON number, a whole number of bytes: the whole of the pack's MicroQuickJS
# context as the box runs it, at 32 bits; a 64-bit device build gives 3/2 of it (script/MqjsHost.hpp's kDeviceHalves)
# and a desktop host more (desktopContextBytes). A package has any of `native`, `script`, `app` and `protocol`: the
# last two are its TypeScript contributions to the app (docs/modular-graph-phase-f1-design.md section 3), a module the
# generator reads, and a package with no native part registers nothing and adds no target here.
#
# A package's `embed` member is a file inside it that a product carries in its binary, in order:
#
#   "retroplug": { "embed": [{ "id": "mgb", "file": "rom/mGB.gb" }] }
#
# `id` names the resource on the wire (putResource's `embedded` field, and TS's marker of the same name), so it is a C
# identifier: it names the array cmake/bin2c.cmake generates. `file` is relative to the package directory and must
# resolve inside it. A product that lists the package gets the bytes in rp_embedded_resources() (graph/Packages.hpp),
# in the order it lists its packages, and a product listed without it has no such id: the lookup refuses it. A package
# may contribute nothing but an `embed`: `mgb` has no native code, its ROM is all it gives a product.
#
# A product is an executable or a module that constructs an Engine. retroplug_product() generates its registration
# source, which defines rp_register_packages() (graph/Packages.hpp) as the entries of the packages it lists, in the
# order it lists them, and links those packages' targets. The engine registers what that function returns, so which
# node types a product carries is its CMake's choice. A binary that pulls the engine in without declaring itself a
# product has no definition of rp_register_packages: an executable (and a module on macOS or Windows) fails to link
# naming it, but a Linux module links and fails only when it is loaded. So the configure checks it instead, for every
# binary: retroplug_check_products() refuses one that reaches the engine through its libraries and is not a product.
#
# This file needs CMake 3.19: string(JSON) for the manifests and cmake_language(DEFER) for the product check. The root's
# cmake_minimum_required is 3.27, which deps/efsw requires.

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
    _retroplug_read_manifest(${name} "${_manifest}")
    get_property(_targets GLOBAL PROPERTY RP_PACKAGE_${name}_TARGETS)
    get_property(_deviceTargets GLOBAL PROPERTY RP_PACKAGE_${name}_DEVICE_TARGETS)

    if(EXISTS "${_dir}/native/CMakeLists.txt")
        add_subdirectory("${_dir}/native" "${CMAKE_BINARY_DIR}/packages/${name}")
        # What its manifest names it defines now, so a typo is refused here, whether or not a product lists the package
        # (node-sdk, which none does). A device build need define only the manifest's `device` targets. A package
        # without a native/CMakeLists.txt (native) has its targets defined later, and retroplug_product() checks those.
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
    # A device build compiles every target as a device does already, so it has nothing to check twice; it registers a
    # script pack from the desktop build's 32-bit source, when it names the desktop build (RP_DESKTOP_BUILD_DIR: an
    # image whose plan runs one). One that names none carries the packages' native entries alone.
    get_property(_script GLOBAL PROPERTY RP_PACKAGE_${name}_SCRIPT_ENTRY)
    # An image's own heap for a pack (RP_SCRIPT_HEAP_BYTES_<package>, _retroplug_device_script_targets) sizes only the
    # script pack a device build registers: set anywhere else it would size nothing, which is refused, not ignored.
    if(DEFINED RP_SCRIPT_HEAP_BYTES_${name})
        if(NOT _script)
            message(FATAL_ERROR "retroplug_package(${name}): RP_SCRIPT_HEAP_BYTES_${name} is set, but the package has "
                                "no script pack (its manifest's retroplug.script) for it to size")
        elseif(NOT RP_DEVICE_BUILD OR NOT RP_DESKTOP_BUILD_DIR)
            message(FATAL_ERROR "retroplug_package(${name}): RP_SCRIPT_HEAP_BYTES_${name} is set, but this build "
                                "registers no script pack for it to size: only a device build given "
                                "RP_DESKTOP_BUILD_DIR does (the desktop's heap is the manifest's)")
        endif()
    endif()
    if(NOT RP_DEVICE_BUILD)
        foreach(_target IN LISTS _deviceTargets)
            _retroplug_device_check(${name} ${_target})
        endforeach()
        if(_script)
            _retroplug_script_targets(${name})
        endif()
    elseif(_script AND RP_DESKTOP_BUILD_DIR)
        _retroplug_device_script_targets(${name})
    endif()
    # Its embedded resources (`embed` above): one C array per entry, in an object library the products that list the
    # package link (retroplug_product). The array's name is the package's and the id's, as _retroplug_embed_symbol()
    # derives it, and a product whose packages run together to one name is refused by _retroplug_embed_entries.
    get_property(_embedIds GLOBAL PROPERTY RP_PACKAGE_${name}_EMBED_IDS)
    get_property(_embedFiles GLOBAL PROPERTY RP_PACKAGE_${name}_EMBED_FILES)
    get_property(_embedSymbols GLOBAL PROPERTY RP_PACKAGE_${name}_EMBED_SYMBOLS)
    if(_embedIds)
        set(_sources "")
        set(_i 0)
        foreach(_id IN LISTS _embedIds)
            list(GET _embedFiles ${_i} _file)
            list(GET _embedSymbols ${_i} _symbol)
            math(EXPR _i "${_i} + 1")
            set(_out "${CMAKE_BINARY_DIR}/packages/${name}/embeds/${_symbol}.c")
            add_custom_command(
                OUTPUT  "${_out}"
                COMMAND ${CMAKE_COMMAND}
                        -DINPUT=${_file}
                        -DSYMBOL=${_symbol}
                        -DOUTPUT=${_out}
                        -P ${CMAKE_SOURCE_DIR}/cmake/bin2c.cmake
                DEPENDS "${_file}" "${CMAKE_SOURCE_DIR}/cmake/bin2c.cmake"
                COMMENT "bin2c ${_id} (${name})"
                VERBATIM)
            list(APPEND _sources "${_out}")
        endforeach()
        add_library(retroplug-${name}-embeds OBJECT ${_sources})
        set_target_properties(retroplug-${name}-embeds PROPERTIES POSITION_INDEPENDENT_CODE ON)
        set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_EMBED_TARGET retroplug-${name}-embeds)
    endif()
    # Its links and include directories, held to the packages its manifest declares, are checked once at the end of the
    # root configure (retroplug_check_package_boundaries): a check here would see only the targets defined before this
    # package, so a link to a later package's target, or to the shell added last, went unnoticed.
endfunction()

# _retroplug_read_manifest(<name> <manifest>): reads one package's manifest into the global properties the products
# read (its targets, its entries and which are `device`, its providers and their facets), refusing a manifest that
# breaks a rule. Apart from retroplug_package() so that a configure-time check can run it over a scratch manifest in
# script mode (cmake -P, packages/native/cmake/tests.cmake), which has no add_subdirectory.
function(_retroplug_read_manifest name _manifest)
    file(READ "${_manifest}" _json)
    _retroplug_read_script(${name} "${_manifest}" "${_json}")
    _retroplug_read_embeds(${name} "${_manifest}" "${_json}")
    get_property(_embedIds GLOBAL PROPERTY RP_PACKAGE_${name}_EMBED_IDS)
    string(JSON _kind ERROR_VARIABLE _err TYPE "${_json}" retroplug native)
    if(_err)
        get_property(_script GLOBAL PROPERTY RP_PACKAGE_${name}_SCRIPT_ENTRY)
        # A package may contribute to the app and carry no native part at all: a TS-only package (every cart's in F1)
        # has an `app` member, a `protocol` member or both, and a product carries its contribution through the
        # generated TS registries (docs/modular-graph-phase-f1-design.md section 3). It registers nothing here and
        # adds no target; what it names is the TS generator's to read, not this file's.
        string(JSON _appKind ERROR_VARIABLE _noApp TYPE "${_json}" retroplug app)
        string(JSON _protocolKind ERROR_VARIABLE _noProtocol TYPE "${_json}" retroplug protocol)
        # An `embed` alone is a contribution too: `mgb` is a ROM a product carries, and needs no target of its own
        # beyond the one its bytes make.
        if(NOT _script AND NOT _embedIds AND _noApp AND _noProtocol)
            message(FATAL_ERROR "retroplug_package(${name}): ${_manifest} has none of a retroplug.native, "
                                "retroplug.script, retroplug.app, retroplug.protocol or retroplug.embed field "
                                "(${_err})")
        endif()
        # No native entry: no targets, no entries, whatever else the manifest contributes.
        set_property(GLOBAL APPEND PROPERTY RP_PACKAGES ${name})
        foreach(_p TARGETS ENTRIES DEVICE_ENTRIES DEVICE_TARGETS PROVIDERS PROVIDER_ENTRIES PROVIDER_TARGETS)
            set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_${_p} "")
        endforeach()
        _retroplug_read_declarations(${name} "${_json}")
        return()
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
    set(_providers "")
    # The entries and targets that carry one, which a product that does not name the package leaves out: a provider is
    # a desktop service (ports, threads, files), and a product that starts none has no reason to link its code. A target
    # that carries an entry as well is refused below, because leaving it out would leave that entry unlinked.
    set(_providerEntries "")
    set(_providerTargets "")
    set(_entryTargets "")
    math(EXPR _last "${_count} - 1")
    foreach(_i RANGE ${_last})
        string(JSON _target ERROR_VARIABLE _err GET "${_native}" ${_i} target)
        if(_err)
            message(FATAL_ERROR "retroplug_package(${name}): retroplug.native[${_i}] names no target")
        endif()
        list(APPEND _targets ${_target})
        string(JSON _entry ERROR_VARIABLE _noEntry GET "${_native}" ${_i} entry)
        string(JSON _provider ERROR_VARIABLE _noProvider GET "${_native}" ${_i} provider)
        string(JSON _rpc ERROR_VARIABLE _noRpc GET "${_native}" ${_i} rpc)
        if(_noEntry AND _noProvider AND _noRpc)
            continue()
        endif()
        if(_noEntry)
            set(_what "${_target}")
        else()
            set(_what "${_entry}")
        endif()
        # A boolean reads back as ON or OFF. Required wherever there is an entry or a provider, so no package's device
        # standing is a default nobody chose.
        string(JSON _isDevice ERROR_VARIABLE _err GET "${_native}" ${_i} device)
        if(_err OR NOT (_isDevice STREQUAL "ON" OR _isDevice STREQUAL "OFF"))
            message(FATAL_ERROR "retroplug_package(${name}): entry ${_what} needs \"device\": true or false")
        endif()
        if(NOT (_noProvider AND _noRpc))
            _retroplug_read_provider(${name} ${_what} ${_isDevice} "${_noProvider}" "${_provider}" "${_noRpc}"
                                     "${_rpc}")
            list(APPEND _providers ${_provider})
            list(APPEND _providerTargets ${_target})
            if(_noEntry)
                continue()   # a provider that registers no node type: its target is all there is
            endif()
            list(APPEND _providerEntries ${_entry})
        endif()
        # An entry is a node TYPE and a provider is a SERVICE that need not belong together: a product that lists the
        # package and not its PROVIDERS must be able to carry the types and none of the service. One item holding both
        # cannot be split (the whole item goes), so a product without the provider would silently lose the node type.
        if(NOT _noEntry AND NOT _noProvider)
            message(FATAL_ERROR "retroplug_package(${name}): entry ${_entry} names a provider as well; a product that "
                                "lists the package without naming it in PROVIDERS would drop the node type with the "
                                "service. Split them into two entries (one with the entry, one with the provider).")
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
        list(APPEND _entryTargets ${_target})
        if(_isDevice)
            list(APPEND _device ${_entry})
            list(APPEND _deviceTargets ${_target})
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _targets)
    list(REMOVE_DUPLICATES _deviceTargets)
    # A product that lists this package without naming it in PROVIDERS drops the provider's target and keeps every entry's
    # registration. One target carrying both would then leave it with an entry whose code nothing links - a link error at
    # the product, far from the manifest that caused it - so the manifest must give the provider a target of its own.
    foreach(_pt IN LISTS _providerTargets)
        if(_pt IN_LIST _entryTargets)
            message(FATAL_ERROR "retroplug_package(${name}): target ${_pt} carries a provider and an entry; a product "
                                "that lists the package without naming it in PROVIDERS would keep the entry and drop "
                                "the target. A target carries a provider or an entry, never both: give the provider "
                                "one of its own.")
        endif()
    endforeach()

    set_property(GLOBAL APPEND PROPERTY RP_PACKAGES ${name})
    set_property(GLOBAL APPEND PROPERTY RP_PACKAGE_ENTRIES ${_entries})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_TARGETS ${_targets})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_ENTRIES ${_entries})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_DEVICE_ENTRIES ${_device})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_DEVICE_TARGETS ${_deviceTargets})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_PROVIDERS ${_providers})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_PROVIDER_ENTRIES ${_providerEntries})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_PROVIDER_TARGETS ${_providerTargets})
    _retroplug_read_declarations(${name} "${_json}")

endfunction()

# _retroplug_read_script(<package> <manifest> <json>): the manifest's script pack, if it has one (the `script` member
# above), into the global properties _retroplug_script_targets() reads: RP_PACKAGE_<package>_SCRIPT_ENTRY (the entry's
# absolute path, empty for a package with no script pack), _SCRIPT_DEVICE (ON or OFF) and _SCRIPT_HEAP_BYTES. The entry
# must exist, so a typo is refused here rather than by the bundler.
function(_retroplug_read_script name _manifest json)
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_SCRIPT_ENTRY "")
    string(JSON _kind ERROR_VARIABLE _none TYPE "${json}" retroplug script)
    if(_none)
        return()
    endif()
    if(NOT _kind STREQUAL "OBJECT")
        message(FATAL_ERROR "retroplug_package(${name}): retroplug.script is a ${_kind}, not an object")
    endif()
    string(JSON _entry ERROR_VARIABLE _err GET "${json}" retroplug script entry)
    if(_err OR _entry STREQUAL "")
        message(FATAL_ERROR "retroplug_package(${name}): retroplug.script needs an \"entry\", the module that defines "
                            "and registers its node types")
    endif()
    get_filename_component(_dir "${_manifest}" DIRECTORY)
    get_filename_component(_entryPath "${_entry}" ABSOLUTE BASE_DIR "${_dir}")
    if(NOT EXISTS "${_entryPath}")
        message(FATAL_ERROR "retroplug_package(${name}): retroplug.script's entry ${_entry} does not exist "
                            "(${_entryPath})")
    endif()
    string(JSON _isDevice ERROR_VARIABLE _err GET "${json}" retroplug script device)
    if(_err OR NOT (_isDevice STREQUAL "ON" OR _isDevice STREQUAL "OFF"))
        message(FATAL_ERROR "retroplug_package(${name}): retroplug.script needs \"device\": true or false")
    endif()
    if(NOT _isDevice)
        message(FATAL_ERROR "retroplug_package(${name}): retroplug.script is \"device\": false, a desktop script pack, "
                            "which is reserved: nothing builds one yet (the desktop's script nodes are the engine's "
                            "own bundle, packages/native/cmake/bundles.cmake)")
    endif()
    # A JSON number, as string(JSON GET) would hand a string's contents back the same. From 4 KiB, the least 64-bit
    # context the engine sets itself up in with mqjs's REPL stdlib, as the device-pack toolchain measures (below it
    # JS_NewContext writes past its buffer rather than throwing; its own assertion, 1 KiB, is no working floor), to
    # the RP2350's whole SRAM, 512 KiB. The engine's floor is per width and per stdlib, and the box's loader, which
    # knows its own, enforces it before it makes a context.
    string(JSON _heapType ERROR_VARIABLE _noHeap TYPE "${json}" retroplug script heapBytes)
    if(NOT _noHeap)
        string(JSON _heap GET "${json}" retroplug script heapBytes)
    endif()
    if(_noHeap OR NOT _heapType STREQUAL "NUMBER" OR NOT _heap MATCHES "^[1-9][0-9]*$" OR _heap LESS 4096
       OR _heap GREATER 524288)
        message(FATAL_ERROR "retroplug_package(${name}): retroplug.script is a device pack, and needs "
                            "\"heapBytes\": a whole number of bytes from 4096 to 524288")
    endif()
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_SCRIPT_ENTRY "${_entryPath}")
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_SCRIPT_DEVICE ${_isDevice})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_SCRIPT_HEAP_BYTES "${_heap}")
endfunction()

# _retroplug_embed_symbol(<package> <id> <outVar>): the C symbol an embedded resource is generated under. The one
# place the name is derived, so the array retroplug_package() writes and the product table that points at it cannot
# disagree. Package and id run together with every character that is no identifier's made `_`, which is not injective:
# two packages whose names and ids run together the same way make one symbol, and _retroplug_embed_entries() refuses
# the product that would carry both rather than leave a duplicate definition to the linker.
function(_retroplug_embed_symbol package id outVar)
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _symbol "rp_embed_${package}_${id}")
    set(${outVar} "${_symbol}" PARENT_SCOPE)
endfunction()

# _retroplug_read_embeds(<package> <manifest> <json>): the manifest's embedded resources (the `embed` member above),
# into RP_PACKAGE_<package>_EMBED_IDS, _EMBED_FILES (absolute paths) and _EMBED_SYMBOLS, in the order the manifest names
# them. An `id` is a C identifier, since it names the generated array; a `file` is relative to the package's own
# directory, resolves inside it, exists and is not empty, so a typo, an escape or an empty array is refused here rather
# than by the compiler or the linker.
function(_retroplug_read_embeds name _manifest json)
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_EMBED_IDS "")
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_EMBED_FILES "")
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_EMBED_SYMBOLS "")
    string(JSON _kind ERROR_VARIABLE _none TYPE "${json}" retroplug embed)
    if(_none)
        return()
    endif()
    if(NOT _kind STREQUAL "ARRAY")
        message(FATAL_ERROR "retroplug_package(${name}): retroplug.embed is a ${_kind}, not a list")
    endif()
    string(JSON _count LENGTH "${json}" retroplug embed)
    if(_count EQUAL 0)
        message(FATAL_ERROR "retroplug_package(${name}): retroplug.embed is an empty list")
    endif()
    get_filename_component(_dir "${_manifest}" DIRECTORY)
    get_filename_component(_dir "${_dir}" ABSOLUTE)
    set(_ids "")
    set(_files "")
    set(_symbols "")
    math(EXPR _last "${_count} - 1")
    foreach(_i RANGE ${_last})
        string(JSON _id ERROR_VARIABLE _noId GET "${json}" retroplug embed ${_i} id)
        if(_noId)
            message(FATAL_ERROR "retroplug_package(${name}): retroplug.embed[${_i}] names no id")
        endif()
        if(NOT _id MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
            message(FATAL_ERROR "retroplug_package(${name}): retroplug.embed[${_i}] id ${_id} is not a C identifier; "
                                "it names the array the build generates")
        endif()
        if(_id IN_LIST _ids)
            message(FATAL_ERROR "retroplug_package(${name}): retroplug.embed names ${_id} twice")
        endif()
        string(JSON _file ERROR_VARIABLE _noFile GET "${json}" retroplug embed ${_i} file)
        if(_noFile OR _file STREQUAL "")
            message(FATAL_ERROR "retroplug_package(${name}): embedded ${_id} names no file")
        endif()
        if(IS_ABSOLUTE "${_file}")
            message(FATAL_ERROR "retroplug_package(${name}): embedded ${_id} names ${_file}, an absolute path; a "
                                "manifest's path is relative to the package's own directory (${_dir})")
        endif()
        # Normalised LEXICALLY: the generator resolves a relative path the same way, and a `..` that walks out of the
        # package is exactly what this refuses.
        get_filename_component(_path "${_dir}/${_file}" ABSOLUTE)
        string(FIND "${_path}" "${_dir}/" _at)
        if(NOT _at EQUAL 0)
            message(FATAL_ERROR "retroplug_package(${name}): embedded ${_id} names ${_file}, which resolves outside "
                                "the package (${_path})")
        endif()
        if(NOT EXISTS "${_path}")
            message(FATAL_ERROR "retroplug_package(${name}): embedded ${_id} names ${_file}, which does not exist "
                                "(${_path})")
        endif()
        file(SIZE "${_path}" _size)
        if(_size EQUAL 0)
            message(FATAL_ERROR "retroplug_package(${name}): embedded ${_id} names ${_file}, which is empty; "
                                "bin2c would generate an array of no bytes, which is no C")
        endif()
        _retroplug_embed_symbol(${name} ${_id} _symbol)
        list(APPEND _ids "${_id}")
        list(APPEND _files "${_path}")
        list(APPEND _symbols "${_symbol}")
    endforeach()
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_EMBED_IDS "${_ids}")
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_EMBED_FILES "${_files}")
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_EMBED_SYMBOLS "${_symbols}")
endfunction()

# _retroplug_read_provider(<package> <entry> <device> <noProvider> <provider> <noRpc> <rpc>): one manifest entry's
# provider (graph/rp_provider.h) and the facet it mounts, `"provider": "<C function>"` and `"rpc": {"header": "<path
# from the package's include root>", "register": "<C++ function>"}`. A device entry may have neither: a device image
# runs no provider and mounts no facet, so a provider there would be a service no device product can start. An rpc
# needs the provider on its own entry, since the service it registers is that provider's. A provider is an extern "C"
# symbol of the program, so two packages may not name one. The register function is
# `template <class Server> void f(Server& server, void* service)`, `service` being what the provider's rpc_service
# answered (null when its create answered null, which the function tolerates), and it mounts methods by name on a
# server whose names are global: a name another facet or pack already mounted is replaced without a word (rpcpp's
# addHandler assigns), so a pack prefixes its own (the N8's are n8*). The server's codec is the product's QuickJSCodec
# for a facet that carries rfl::Bytestring.
function(_retroplug_read_provider name entry isDevice noProvider provider noRpc rpc)
    if(isDevice)
        message(FATAL_ERROR "retroplug_package(${name}): entry ${entry} is `device` and names a provider or an rpc, "
                            "which only a desktop entry may (a device image runs no provider and mounts no facet)")
    endif()
    if(noProvider)
        message(FATAL_ERROR "retroplug_package(${name}): entry ${entry} names an rpc and no provider, whose service "
                            "the rpc's registration would be handed")
    endif()
    if(NOT provider MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR "retroplug_package(${name}): provider \"${provider}\" is not a C identifier")
    endif()
    get_property(_taken GLOBAL PROPERTY RP_PROVIDERS)
    if(provider IN_LIST _taken)
        message(FATAL_ERROR "retroplug_package(${name}): provider ${provider} is taken")
    endif()
    set(_header "")
    set(_register "")
    if(NOT noRpc)
        string(JSON _header ERROR_VARIABLE _e1 GET "${rpc}" header)
        string(JSON _register ERROR_VARIABLE _e2 GET "${rpc}" register)
        if(_e1 OR _e2 OR NOT _register MATCHES "^[A-Za-z_][A-Za-z0-9_:]*$" OR _header STREQUAL "")
            message(FATAL_ERROR "retroplug_package(${name}): entry ${entry}'s rpc needs a \"header\" and a "
                                "\"register\" naming a C++ function")
        endif()
    endif()
    set_property(GLOBAL APPEND PROPERTY RP_PROVIDERS ${provider})
    set_property(GLOBAL PROPERTY RP_PROVIDER_${provider}_HEADER "${_header}")
    set_property(GLOBAL PROPERTY RP_PROVIDER_${provider}_REGISTER "${_register}")
endfunction()

# _retroplug_read_declarations(<package> <json>): the packages a manifest declares, from its `dependencies` and its
# `devDependencies` (`@retroplug/<name>` entries only), into RP_PACKAGE_<package>_DECLARATIONS and
# RP_PACKAGE_<package>_DEV_DECLARATIONS. `dependencies` are what the package's library targets may link and include from
# (the boundary check below), and what its TS may import (the TS check,
# packages/retroplug/scripts/check-boundaries.mjs); `devDependencies` are its test targets' and its test files' alone:
# ONE reading for both languages, which is why it is read here and not spelled again in a CMakeLists.
function(_retroplug_read_declarations package json)
    set(_declared "")
    set(_dev "")
    foreach(_member dependencies devDependencies)
        string(JSON _deps ERROR_VARIABLE _none GET "${json}" ${_member})
        if(_none)
            continue()
        endif()
        string(JSON _count ERROR_VARIABLE _err LENGTH "${_deps}")
        if(_err OR _count EQUAL 0)
            continue()
        endif()
        math(EXPR _last "${_count} - 1")
        foreach(_i RANGE ${_last})
            string(JSON _specifier MEMBER "${_deps}" ${_i})
            if(_specifier MATCHES "^@retroplug/(.+)$")
                if(_member STREQUAL "dependencies")
                    list(APPEND _declared "${CMAKE_MATCH_1}")
                else()
                    list(APPEND _dev "${CMAKE_MATCH_1}")
                endif()
            endif()
        endforeach()
    endforeach()
    set_property(GLOBAL PROPERTY RP_PACKAGE_${package}_DECLARATIONS "${_declared}")
    set_property(GLOBAL PROPERTY RP_PACKAGE_${package}_DEV_DECLARATIONS "${_dev}")
endfunction()

# _retroplug_unwrap_item(<out> <item> <unwrapable>): the item a link's, include directory's or source's generator
# expressions leave, and whether every one of them is a form this check evaluates. $<BUILD_INTERFACE:x>, $<LINK_ONLY:x>
# and $<TARGET_NAME:x> each name x as it stands in this build, so x is judged; nesting unwraps too
# ($<BUILD_INTERFACE:$<TARGET_NAME:foo>>). Anything else ($<TARGET_PROPERTY:...>, $<TARGET_OBJECTS:...>, a configuration
# expression) is a value only the build generator knows, which the caller refuses rather than passes unseen.
function(_retroplug_unwrap_item out item unwrapable)
    set(_value "${item}")
    set(_ok TRUE)
    while(_value MATCHES "^\\$<")
        if(_value MATCHES "^\\$<(BUILD_INTERFACE|LINK_ONLY|TARGET_NAME):(.+)>$")
            set(_value "${CMAKE_MATCH_2}")
        else()
            set(_ok FALSE)
            break()
        endif()
    endwhile()
    set(${out} "${_value}" PARENT_SCOPE)
    set(${unwrapable} "${_ok}" PARENT_SCOPE)
endfunction()

# _retroplug_unwrap_option(<out> <item> <unwrapable>): an option as a literal when the generator expressions around it
# are forms this check evaluates: the ones _retroplug_unwrap_item knows, and a condition's own value
# ($<$<COMPILE_LANGUAGE:CXX>:-Ifoo>, $<$<CONFIG:Debug>:-Ifoo>), whose inner value is a literal. An option hidden behind
# anything else (a target property, a configuration expression) is not evaluated, and the caller passes it unseen: a
# compile option legitimately holds such an expression ($<COMPILE_LANGUAGE:CXX>:/bigobj), and refusing every one would
# refuse a build that declared nothing. The header says what that leaves unjudged.
function(_retroplug_unwrap_option out item unwrapable)
    set(_value "${item}")
    set(_ok TRUE)
    while(_value MATCHES "^\\$<")
        if(_value MATCHES "^\\$<(BUILD_INTERFACE|LINK_ONLY|TARGET_NAME):(.+)>$")
            set(_value "${CMAKE_MATCH_2}")
        elseif(_value MATCHES "^\\$<\\$<(COMPILE_LANGUAGE|CONFIG):[^>]+>:(.+)>$")
            set(_value "${CMAKE_MATCH_2}")
        else()
            set(_ok FALSE)
            break()
        endif()
    endwhile()
    set(${out} "${_value}" PARENT_SCOPE)
    set(${unwrapable} "${_ok}" PARENT_SCOPE)
endfunction()

# _retroplug_unquote_path(<out> <value>): a compile definition's value as a DEFS in the tree spells it. Every path
# definition is written NAME="${...}", and a target property keeps those quotes, so a value that is a path arrives
# quoted and is unwrapped before it is judged (an escaped \" as well as a plain "). A value with no quotes is unchanged.
function(_retroplug_unquote_path out value)
    set(_value "${value}")
    if(_value MATCHES "^\\\\?\"")
        string(REPLACE "\\\"" "\"" _value "${_value}")
        foreach(_i RANGE 2)
            if(_value MATCHES "^\"(.*)\"$")
                set(_value "${CMAKE_MATCH_1}")
            else()
                break()
            endif()
        endforeach()
    endif()
    set(${out} "${_value}" PARENT_SCOPE)
endfunction()

# _retroplug_include_flag(<dir> <wants> <isFlag> <item>): whether an option is an include flag, the directory it spells,
# and whether that directory is the next item (a bare -I). The spellings the compilers take are all judged: -I, -iquote,
# -idirafter and -isystem, attached (-Ifoo) or apart (-I foo), MSVC's /I and /external:I, and CMake's SHELL: form of the
# separated ones (SHELL:-I foo). A SHELL: command that is none of these is no include flag.
function(_retroplug_include_flag dir wants isFlag item)
    set(_dir "")
    set(_wants FALSE)
    set(_is FALSE)
    if(item MATCHES "^SHELL:-(I|iquote|idirafter|isystem) (.+)$")
        set(_dir "${CMAKE_MATCH_2}")
        set(_is TRUE)
    elseif(item MATCHES "^(/external:I|-iquote|-idirafter|-isystem|-I|/I)(.+)$")
        set(_dir "${CMAKE_MATCH_2}")
        set(_is TRUE)
    elseif(item STREQUAL "/external:I" OR item STREQUAL "-iquote" OR item STREQUAL "-idirafter"
           OR item STREQUAL "-isystem" OR item STREQUAL "-I" OR item STREQUAL "/I")
        set(_wants TRUE)
        set(_is TRUE)
    endif()
    set(${dir} "${_dir}" PARENT_SCOPE)
    set(${wants} "${_wants}" PARENT_SCOPE)
    set(${isFlag} "${_is}" PARENT_SCOPE)
endfunction()

# _retroplug_check_native_boundaries(<package> LINKS <target=linked>... LINK_PATHS <target=file>...
#                                    LINK_DIRS <target=dir>... LIBS <target=name>... INCLUDES <target=dir>...
#                                    DEFINITIONS <target=NAME=path>... SOURCES <target=file>...
#                                    UNJUDGED <target=item>... DECLARED <package>... DEV_DECLARED <package>...
#                                    [TEST] OWNERS <target=package>...): a package's native targets may link the targets of
# the packages its manifest declares, link a library of a declared package's by path, add include or link-search
# directories inside its own tree or a declared package's, define a macro as a path inside one of those, and compile
# sources inside its own tree. A TEST target (retroplug_add_test_binary's) may also link, include or define a path into
# its package's devDependencies, as a test file may import them or open their fixtures by path; a library target may not.
# Nothing may link, include, search or define a path into the shell (packages/native, packages/retroplug). Anything else
# is refused, naming the package, the target and the dependency, because a package that reaches past its manifest makes
# the split a convention again (docs/modular-graph-phase-f1-design.md section 4).
#
# UNJUDGED is what the gatherer could not evaluate (a generator expression outside the forms _retroplug_unwrap_item
# knows, or a bare flag whose value it cannot attribute), for a target that is not vouched for by RP_BOUNDARY_SKIP:
# refused here, so the message is held with the rest of the rules rather than live in the gatherer alone.
#
# What this guards is ACCIDENT AND DRIFT, NOT A DETERMINED AUTHOR. It reads the target properties CMake has already
# computed and the OWNERS table the build's target directories give it: a review's first pass, not a sandbox. The places
# it deliberately does not look, so a later author does not read the list as a promise:
#
#   - a per-source property (set_source_files_properties' COMPILE_OPTIONS or INCLUDE_DIRECTORIES), which is not the
#     target's, and the cache flags that apply to every target (CMAKE_CXX_FLAGS and its sisters), which are the root's;
#   - a generator expression whose value only the build generator knows ($<TARGET_PROPERTY:...>,
#     $<TARGET_OBJECTS:...>, a condition with more than one operand such as $<$<AND:...>:-Ifoo>): a compile or link
#     OPTION in one is passed unseen, as the comment in the gatherer says, because a build legitimately holds one that
#     names no path;
#   - a linker flag wrapped in -Xlinker or -Wl (the options this reads are the plain -L, -l and -I spellings);
#   - a library name that matches no target of the build (a system library, a name a -L elsewhere resolves),
#     `LINK_DIRECTORIES`, an IMPORTED target's location, and a target's OUTPUT_NAME or LINK_DEPENDS;
#   - `include()` of another package's CMake file, and a target whose properties another package's CMakeList sets: the
#     owner table attributes a target by its SOURCE_DIR, which such a file does not move.
#
# It takes plain lists so a configure-time fixture can drive it (cmake/check-native-boundaries.cmake, held by
# packages/native/cmake/tests.cmake): a target's real properties need a target, which script mode has none of.
function(_retroplug_check_native_boundaries package)
    cmake_parse_arguments(B "TEST" ""
                          "LINKS;LINK_PATHS;LINK_DIRS;LIBS;INCLUDES;DEFINITIONS;SOURCES;UNJUDGED;DECLARED;DEV_DECLARED;OWNERS"
                          ${ARGN})
    if(B_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "_retroplug_check_native_boundaries(${package}): unrecognised argument(s): "
                            "${B_UNPARSED_ARGUMENTS}")
    endif()
    set(_allowed ${B_DECLARED} node-sdk)  # the SDK, which every package's library is built on
    if(B_TEST)
        list(APPEND _allowed ${B_DEV_DECLARED})
    endif()
    set(_shells native retroplug)
    # The package that owns each target, by where its directory lies. A target with no package's directory (a vendored
    # library, an imported one) is nobody's to judge here.
    foreach(_pair IN LISTS B_OWNERS)
        string(FIND "${_pair}" "=" _at)
        string(SUBSTRING "${_pair}" 0 ${_at} _owned)
        math(EXPR _at "${_at} + 1")
        string(SUBSTRING "${_pair}" ${_at} -1 _owner)
        set(_owner_of_${_owned} "${_owner}")
    endforeach()
    foreach(_pair IN LISTS B_LINKS)
        string(FIND "${_pair}" "=" _at)
        string(SUBSTRING "${_pair}" 0 ${_at} _target)
        math(EXPR _at "${_at} + 1")
        string(SUBSTRING "${_pair}" ${_at} -1 _linked)
        if(NOT DEFINED _owner_of_${_linked} OR "${_owner_of_${_linked}}" STREQUAL "${package}")
            continue()  # not a package's target, or this package's own
        endif()
        if(_owner_of_${_linked} IN_LIST _shells)
            message(FATAL_ERROR "retroplug_package(${package}): ${_target} links ${_linked}, a target of the shell "
                                "(packages/${_owner_of_${_linked}}): no package may use the shell")
        endif()
        if(NOT _owner_of_${_linked} IN_LIST _allowed)
            message(FATAL_ERROR "retroplug_package(${package}): ${_target} links ${_linked}, a target of the "
                                "${_owner_of_${_linked}} package, which ${package} does not declare (its package.json "
                                "dependencies)")
        endif()
    endforeach()
    # A library linked by its file path is the same door as a linked target: what the path is in decides.
    foreach(_pair IN LISTS B_LINK_PATHS)
        string(FIND "${_pair}" "=" _at)
        string(SUBSTRING "${_pair}" 0 ${_at} _target)
        math(EXPR _at "${_at} + 1")
        string(SUBSTRING "${_pair}" ${_at} -1 _path)
        file(RELATIVE_PATH _rel "${RP_PACKAGES_DIR}" "${_path}")
        if(IS_ABSOLUTE "${_rel}" OR _rel MATCHES "^\\.\\.")
            continue()  # outside the packages' trees: a vendored or system library, or the build's own output
        endif()
        string(REGEX REPLACE "/.*$" "" _owner "${_rel}")
        if(_owner STREQUAL package)
            continue()
        endif()
        if(_owner IN_LIST _shells)
            message(FATAL_ERROR "retroplug_package(${package}): ${_target} links ${_path}, which is in the shell "
                                "(packages/${_owner}): no package may use the shell")
        endif()
        if(NOT _owner IN_LIST _allowed)
            message(FATAL_ERROR "retroplug_package(${package}): ${_target} links ${_path}, which is in the "
                                "${_owner} package's tree, which ${package} does not declare (its package.json "
                                "dependencies)")
        endif()
    endforeach()
    # A link search directory (-L) and a library name (-l) are the same door as a path: the directory a package's
    # library is found in, and the name it is found by.
    foreach(_pair IN LISTS B_LINK_DIRS)
        string(FIND "${_pair}" "=" _at)
        string(SUBSTRING "${_pair}" 0 ${_at} _target)
        math(EXPR _at "${_at} + 1")
        string(SUBSTRING "${_pair}" ${_at} -1 _dir)
        file(RELATIVE_PATH _rel "${RP_PACKAGES_DIR}" "${_dir}")
        if(IS_ABSOLUTE "${_rel}" OR _rel MATCHES "^\\.\\.")
            continue()
        endif()
        string(REGEX REPLACE "/.*$" "" _owner "${_rel}")
        if(_owner STREQUAL package)
            continue()
        endif()
        if(_owner IN_LIST _shells)
            message(FATAL_ERROR "retroplug_package(${package}): ${_target} adds the link directory ${_dir}, which "
                                "is in the shell (packages/${_owner}): no package may use the shell")
        endif()
        if(NOT _owner IN_LIST _allowed)
            message(FATAL_ERROR "retroplug_package(${package}): ${_target} adds the link directory ${_dir}, which is "
                                "in the ${_owner} package's tree, which ${package} does not declare (its package.json "
                                "dependencies)")
        endif()
    endforeach()
    foreach(_pair IN LISTS B_LIBS)
        string(FIND "${_pair}" "=" _at)
        string(SUBSTRING "${_pair}" 0 ${_at} _target)
        math(EXPR _at "${_at} + 1")
        string(SUBSTRING "${_pair}" ${_at} -1 _name)
        if(NOT DEFINED _owner_of_${_name})
            continue()  # not a target of the build, so no package's to judge (a system library)
        endif()
        set(_owner "${_owner_of_${_name}}")
        if(_owner STREQUAL package)
            continue()
        endif()
        if(_owner IN_LIST _shells)
            message(FATAL_ERROR "retroplug_package(${package}): ${_target} links -l${_name}, a library of the shell "
                                "(packages/${_owner}): no package may use the shell")
        endif()
        if(NOT _owner IN_LIST _allowed)
            message(FATAL_ERROR "retroplug_package(${package}): ${_target} links -l${_name}, a library of the "
                                "${_owner} package, which ${package} does not declare (its package.json dependencies)")
        endif()
    endforeach()
    foreach(_pair IN LISTS B_INCLUDES)
        string(FIND "${_pair}" "=" _at)
        string(SUBSTRING "${_pair}" 0 ${_at} _target)
        math(EXPR _at "${_at} + 1")
        string(SUBSTRING "${_pair}" ${_at} -1 _dir)
        file(RELATIVE_PATH _rel "${RP_PACKAGES_DIR}" "${_dir}")
        if(IS_ABSOLUTE "${_rel}" OR _rel MATCHES "^\\.\\.")
            continue()  # outside the packages' trees: a vendored dep's headers, or the build's own
        endif()
        string(REGEX REPLACE "/.*$" "" _owner "${_rel}")
        if(_owner STREQUAL package OR _owner IN_LIST _allowed)
            continue()
        endif()
        if(_owner IN_LIST _shells)
            message(FATAL_ERROR "retroplug_package(${package}): ${_target} adds the include directory ${_dir}, which "
                                "is in the shell (packages/${_owner}): no package may use the shell")
        endif()
        message(FATAL_ERROR "retroplug_package(${package}): ${_target} adds the include directory ${_dir}, which is "
                            "in the ${_owner} package's tree, which ${package} does not declare (its package.json "
                            "dependencies)")
    endforeach()
    # A compile definition whose VALUE is a path is the same door as an include directory: a test binary that opens a
    # package's file (a manifest's ROM) names the path in a -D, and reaches that package's tree though it links nothing
    # of it. The name is kept for the message, since the value alone would not say how it got in.
    foreach(_pair IN LISTS B_DEFINITIONS)
        string(FIND "${_pair}" "=" _at)
        string(SUBSTRING "${_pair}" 0 ${_at} _target)
        math(EXPR _at "${_at} + 1")
        string(SUBSTRING "${_pair}" ${_at} -1 _rest)
        string(FIND "${_rest}" "=" _at)
        string(SUBSTRING "${_rest}" 0 ${_at} _name)
        math(EXPR _at "${_at} + 1")
        string(SUBSTRING "${_rest}" ${_at} -1 _path)
        _retroplug_unquote_path(_path "${_path}")
        file(RELATIVE_PATH _rel "${RP_PACKAGES_DIR}" "${_path}")
        if(IS_ABSOLUTE "${_rel}" OR _rel MATCHES "^\\.\\.")
            continue()  # outside the packages' trees: a vendored dep's data, or resources/
        endif()
        string(REGEX REPLACE "/.*$" "" _owner "${_rel}")
        if(_owner STREQUAL package OR _owner IN_LIST _allowed)
            continue()
        endif()
        if(_owner IN_LIST _shells)
            message(FATAL_ERROR "retroplug_package(${package}): ${_target} defines ${_name} as ${_path}, which is in the "
                                "shell (packages/${_owner}): no package may use the shell")
        endif()
        message(FATAL_ERROR "retroplug_package(${package}): ${_target} defines ${_name} as ${_path}, which is in the "
                            "${_owner} package's tree, which ${package} does not declare (its package.json dependencies; "
                            "a test may reach a devDependency's)")
    endforeach()
    # A source outside the package's own tree is compiled by that package's target unseen: the same boundary as an
    # include directory, and the same refusal. A source outside packages/ (a vendored dep's, a generated one) is nobody's.
    foreach(_pair IN LISTS B_SOURCES)
        string(FIND "${_pair}" "=" _at)
        string(SUBSTRING "${_pair}" 0 ${_at} _target)
        math(EXPR _at "${_at} + 1")
        string(SUBSTRING "${_pair}" ${_at} -1 _source)
        file(RELATIVE_PATH _rel "${RP_PACKAGES_DIR}" "${_source}")
        if(IS_ABSOLUTE "${_rel}" OR _rel MATCHES "^\\.\\.")
            continue()
        endif()
        string(REGEX REPLACE "/.*$" "" _owner "${_rel}")
        if(_owner STREQUAL package)
            continue()
        endif()
        if(_owner IN_LIST _shells)
            message(FATAL_ERROR "retroplug_package(${package}): ${_target} compiles ${_source}, which is in the shell "
                                "(packages/${_owner}): no package may use the shell")
        endif()
        message(FATAL_ERROR "retroplug_package(${package}): ${_target} compiles ${_source}, which is in the "
                            "${_owner} package's tree, which ${package} does not declare (its package.json "
                            "dependencies)")
    endforeach()
    # What the gatherer could not evaluate: a link, include directory or source the build generator resolves to
    # something this check cannot know. Refused rather than passed unseen, unless the target is vouched for
    # (RP_BOUNDARY_SKIP), which is the gatherer's to drop.
    foreach(_pair IN LISTS B_UNJUDGED)
        string(FIND "${_pair}" "=" _at)
        string(SUBSTRING "${_pair}" 0 ${_at} _target)
        math(EXPR _at "${_at} + 1")
        string(SUBSTRING "${_pair}" ${_at} -1 _item)
        message(FATAL_ERROR "retroplug_package(${package}): ${_target} names ${_item}, which the boundary check cannot "
                            "evaluate (a generator expression, or a flag whose value it cannot attribute): mark the "
                            "target RP_BOUNDARY_SKIP if it is nobody's to declare")
    endforeach()
endfunction()

# retroplug_check_package_boundaries(): the boundary check over every package's native targets, from what the build
# holds. Deferred to the end of the root's CMakeLists (cmake_language(DEFER), as retroplug_check_products is), because a
# package's check run while it is configured sees only the targets defined before it: a link to a later package's
# target, or to the shell added last, would pass. Every target is attributed to the package whose directory holds its
# SOURCE_DIR, so a helper a manifest never names (retroplug-dsp-kernels, retroplug-graph-exec, retroplug-mqjs-host)
# belongs to its package and is judged like the rest.
#
# A device build defines a manifest's `device` targets alone, so a target it does not define is skipped; a target
# marked RP_BOUNDARY_SKIP (a `-device` twin, a script pack's generated registration) is not judged of its own. A
# `-device` twin copies its target's include directories through a generator expression and links the SDK's headers
# alone, and a script pack's registration targets are generated by this file and link the engine's MicroQuickJS host by
# construction (a pack's own boundary is its TypeScript's, which the TS check holds).
#
# A PRODUCT target is not judged either: retroplug_product() links every listed package's targets into it, and its
# PACKAGES is that link's declaration. The device check's case generator is the one product in a package's directory, and
# it carries the device check's own package list rather than its directory package's manifest.
function(retroplug_check_package_boundaries)
    get_property(_packages GLOBAL PROPERTY RP_PACKAGES)
    if(NOT _packages)
        return()
    endif()
    get_property(_product_targets GLOBAL PROPERTY RP_PRODUCT_TARGETS)
    get_property(_device_products GLOBAL PROPERTY RP_DEVICE_PRODUCTS)
    _retroplug_build_targets(_targets "${CMAKE_SOURCE_DIR}")
    set(_owners "")
    foreach(_target IN LISTS _targets)
        get_target_property(_skip ${_target} RP_BOUNDARY_SKIP)
        if(_skip OR _target IN_LIST _product_targets OR _target IN_LIST _device_products)
            continue()
        endif()
        get_target_property(_dir ${_target} SOURCE_DIR)
        if(NOT _dir OR _dir MATCHES "-NOTFOUND$")
            continue()
        endif()
        file(RELATIVE_PATH _rel "${RP_PACKAGES_DIR}" "${_dir}")
        if(IS_ABSOLUTE "${_rel}" OR _rel MATCHES "^\\.\\.")
            continue()
        endif()
        string(REGEX REPLACE "/.*$" "" _owner "${_rel}")
        if(NOT _owner IN_LIST _packages)
            continue()
        endif()
        list(APPEND _owners "${_target}=${_owner}")
        list(APPEND _targets_of_${_owner} "${_target}")
    endforeach()
    foreach(_package IN LISTS _packages)
        if(_package STREQUAL "native" OR _package STREQUAL "retroplug")
            continue()  # the shells: they may use every package
        endif()
        get_property(_declared GLOBAL PROPERTY RP_PACKAGE_${_package}_DECLARATIONS)
        get_property(_dev GLOBAL PROPERTY RP_PACKAGE_${_package}_DEV_DECLARATIONS)
        foreach(_target IN LISTS _targets_of_${_package})
            get_target_property(_test ${_target} RP_TEST_BINARY)
            set(_links "")
            set(_link_paths "")
            set(_link_dirs "")
            set(_libs "")
            set(_includes "")
            set(_definitions "")
            set(_sources "")
            set(_unjudged "")
            foreach(_property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
                get_target_property(_value ${_target} ${_property})
                if(NOT _value OR _value MATCHES "-NOTFOUND$")
                    continue()
                endif()
                foreach(_item IN LISTS _value)
                    # A generator expression names its item in the forms _retroplug_unwrap_item evaluates; anything else
                    # resolves per configuration, which this check cannot know, so it is refused (B_UNJUDGED).
                    _retroplug_unwrap_item(_item "${_item}" _unwrapable)
                    if(NOT _unwrapable)
                        list(APPEND _unjudged "${_target}=${_item}")
                        continue()
                    endif()
                    # A target is attributed by its SOURCE_DIR; a link item spelled as a path (a package's static
                    # library, "${CMAKE_BINARY_DIR}/packages/beta/libretroplug-beta.a") is attributed by where it lies,
                    # so it takes the same judgement. A plain name (pthread, m) is the linker's, not a package's.
                    if(NOT TARGET ${_item})
                        if(_item MATCHES "[\\\\/]")
                            get_target_property(_bin_dir ${_target} BINARY_DIR)
                            if(IS_ABSOLUTE "${_item}")
                                set(_path "${_item}")
                            elseif(_bin_dir AND NOT _bin_dir MATCHES "-NOTFOUND$")
                                set(_path "${_bin_dir}/${_item}")
                            else()
                                list(APPEND _unjudged "${_target}=${_item}")
                                continue()
                            endif()
                            get_filename_component(_path "${_path}" ABSOLUTE)
                            list(APPEND _link_paths "${_target}=${_path}")
                        endif()
                        continue()
                    endif()
                    # An alias (`foo::foo`, the way a library is usually exported) is not a target of its own: the one
                    # it stands for has the SOURCE_DIR that decides its package, and the owner table is keyed by those.
                    get_target_property(_aliased ${_item} ALIASED_TARGET)
                    if(_aliased)
                        set(_item "${_aliased}")
                    endif()
                    list(APPEND _links "${_target}=${_item}")
                endforeach()
            endforeach()
            # An include flag in the compile options (-I, -iquote, -idirafter, -isystem, MSVC's /I and /external:I, and
            # CMake's SHELL: form) is an include directory by another door, so it is judged like one. An option a
            # generator expression merely conditions ($<$<COMPILE_LANGUAGE:CXX>:-Ifoo>) is still that literal, and is
            # judged; one it hides behind another form is passed unseen, as the header says.
            set(_flag_dir "")
            foreach(_property COMPILE_OPTIONS INTERFACE_COMPILE_OPTIONS)
                get_target_property(_value ${_target} ${_property})
                if(NOT _value OR _value MATCHES "-NOTFOUND$")
                    continue()
                endif()
                foreach(_item IN LISTS _value)
                    _retroplug_unwrap_option(_item "${_item}" _unwrapable)
                    if(NOT _unwrapable)
                        continue()
                    endif()
                    if(_flag_dir)
                        # The directory the bare flag on the previous item named.
                        set(_dir "${_item}")
                        set(_flag_dir "")
                    else()
                        _retroplug_include_flag(_dir _wants _is_flag "${_item}")
                        if(NOT _is_flag)
                            continue()
                        endif()
                        if(_wants)
                            set(_flag_dir "${_item}")
                            continue()
                        endif()
                    endif()
                    # A value the SHELL: form splits with a space in it is no one directory this can attribute.
                    if(_dir MATCHES " ")
                        list(APPEND _unjudged "${_target}=${_item}")
                        continue()
                    endif()
                    # A relative flag names no directory this check can attribute: the compiler resolves it against
                    # its working directory, which the build generator decides (Ninja compiles from the build root,
                    # Makefiles from the target's binary dir) and never from the target's SOURCE_DIR. Reading it from
                    # SOURCE_DIR would attribute a path the compiler does not use, so it is refused as unjudged.
                    if(NOT IS_ABSOLUTE "${_dir}")
                        list(APPEND _unjudged "${_target}=${_item}")
                        continue()
                    endif()
                    get_filename_component(_dir "${_dir}" ABSOLUTE)
                    list(APPEND _includes "${_target}=${_dir}")
                endforeach()
            endforeach()
            # A bare include flag at the end of the list names no directory this can attribute: refused, not passed.
            if(_flag_dir)
                list(APPEND _unjudged "${_target}=${_flag_dir}")
            endif()
            # A link option is another door: -L<dir> adds a search directory (a package's build tree there is a link
            # into it), and -l<name> names a library the linker finds in one, which this attributes by the build's
            # target of that name. An option a generator expression hides is passed unseen, as a compile option's is.
            set(_lib_dir "")
            foreach(_property LINK_OPTIONS INTERFACE_LINK_OPTIONS)
                get_target_property(_value ${_target} ${_property})
                if(NOT _value OR _value MATCHES "-NOTFOUND$")
                    continue()
                endif()
                foreach(_item IN LISTS _value)
                    _retroplug_unwrap_option(_item "${_item}" _unwrapable)
                    if(NOT _unwrapable)
                        continue()
                    endif()
                    if(_lib_dir)
                        set(_dir "${_item}")
                        set(_lib_dir "")
                    elseif(_item MATCHES "^-L(.+)$")
                        set(_dir "${CMAKE_MATCH_1}")
                    elseif(_item STREQUAL "-L")
                        set(_lib_dir "${_item}")
                        continue()
                    elseif(_item MATCHES "^-l(.+)$")
                        list(APPEND _libs "${_target}=${CMAKE_MATCH_1}")
                        continue()
                    else()
                        continue()
                    endif()
                    if(NOT IS_ABSOLUTE "${_dir}")
                        get_target_property(_bin_dir ${_target} BINARY_DIR)
                        if(NOT _bin_dir OR _bin_dir MATCHES "-NOTFOUND$")
                            list(APPEND _unjudged "${_target}=${_item}")
                            continue()
                        endif()
                        set(_dir "${_bin_dir}/${_dir}")
                    endif()
                    get_filename_component(_dir "${_dir}" ABSOLUTE)
                    list(APPEND _link_dirs "${_target}=${_dir}")
                endforeach()
            endforeach()
            if(_lib_dir)
                list(APPEND _unjudged "${_target}=${_lib_dir}")
            endif()
            # A compile definition's value is a path by another name: a test binary opens a package's file by the path
            # its manifest gives, which no link and no include directory says. Only a literal absolute value can be
            # judged; an expression the build generator resolves is passed unseen, as a compile option's is (the
            # header's list).
            foreach(_property COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS)
                get_target_property(_value ${_target} ${_property})
                if(NOT _value OR _value MATCHES "-NOTFOUND$")
                    continue()
                endif()
                foreach(_item IN LISTS _value)
                    _retroplug_unwrap_item(_item "${_item}" _unwrapable)
                    if(NOT _unwrapable)
                        continue()
                    endif()
                    string(FIND "${_item}" "=" _eq)
                    if(_eq EQUAL -1)
                        continue()
                    endif()
                    math(EXPR _eq "${_eq} + 1")
                    string(SUBSTRING "${_item}" 0 ${_eq} _name)
                    string(SUBSTRING "${_item}" ${_eq} -1 _arg)
                    _retroplug_unquote_path(_arg "${_arg}")
                    if(NOT IS_ABSOLUTE "${_arg}")
                        continue()
                    endif()
                    get_filename_component(_arg "${_arg}" ABSOLUTE)
                    list(APPEND _definitions "${_target}=${_name}${_arg}")
                endforeach()
            endforeach()
            # The interface directories are what a CONSUMER of the target adds, so they are judged like the target's own.
            foreach(_property INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES)
                get_target_property(_value ${_target} ${_property})
                if(NOT _value OR _value MATCHES "-NOTFOUND$")
                    continue()
                endif()
                foreach(_item IN LISTS _value)
                    _retroplug_unwrap_item(_item "${_item}" _unwrapable)
                    if(NOT _unwrapable)
                        list(APPEND _unjudged "${_target}=${_item}")
                        continue()
                    endif()
                    if(NOT IS_ABSOLUTE "${_item}")
                        continue()
                    endif()
                    # Normalised lexically, as a source is below: a directory spelled with a `..` in it would otherwise
                    # be attributed to the directory it starts out in, which is what an escaped path hides best.
                    get_filename_component(_item "${_item}" ABSOLUTE)
                    list(APPEND _includes "${_target}=${_item}")
                endforeach()
            endforeach()
            # A source outside the package's tree is compiled into its target, which is the same boundary as an include
            # directory; a source the build generator chooses is refused like any other expression this cannot read.
            foreach(_property SOURCES INTERFACE_SOURCES)
                get_target_property(_value ${_target} ${_property})
                if(NOT _value OR _value MATCHES "-NOTFOUND$")
                    continue()
                endif()
                foreach(_item IN LISTS _value)
                    _retroplug_unwrap_item(_item "${_item}" _unwrapable)
                    if(NOT _unwrapable)
                        list(APPEND _unjudged "${_target}=${_item}")
                        continue()
                    endif()
                    if(NOT IS_ABSOLUTE "${_item}")
                        get_target_property(_dir ${_target} SOURCE_DIR)
                        if(NOT _dir OR _dir MATCHES "-NOTFOUND$")
                            continue()
                        endif()
                        set(_item "${_dir}/${_item}")
                    endif()
                    # Normalised LEXICALLY, so a relative source's `..` does not reach the message or the owner's
                    # spelling. Not file(REAL_PATH): that follows symlinks, and RP_PACKAGES_DIR is a lexical path, so a
                    # checkout configured through a symlinked directory would have its sources judged against a path no
                    # package owns and pass unseen.
                    get_filename_component(_item "${_item}" ABSOLUTE)
                    list(APPEND _sources "${_target}=${_item}")
                endforeach()
            endforeach()
            set(_test_arg "")
            if(_test)
                set(_test_arg TEST)
            endif()
            _retroplug_check_native_boundaries(${_package} LINKS ${_links} LINK_PATHS ${_link_paths}
                                               LINK_DIRS ${_link_dirs} LIBS ${_libs} INCLUDES ${_includes}
                                               DEFINITIONS ${_definitions} SOURCES ${_sources} UNJUDGED ${_unjudged}
                                               DECLARED ${_declared} DEV_DECLARED ${_dev} ${_test_arg}
                                               OWNERS ${_owners})
        endforeach()
    endforeach()
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
    set_target_properties(${target}-device PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
                                                      RP_BOUNDARY_SKIP "the device twin of ${target}")
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

# _retroplug_script_targets(<package>): a package's device script pack (the manifest's `script` member), which
# retroplug_package() gives its targets in the desktop build (a device image takes its 32-bit bytecode from the desktop
# build, as it takes its exported plan: _retroplug_device_script_targets): those of
# retroplug_device_script_pack(<package>), from the manifest's entry and heap. Its registration is the package's script
# entry, RP_PACKAGE_<package>_SCRIPT_FN (rp_<package>_script_package), and its target RP_PACKAGE_<package>_SCRIPT_TARGET
# (retroplug-<package>-script): a product that lists the package registers it beside the package's native entries
# (retroplug_product), and retroplug_device_mode_objects compiles it again as the device does, renamed with the rest.
# It is not one of the package's `device` entries, which are its C++ node types, the ones a device check carries.
function(_retroplug_script_targets package)
    get_property(_entry GLOBAL PROPERTY RP_PACKAGE_${package}_SCRIPT_ENTRY)
    get_property(_heap GLOBAL PROPERTY RP_PACKAGE_${package}_SCRIPT_HEAP_BYTES)
    retroplug_device_script_pack(${package} ENTRY "${_entry}" HEAP_BYTES ${_heap}
                                 PACK_DIR "${RP_PACKAGES_DIR}/${package}")
    get_target_property(_fn retroplug-${package}-script RP_SCRIPT_FN)
    set_property(GLOBAL PROPERTY RP_PACKAGE_${package}_SCRIPT_FN ${_fn})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${package}_SCRIPT_TARGET retroplug-${package}-script)
endfunction()

# _retroplug_device_script_targets(<package>): a device build's registration of a package's script pack, the
# retroplug-<package>-script target and the same rp_<package>_script_package() the desktop's registration defines
# (_retroplug_script_targets), so a DEVICE product that lists the package registers it as the desktop's does. A device
# build compiles no TypeScript: its packs come from the desktop build RP_DESKTOP_BUILD_DIR names, the one whose
# toolchain compiled them and whose exporter exported the image's plan (its trial load ran the same bundle at 64 bits),
# as <package>.pack32.c (the device's runtime of the pack at 32 bits, build-device-pack.mjs). Its bytecode is kept in
# flash (rp_mqjs_pack.h's RP_MQJS_BYTECODE_IN_FLASH: the Pico SDK's .flashdata, which an image copied into RAM at boot
# leaves where it is), since the pack's setup copies it into the pack's own memory in the arena, and relocates the copy
# there against the image's standard library. The registration links the device's MicroQuickJS host, which
# graph-runtime defines in a device build given RP_DESKTOP_BUILD_DIR.
#
# An image may give a pack a context of its own in place of its manifest's heapBytes: RP_SCRIPT_HEAP_BYTES_<package>,
# set before retroplug_package(<package>), a whole number of bytes from 4096 to 524288 as the manifest's is. The
# registration then registers a copy of the pack's source with that figure. The device check does, for the devtest
# pack, whose manifest's figure is the desktop tests' (its description says so), and the box does not.
function(_retroplug_device_script_targets package)
    if(NOT TARGET retroplug-mqjs-host)
        message(FATAL_ERROR "retroplug_package(${package}): a script pack needs the device's MicroQuickJS host first "
                            "(retroplug_package(graph-runtime), with RP_DESKTOP_BUILD_DIR set)")
    endif()
    set(_source "${RP_DESKTOP_BUILD_DIR}/packages/${package}/script/${package}.pack32.c")
    if(NOT EXISTS "${_source}")
        message(FATAL_ERROR "retroplug_package(${package}): no ${_source}; build the pack in the desktop build (cmake "
                            "--build ${RP_DESKTOP_BUILD_DIR} --target retroplug-export-plan-current)")
    endif()
    string(MAKE_C_IDENTIFIER "${package}" _id)
    set(_fn rp_${_id}_script_package)
    set(_registration "${CMAKE_BINARY_DIR}/packages/${package}/script/${package}-script.cpp")
    if(DEFINED RP_SCRIPT_HEAP_BYTES_${package})
        set(_heap "${RP_SCRIPT_HEAP_BYTES_${package}}")
        if(NOT _heap MATCHES "^[1-9][0-9]*$" OR _heap LESS 4096 OR _heap GREATER 524288)
            message(FATAL_ERROR "retroplug_package(${package}): RP_SCRIPT_HEAP_BYTES_${package} (\"${_heap}\") is "
                                "not a whole number of bytes from 4096 to 524288")
        endif()
        # Filled at the call rather than initialised from the source, which at namespace scope would take a
        # constructor; every call writes the same fields, and the host keys the pack on the copy's address.
        set(_body
"// The pack as this image registers it: its source with the image's own heap, ${_heap} bytes
// (RP_SCRIPT_HEAP_BYTES_${package}), in place of its manifest's heapBytes.
static rp_mqjs_pack_source gSource;

extern \"C\" const rp_package* ${_fn}(void) {
    gSource = rp_script_${_id};
    gSource.heap_bytes = ${_heap}u;
    return rp::mqjs::package(gSource);
}")
    else()
        set(_body "extern \"C\" const rp_package* ${_fn}(void) { return rp::mqjs::package(rp_script_${_id}); }")
    endif()
    file(GENERATE OUTPUT "${_registration}" CONTENT
"// Generated by cmake/packages.cmake (_retroplug_device_script_targets) for the ${package} script pack: its
// registration on the device's MicroQuickJS host, from the 32-bit bytecode the desktop build compiled
// (${_source}). Do not edit.

#include \"script/MqjsHost.hpp\"

extern \"C\" const rp_mqjs_pack_source rp_script_${_id};

${_body}
")
    retroplug_package_library(retroplug-${package}-script "${_registration}" "${_source}")
    target_compile_definitions(retroplug-${package}-script PRIVATE RP_MQJS_BYTECODE_IN_FLASH)
    target_link_libraries(retroplug-${package}-script PUBLIC retroplug-mqjs-host)
    set_property(GLOBAL PROPERTY RP_PACKAGE_${package}_SCRIPT_FN ${_fn})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${package}_SCRIPT_TARGET retroplug-${package}-script)
endfunction()

# retroplug_device_script_pack(<name> ENTRY <file.ts> HEAP_BYTES <n> PACK_DIR <dir> [OUT_DIR <dir>]): a device script
# pack's targets, desktop build only: a package's (_retroplug_script_targets) or a test's own fixture.
#
#   retroplug-<name>-script-pack  compiles the pack through the device-pack toolchain
#       (packages/retroplug/scripts/build-device-pack.mjs: bundled against its allowlist, PACK_DIR and the node SDK,
#       lowered to ES5, scanned for library calls the engine lacks, its definitions run under Node and its bytecode on
#       the runner, retroplug-mqjs-run, inside HEAP_BYTES). It writes OUT_DIR (default build/packages/<name>/script):
#       <name>.mqjs64.bin (the whole pack), <name>.device64.bin and <name>.mqjs32.bin (the device's runtime of the pack,
#       which every node runs, at each width), and the bytecode as C, <name>.pack64.c (both 64-bit bundles) and
#       <name>.pack32.c (graph-runtime's script/rp_mqjs_pack.h, rp_script_<name>), and names the whole pack's and the
#       box's bytecode in its RP_BYTECODE_64 and RP_BYTECODE_32 properties; a pack the toolchain refuses fails the
#       target's build, saying why. It runs the toolchain on every
#       build of it (as packages/native/cmake/bundles.cmake's bundles run esbuild), which rewrites an output only when
#       it changes, so what depends on one is rebuilt only then.
#   retroplug-<name>-pack64  the 64-bit C source as a library: what the desktop's host loads.
#   retroplug-<name>-script  the pack's registration: rp_<name>_script_package() (its RP_SCRIPT_FN property), an
#       extern "C" function as a native entry is, answering the pack's rp_package on the MicroQuickJS host
#       (rp::mqjs::package), with the host and the bytecode linked.
#
# <name> is a C identifier once each character that cannot be one is an underscore (string(MAKE_C_IDENTIFIER)).
function(retroplug_device_script_pack name)
    cmake_parse_arguments(P "" "ENTRY;HEAP_BYTES;PACK_DIR;OUT_DIR" "" ${ARGN})
    if(P_UNPARSED_ARGUMENTS OR NOT P_ENTRY OR NOT P_HEAP_BYTES OR NOT P_PACK_DIR)
        message(FATAL_ERROR "retroplug_device_script_pack(${name}): ENTRY, HEAP_BYTES and PACK_DIR are required")
    endif()
    if(RP_DEVICE_BUILD)
        message(FATAL_ERROR "retroplug_device_script_pack(${name}): a device build takes a pack's bytecode from the "
                            "desktop build, whose mqjs and Node compile it")
    endif()
    if(NOT TARGET mqjs OR NOT TARGET retroplug-mqjs-run OR NOT TARGET retroplug-mqjs-host)
        message(FATAL_ERROR "retroplug_device_script_pack(${name}): a script pack needs mqjs (cmake/mquickjs.cmake) "
                            "and the MicroQuickJS host (retroplug_package(graph-runtime)) first")
    endif()
    if(NOT P_OUT_DIR)
        set(P_OUT_DIR "${CMAKE_BINARY_DIR}/packages/${name}/script")
    endif()
    set(_out "${P_OUT_DIR}")
    set(_bytecode "${_out}/${name}.mqjs64.bin" "${_out}/${name}.device64.bin" "${_out}/${name}.mqjs32.bin")
    set(_sources "${_out}/${name}.pack64.c" "${_out}/${name}.pack32.c")
    set(_pack retroplug-${name}-script-pack)
    add_custom_target(${_pack}
        BYPRODUCTS ${_bytecode} ${_sources} "${_out}/${name}.js" "${_out}/${name}.device.js"
                   "${_out}/${name}.transcript.txt"
        COMMAND ${NODE_EXECUTABLE} "${RP_PACKAGES_DIR}/retroplug/scripts/build-device-pack.mjs"
                --entry "${P_ENTRY}" --name ${name} --heap-bytes ${P_HEAP_BYTES} --out-dir "${_out}"
                --pack-dir "${P_PACK_DIR}" --mqjs "$<TARGET_FILE:mqjs>"
                --runner "$<TARGET_FILE:retroplug-mqjs-run>"
        COMMENT "Compiling the ${name} device script pack (MicroQuickJS bytecode, 64- and 32-bit)"
        VERBATIM)
    add_dependencies(${_pack} mqjs retroplug-mqjs-run)
    set_target_properties(${_pack} PROPERTIES RP_BYTECODE_64 "${_out}/${name}.mqjs64.bin"
                                              RP_BYTECODE_32 "${_out}/${name}.mqjs32.bin"
                                              RP_BOUNDARY_SKIP "a generated script pack's toolchain")

    # The bytecode as C: built as it stands by whatever links it, in the device-mode objects' binary too
    # (RP_DEVICE_MODE_SHARED), as it names nothing in namespace rp.
    add_library(retroplug-${name}-pack64 STATIC EXCLUDE_FROM_ALL "${_out}/${name}.pack64.c")
    add_dependencies(retroplug-${name}-pack64 ${_pack})
    target_include_directories(retroplug-${name}-pack64 PRIVATE "${RP_PACKAGES_DIR}/graph-runtime/native/src")
    set_target_properties(retroplug-${name}-pack64 PROPERTIES POSITION_INDEPENDENT_CODE ON RP_DEVICE_MODE_SHARED ON
                                                             RP_BOUNDARY_SKIP "a generated script pack's bytecode")

    string(MAKE_C_IDENTIFIER "${name}" _id)
    set(_fn rp_${_id}_script_package)
    set(_registration "${_out}/${name}-script.cpp")
    file(GENERATE OUTPUT "${_registration}" CONTENT
"// Generated by cmake/packages.cmake (retroplug_device_script_pack) from the ${name} script pack: its registration on
// the MicroQuickJS host, from the bytecode the device-pack toolchain compiled (${name}.pack64.c). Do not edit.

#include \"script/MqjsHost.hpp\"

extern \"C\" const rp_mqjs_pack_source rp_script_${_id};

extern \"C\" const rp_package* ${_fn}(void) { return rp::mqjs::package(rp_script_${_id}); }
")
    retroplug_package_library(retroplug-${name}-script "${_registration}")
    set_target_properties(retroplug-${name}-script PROPERTIES EXCLUDE_FROM_ALL ON RP_SCRIPT_FN ${_fn}
                                                             RP_BOUNDARY_SKIP "a generated script pack's registration")
    target_link_libraries(retroplug-${name}-script PUBLIC retroplug-mqjs-host retroplug-${name}-pack64)
endfunction()

# _retroplug_embed_entries(<product> <out> <package>...): the embedded resources a product carries (`embed` above), in
# the order it lists its packages, as "package|id|symbol" entries. An id two of a product's packages both embed is
# refused, naming both: the id names one entry of the product's table, so a second could only shadow the first. So is
# one symbol two of them would generate, which two different id-and-package pairs can run together to
# (_retroplug_embed_symbol): the product would define the array twice, and a duplicate-symbol link error is no
# configure refusal. The symbol is the package's and the id's, the array retroplug_package() generated.
function(_retroplug_embed_entries product outVar)
    set(_entries "")
    set(_ids "")
    # Every C name the entries define (bin2c writes an array <symbol> and its length <symbol>_len), each with the
    # package that defines it.
    set(_names "")
    set(_nameOwners "")
    foreach(_p IN LISTS ARGN)
        get_property(_idList GLOBAL PROPERTY RP_PACKAGE_${_p}_EMBED_IDS)
        get_property(_symbolList GLOBAL PROPERTY RP_PACKAGE_${_p}_EMBED_SYMBOLS)
        set(_i 0)
        foreach(_id IN LISTS _idList)
            list(GET _symbolList ${_i} _symbol)
            math(EXPR _i "${_i} + 1")
            list(FIND _ids "${_id}" _at)
            if(NOT _at EQUAL -1)
                list(GET _entries ${_at} _owner)
                string(REGEX REPLACE "^([^|]*)\\|.*$" "\\1" _owner "${_owner}")
                message(FATAL_ERROR "retroplug_product(${product}): package ${_p} embeds ${_id}, which package "
                                    "${_owner} already embeds; an embedded resource's id names one entry of the "
                                    "product's table")
            endif()
            # Both names an entry defines are checked, so an id that runs into another entry's length object is
            # refused too: package `a` with id `x` and package `a_x` with id `len` both want rp_embed_a_x_len.
            foreach(_name IN ITEMS "${_symbol}" "${_symbol}_len")
                list(FIND _names "${_name}" _at)
                if(NOT _at EQUAL -1)
                    list(GET _nameOwners ${_at} _owner)
                    message(FATAL_ERROR "retroplug_product(${product}): package ${_p} embeds ${_id}, and package "
                                        "${_owner} embeds one whose id runs together with ${_p}'s to the symbol "
                                        "${_symbol}; bin2c defines an array and its length, so the build would "
                                        "define ${_name} twice")
                endif()
            endforeach()
            list(APPEND _ids "${_id}")
            list(APPEND _names "${_symbol}" "${_symbol}_len")
            list(APPEND _nameOwners "${_p}" "${_p}")
            list(APPEND _entries "${_p}|${_id}|${_symbol}")
        endforeach()
    endforeach()
    set(${outVar} "${_entries}" PARENT_SCOPE)
endfunction()

# retroplug_product(<product> PACKAGES <package>... [PROVIDERS <package>...] [TARGETS <target>...] [DEVICE]): declares a
# product and the packages it carries, in registration order. Generates build/packages/products/<product>.cpp, which
# defines the product's rp_register_packages() and rp_embedded_resources() (the embedded resources of the packages it
# lists, `embed` above), and adds it to each of TARGETS (default: the target named <product>) with the packages' targets
# and their embed libraries linked. TARGETS is for one product built as several binaries: the plugin's formats, each its
# own module.
#
# PROVIDERS names the packages whose provider the product starts (graph/rp_provider.h), in the order given, and that
# ORDER is the one its generated rp_product_providers.hpp holds: started in it, stopped in the reverse, and the facets
# mounted in it. A package named twice is refused, since there is no order a second mention could mean. A package listed
# in PACKAGES and not here contributes its node types and no provider: its provider's entry is not registered and its
# target is not linked, which is how a product carries a pack's nodes without its hardware. The default is none, so a
# product says what it starts: a provider is a service that opens ports and runs threads, and a binary that never asked
# for it must not link one.
#
# DEVICE makes it a device product: it takes each package's `device` entries alone, and their targets, and a package
# with none is refused, since a device product that names one would carry a node type the device cannot run (as the
# planner refuses one on the Pico). A device build's products are all device products: the RP2350 check, whose targets
# are device-mode libraries. A desktop build may declare one too, which links the desktop's builds of the same entries:
# the device check's case generator, which lists what the device registers. A device product constructs no engine, so
# it is not in the engine's registration golden (retroplug_product_table); retroplug-device-mode-test holds what a
# device registers instead.
#
# A listed package's script pack (its manifest's `script` member, _retroplug_script_targets) registers beside its
# native entries, every script pack being a device's: its registration function among the entries, its target among
# the targets. A package with no native part (its contributions are its `app` and `protocol` members, TypeScript's) has
# no entries and no targets, so listing it here contributes nothing to the registration and nothing to link.
function(retroplug_product product)
    cmake_parse_arguments(P "DEVICE" "" "PACKAGES;TARGETS;PROVIDERS" ${ARGN})
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
    # The registration golden's table is generated once, from the products declared by then: a later one would escape it.
    get_property(_tableDone GLOBAL PROPERTY RP_PRODUCT_TABLE_DONE)
    if(_tableDone AND NOT P_DEVICE)
        message(FATAL_ERROR "retroplug_product(${product}): declared after retroplug_product_table(), so the engine's "
                            "registration golden would not cover it; declare it before that call")
    endif()
    get_property(_products GLOBAL PROPERTY RP_PRODUCTS)
    get_property(_deviceProducts GLOBAL PROPERTY RP_DEVICE_PRODUCTS)
    if(product IN_LIST _products OR product IN_LIST _deviceProducts)
        message(FATAL_ERROR "retroplug_product(${product}): the product is declared twice")
    endif()
    # Its entry would be called twice: the engine refuses the second offer with a line on stderr and carries on.
    set(_seen "")
    foreach(_p IN LISTS P_PACKAGES)
        if(_p IN_LIST _seen)
            message(FATAL_ERROR "retroplug_product(${product}): package `${_p}` is listed twice in PACKAGES")
        endif()
        list(APPEND _seen ${_p})
    endforeach()
    get_property(_known GLOBAL PROPERTY RP_PACKAGES)
    # Named providers are checked first, so a typo fails as one rather than as a package that happens to start nothing.
    if(P_DEVICE AND P_PROVIDERS)
        message(FATAL_ERROR "retroplug_product(${product}): a device product runs no provider, so PROVIDERS is "
                            "refused")
    endif()
    foreach(_pv IN LISTS P_PROVIDERS)
        if(NOT _pv IN_LIST P_PACKAGES)
            message(FATAL_ERROR "retroplug_product(${product}): PROVIDERS names `${_pv}`, which is not in PACKAGES")
        endif()
        get_property(_declared GLOBAL PROPERTY RP_PACKAGE_${_pv}_PROVIDERS)
        if(NOT _declared)
            message(FATAL_ERROR "retroplug_product(${product}): PROVIDERS names `${_pv}`, whose manifest names no "
                                "provider")
        endif()
    endforeach()
    set(_entries "")
    set(_targets "")
    set(_leftOut "")
    set(_seenProviders "")
    foreach(_pv IN LISTS P_PROVIDERS)
        if(_pv IN_LIST _seenProviders)
            message(FATAL_ERROR "retroplug_product(${product}): package `${_pv}` is listed twice in PROVIDERS")
        endif()
        list(APPEND _seenProviders ${_pv})
    endforeach()
    foreach(_p IN LISTS P_PACKAGES)
        if(NOT _p IN_LIST _known)
            message(FATAL_ERROR "retroplug_product(${product}): no package `${_p}` (retroplug_package() adds one)")
        endif()
        get_property(_e GLOBAL PROPERTY RP_PACKAGE_${_p}_ENTRIES)
        get_property(_t GLOBAL PROPERTY RP_PACKAGE_${_p}_TARGETS)
        # A provider's entry and target are the product's word, not the package's: a product that does not name the
        # package in PROVIDERS carries its node types and links nothing of its desktop half.
        if(NOT P_DEVICE AND NOT _p IN_LIST P_PROVIDERS)
            get_property(_providerEntries GLOBAL PROPERTY RP_PACKAGE_${_p}_PROVIDER_ENTRIES)
            get_property(_providerTargets GLOBAL PROPERTY RP_PACKAGE_${_p}_PROVIDER_TARGETS)
            foreach(_left IN LISTS _providerEntries)
                list(REMOVE_ITEM _e ${_left})
            endforeach()
            foreach(_left IN LISTS _providerTargets)
                list(REMOVE_ITEM _t ${_left})
            endforeach()
        endif()
        # Its script pack's registration (_retroplug_script_targets), which every script pack's is a device's.
        get_property(_scriptFn GLOBAL PROPERTY RP_PACKAGE_${_p}_SCRIPT_FN)
        get_property(_scriptTarget GLOBAL PROPERTY RP_PACKAGE_${_p}_SCRIPT_TARGET)
        if(P_DEVICE)
            set(_all ${_e})
            get_property(_e GLOBAL PROPERTY RP_PACKAGE_${_p}_DEVICE_ENTRIES)
            get_property(_t GLOBAL PROPERTY RP_PACKAGE_${_p}_DEVICE_TARGETS)
            if(NOT _e AND NOT _scriptFn)
                message(FATAL_ERROR "retroplug_product(${product}): package `${_p}` has no `device` entry "
                                    "(${RP_PACKAGES_DIR}/${_p}/package.json), so a device product cannot carry it")
            endif()
            if(_e)
                list(REMOVE_ITEM _all ${_e})
            endif()
            list(APPEND _leftOut ${_all})
        endif()
        list(APPEND _e ${_scriptFn})
        list(APPEND _t ${_scriptTarget})
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

    # Its embedded resources: every listed package's `embed` entries, in the order it lists them. The generated source
    # declares each package's array and points the product's table at it; the array itself is the package's object
    # library's, linked here. A DEVICE product carries none: its nodes reach no embedded resource (putResource is the
    # desktop's RPC), and its image is left byte for byte as it was.
    _retroplug_embed_entries(${product} _embedEntries ${P_PACKAGES})
    set(_embedDecls "")
    set(_embedRows "")
    set(_embedTargets "")
    set(_embedCount 0)
    foreach(_entry IN LISTS _embedEntries)
        string(REPLACE "|" ";" _entry "${_entry}")
        list(GET _entry 0 _entryPackage)
        list(GET _entry 1 _entryId)
        list(GET _entry 2 _entrySymbol)
        get_property(_embedTarget GLOBAL PROPERTY RP_PACKAGE_${_entryPackage}_EMBED_TARGET)
        list(APPEND _embedTargets ${_embedTarget})
        string(APPEND _embedDecls "extern \"C\" const unsigned char ${_entrySymbol}[];\n"
                                  "extern \"C\" const unsigned int ${_entrySymbol}_len;\n")
        string(APPEND _embedRows "    {\"${_entryId}\", ${_entrySymbol}, ${_entrySymbol}_len},\n")
        math(EXPR _embedCount "${_embedCount} + 1")
    endforeach()
    list(REMOVE_DUPLICATES _embedTargets)
    if(P_DEVICE)
        set(_embedDecls "")
        set(_embedTargets "")
        set(_embedCount 0)
        set(_embedSection "")
    elseif(_embedCount EQUAL 0)
        string(CONCAT _embedBody
            "const rp_embedded_resource* rp_embedded_resources(size_t* count) {\n"
            "    *count = 0;\n"
            "    return nullptr;\n"
            "}\n")
        string(CONCAT _embedSection "\n" "${_embedDecls}" "\n" "${_embedBody}")
    else()
        # A function-local table, set up at the first call as rp_register_packages' is: its rows read each array's
        # length from the object the package's embed library defines, which is no constant expression here, so a
        # namespace-scope table would be one static initialiser running before a caller could ask (and a host's own
        # static constructor asking would see nothing). A desktop product is the only one that gets this table, so the
        # guard a function-local static needs is linked.
        string(CONCAT _embedBody
            "const rp_embedded_resource* rp_embedded_resources(size_t* count) {\n"
            "    static const rp_embedded_resource kResources[] = {\n"
            "${_embedRows}"
            "    };\n"
            "    *count = sizeof kResources / sizeof kResources[0];\n"
            "    return kResources;\n"
            "}\n")
        string(CONCAT _embedSection "\n" "${_embedDecls}" "\n" "${_embedBody}")
    endif()

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
${_body}}${_embedSection}")
    # A desktop product's providers, which a device product never has (a device image runs none).
    if(NOT P_DEVICE)
        retroplug_providers_header("${RP_PRODUCTS_DIR}/${product}/rp_product_providers.hpp"
                                   "retroplug_product(${product})" ${P_PROVIDERS})
    endif()
    foreach(_t IN LISTS P_TARGETS)
        if(NOT TARGET ${_t})
            message(FATAL_ERROR "retroplug_product(${product}): no target `${_t}`")
        endif()
        target_sources(${_t} PRIVATE "${_src}")
        # Every target of every listed package but the desktop halves its providers need: those are linked only where
        # the product names the package in PROVIDERS, so a binary that starts no provider links no port library, no
        # thread and no system framework for one.
        target_link_libraries(${_t} PRIVATE ${_targets} ${_embedTargets})
        # The directory of this product's own rp_product_providers.hpp, the header its sources include to start, stop
        # and mount its providers: the product's alone, so each binary includes the header made for its packages.
        if(NOT P_DEVICE)
            target_include_directories(${_t} PRIVATE "${RP_PRODUCTS_DIR}/${product}")
        endif()
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

# retroplug_device_mode_objects(<target> PACKAGES <package>... [SOURCES <source>...] [INCLUDES <dir>...]
#                               [SCRIPTS <target>...] [OWNERS <variable>]): the device's build of the packages' `device`
# entries, and of their script packs' registrations, for a
# desktop binary that holds it beside the desktop's build of the same entries: the device-mode test, and the plan
# exporter (packages/graph-runtime/export-plan), which plans with the desktop's build of each type and sizes the plan
# with the device's. An object library, EXCLUDE_FROM_ALL, of the sources of every `device` entry's target of the
# packages (each one's <target>-device check holds the same list), plus SOURCES, compiled as a device compiles them (the
# device flags, no exceptions, no RTTI) and one definition more, rp=rp_devmode: a token rename, so every `namespace rp`
# and `rp::` in their translation units is rp_devmode's, and no C++ name has a definition in each build (rp::text has
# no str() in the device's, its instance owns its settings in place, and the linker would otherwise keep whichever
# definition it saw first). rp_node, rp_package, RP_DEVICE, a header's name and a string literal ("rp.core.gain") are
# other tokens and keep their spelling. The kernels the nodes call (rp::dsp) are compiled into the objects too, as
# rp_devmode::dsp, which also proves them built without exceptions: the sources of every package library a device
# entry's target links. What the rename leaves the same in both builds is a name outside rp, so a source built both
# ways keeps its names with linkage under rp (rp::graph, rp::dsp, ...) or in an anonymous namespace. It is a rename of
# these objects, rather than a namespace the SDK opens in device mode, because only such a binary holds both builds: a
# device image and the <target>-device checks hold one, and compile the sources exactly as written. (An inline
# namespace rp::device would move a source's names with no change to it, but MSVC does not extend one through a later
# `namespace rp::graph` (CWG 2061).) The packages' C entries, which no namespace reaches, are renamed by a compile
# definition each, <entry>_device_mode, and the objects define rp_register_device_mode_packages() (graph/Packages.hpp),
# which lists the renamed entries in the order PACKAGES does, as a device product's rp_register_packages() lists its
# own. The binary keeps exceptions in its own translation units (Catch2 is built with them) and reaches the device-mode
# code through the C ABI alone, as a device's host would. MSVC keeps its /EHsc: a second /EH option on one command line
# is the D9025 warning AGENTS.md describes, and the GCC and Clang builds are where -fno-exceptions is proved. OWNERS
# names a variable set to the desktop targets the objects twin, which the binary links for the desktop's build.
#
# A listed package's script pack (_retroplug_script_targets), and each of SCRIPTS (a test's own, made by
# retroplug_device_script_pack), is compiled the same way: its registration's source and the MicroQuickJS host it links,
# its function renamed as an entry is, <rp_..._script_package>_device_mode. A library marked RP_DEVICE_MODE_SHARED (the
# engine and a pack's bytecode, C that names nothing in namespace rp) is not compiled again: the binary links the one
# the desktop's build links.
function(retroplug_device_mode_objects target)
    cmake_parse_arguments(P "" "OWNERS" "PACKAGES;SOURCES;INCLUDES;SCRIPTS" ${ARGN})
    if(RP_DEVICE_BUILD)
        message(FATAL_ERROR "retroplug_device_mode_objects(${target}): a device build compiles every package in device "
                            "mode already")
    endif()
    if(P_UNPARSED_ARGUMENTS OR NOT P_PACKAGES)
        message(FATAL_ERROR "retroplug_device_mode_objects(${target}): PACKAGES is required, and nothing else is "
                            "unnamed (${P_UNPARSED_ARGUMENTS})")
    endif()
    # Every device entry's owner, of every package: a library one of them links that is another's owner is compiled
    # from that owner's own sources, once, and only where its package is listed.
    get_property(_known GLOBAL PROPERTY RP_PACKAGES)
    set(_allOwners "")
    foreach(_p IN LISTS _known)
        get_property(_t GLOBAL PROPERTY RP_PACKAGE_${_p}_DEVICE_TARGETS)
        list(APPEND _allOwners ${_t})
    endforeach()
    set(_sources "")
    set(_includes "")
    set(_definitions "")
    set(_owners "")
    set(_libraries "")
    set(_entries "")
    set(_ownerQueue "")
    set(_scriptQueue "")
    foreach(_p IN LISTS P_PACKAGES)
        if(NOT _p IN_LIST _known)
            message(FATAL_ERROR "retroplug_device_mode_objects(${target}): no package `${_p}`")
        endif()
        get_property(_e GLOBAL PROPERTY RP_PACKAGE_${_p}_DEVICE_ENTRIES)
        get_property(_t GLOBAL PROPERTY RP_PACKAGE_${_p}_DEVICE_TARGETS)
        # A script pack's registration is a device's too (_retroplug_script_targets): its source, and the MicroQuickJS
        # host it links, compiled here as the device compiles them, the bytecode and the engine linked as they are.
        get_property(_scriptFn GLOBAL PROPERTY RP_PACKAGE_${_p}_SCRIPT_FN)
        get_property(_scriptTarget GLOBAL PROPERTY RP_PACKAGE_${_p}_SCRIPT_TARGET)
        if(NOT _e AND NOT _scriptFn)
            message(FATAL_ERROR "retroplug_device_mode_objects(${target}): package `${_p}` has no `device` entry")
        endif()
        list(APPEND _entries ${_e} ${_scriptFn})
        list(APPEND _ownerQueue ${_t})
        list(APPEND _scriptQueue ${_scriptTarget})
    endforeach()
    # SCRIPTS: a script pack's registration that is no package's (retroplug_device_script_pack: a test's fixture).
    foreach(_scriptTarget IN LISTS P_SCRIPTS)
        get_target_property(_scriptFn ${_scriptTarget} RP_SCRIPT_FN)
        if(NOT _scriptFn)
            message(FATAL_ERROR "retroplug_device_mode_objects(${target}): ${_scriptTarget} is no script pack's "
                                "registration (retroplug_device_script_pack)")
        endif()
        list(APPEND _entries ${_scriptFn})
        list(APPEND _scriptQueue ${_scriptTarget})
    endforeach()
    foreach(_owner IN LISTS _ownerQueue _scriptQueue)
        # The owner's <target>-device check holds its sources made absolute (_retroplug_device_check); a script pack's
        # registration is one generated source, absolute already.
        if(_owner IN_LIST _scriptQueue)
            get_target_property(_s ${_owner} SOURCES)
        else()
            get_target_property(_s ${_owner}-device SOURCES)
        endif()
        list(APPEND _sources ${_s})
        list(APPEND _owners ${_owner})
        list(APPEND _includes $<TARGET_PROPERTY:${_owner},INCLUDE_DIRECTORIES>)
        list(APPEND _definitions $<TARGET_PROPERTY:${_owner},COMPILE_DEFINITIONS>)
        # Every static library it links (the rp::dsp kernels), and theirs: renamed with it, so compiled with it, so
        # a library here is the owner's own and not a vendored one. The SDK is headers alone in device mode, so
        # retroplug-node-sdk's own sources (the description library) are not the device's.
        get_target_property(_pending ${_owner} LINK_LIBRARIES)
        while(_pending)
            list(POP_FRONT _pending _lib)
            if(NOT TARGET ${_lib} OR _lib STREQUAL "retroplug-node-sdk" OR _lib IN_LIST _libraries
               OR _lib IN_LIST _allOwners)
                continue()
            endif()
            get_target_property(_type ${_lib} TYPE)
            get_target_property(_shared ${_lib} RP_DEVICE_MODE_SHARED)
            if(NOT _type STREQUAL "STATIC_LIBRARY" OR _shared)
                continue()
            endif()
            list(APPEND _libraries ${_lib})
            get_target_property(_s ${_lib} SOURCES)
            get_target_property(_dir ${_lib} SOURCE_DIR)
            foreach(_one IN LISTS _s)
                cmake_path(ABSOLUTE_PATH _one BASE_DIRECTORY "${_dir}" OUTPUT_VARIABLE _one)
                list(APPEND _sources ${_one})
            endforeach()
            list(APPEND _includes $<TARGET_PROPERTY:${_lib},INCLUDE_DIRECTORIES>)
            list(APPEND _definitions $<TARGET_PROPERTY:${_lib},COMPILE_DEFINITIONS>)
            get_target_property(_more ${_lib} LINK_LIBRARIES)
            if(_more)
                list(APPEND _pending ${_more})
            endif()
        endwhile()
    endforeach()
    list(REMOVE_DUPLICATES _sources)
    set(_renames "")
    set(_decls "")
    set(_body "")
    list(LENGTH _entries _n)
    set(_i 0)
    foreach(_e IN LISTS _entries)
        list(APPEND _renames ${_e}=${_e}_device_mode)
        string(APPEND _decls "extern \"C\" const rp_package* ${_e}_device_mode(void);\n")
        string(APPEND _body "    packages[${_i}] = ${_e}_device_mode();\n")
        math(EXPR _i "${_i} + 1")
    endforeach()
    list(JOIN P_PACKAGES " " _listed)
    set(_registration "${CMAKE_BINARY_DIR}/device-mode/${target}-packages.cpp")
    file(GENERATE OUTPUT "${_registration}" CONTENT
"// Generated by retroplug_device_mode_objects(${target}) in cmake/packages.cmake from the manifests of the packages it
// lists: ${_listed}. Do not edit.

#include <stddef.h>

#include \"graph/rp_node.h\"

${_decls}
// Filled on every call, as a device product's registration is (a guarded static would need __cxa_guard_acquire).
extern \"C\" const rp_package* const* rp_register_device_mode_packages(size_t* count) {
    static const rp_package* packages[${_n}];
${_body}    *count = ${_n};
    return packages;
}
")
    add_library(${target} OBJECT EXCLUDE_FROM_ALL ${_sources} ${P_SOURCES} "${_registration}")
    # The device's build of the packages PACKAGES lists, each of whose own entries it compiles: that list is this
    # target's declaration, as a product's PACKAGES is its, so the native boundary check does not judge it.
    set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
                                              RP_BOUNDARY_SKIP "the device's build of the packages PACKAGES lists")
    target_compile_definitions(${target} PRIVATE ${_definitions} RP_DEVICE rp=rp_devmode ${_renames})
    if(MSVC)
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/bigobj>)  # retroplug_package_library's
    else()
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>")
    endif()
    # The SDK's headers and reflect-cpp's (the field reflection, header-only and SYSTEM: nothing of reflect-cpp is
    # linked), and the packages' own source roots for their headers and the kernels'.
    target_link_libraries(${target} PRIVATE retroplug-node-sdk-headers retroplug-reflect-headers)
    target_include_directories(${target} PRIVATE ${_includes} ${P_INCLUDES})
    if(P_OWNERS)
        set(${P_OWNERS} ${_owners} PARENT_SCOPE)
    endif()
endfunction()

# retroplug_providers_header(<file> <label> <package>...): generates <file>, the providers of the packages named
# (graph/rp_provider.h) as rp_start_providers(host), rp_restore_providers(providers), rp_stop_providers(providers),
# rp_wake_providers(providers) and rp_register_package_rpc<Server>(server, providers), in the order the caller's list
# gives the packages (a product's PROVIDERS order), and per package in the order its manifest lists its providers
# (stopped in the reverse). retroplug_product() writes one per desktop product over the
# packages it names in PROVIDERS, rp_product_providers.hpp in an include directory of its own; a test writes one for a
# list of its choosing. Header-only and inline on purpose: a binary that never includes it (the render host, a test
# that starts no provider) names no provider, and a static library's members are linked only where something names
# them, so it carries none of the providers' code, threads or ports. The providers' state is an rp::Providers the
# product holds per instance (a DAW holds several), not a static here.
function(retroplug_providers_header file label)
    set(_providers "")
    foreach(_p IN LISTS ARGN)
        get_property(_pp GLOBAL PROPERTY RP_PACKAGE_${_p}_PROVIDERS)
        list(APPEND _providers ${_pp})
    endforeach()
    list(JOIN ARGN " " _listed)
    set(_includes "")
    set(_decls "")
    set(_table "")
    set(_registers "")
    set(_i 0)
    foreach(_v IN LISTS _providers)
        get_property(_h GLOBAL PROPERTY RP_PROVIDER_${_v}_HEADER)
        get_property(_r GLOBAL PROPERTY RP_PROVIDER_${_v}_REGISTER)
        string(APPEND _decls "extern \"C\" const rp_provider* ${_v}(void);\n")
        string(APPEND _table "        &${_v},\n")
        if(_r)
            string(APPEND _includes "#include \"${_h}\"\n")
            string(APPEND _registers "    ${_r}(server, providers.service(${_i}));\n")
        endif()
        math(EXPR _i "${_i} + 1")
    endforeach()
    list(LENGTH _providers _n)
    if(_n EQUAL 0)
        set(_start "    (void)host;\n    return {};\n")
        set(_registers "    (void)server;\n    (void)providers;\n")
    else()
        set(_start "    static const rp_provider_fn kProviders[] = {\n${_table}    };\n")
        string(APPEND _start "    return rp::Providers(kProviders, ${_n}, host);\n")
    endif()
    if(NOT _registers)
        set(_registers "    (void)server;\n    (void)providers;\n")
    endif()
    file(GENERATE OUTPUT "${file}" CONTENT
"// Generated by ${label} in cmake/packages.cmake from the manifests of the packages it starts:
// ${_listed}. Do not edit; edit the product's PROVIDERS or a manifest.
//
// The providers of those packages (graph/rp_provider.h), in the order the list above gives them, and the facets they
// mount. A product starts them after its engine and before its control plane can publish a graph, mounts their facets
// where it mounts its own, restores them where it restores its own state if it restores any, wakes them once a frame if
// it has a frame loop, and stops them after it has reclaimed its plans.
//
// A product's sources include it by its bare name, #include \"rp_product_providers.hpp\" (the SDL standalone's
// main.cpp, the plugin's PluginDSP.cpp and the CLI's main.cpp, the three that start providers): its directory is an
// include directory of that product's targets alone, so each binary reads the header made for its own packages.
#pragma once

#include \"graph/desktop/Providers.hpp\"
${_includes}
${_decls}
// [control] Creates each provider in order, into the registry `host` forwards to.
inline rp::Providers rp_start_providers(const rp_device_host* host) {
${_start}}

// [control] Each provider's persisted state back (a link left enabled reconnects), in order.
inline void rp_restore_providers(rp::Providers& providers) { providers.restore(); }

// [control] Destroys each provider in the reverse order, after the product's plans are reclaimed.
inline void rp_stop_providers(rp::Providers& providers) { providers.stop(); }

// [control] A host frame: the provider writes what the audio thread has queued since the last one (a writer thread waits
// on it rather than on a timeout, so an LED frame is not up to a poll late). Never blocks.
inline void rp_wake_providers(rp::Providers& providers) { providers.wake(); }

// Mounts each started provider's facet on `server`, an rpcpp TypedRpcServer.
template <class Server>
void rp_register_package_rpc(Server& server, const rp::Providers& providers) {
${_registers}}
")
endfunction()

# retroplug_check_products(LIBRARIES <library>...): refuses, at configure time, every executable, module or shared
# library in the build that reaches the engine and is not a product's. LIBRARIES are the seeds, the libraries that hold
# the engine (retroplug-backend: Engine.o); the set of libraries that carry it is computed outward from them, every
# static, object or interface library of the build whose LINK_LIBRARIES or INTERFACE_LINK_LIBRARIES name a carrier (a
# generator expression's operands included, so `$<LINK_ONLY:x>` and `$<$<BOOL:..>:x>` count), repeated until nothing
# joins, so a library of ours that links the backend makes whatever links it a suspect without anyone listing it. A
# binary that links a carrier is then refused: a test that needs something the backend used to carry links that
# library instead, so there is no exemption to keep. It stands in for the link error a Linux module never gives (this
# file's header says why); -Wl,--no-undefined would not, as it breaks the sanitizer builds and guards only the modules
# that are already products.
#
# It must see every binary of the build, so it runs when the root's CMakeLists is done:
#
#   cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL retroplug_check_products LIBRARIES ...)
#
# (3.19; its arguments are expanded when it runs, in the root's scope, so pass names, not variables of the calling
# directory). A binary defined after the call, anywhere in the tree, is checked as one before it.
function(retroplug_check_products)
    cmake_parse_arguments(C "" "" "LIBRARIES" ${ARGN})
    if(NOT C_LIBRARIES)
        message(FATAL_ERROR "retroplug_check_products: LIBRARIES is required")
    endif()
    foreach(_t IN LISTS C_LIBRARIES)
        if(NOT TARGET ${_t})
            message(FATAL_ERROR "retroplug_check_products: `${_t}`, named in LIBRARIES, is not a target")
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
        if(_t IN_LIST _products)
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
                            "PACKAGES in retroplug_add_test_binary); a test that needs one of the shell's runtime "
                            "libraries links that library instead of the backend.")
    endif()
endfunction()

# retroplug_check_runtime_libraries(): refuses, at configure time, a target that must stay a bare-QuickJS host or an
# engine binary carrying a runtime library it does not want (docs/modular-graph-phase-f2-design.md section 6): txiki's
# host (`tjs`), libuv (`uv_a`) or efsw, in the link closure of the Node addon, the render host, the backend or the two
# engine test binaries. The two host runtime libraries are the shell's (packages/native's retroplug-tjs-runtime and
# retroplug-file-watcher), and only the products that instantiate a txiki host or want a watcher link them; a test that
# links the engine and so carries one anyway is a build that would hide that. The walk keeps the path that reached each
# library, so the message says what brought it in, step by step, not only that it is there. A guarded name with no
# target is refused, since the rule would otherwise stop covering a renamed binary; the opt-in Node addon, when it is
# not configured, is the one name allowed to be absent.
#
# TARGETS is for a configure fixture to drive the rule over a scratch target (packages/native/test/packages/gather): the
# default is the five binaries the rule is about.
function(retroplug_check_runtime_libraries)
    cmake_parse_arguments(C "" "" "TARGETS" ${ARGN})
    if(C_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "retroplug_check_runtime_libraries: unrecognised argument(s): ${C_UNPARSED_ARGUMENTS}")
    endif()
    set(_guarded ${C_TARGETS})
    if(NOT _guarded)
        set(_guarded retroplug-backend retroplug-node retroplug-render-host-test retroplug-engine-graph-test
                       retroplug-audio-test)
    endif()
    set(_forbidden tjs uv_a uv efsw-static efsw)
    _retroplug_build_targets(_targets "${CMAKE_SOURCE_DIR}")
    # Every target's own link names, so a closure is followed without asking the generator to evaluate a target's.
    foreach(_t IN LISTS _targets)
        _retroplug_linked_names(_names_${_t} ${_t} LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    endforeach()
    set(_found "")
    foreach(_root IN LISTS _guarded)
        if(NOT TARGET ${_root})
            # A name that is absent is a target this rule would stop covering, so it is refused. The one exception is
            # the Node addon, which is opt-in and may simply not be configured.
            if(_root STREQUAL "retroplug-node" AND NOT RETROPLUG_NODE_ADDON)
                continue()
            endif()
            message(FATAL_ERROR "retroplug_check_runtime_libraries: `${_root}` is one of the targets this rule guards "
                                "and this build defines no target of that name, so the rule would cover nothing; name "
                                "the target here after renaming it (only the opt-in Node addon may be absent, when "
                                "RETROPLUG_NODE_ADDON is off)")
        endif()
        # Breadth-first from the target, each queue entry "path@name": path is every step that reached this one, joined
        # with @ so the message can name the intermediate that brought the library in (a target or library name holds
        # no @, and a pipe would split the path itself).
        set(_pending "${_root}")
        set(_seen "")
        while(_pending)
            list(POP_FRONT _pending _entry)
            string(FIND "${_entry}" "@" _at REVERSE)
            if(_at EQUAL -1)
                set(_name "${_entry}")
            else()
                math(EXPR _at "${_at} + 1")
                string(SUBSTRING "${_entry}" ${_at} -1 _name)
            endif()
            if(_name IN_LIST _forbidden)
                string(REPLACE "@" " -> " _path "${_entry}")
                list(APPEND _found "${_path}")
                break()
            endif()
            if(_name IN_LIST _seen)
                continue()
            endif()
            list(APPEND _seen ${_name})
            foreach(_next IN LISTS _names_${_name})
                if(NOT _next MATCHES "-NOTFOUND$")
                    list(APPEND _pending "${_entry}@${_next}")
                endif()
            endforeach()
        endwhile()
    endforeach()
    if(_found)
        list(JOIN _found "; " _found)
        message(FATAL_ERROR "retroplug_check_runtime_libraries: these targets carry a host runtime library they must "
                            "not (txiki's tjs, libuv's uv_a or efsw): ${_found}. The two host runtime libraries are "
                            "the shell's own (retroplug-tjs-runtime, retroplug-file-watcher), and only the products "
                            "that instantiate a txiki host or want a file watcher link them.")
    endif()
endfunction()

# retroplug_check_test_helpers(): refuses, at configure time, every target whose link closure reaches a package's native
# test helpers (retroplug-testing's headers, or retroplug-testing-allocator's replaced global allocator,
# packages/testing/native) unless the target is a test binary (retroplug_add_test_binary marks one RP_TEST_BINARY). The
# helpers are a replaced global operator new and the shared test fixtures: a product that linked them would count
# allocations in production, and a library that did would hand them to every consumer. A package declares
# @retroplug/testing a devDependency, which the boundary check also allows only a test target to use, so this holds the
# target properties that check cannot see (a library that links a test binary's helper transitively, say). Deferred as
# retroplug_check_products is, so a target defined anywhere is checked.
function(retroplug_check_test_helpers)
    set(_helpers retroplug-testing retroplug-testing-allocator)
    if(NOT TARGET retroplug-testing)
        return()  # a build without the testing package (a device image) links none
    endif()
    _retroplug_build_targets(_targets "${CMAKE_SOURCE_DIR}")
    # Every target's own link names, so a closure is followed without asking the generator to evaluate a target's.
    foreach(_t IN LISTS _targets)
        _retroplug_linked_names(_names_${_t} ${_t} LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    endforeach()
    set(_offenders "")
    foreach(_t IN LISTS _targets)
        get_target_property(_test ${_t} RP_TEST_BINARY)
        if(_test OR _t IN_LIST _helpers)
            continue()  # a test binary may link them, and the helpers link each other (the allocator's header root)
        endif()
        # A breadth-first walk of what the target links, through the libraries of this build alone: a name that is no
        # target (a system library) has no closure to follow.
        set(_pending ${_names_${_t}})
        set(_seen "")
        set(_reached "")
        while(_pending)
            list(POP_FRONT _pending _next)
            if(_next IN_LIST _helpers)
                set(_reached ${_next})
                break()
            endif()
            if(_next IN_LIST _seen OR NOT TARGET ${_next})
                continue()
            endif()
            list(APPEND _seen ${_next})
            if(_names_${_next})
                list(APPEND _pending ${_names_${_next}})
            endif()
        endwhile()
        if(_reached)
            list(APPEND _offenders "${_t} (${_reached})")
        endif()
    endforeach()
    if(_offenders)
        list(JOIN _offenders ", " _offenders)
        message(FATAL_ERROR "retroplug_check_test_helpers: these targets reach the native test helpers "
                            "(packages/testing/native), and are no test binary: ${_offenders}. A product or a library "
                            "that links them counts allocations in production; link them from a "
                            "retroplug_add_test_binary alone.")
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
# operands of a generator expression are in it, with its keywords. An item with a slash is a path to a file (a segment
# of one could spell a target's name), not a target, and is left out; an alias is named by the target it stands for.
function(_retroplug_linked_names out target)
    set(_names "")
    foreach(_property IN LISTS ARGN)
        get_target_property(_value ${target} ${_property})
        foreach(_item IN LISTS _value)
            if(_item MATCHES "[/\\\\]")
                continue()
            endif()
            string(REGEX MATCHALL "[A-Za-z0-9_.+-]+(::[A-Za-z0-9_.+-]+)*" _words "${_item}")
            foreach(_word IN LISTS _words)
                if(TARGET ${_word})
                    get_target_property(_aliased ${_word} ALIASED_TARGET)
                    if(_aliased)
                        list(APPEND _names ${_aliased})
                    endif()
                endif()
                list(APPEND _names ${_word})
            endforeach()
        endforeach()
    endforeach()
    set(${out} ${_names} PARENT_SCOPE)
endfunction()

# retroplug_product_table(<target>): generates build/packages/products/product-table.cpp, every product this configure
# declared with the entries its rp_register_packages() returns, as rp_product_registrations(), and adds it to <target>,
# which must link every package those name. The registration golden reads it (test/engine-graph/Packages.test.cpp), so
# call this after the last retroplug_product(); a
# non-DEVICE retroplug_product() declared after it is refused.
function(retroplug_product_table target)
    set_property(GLOBAL PROPERTY RP_PRODUCT_TABLE_DONE TRUE)
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

# retroplug_examples(<names-out> <entries-out>): every package's example sessions, in product order, from the manifests'
# `retroplug.examples` lists (docs/modular-graph-phase-f4b-design.md section 5.3). The first output holds the names the
# sessions run as (the .js file's stem), the second their TypeScript entries, so a caller zips the two lists. A package
# in the product list with no examples member contributes nothing; the shell's own sessions are not a package's and are
# named by the target itself.
function(retroplug_examples names_out entries_out)
    set(_names "")
    set(_entries "")
    foreach(_package IN LISTS RP_DESKTOP_PACKAGES)
        set(_manifest "${RP_PACKAGES_DIR}/${_package}/package.json")
        file(READ "${_manifest}" _json)
        string(JSON _type ERROR_VARIABLE _err TYPE "${_json}" retroplug examples)
        if(NOT _err STREQUAL "NOTFOUND")
            continue()
        endif()
        string(JSON _count LENGTH "${_json}" retroplug examples)
        if(_count EQUAL 0)
            continue()
        endif()
        math(EXPR _last "${_count} - 1")
        foreach(_i RANGE ${_last})
            string(JSON _name GET "${_json}" retroplug examples ${_i} name)
            string(JSON _entry GET "${_json}" retroplug examples ${_i} entry)
            list(APPEND _names "${_name}")
            list(APPEND _entries "${RP_PACKAGES_DIR}/${_package}/${_entry}")
        endforeach()
    endforeach()
    set(${names_out} "${_names}" PARENT_SCOPE)
    set(${entries_out} "${_entries}" PARENT_SCOPE)
endfunction()
