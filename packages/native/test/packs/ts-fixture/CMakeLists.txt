# The TS fixture pack (docs/downloadable-packs-m2c-plan.md): tsfix, built from ts/ by tools/build-pack-halves.mjs as a
# package's halves are, and its broken variants, hand-written (the tool would refuse them), each staged under
# build/packs/<id>/1.0.0/ as a development pack. No product lists any of them.
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
add_custom_target(retroplug-ts-fixture-packs DEPENDS "${_tsfix_out}/pack.json" ${_tsfix_halves})
_tsfix_claim(tsfix)

foreach(_v tsfix-throws tsfix-guard tsfix-undeclared tsfix-cliclash tsfix-dependent tsfix-protocol)
    _tsfix_claim(${_v})
    configure_file("${_tsfix_dir}/variants/${_v}/pack.json.in" "${RP_PACKS_DIR}/${_v}/1.0.0/pack.json" @ONLY)
    file(GLOB _v_js RELATIVE "${_tsfix_dir}/variants/${_v}" CONFIGURE_DEPENDS "${_tsfix_dir}/variants/${_v}/js/*.js")
    foreach(_js IN LISTS _v_js)
        configure_file("${_tsfix_dir}/variants/${_v}/${_js}" "${RP_PACKS_DIR}/${_v}/1.0.0/${_js}" COPYONLY)
    endforeach()
endforeach()
