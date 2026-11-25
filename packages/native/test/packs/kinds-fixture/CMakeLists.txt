# The kinds fixture packs (docs/downloadable-packs-m2f-plan.md): hand-written, each a pack.json and a global-code kinds
# half in the shape tools/build-pack-halves.mjs writes one, staged under build/packs/<id>/1.0.0/ as development packs at
# configure time. No product lists any of them.
#   kindfix            registers one controller app, kindfix-app; its target sends CC 127 = 42 once, when the session
#                      builds it, so a document driving the session out of the host sees the runtime ran
#   kindfix-late       its target calls the registrar the Engine closed, and sends CC 127 = 1 if it threw, 0 if not
#   kindfix-throws     its bundle throws as it loads
#   kindfix-taken      registers lsdj-midimap, a built-in app's id
#   kindfix-unknown    registers items of a kind no package declares, nope
#   kindfix-twin       registers kindfix-app, the id kindfix registers
#   kindfix-undeclared registers an id its pack.json's kinds member does not list
#   kindfix-syntax     a bundle that does not compile
#   kindfix-missing    its halves.kinds names a file the pack does not have
foreach(_v kindfix kindfix-late kindfix-throws kindfix-taken kindfix-unknown kindfix-twin kindfix-undeclared
           kindfix-syntax kindfix-missing)
    get_property(_staged GLOBAL PROPERTY RP_PACK_STAGED)
    if("${_v}/1.0.0" IN_LIST _staged)
        message(FATAL_ERROR "the kinds fixture pack ${_v}/1.0.0 is already staged by another target")
    endif()
    set_property(GLOBAL APPEND PROPERTY RP_PACK_STAGED "${_v}/1.0.0")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${_v}/pack.json.in" "${RP_PACKS_DIR}/${_v}/1.0.0/pack.json" @ONLY)
    if(NOT _v STREQUAL "kindfix-missing")
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${_v}/js/kinds.js" "${RP_PACKS_DIR}/${_v}/1.0.0/js/kinds.js"
                       COPYONLY)
    endif()
endforeach()
