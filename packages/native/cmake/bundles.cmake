# Included by packages/native/CMakeLists.txt, which is the only caller. `include()` keeps the
# CURRENT directory scope, so ${CMAKE_CURRENT_SOURCE_DIR} still means packages/native/ here and every
# relative source path below resolves exactly as it did inline. (add_subdirectory would NOT: it opens
# a child scope and re-roots that variable, which is the trap this split is avoiding.)
#
# The embedded JS bytecode bundles: the control plane, the React UI, and the script nodes (with a test-only twin).

# --- the TS package registries and the typecheck paths (docs/modular-graph-phase-f1-design.md section 3,
# docs/modular-graph-phase-f4b-design.md section 5.4): the shell's src/generated/ (packages.ts, consoles.ts,
# protocol.ts, scriptNodes.ts and placers.ts), ui/generated/menus.ts, ui/generated/views.ts and cli/generated/tools.ts,
# and the two
# tsconfig path files the same list writes. Their inputs are the product list (the root package.json's
# retroplug.products.desktop), every package's manifest and the shell's host-modules.json. Every bundle whose inputs
# reach one of them imports it, so each is ordered after this target.
# It runs on every build (the bundle targets regenerate unconditionally anyway) and writes only a file whose content
# changed, so a manifest edit rebuilds the bundles and a build that changes nothing touches no source file.
#
# The shell is not a product package, so its manifest is named here beside the product's. It is the package the
# generator lives in (packages/retroplug: the app itself), which is where the generated modules land too, so both are
# derived from the generator's path rather than from a name either side has to agree on.
set(RP_TS_GENERATOR "${CMAKE_SOURCE_DIR}/packages/retroplug/scripts/lib/packages.mjs")
get_filename_component(RP_TS_SHELL "${RP_TS_GENERATOR}/../../.." ABSOLUTE)
set(RP_TS_GENERATED "${RP_TS_SHELL}/src/generated")
set(RP_TS_UI_GENERATED "${RP_TS_SHELL}/ui/generated")
set(RP_TS_CLI_GENERATED "${RP_TS_SHELL}/cli/generated")
set(_rp_ts_manifests "${RP_TS_SHELL}/package.json")
foreach(_rp_ts_package IN LISTS RP_DESKTOP_PACKAGES)
    list(APPEND _rp_ts_manifests "${CMAKE_SOURCE_DIR}/packages/${_rp_ts_package}/package.json")
endforeach()
add_custom_target(retroplug-ts-packages
    COMMAND ${NODE_EXECUTABLE} ${RP_TS_GENERATOR}
    BYPRODUCTS "${RP_TS_GENERATED}/packages.ts" "${RP_TS_GENERATED}/consoles.ts"
               "${RP_TS_GENERATED}/protocol.ts" "${RP_TS_GENERATED}/scriptNodes.ts"
               "${RP_TS_GENERATED}/placers.ts"
               "${RP_TS_UI_GENERATED}/menus.ts" "${RP_TS_UI_GENERATED}/views.ts"
               "${RP_TS_CLI_GENERATED}/tools.ts"
               "${RP_TS_SHELL}/tsconfig.host-modules.json" "${RP_TS_SHELL}/tsconfig.paths.json"
    SOURCES ${RP_TS_GENERATOR} ${_rp_ts_manifests} "${CMAKE_SOURCE_DIR}/package.json"
            "${RP_TS_SHELL}/host-modules.json" "${RP_TS_SHELL}/tsconfig.aliases.json"
    COMMENT "Generating the TS package registries and the typecheck paths"
    VERBATIM)
