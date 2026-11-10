# Included by packages/native/CMakeLists.txt, which is the only caller. `include()` keeps the
# CURRENT directory scope, so ${CMAKE_CURRENT_SOURCE_DIR} still means packages/native/ here and every
# relative source path below resolves exactly as it did inline. (add_subdirectory would NOT: it opens
# a child scope and re-roots that variable, which is the trap this split is avoiding.)
#
# The one embedded JS bundle a product does not own: the TEST-ONLY script bundle,
# test/script/scriptTestBundle.ts (the authoring layer for the native tests' toy nodes, and one node of its own),
# embedded only in retroplug-engine-graph-test (cmake/tests.cmake), so the toy nodes and the test node never ship.
# A product's own four (the control plane, the React UI, the script nodes, the render worker) are
# cmake/product-bundles.cmake's, because every retroplug_product() adds two of them to its targets.
# rp_script_test_bundle[] + _size.
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
