# retroplug_cli(<target> <product> [EXCLUDE_FROM_ALL]): retroplug-cli built for one of retroplug.products' lists
# (docs/downloadable-packs-cli-plan.md). The product's cli shared set (its dispatcher rp:cli, the SDK rp:sdk a session
# imports, and the contract modules both link to) is built from the product's generated tree, so its tools are the
# list's and a pack's cli half adds the rest; the type stripper is every product's one. The desktop's targets keep
# their names (retroplug-cli-bundle, retroplug-cli-sdk-assets) and its arrays build/native; another product's carry
# -<product> and live under build/products/<product>/native. Products with equal lists share a bundle set, so a
# product that shares another's must not also ask for a CLI of its own: the arrays are keyed on the product's name.
#
# Read from the caller's scope: _MODC_COMMAND, _MODC_DEP, TS_STRIPPER_BUNDLE_C, RP_DESKTOP_PACKAGES /
# RP_PRODUCT_PACKAGES_<product> and the retroplug-ts-stripper-bundle target, so include this after the type stripper is
# defined.
function(retroplug_cli target product)
    cmake_parse_arguments(ARG "EXCLUDE_FROM_ALL" "" "" ${ARGN})
    retroplug_bundles(${product})
    if(NOT product STREQUAL "desktop"
       AND NOT RP_BUNDLE_${product}_DIR STREQUAL "${CMAKE_BINARY_DIR}/products/${product}/native")
        message(FATAL_ERROR "retroplug_cli(${target} ${product}): ${product} shares another product's bundle set "
            "(${RP_BUNDLE_${product}_DIR}), and a CLI's arrays are keyed on its own product")
    endif()
    if(product STREQUAL "desktop")
        set(_suffix "")
        set(_product_args "")
        set(_packages ${RP_DESKTOP_PACKAGES})
    else()
        set(_suffix "-${product}")
        set(_product_args --product-dir "${RP_BUNDLE_${product}_TREE}")
        set(_packages ${RP_PRODUCT_PACKAGES_${product}})
    endif()
    set(_dir "${RP_BUNDLE_${product}_DIR}")
    set(_modules_dir "${_dir}/cli-modules")
    set(_bundle_c  "${_dir}/cli-bundle_data.c")
    set(_modules_c "${_dir}/cli-modules_data.c")
    set(_sdk_c     "${_dir}/sdk-module_data.c")
    set(_assets_c  "${_dir}/sdk-assets_data.c")

    # The root CLI dispatcher (cli/cli.ts) and the shell's contract modules as the CLI's shared module set
    # (docs/downloadable-packs-m2b-plan.md): rp_cli_modules is the ordered table the launcher registers before it
    # evaluates the dispatcher (rp_cli_bundle, now importing those modules by name), so a pack's CLI half can link to
    # the same contracts (M2c). Derived; never committed. Adding a command is pure TS (cli/tools.ts): no change here.
    if(NOT TARGET retroplug-cli-bundle${_suffix})
        # Produced before the CLI compiles.
        set_source_files_properties(${_bundle_c} ${_modules_c} ${_sdk_c} PROPERTIES GENERATED TRUE)
        add_custom_target(retroplug-cli-bundle${_suffix}
            BYPRODUCTS ${_bundle_c} ${_bundle_c}.new ${_modules_c} ${_modules_c}.new
                       ${_sdk_c} ${_sdk_c}.new
                       ${_modules_dir}/shared.tsv ${_modules_dir}/hosts.tsv
            COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-shared-modules.mjs cli ${_modules_dir}
                    ${_product_args}
            COMMAND ${_MODC_COMMAND} table -s rp_cli_modules ${_modules_dir}/shared.tsv ${_modules_c}.new
            COMMAND ${_MODC_COMMAND} array -s rp:cli rp_cli_bundle ${_modules_dir}/js/rp/cli.js ${_bundle_c}.new
            COMMAND ${_MODC_COMMAND} array -s retroplug-cli rp_retroplug_cli ${_modules_dir}/js/rp/sdk.js ${_sdk_c}.new
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_modules_c}.new ${_modules_c}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_bundle_c}.new ${_bundle_c}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_sdk_c}.new ${_sdk_c}
            COMMENT "Building the CLI's shared module set${_suffix} (esbuild split + retroplug-modc)"
            VERBATIM)
        # Order against the in-tree tool target only; a cross-build's -DRP_MODC_EXECUTABLE is a host file path (not a
        # target).
        if(TARGET ${_MODC_DEP})
            add_dependencies(retroplug-cli-bundle${_suffix} ${_MODC_DEP})
        endif()
        # ...and against the product's generator: cli.ts imports cli/generated/tools.ts and the dispatcher's own
        # modules import src/generated, which a fresh tree has not written yet. Unordered from this edge, a parallel
        # build from a tree with no src/generated can bundle the CLI before the generator has written it.
        add_dependencies(retroplug-cli-bundle${_suffix} ${RP_BUNDLE_${product}_GENERATOR})

        # The test SDK, embedded twice, for two different consumers:
        #
        #   rp_retroplug_cli[]  MODULE bytecode of the cli shared set's `rp:sdk` host (cli/sdkModule.ts), compiled by
        #                       retroplug-modc (retroplug-cli-bundle) under the module name `retroplug-cli`.
        #                       cli/main.cpp JS_ReadObject's it before evaluating a session, which registers it in
        #                       QuickJS's loaded-module table; an `import ... from "retroplug-cli"` then resolves from
        #                       that table, because js_host_resolve_imported_module looks there BEFORE calling the
        #                       module loader (quickjs.c: "first look at the loaded modules"). So the SDK never touches
        #                       the filesystem - there is no .js to generate, sync, or let go stale. Its imports are
        #                       the set's contract modules, registered first, and it commits the launch packs' halves
        #                       before a session's body.
        #   rp_sdk_dts[]        the .d.ts as raw text. Still materialized next to a consumer's tests, because
        #                       editors and `tsc` read declarations off disk; a bare specifier resolves to it
        #                       via a tsconfig `paths` entry.
        #
        # The relative-import form could never work this way: tjs_module_normalizer only consults the import
        # map for BARE specifiers (modules.c, `if (name[0] != '.')`), so "../sdk/x.js" always hits the disk.
        set_source_files_properties(${_assets_c} PROPERTIES GENERATED TRUE)
        add_custom_target(retroplug-cli-sdk-assets${_suffix}
            BYPRODUCTS ${_assets_c} ${_assets_c}.new
            # Embeds the .d.ts and a content stamp over the set's rp:sdk output and the .d.ts.
            COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/embed-sdk.mjs
                    ${_modules_dir}/js/rp/sdk.js ${_assets_c}.new
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_assets_c}.new ${_assets_c}
            COMMENT "Embedding the retroplug-cli test SDK's declarations and stamp${_suffix}"
            VERBATIM)
        # The stamp hashes the set's rp:sdk output, so the set is built first.
        add_dependencies(retroplug-cli-sdk-assets${_suffix} retroplug-cli-bundle${_suffix})
    endif()

    # retroplug-cli: the standalone runner - the backend lib + a main() that evals either the COMPILED-IN
    # dispatcher bundle (commands + help) or, when argv[1] is a `.js`, a session file by path. No Node at
    # runtime. Output binary lands in build/bin/.
    set(_exclude "")
    if(ARG_EXCLUDE_FROM_ALL)
        set(_exclude EXCLUDE_FROM_ALL)
    endif()
    add_executable(${target} ${_exclude} cli/main.cpp src/host/SessionPump.cpp
        ${_bundle_c} ${_modules_c} ${TS_STRIPPER_BUNDLE_C} ${_assets_c} ${_sdk_c})
    target_link_libraries(${target} PRIVATE retroplug-backend retroplug-tjs-runtime)
    retroplug_product(${target} PACKAGES ${_packages} PROVIDERS n8 launchpad midi)
    add_dependencies(${target} retroplug-cli-bundle${_suffix})      # the embedded dispatcher bytecode must exist first
    add_dependencies(${target} retroplug-ts-stripper-bundle)        # ...and the stripper bytecode
    add_dependencies(${target} retroplug-cli-sdk-assets${_suffix})  # ...and the embedded SDK
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endfunction()
