# cmake -DTOOL=<mqjs_stdlib> [-DARGS=<args>] -DOUTPUT=<file> -P cmake/mquickjs-table.cmake: runs MicroQuickJS's stdlib
# build tool, which prints the table it builds on stdout, into OUTPUT (cmake/mquickjs.cmake's tables). A script rather
# than a shell redirect, so the command is the same on every generator and platform; a tool that fails leaves no
# half-written table behind.
cmake_minimum_required(VERSION 3.19)
if(NOT TOOL OR NOT OUTPUT)
    message(FATAL_ERROR "mquickjs-table.cmake: pass -DTOOL=<mqjs_stdlib> and -DOUTPUT=<file>")
endif()
# Leak detection off for the tool: in a sanitizer build it is instrumented as everything is, and upstream's build tool
# frees nothing before it exits, which LeakSanitizer would otherwise fail it for.
set(_asan "detect_leaks=0")
if(DEFINED ENV{ASAN_OPTIONS} AND NOT "$ENV{ASAN_OPTIONS}" STREQUAL "")
    set(_asan "$ENV{ASAN_OPTIONS}:detect_leaks=0")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "ASAN_OPTIONS=${_asan}" "${TOOL}" ${ARGS}
                OUTPUT_FILE "${OUTPUT}.new" RESULT_VARIABLE _result ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    file(REMOVE "${OUTPUT}.new")
    message(FATAL_ERROR "mquickjs-table.cmake: ${TOOL} ${ARGS} failed (${_result}):\n${_error}")
endif()
file(RENAME "${OUTPUT}.new" "${OUTPUT}")