# The generator IS the shell's script (the shell's tree holds it, and it writes the shell's registries and typecheck
# paths), so this target names the shell by construction and is nobody's to judge against the native package's
# declarations: the boundary check's voucher, stated here where that is read (cmake/packages.cmake).
set_target_properties(retroplug-ts-packages PROPERTIES RP_BOUNDARY_SKIP "the shell's own TS generator")
# The bundles whose esbuild inputs reach the generated modules but which are defined in packages/native/CMakeLists.txt
# above this include: the render worker, the CLI dispatcher, its example sessions and the SDK it embeds. The control
# plane, the UI and the script nodes are defined here, below, and take the same dependency where they are made.
add_dependencies(retroplug-render-worker-bundle retroplug-ts-packages)
add_dependencies(retroplug-cli-bundle retroplug-ts-packages)
add_dependencies(retroplug-example-session retroplug-ts-packages)
# tools/embed-sdk.mjs -> tools/build-cli-sdk.mjs esbuilds cli/sdk.ts, which reaches the registries through
# cli/session.ts -> src/bootSession.ts -> src/appHost.ts: unbundled from this target, a parallel build from a tree with
# no src/generated can bundle the SDK before the generator has written it.
add_dependencies(retroplug-cli-sdk-assets retroplug-ts-packages)

# --- control-plane bundle: pluginControlPlane.ts → bytecode C array (mirrors ui-regenerate). The DPF
# plugin evals this at construct to compose the stores (what runs on the audio thread is the script nodes, in the
# script-node bundle below). Derived; never committed.
set(CP_BUNDLE_JS "${CMAKE_BINARY_DIR}/native/cp-bundle.js")
set(CP_BUNDLE_C  "${CMAKE_BINARY_DIR}/native/cp-bundle_data.c")
set_source_files_properties(${CP_BUNDLE_C} PROPERTIES GENERATED TRUE)  # produced before the plugin compiles

