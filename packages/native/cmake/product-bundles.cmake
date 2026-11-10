# Included by packages/native/CMakeLists.txt before the first retroplug_product(), which every product's targets then
# get two of the four embedded bundles from (the script nodes and the render worker): the backend declares those two
# symbols extern, so a product that does not supply them fails to link, and a product's script host loads ITS node set.
# The control plane and the UI are embedded by the product's own sources, through the variables this file sets.
#
# The desktop's four bundles are the shell's own tree's, written under the binary directory's native/, the paths every
# existing reference reads; every other list has its own set, under build/products/<name>/, built from the tree the
# generator writes for that list
# (docs/modular-graph-phase-f5a-design.md section 7.2). A product's packages are one of the root manifest's lists:
# retroplug_product() refuses packages that are no list's, since a bundle set is built from a list's tree.

# The generator and the shell it lives in. The shell is not a product package, so its manifest is named beside the
# product's; both are derived from the generator's path rather than from a name either side has to agree on.
set(RP_TS_GENERATOR "${CMAKE_SOURCE_DIR}/packages/retroplug/scripts/lib/packages.mjs")
get_filename_component(RP_TS_SHELL "${RP_TS_GENERATOR}/../../.." ABSOLUTE)

# --- the per-product generated modules and embedded bundles (docs/modular-graph-phase-f5a-design.md section 7.2).
#
# Every product has four embedded bundles: the control plane, the React UI, the script nodes and the render worker.
# The desktop's are the shell's own tree's (src/generated, ui/generated), which the shell program, the pure tier and
# every existing path already read, and its bundles keep the names and paths they had. Any other product's are written
# under build/products/<name>/, laid out as the shell's tree is, by the same generator run with --product: its package
# list differs, so its registries do (the plugin's carry no Launchpad); its bundles are built from that tree through
# tools/product-generated.js, the esbuild resolver that points the shell's `../generated/packages`-style imports at the
# product's file. The bundle targets are made ONCE PER DISTINCT LIST CONTENT: two products whose lists are equal share
# one set of bundles, named after the first of them.
#
# retroplug_bundles(<name>) defines that product's four bundle targets (once) and hands the caller the C arrays' paths,
# the targets and the generator target in RP_BUNDLE_<name>_*. retroplug_product() adds the script-node and render-worker
# arrays to its TARGETS: the backend declares those two symbols extern, so every product has to supply them, and a
# product's script host then loads ITS nodes. The control plane and the UI are embedded by the product's own sources
# (the plugin through PluginDSP.cpp/PluginUI.cpp, the SDL binary and the UI test through their own lists), which is why
# they are variables here rather than target sources.
function(retroplug_bundles name)
    # The list's CONTENT decides which set a product shares: two products whose packages are equal build one set of
    # bundles, named after the first of them that asked for it.
    get_property(_packages GLOBAL PROPERTY RP_BUNDLE_PACKAGES_${name})
    set(_key "${name}")
    string(REPLACE ";" "-" _content "${_packages}")
    if(_content)
        get_property(_shares GLOBAL PROPERTY RP_BUNDLE_SHARED_${_content})
        if(_shares)
            set(_key "${_shares}")
        else()
            set_property(GLOBAL PROPERTY RP_BUNDLE_SHARED_${_content} "${name}")
        endif()
    endif()
    get_property(_done GLOBAL PROPERTY RP_BUNDLES_${_key}_DONE)
    if(NOT _done)
        get_property(_packages GLOBAL PROPERTY RP_BUNDLE_PACKAGES_${_key})
        if(_key STREQUAL "desktop")
            set(_suffix "")
            set(_dir "${CMAKE_BINARY_DIR}/native")
            set(_product_tree "")
            set(_generated "${RP_TS_SHELL}/src/generated")
            set(_ui_generated "${RP_TS_SHELL}/ui/generated")
            set(_cli_generated "${RP_TS_SHELL}/cli/generated")
            set(_tsconfigs "${RP_TS_SHELL}/tsconfig.host-modules.json" "${RP_TS_SHELL}/tsconfig.paths.json")
            set(_generator_args "")
            set(_manifests "${RP_TS_SHELL}/package.json")
            foreach(_package IN LISTS _packages)
                list(APPEND _manifests "${CMAKE_SOURCE_DIR}/packages/${_package}/package.json")
            endforeach()
        else()
            set(_suffix "-${_key}")
            set(_dir "${CMAKE_BINARY_DIR}/products/${_key}/native")
            set(_product_tree "${CMAKE_BINARY_DIR}/products/${_key}/packages/retroplug")
            set(_generated "${_product_tree}/src/generated")
            set(_ui_generated "${_product_tree}/ui/generated")
            set(_cli_generated "")
            set(_tsconfigs "${CMAKE_BINARY_DIR}/products/${_key}/tsconfig.json"
                           "${CMAKE_BINARY_DIR}/products/${_key}/tsconfig.paths.json")
            set(_generator_args --product ${_key} --build-dir ${CMAKE_BINARY_DIR})
            set(_manifests "${RP_TS_SHELL}/package.json" "${CMAKE_SOURCE_DIR}/package.json")
            foreach(_package IN LISTS _packages)
                list(APPEND _manifests "${CMAKE_SOURCE_DIR}/packages/${_package}/package.json")
            endforeach()
        endif()
        # The scripts' one product form, emitted only for a set that has a tree of its own (the desktop's is the
        # shell's).
        set(_product_args "")
        if(_product_tree)
            set(_product_args --product-dir "${_product_tree}")
        endif()
        set(_generated_files "${_generated}/packages.ts" "${_generated}/consoles.ts" "${_generated}/protocol.ts"
                             "${_generated}/scriptNodes.ts" "${_generated}/placers.ts"
                             "${_ui_generated}/menus.ts" "${_ui_generated}/views.ts")
        if(_cli_generated)
            list(APPEND _generated_files "${_cli_generated}/tools.ts")
        endif()
        set(_generator_target "retroplug-ts-packages${_suffix}")
        add_custom_target(${_generator_target}
            COMMAND ${NODE_EXECUTABLE} ${RP_TS_GENERATOR} ${_generator_args}
            BYPRODUCTS ${_generated_files} ${_tsconfigs}
            SOURCES ${RP_TS_GENERATOR} ${_manifests} "${CMAKE_SOURCE_DIR}/package.json"
                    "${RP_TS_SHELL}/host-modules.json" "${RP_TS_SHELL}/tsconfig.aliases.json"
            COMMENT "Generating the TS package registries${_suffix} and the typecheck paths"
            VERBATIM)
        # The generator IS the shell's script (the shell's tree holds it, and it writes registries), so this target
        # names the shell by construction and is nobody's to judge against the native package's declarations: the
        # boundary check's voucher, stated here where that is read (cmake/packages.cmake).
        set_target_properties(${_generator_target} PROPERTIES RP_BOUNDARY_SKIP "the shell's own TS generator")

        # --- control-plane bundle: pluginControlPlane.ts -> bytecode C array (mirrors ui-regenerate). The DPF plugin
        # evals this at construct to compose the stores (what runs on the audio thread is the script nodes, below).
        set(_cp_js "${_dir}/cp-bundle.js")
        set(_cp_c "${_dir}/cp-bundle_data.c")
        set_source_files_properties(${_cp_c} PROPERTIES GENERATED TRUE)
        add_custom_target(retroplug-cp-bundle${_suffix} ALL
            BYPRODUCTS ${_cp_js} ${_cp_c} ${_cp_c}.new
            COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-controlplane.js ${_cp_js} ${_product_args}
            COMMAND ${_TJSC_COMMAND} -m -s -p rp_ -o ${_cp_c}.new ${_cp_js}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_cp_c}.new ${_cp_c}
            COMMENT "Regenerating control-plane bundle${_suffix} (esbuild + bytecode)"
            VERBATIM)
        if(TARGET ${_TJSC_DEP})
            add_dependencies(retroplug-cp-bundle${_suffix} ${_TJSC_DEP})
        endif()
        add_dependencies(retroplug-cp-bundle${_suffix} ${_generator_target})

        # --- the UI bundle: main.tsx -> bytecode C array (React entry via build-ui.js).
        set(_ui_js "${_dir}/ui-bundle.js")
        set(_ui_c "${_dir}/ui-bundle_data.c")
        set_source_files_properties(${_ui_c} PROPERTIES GENERATED TRUE)
        add_custom_target(retroplug-ui-bundle${_suffix}
            BYPRODUCTS ${_ui_js} ${_ui_c} ${_ui_c}.new ${_ui_js}.d
            COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-ui.js
                    ${_ui_js} ${_ui_js}.d
                    ${CMAKE_SOURCE_DIR}/packages/retroplug/ui/main.tsx ${_product_args}
            COMMAND ${_TJSC_COMMAND} -m -s -p rp_ -o ${_ui_c}.new ${_ui_js}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_ui_c}.new ${_ui_c}
            COMMENT "Regenerating UI bundle${_suffix} (esbuild + bytecode)"
            VERBATIM)
        if(TARGET ${_TJSC_DEP})
            add_dependencies(retroplug-ui-bundle${_suffix} ${_TJSC_DEP})
        endif()
        add_dependencies(retroplug-ui-bundle${_suffix} ${_generator_target})

        # --- the script-node bundle: the product's generated scriptNodes.ts, every TS node type its packages
        # contribute, esbuilt to a global-code IIFE (each Engine's script host evaluates it on two BARE QuickJS
        # runtimes, which have no module loader) + tjsc'd WITHOUT -m to global-code bytecode, as the render worker is.
        # The array name derives from the output stem: rp_script_bundle[] + _size, the same for every product (the
        # backend declares them extern).
        set(_script_js "${_dir}/script-bundle.js")
        set(_script_c "${_dir}/script-bundle_data.c")
        set_source_files_properties(${_script_c} PROPERTIES GENERATED TRUE)
        add_custom_target(retroplug-script-bundle${_suffix}
            BYPRODUCTS ${_script_js} ${_script_c} ${_script_c}.new
            COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
                    ${_generated}/scriptNodes.ts ${_script_js} iife ${_product_args}
            COMMAND ${_TJSC_COMMAND} -s -p rp_ -o ${_script_c}.new ${_script_js}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_script_c}.new ${_script_c}
            COMMENT "Bundling the script nodes${_suffix} (esbuild iife + global-code bytecode)"
            VERBATIM)
        if(TARGET ${_TJSC_DEP})
            add_dependencies(retroplug-script-bundle${_suffix} ${_TJSC_DEP})
        endif()
        add_dependencies(retroplug-script-bundle${_suffix} ${_generator_target})

        # --- the render-worker bundle: src/render/worker.ts, the same sources for every product (it renders the plan it
        # is handed), built against the product's registries through its generated tree.
        set(_render_js "${_dir}/render-worker-bundle.js")
        set(_render_c "${_dir}/render-worker-bundle_data.c")
        set_source_files_properties(${_render_c} PROPERTIES GENERATED TRUE)
        add_custom_target(retroplug-render-worker-bundle${_suffix}
            BYPRODUCTS ${_render_js} ${_render_c} ${_render_c}.new
            COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
                    ${CMAKE_SOURCE_DIR}/packages/retroplug/src/render/worker.ts ${_render_js} iife ${_product_args}
            COMMAND ${_TJSC_COMMAND} -s -p rp_ -o ${_render_c}.new ${_render_js}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_render_c}.new ${_render_c}
            COMMENT "Bundling render worker${_suffix} (esbuild iife + global-code bytecode)"
            VERBATIM)
        if(TARGET ${_TJSC_DEP})
            add_dependencies(retroplug-render-worker-bundle${_suffix} ${_TJSC_DEP})
        endif()
        add_dependencies(retroplug-render-worker-bundle${_suffix} ${_generator_target})

        # The two arrays, compiled ONCE per set: every product target of a set uses these objects, where before each
        # one compiled its own copy (the render worker's array is 4.7 MB of C, and eight desktop products, each test
        # binary and four plugin formats compiled it). An object library keeps one definition per binary and one
        # compile per set; retroplug_product() adds its objects to each of its TARGETS.
        add_library(retroplug-bundles${_suffix}-objects OBJECT EXCLUDE_FROM_ALL ${_script_c} ${_render_c})
        add_dependencies(retroplug-bundles${_suffix}-objects
                         retroplug-script-bundle${_suffix} retroplug-render-worker-bundle${_suffix})
        # Judged at our own warning level, as the products that compiled the arrays were (cmake/warnings.cmake reads
        # this property: a set's name is the list's, so naming them here would miss the next one).
        set_property(GLOBAL APPEND PROPERTY RP_BUNDLE_OBJECTS_TARGETS "retroplug-bundles${_suffix}-objects")

        set_property(GLOBAL PROPERTY RP_BUNDLES_${_key}_SUFFIX "${_suffix}")
        set_property(GLOBAL PROPERTY RP_BUNDLES_${_key}_DIR "${_dir}")
        set_property(GLOBAL PROPERTY RP_BUNDLES_${_key}_DONE TRUE)
    endif()
    # The caller's view, whether this product's set is its own or one it shares.
    get_property(_suffix GLOBAL PROPERTY RP_BUNDLES_${_key}_SUFFIX)
    get_property(_dir GLOBAL PROPERTY RP_BUNDLES_${_key}_DIR)
    set(RP_BUNDLE_${name}_GENERATOR "retroplug-ts-packages${_suffix}" PARENT_SCOPE)
    set(RP_BUNDLE_${name}_CP_TARGET "retroplug-cp-bundle${_suffix}" PARENT_SCOPE)
    set(RP_BUNDLE_${name}_UI_TARGET "retroplug-ui-bundle${_suffix}" PARENT_SCOPE)
    set(RP_BUNDLE_${name}_SCRIPT_TARGET "retroplug-script-bundle${_suffix}" PARENT_SCOPE)
    set(RP_BUNDLE_${name}_RENDER_TARGET "retroplug-render-worker-bundle${_suffix}" PARENT_SCOPE)
    set(RP_BUNDLE_${name}_CP_C "${_dir}/cp-bundle_data.c" PARENT_SCOPE)
    set(RP_BUNDLE_${name}_UI_C "${_dir}/ui-bundle_data.c" PARENT_SCOPE)
    set(RP_BUNDLE_${name}_OBJECTS "retroplug-bundles${_suffix}-objects" PARENT_SCOPE)
endfunction()


# The desktop's set, made here so every product declared below can share it and every existing reference to its C arrays
# (CP_BUNDLE_C, UI_BUNDLE_C) keeps its meaning. A product declared with LIST <name> gets its own set through
# retroplug_product().
set_property(GLOBAL PROPERTY RP_BUNDLE_PACKAGES_desktop "${RP_PRODUCT_PACKAGES_desktop}")
retroplug_bundles(desktop)
set(CP_BUNDLE_C "${RP_BUNDLE_desktop_CP_C}")
set(UI_BUNDLE_C "${RP_BUNDLE_desktop_UI_C}")
