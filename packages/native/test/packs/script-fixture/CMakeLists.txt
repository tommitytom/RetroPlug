# The script fixture packs (docs/downloadable-packs-m2d-plan.md): devtest's device script pack as a pack's script half,
# its two ES5 bundles copied from the device-pack toolchain's output (retroplug-devtest-script-pack), and its broken
# variants, each staged under build/packs/<id>/1.0.0/ as a development pack. No product lists any of them.
#
#   scriptfix         the good pack: devtest's three types
#   scriptfix-twin    the same script under another id: its types are scriptfix's, so it is refused after it
#   scriptfix-types   declares two of the three types: the host's control context finds the third and refuses it
#   scriptfix-syntax  a whole pack that does not parse
#   scriptfix-nofile  a runtime file the pack does not have
set(_sf_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(_sf_from "${CMAKE_BINARY_DIR}/packages/devtest/script")
set(_sf_all "\"rp.devtest.script-transpose\", \"rp.devtest.script-nest\", \"rp.devtest.script-loop\"")
set(_sf_two "\"rp.devtest.script-transpose\", \"rp.devtest.script-nest\"")

function(_sf_claim id)
    get_property(_staged GLOBAL PROPERTY RP_PACK_STAGED)
    if("${id}/1.0.0" IN_LIST _staged)
        message(FATAL_ERROR "the script fixture pack ${id}/1.0.0 is already staged by another target")
    endif()
    set_property(GLOBAL APPEND PROPERTY RP_PACK_STAGED "${id}/1.0.0")
endfunction()

# One variant's pack.json, and (COPY) devtest's two bundles as its script half.
set(_sf_outputs "")
function(_sf_variant id)
    cmake_parse_arguments(V "COPY" "PROVIDES;TYPES;RUNTIME" "" ${ARGN})
    _sf_claim(${id})
    set(FIX_ID "${id}")
    set(FIX_PROVIDES "${V_PROVIDES}")
    set(FIX_TYPES "${V_TYPES}")
    set(FIX_RUNTIME "${V_RUNTIME}")
    set(_out "${RP_PACKS_DIR}/${id}/1.0.0")
    configure_file("${_sf_dir}/scriptfix.pack.json.in" "${_out}/pack.json" @ONLY)
    if(V_COPY)
        add_custom_command(
            OUTPUT "${_out}/js/script.js" "${_out}/js/script.device.js"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_out}/js"
            COMMAND ${CMAKE_COMMAND} -E copy "${_sf_from}/devtest.js" "${_out}/js/script.js"
            COMMAND ${CMAKE_COMMAND} -E copy "${_sf_from}/devtest.device.js" "${_out}/js/script.device.js"
            DEPENDS retroplug-devtest-script-pack "${_sf_from}/devtest.js" "${_sf_from}/devtest.device.js"
            COMMENT "Staging the script fixture pack ${id}"
            VERBATIM)
        set(_sf_outputs ${_sf_outputs} "${_out}/js/script.js" "${_out}/js/script.device.js" PARENT_SCOPE)
    endif()
endfunction()

_sf_variant(scriptfix COPY PROVIDES "${_sf_all}" TYPES "${_sf_all}" RUNTIME "js/script.device.js")
_sf_variant(scriptfix-twin COPY PROVIDES "${_sf_all}" TYPES "${_sf_all}" RUNTIME "js/script.device.js")
_sf_variant(scriptfix-types COPY PROVIDES "${_sf_two}" TYPES "${_sf_two}" RUNTIME "js/script.device.js")
_sf_variant(scriptfix-nofile COPY PROVIDES "${_sf_all}" TYPES "${_sf_all}" RUNTIME "js/missing.js")
_sf_variant(scriptfix-syntax PROVIDES "${_sf_all}" TYPES "${_sf_all}" RUNTIME "js/script.device.js")
foreach(_js script.js script.device.js)
    configure_file("${_sf_dir}/variants/scriptfix-syntax/js/${_js}" "${RP_PACKS_DIR}/scriptfix-syntax/1.0.0/js/${_js}"
                   COPYONLY)
endforeach()

add_custom_target(retroplug-script-fixture-packs DEPENDS ${_sf_outputs})
