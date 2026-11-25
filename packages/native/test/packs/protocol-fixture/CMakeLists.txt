# The protocol fixture packs (docs/downloadable-packs-m2e-plan.md): hand-written, each a pack.json and a global-code node
# bundle in the shape tools/build-pack-halves.mjs writes one, staged under build/packs/<id>/1.0.0/ as development packs
# at configure time. No product lists any of them.
#   protofix             rp.protofix.up (MIDI in, each message's second byte 12 up, out as host MIDI) and rp.protofix.idle
#   protofix-undeclared  its bundle defines a type its protocol.nodes does not declare
#   protofix-missing     its protocol.nodes declares a type its bundle does not define
#   protofix-clash       it defines rp.midi.route, a built-in script node
#   protofix-syntax      a bundle that does not compile
#   protofix-throws      a bundle that throws as it loads
foreach(_v protofix protofix-undeclared protofix-missing protofix-clash protofix-syntax protofix-throws)
    get_property(_staged GLOBAL PROPERTY RP_PACK_STAGED)
    if("${_v}/1.0.0" IN_LIST _staged)
        message(FATAL_ERROR "the protocol fixture pack ${_v}/1.0.0 is already staged by another target")
    endif()
    set_property(GLOBAL APPEND PROPERTY RP_PACK_STAGED "${_v}/1.0.0")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${_v}/pack.json.in" "${RP_PACKS_DIR}/${_v}/1.0.0/pack.json" @ONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${_v}/js/protocol.js" "${RP_PACKS_DIR}/${_v}/1.0.0/js/protocol.js"
                   COPYONLY)
endforeach()
