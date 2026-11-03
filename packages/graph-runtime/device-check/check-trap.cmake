# The link trap's second half (CMakeLists.txt): run on the linked image, it fails the build if the image defines the
# wrapper of any trapped symbol. --wrap turns each reference into a reference to __wrap_<symbol>, which only an
# undefined wrapper makes a link error; a library that defines one (the Pico SDK's pico_malloc defines the malloc
# family's, with a heap behind them) would let every use through. It fails the build too if the image still refers to
# any __wrap_ symbol it does not define: that is a trapped use the link let through, which only a link option that
# tolerates an undefined symbol (--unresolved-symbols=ignore-all, --warn-unresolved-symbols, --noinhibit-exec) does,
# so the image's own link is a proof of the trap as well as the probe's.
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
                        "defines them must leave the image (see the runtime's comment in CMakeLists.txt).")
endif()

execute_process(COMMAND "${NM}" --undefined-only "${IMAGE}" OUTPUT_VARIABLE _undefined RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "check-trap: ${NM} could not read ${IMAGE}")
endif()
string(REGEX MATCHALL "__wrap_[A-Za-z0-9_.$]+" _referenced "${_undefined}")
if(_referenced)
    list(REMOVE_DUPLICATES _referenced)
    list(JOIN _referenced ", " _referenced)
    message(FATAL_ERROR "check-trap: ${IMAGE} refers to ${_referenced} and defines none of them: a trapped use that "
                        "linked, so a link option lets an undefined symbol through. The image must not link that way.")
endif()
