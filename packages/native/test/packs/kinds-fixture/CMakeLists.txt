# The kinds fixture packs (docs/downloadable-packs-m2f-plan.md): hand-written, each a pack.json and a global-code kinds
# half in the shape tools/build-pack-halves.mjs writes one, staged under build/packs/<id>/1.0.0/ as development packs at
# configure time. No product lists any of them.
#   kindfix          registers one controller app, kindfix-app; its target sends CC 127 = 42 once, when the session
#                    builds it, so a document driving the session out of the host sees the runtime ran
#   kindfix-throws   its bundle throws as it loads
#   kindfix-taken    registers lsdj-midimap, a built-in app's id
#   kindfix-unknown  registers items of a kind no package declares, nope
foreach(_v kindfix kindfix-throws kindfix-taken kindfix-unknown)
    get_property(_staged GLOBAL PROPERTY RP_PACK_STAGED)
    if("${_v}/1.0.0" IN_LIST _staged)
        message(FATAL_ERROR "the kinds fixture pack ${_v}/1.0.0 is already staged by another target")
    endif()
    set_property(GLOBAL APPEND PROPERTY RP_PACK_STAGED "${_v}/1.0.0")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${_v}/pack.json.in" "${RP_PACKS_DIR}/${_v}/1.0.0/pack.json" @ONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${_v}/js/kinds.js" "${RP_PACKS_DIR}/${_v}/1.0.0/js/kinds.js" COPYONLY)
endforeach()
