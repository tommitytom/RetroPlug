# The link trap's second half (CMakeLists.txt): run on the linked image, it fails the build if the image defines the
# wrapper of any trapped symbol. --wrap turns each reference into a reference to __wrap_<symbol>, which only an
# undefined wrapper makes a link error; a library that defines one (the Pico SDK's pico_malloc defines the malloc
# family's, with a heap behind them) would let every use through.
#
# cmake -DNM=<nm> -DIMAGE=<elf> -DTRAPPED=<file, one symbol a line> -P check-trap.cmake
execute_process(COMMAND "${NM}" --defined-only "${IMAGE}" OUTPUT_VARIABLE _defined RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "check-trap: ${NM} could not read ${IMAGE}")
endif()
file(STRINGS "${TRAPPED}" _trapped)
set(_found "")
foreach(_symbol IN LISTS _trapped)
    string(FIND "${_defined}" " __wrap_${_symbol}\n" _at)
    if(NOT _at EQUAL -1)
        list(APPEND _found "__wrap_${_symbol}")
    endif()
endforeach()
if(_found)
    list(JOIN _found ", " _found)
    message(FATAL_ERROR "check-trap: ${IMAGE} defines ${_found}, so the link trap lets those uses through. Whatever "
                        "defines them must leave the image (see the rp-device-runtime comment in CMakeLists.txt).")
endif()
