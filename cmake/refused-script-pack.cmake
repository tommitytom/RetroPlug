# cmake -DNODE=<node> -DSCRIPT=<build-device-pack.mjs> -DENTRY=<index.ts> -DNAME=<pack> -DHEAP_BYTES=<n>
#       -DOUT=<dir> -DPACK_DIR=<dir> -DMQJS=<mqjs> -DRUNNER=<retroplug-mqjs-run> -P cmake/refused-script-pack.cmake:
# runs the device-pack toolchain over a pack it must refuse, and fails the build if it does not, naming the toolchain's
# own words. The toolchain is the fixture: a rule that stopped firing (a pack whose type promises a core it cannot build)
# would otherwise ship a node the engine lines up against a slot that stays null.
#
# It runs when the binary the pack is held for is built rather than at configure, because the toolchain needs the built
# mqjs compiler and runner (retroplug-mqjs-run) and Node.
foreach(_arg NODE SCRIPT ENTRY NAME HEAP_BYTES OUT PACK_DIR MQJS RUNNER)
    if(NOT ${_arg})
        message(FATAL_ERROR "refused-script-pack.cmake: pass -D${_arg}=<value>")
    endif()
endforeach()
execute_process(
    COMMAND "${NODE}" "${SCRIPT}" --entry "${ENTRY}" --name "${NAME}" --heap-bytes "${HEAP_BYTES}"
            --out-dir "${OUT}" --pack-dir "${PACK_DIR}" --mqjs "${MQJS}" --runner "${RUNNER}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _output)
if(_result EQUAL 0)
    message(FATAL_ERROR "refused-script-pack.cmake: the toolchain compiled the ${NAME} pack; the MicroQuickJS host "
                        "must refuse a type whose descriptor declares category \"system\":\n${_output}")
endif()
set(_reason "declares category \"system\", and a script node cannot build a core")
string(FIND "${_output}" "${_reason}" _at)
if(_at EQUAL -1)
    message(FATAL_ERROR "refused-script-pack.cmake: the toolchain refused the ${NAME} pack, but not with "
                        "\"${_reason}\":\n${_output}")
endif()
message(STATUS "refused-script-pack.cmake: the toolchain refused the ${NAME} pack, as it must")