add_custom_target(retroplug-cp-bundle ALL
    BYPRODUCTS ${CP_BUNDLE_JS} ${CP_BUNDLE_C} ${CP_BUNDLE_C}.new
    COMMAND ${NODE_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/build-controlplane.js ${CP_BUNDLE_JS}
    COMMAND ${_TJSC_COMMAND} -m -s -p rp_ -o ${CP_BUNDLE_C}.new ${CP_BUNDLE_JS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CP_BUNDLE_C}.new ${CP_BUNDLE_C}
    COMMENT "Regenerating control-plane bundle (esbuild + bytecode)"
    VERBATIM)
# Order against the in-tree tjsc target only; a cross-build's -DTJSC_EXECUTABLE is a host file
# path (not a target) — the custom command's DEPENDS already tracks it.
if(TARGET ${_TJSC_DEP})
  add_dependencies(retroplug-cp-bundle ${_TJSC_DEP})
endif()
add_dependencies(retroplug-cp-bundle retroplug-ts-packages)  # the generated registries its stores import

# The UI bundle: main.tsx → bytecode C array (React entry via build-ui.js). Derived; never
# committed. Defined BEFORE the plugin so its FILES_UI can embed it; the UI-test binary below reuses the
# same vars/target.
set(UI_BUNDLE_JS "${CMAKE_BINARY_DIR}/native/ui-bundle.js")
set(UI_BUNDLE_C  "${CMAKE_BINARY_DIR}/native/ui-bundle_data.c")
set_source_files_properties(${UI_BUNDLE_C} PROPERTIES GENERATED TRUE)

add_custom_target(retroplug-ui-bundle
    BYPRODUCTS ${UI_BUNDLE_JS} ${UI_BUNDLE_C} ${UI_BUNDLE_C}.new
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-ui.js
            ${UI_BUNDLE_JS} ${UI_BUNDLE_JS}.d
            ${CMAKE_SOURCE_DIR}/packages/retroplug/ui/main.tsx
    COMMAND ${_TJSC_COMMAND} -m -s -p rp_ -o ${UI_BUNDLE_C}.new ${UI_BUNDLE_JS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${UI_BUNDLE_C}.new ${UI_BUNDLE_C}
    COMMENT "Regenerating UI bundle (esbuild + bytecode)"
    VERBATIM)
# Order against the in-tree tjsc target only; a cross-build's -DTJSC_EXECUTABLE is a host file
# path (not a target) — the custom command's DEPENDS already tracks it.
if(TARGET ${_TJSC_DEP})
  add_dependencies(retroplug-ui-bundle ${_TJSC_DEP})
endif()
add_dependencies(retroplug-ui-bundle retroplug-ts-packages)  # the generated registries its stores import

# The script-node bundle: src/generated/scriptNodes.ts, every TS node type the product's packages contribute, esbuilt
# to a global-code IIFE (each Engine's script host evaluates it on two BARE QuickJS runtimes, which have no module
# loader) + tjsc'd WITHOUT -m to global-code bytecode, as the render worker is. Compiled into retroplug-backend, so
# every host's Engines load it at construction (docs/modular-graph-phase-b3a-design.md section 2.5). The array name
# derives from the output stem: rp_script_bundle[] + _size. Derived; never committed.
set(SCRIPT_BUNDLE_JS "${CMAKE_BINARY_DIR}/native/script-bundle.js")
set(SCRIPT_BUNDLE_C  "${CMAKE_BINARY_DIR}/native/script-bundle_data.c")
set_source_files_properties(${SCRIPT_BUNDLE_C} PROPERTIES GENERATED TRUE)
add_custom_target(retroplug-script-bundle
    BYPRODUCTS ${SCRIPT_BUNDLE_JS} ${SCRIPT_BUNDLE_C} ${SCRIPT_BUNDLE_C}.new
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${RP_TS_GENERATED}/scriptNodes.ts ${SCRIPT_BUNDLE_JS} iife
    COMMAND ${_TJSC_COMMAND} -s -p rp_ -o ${SCRIPT_BUNDLE_C}.new ${SCRIPT_BUNDLE_JS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SCRIPT_BUNDLE_C}.new ${SCRIPT_BUNDLE_C}
    COMMENT "Bundling the script nodes (esbuild iife + global-code bytecode)"
    VERBATIM)
if(TARGET ${_TJSC_DEP})
  add_dependencies(retroplug-script-bundle ${_TJSC_DEP})
endif()
add_dependencies(retroplug-script-bundle retroplug-ts-packages)  # its entry is a generated module
# retroplug-backend is defined before this file is included; the array must exist before it compiles.
target_sources(retroplug-backend PRIVATE ${SCRIPT_BUNDLE_C})
add_dependencies(retroplug-backend retroplug-script-bundle)

# Its test-only twin: test/script/scriptTestBundle.ts (the authoring layer for the native tests' toy nodes, and one
# node of its own) by the same recipe, embedded only in retroplug-engine-graph-test (cmake/tests.cmake), so the
# toy nodes and the test node never ship. rp_script_test_bundle[] + _size.
set(SCRIPT_TEST_BUNDLE_JS "${CMAKE_BINARY_DIR}/native/script-test-bundle.js")
set(SCRIPT_TEST_BUNDLE_C  "${CMAKE_BINARY_DIR}/native/script-test-bundle_data.c")
set_source_files_properties(${SCRIPT_TEST_BUNDLE_C} PROPERTIES GENERATED TRUE)
add_custom_target(retroplug-script-test-bundle
    BYPRODUCTS ${SCRIPT_TEST_BUNDLE_JS} ${SCRIPT_TEST_BUNDLE_C} ${SCRIPT_TEST_BUNDLE_C}.new
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${CMAKE_SOURCE_DIR}/packages/retroplug/test/script/scriptTestBundle.ts ${SCRIPT_TEST_BUNDLE_JS} iife
    COMMAND ${_TJSC_COMMAND} -s -p rp_ -o ${SCRIPT_TEST_BUNDLE_C}.new ${SCRIPT_TEST_BUNDLE_JS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SCRIPT_TEST_BUNDLE_C}.new ${SCRIPT_TEST_BUNDLE_C}
    COMMENT "Bundling the script test nodes (esbuild iife + global-code bytecode)"
    VERBATIM)
if(TARGET ${_TJSC_DEP})
  add_dependencies(retroplug-script-test-bundle ${_TJSC_DEP})
endif()
