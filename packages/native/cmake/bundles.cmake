# Included by packages/native/CMakeLists.txt, which is the only caller. `include()` keeps the
# CURRENT directory scope, so ${CMAKE_CURRENT_SOURCE_DIR} still means packages/native/ here and every
# relative source path below resolves exactly as it did inline. (add_subdirectory would NOT: it opens
# a child scope and re-roots that variable, which is the trap this split is avoiding.)
#
# The embedded JS bytecode bundles: the control plane, the React UI, and the script nodes (with a test-only twin).

# --- control-plane bundle: pluginControlPlane.ts → bytecode C array (mirrors ui-regenerate). The DPF
# plugin evals this at construct to compose the stores + DSP kernel. Derived; never committed.
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

# The script-node bundle: src/script/bundle.ts, every TS node type the engine runs, esbuilt to a global-code IIFE
# (each Engine's script host evaluates it on two BARE QuickJS runtimes, which have no module loader) + tjsc'd
# WITHOUT -m to global-code bytecode, as the render worker is. Compiled into retroplug-backend, so every host's
# Engines load it at construction (docs/modular-graph-phase-b3a-design.md section 2.5). The array name derives
# from the output stem: rp_script_bundle[] + _size. Derived; never committed.
set(SCRIPT_BUNDLE_JS "${CMAKE_BINARY_DIR}/native/script-bundle.js")
set(SCRIPT_BUNDLE_C  "${CMAKE_BINARY_DIR}/native/script-bundle_data.c")
set_source_files_properties(${SCRIPT_BUNDLE_C} PROPERTIES GENERATED TRUE)
add_custom_target(retroplug-script-bundle
    BYPRODUCTS ${SCRIPT_BUNDLE_JS} ${SCRIPT_BUNDLE_C} ${SCRIPT_BUNDLE_C}.new
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-session.js
            ${CMAKE_SOURCE_DIR}/packages/retroplug/src/script/bundle.ts ${SCRIPT_BUNDLE_JS} iife
    COMMAND ${_TJSC_COMMAND} -s -p rp_ -o ${SCRIPT_BUNDLE_C}.new ${SCRIPT_BUNDLE_JS}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SCRIPT_BUNDLE_C}.new ${SCRIPT_BUNDLE_C}
    COMMENT "Bundling the script nodes (esbuild iife + global-code bytecode)"
    VERBATIM)
if(TARGET ${_TJSC_DEP})
  add_dependencies(retroplug-script-bundle ${_TJSC_DEP})
endif()
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
