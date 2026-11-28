# The TS fixture pack (docs/downloadable-packs-m2c-plan.md): tsfix, built from ts/ by tools/build-pack-halves.mjs as a
# package's halves are, and its broken variants, hand-written (the tool would refuse them), each staged under
# build/packs/<id>/1.0.0/ as a development pack. No product lists any of them.
# tsfix-render  an app half whose role every ROM gets, which a render's console shows
# tsfix-proto   an app, a protocol and a placer half (a role placed as a script node)
# tsfix-kind    an app half and a kinds half (a controller app's runtime)
# tsfix-proto-kind  an app, a protocol and a kinds half: an Engine refuses it when it is loaded live
# Every staged folder is claimed once (cmake/packs.cmake's RP_PACK_STAGED), so two fixtures cannot stage one id: a second
# claim is a configure error here, as retroplug_pack_module's is.
function(_tsfix_claim id)
    get_property(_staged GLOBAL PROPERTY RP_PACK_STAGED)
    if("${id}/1.0.0" IN_LIST _staged)
        message(FATAL_ERROR "the TS fixture pack ${id}/1.0.0 is already staged by another target")
    endif()
    set_property(GLOBAL APPEND PROPERTY RP_PACK_STAGED "${id}/1.0.0")
endfunction()

set(_tsfix_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(_tsfix_out "${RP_PACKS_DIR}/tsfix/1.0.0")
file(GLOB _tsfix_sources CONFIGURE_DEPENDS "${_tsfix_dir}/ts/*.ts")
set(_tsfix_halves "${_tsfix_out}/js/app.js" "${_tsfix_out}/js/ui.js" "${_tsfix_out}/js/cli.js")
add_custom_command(
    # The halves are outputs of the same run, so deleting one rebuilds (a byproduct would not: Makefiles have no rule for
    # a missing one, and the target would fail rather than rebuild).
    OUTPUT "${_tsfix_out}/pack.json" ${_tsfix_halves}
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-pack-halves.mjs
            ${_tsfix_dir} ${_tsfix_out} ${_tsfix_dir}/pack.json
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/build-pack-halves.mjs ${_tsfix_dir}/package.json ${_tsfix_dir}/pack.json
            ${_tsfix_sources}
            # what the tool reads besides the package: the API level it stamps and the contracts it keeps external
            ${CMAKE_SOURCE_DIR}/packages/node-sdk/native/src/graph/rp_pack.h
            ${CMAKE_SOURCE_DIR}/packages/retroplug/scripts/lib/contracts.mjs
            ${CMAKE_SOURCE_DIR}/packages/retroplug/host-modules.json
    COMMENT "Building the TS fixture pack"
    VERBATIM)
_tsfix_claim(tsfix)

# tsfix-render: built as tsfix is, from render/ (docs/downloadable-packs-m2d-plan.md).
set(_tsr_dir "${_tsfix_dir}/render")
set(_tsr_out "${RP_PACKS_DIR}/tsfix-render/1.0.0")
file(GLOB _tsr_sources CONFIGURE_DEPENDS "${_tsr_dir}/ts/*.ts")
add_custom_command(
    OUTPUT "${_tsr_out}/pack.json" "${_tsr_out}/js/app.js"
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-pack-halves.mjs
            ${_tsr_dir} ${_tsr_out} ${_tsr_dir}/pack.json
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/build-pack-halves.mjs ${_tsr_dir}/package.json ${_tsr_dir}/pack.json
            ${_tsr_sources}
            ${CMAKE_SOURCE_DIR}/packages/node-sdk/native/src/graph/rp_pack.h
            ${CMAKE_SOURCE_DIR}/packages/retroplug/scripts/lib/contracts.mjs
            ${CMAKE_SOURCE_DIR}/packages/retroplug/host-modules.json
    COMMENT "Building the TS render fixture pack"
    VERBATIM)
_tsfix_claim(tsfix-render)

# tsfix-proto: built as tsfix is, from protocol/ (docs/downloadable-packs-m2e-plan.md): an app, a protocol and a placer half.
set(_tsp_dir "${_tsfix_dir}/protocol")
set(_tsp_out "${RP_PACKS_DIR}/tsfix-proto/1.0.0")
file(GLOB _tsp_sources CONFIGURE_DEPENDS "${_tsp_dir}/ts/*.ts")
set(_tsp_halves "${_tsp_out}/js/app.js" "${_tsp_out}/js/protocol.js" "${_tsp_out}/js/placer.js")
add_custom_command(
    OUTPUT "${_tsp_out}/pack.json" ${_tsp_halves}
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-pack-halves.mjs
            ${_tsp_dir} ${_tsp_out} ${_tsp_dir}/pack.json
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/build-pack-halves.mjs ${_tsp_dir}/package.json ${_tsp_dir}/pack.json
            ${_tsp_sources}
            ${CMAKE_SOURCE_DIR}/packages/node-sdk/native/src/graph/rp_pack.h
            ${CMAKE_SOURCE_DIR}/packages/retroplug/scripts/lib/contracts.mjs
            ${CMAKE_SOURCE_DIR}/packages/retroplug/host-modules.json
            # the protocol half bundles core's node SDK (core's own ts/*.ts are not listed, as the other fixtures
            # list only their own sources; spec/07 records the DEPFILE question)
            ${CMAKE_SOURCE_DIR}/packages/core/package.json
    COMMENT "Building the TS protocol fixture pack"
    VERBATIM)
_tsfix_claim(tsfix-proto)

# tsfix-kind: built as tsfix is, from kinds/ (docs/downloadable-packs-m2f-plan.md): an app half and a kinds half, a
# controller app's runtime.
set(_tsk_dir "${_tsfix_dir}/kinds")
set(_tsk_out "${RP_PACKS_DIR}/tsfix-kind/1.0.0")
file(GLOB _tsk_sources CONFIGURE_DEPENDS "${_tsk_dir}/ts/*.ts")
set(_tsk_halves "${_tsk_out}/js/app.js" "${_tsk_out}/js/kinds.js")
add_custom_command(
    OUTPUT "${_tsk_out}/pack.json" ${_tsk_halves}
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-pack-halves.mjs
            ${_tsk_dir} ${_tsk_out} ${_tsk_dir}/pack.json
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/build-pack-halves.mjs ${_tsk_dir}/package.json ${_tsk_dir}/pack.json
            ${_tsk_sources}
            ${CMAKE_SOURCE_DIR}/packages/node-sdk/native/src/graph/rp_pack.h
            ${CMAKE_SOURCE_DIR}/packages/retroplug/scripts/lib/contracts.mjs
            ${CMAKE_SOURCE_DIR}/packages/retroplug/host-modules.json
            # the tool reads each package's declared kind runtime modules (launchpad's controllerApps) to refuse bundling
            # them; this fixture value-imports no @retroplug module
            ${CMAKE_SOURCE_DIR}/packages/launchpad/package.json
    COMMENT "Building the TS kinds fixture pack"
    VERBATIM)
_tsfix_claim(tsfix-kind)

add_custom_target(retroplug-ts-fixture-packs DEPENDS "${_tsfix_out}/pack.json" ${_tsfix_halves}
                  "${_tsr_out}/pack.json" "${_tsr_out}/js/app.js"
                  "${_tsp_out}/pack.json" ${_tsp_halves}
                  "${_tsk_out}/pack.json" ${_tsk_halves})

foreach(_v tsfix-throws tsfix-guard tsfix-undeclared tsfix-cliclash tsfix-dependent tsfix-protocol tsfix-proto-ports
        tsfix-proto-kind)
    _tsfix_claim(${_v})
    configure_file("${_tsfix_dir}/variants/${_v}/pack.json.in" "${RP_PACKS_DIR}/${_v}/1.0.0/pack.json" @ONLY)
    file(GLOB _v_js RELATIVE "${_tsfix_dir}/variants/${_v}" CONFIGURE_DEPENDS "${_tsfix_dir}/variants/${_v}/js/*.js")
    foreach(_js IN LISTS _v_js)
        configure_file("${_tsfix_dir}/variants/${_v}/${_js}" "${RP_PACKS_DIR}/${_v}/1.0.0/${_js}" COPYONLY)
    endforeach()
endforeach()
