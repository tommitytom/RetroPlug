# Holds MicroQuickJS's library to its renamed external names (graph-runtime's script/rp_mqjs_names.h): every symbol
# the library defines with external linkage must be one of RetroPlug's, rp_mqjs_<name>, since a desktop binary links
# the engine beside QuickJS, which defines many of upstream's own names. A name an upstream bump adds and the header
# does not rename fails the build here, naming it, rather than at the link of every product.
#
#   cmake -DNM=<nm> -DLIBRARY=<archive> -P cmake/mquickjs-names.cmake
execute_process(COMMAND "${NM}" -g --defined-only "${LIBRARY}" OUTPUT_VARIABLE _out RESULT_VARIABLE _rc ERROR_QUIET)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "mquickjs-names: ${NM} could not read ${LIBRARY}")
endif()
string(REPLACE "\n" ";" _lines "${_out}")
set(_stray "")
set(_ours 0)
foreach(_line IN LISTS _lines)
    # "<address> <type> <name>": a defined name of external linkage (Mach-O's names lead with an underscore).
    if(_line MATCHES "^[0-9A-Fa-f]+ [A-Za-z] _?([A-Za-z_][A-Za-z0-9_]*)$")
        if(CMAKE_MATCH_1 MATCHES "^rp_mqjs_")
            math(EXPR _ours "${_ours} + 1")
        else()
            list(APPEND _stray "${CMAKE_MATCH_1}")
        endif()
    endif()
endforeach()
# An nm that lists nothing it could parse would pass the loop above vacuously.
if(_ours EQUAL 0)
    message(FATAL_ERROR "mquickjs-names: ${NM} listed no rp_mqjs_ name defined in ${LIBRARY}, so nothing was checked")
endif()
if(_stray)
    list(REMOVE_DUPLICATES _stray)
    list(JOIN _stray " " _stray)
    message(FATAL_ERROR "mquickjs-names: ${LIBRARY} defines names that are not RetroPlug's (rename each in "
                        "packages/graph-runtime/native/src/script/rp_mqjs_names.h): ${_stray}")
endif()
