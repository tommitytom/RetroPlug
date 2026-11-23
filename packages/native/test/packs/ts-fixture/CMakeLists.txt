# The TS fixture pack (docs/downloadable-packs-m2c-plan.md): tsfix, built from ts/ by tools/build-pack-halves.mjs as a
# package's halves are, and its broken variants, hand-written (the tool would refuse them), each staged under
# build/packs/<id>/1.0.0/ as a development pack. No product lists any of them.
set(_tsfix_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(_tsfix_out "${RP_PACKS_DIR}/tsfix/1.0.0")
file(GLOB _tsfix_sources CONFIGURE_DEPENDS "${_tsfix_dir}/ts/*.ts")
add_custom_command(
    OUTPUT "${_tsfix_out}/pack.json"
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
add_custom_target(retroplug-ts-fixture-packs DEPENDS "${_tsfix_out}/pack.json")
# Every staged folder is claimed once (cmake/packs.cmake's RP_PACK_STAGED), so two fixtures cannot stage one id.
set_property(GLOBAL APPEND PROPERTY RP_PACK_STAGED "tsfix/1.0.0")

foreach(_v tsfix-throws tsfix-guard tsfix-undeclared tsfix-cliclash tsfix-dependent tsfix-protocol)
    set_property(GLOBAL APPEND PROPERTY RP_PACK_STAGED "${_v}/1.0.0")
    configure_file("${_tsfix_dir}/variants/${_v}/pack.json.in" "${RP_PACKS_DIR}/${_v}/1.0.0/pack.json" @ONLY)
    file(GLOB _v_js RELATIVE "${_tsfix_dir}/variants/${_v}" CONFIGURE_DEPENDS "${_tsfix_dir}/variants/${_v}/js/*.js")
    foreach(_js IN LISTS _v_js)
        configure_file("${_tsfix_dir}/variants/${_v}/${_js}" "${RP_PACKS_DIR}/${_v}/1.0.0/${_js}" COPYONLY)
    endforeach()
endforeach()
