# Included by packages/native/CMakeLists.txt, which is the only caller. `include()` keeps the
# CURRENT directory scope, so ${CMAKE_CURRENT_SOURCE_DIR} still means packages/native/ here and every
# relative source path below resolves exactly as it did inline. (add_subdirectory would NOT: it opens
# a child scope and re-roots that variable, which is the trap this split is avoiding.)
#
# The embedded JS bytecode bundles: the control plane, and the React UI.

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
