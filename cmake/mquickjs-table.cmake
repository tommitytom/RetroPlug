# cmake -DTOOL=<mqjs_stdlib> [-DARGS=<args>] -DOUTPUT=<file> -P cmake/mquickjs-table.cmake: runs MicroQuickJS's stdlib
# build tool, which prints the table it builds on stdout, into OUTPUT (cmake/mquickjs.cmake's tables). A script rather
# than a shell redirect, so the command is the same on every generator and platform; a tool that fails leaves no
# half-written table behind.
cmake_minimum_required(VERSION 3.19)
if(NOT TOOL OR NOT OUTPUT)
    message(FATAL_ERROR "mquickjs-table.cmake: pass -DTOOL=<mqjs_stdlib> and -DOUTPUT=<file>")
endif()
execute_process(COMMAND "${TOOL}" ${ARGS} OUTPUT_FILE "${OUTPUT}.new" RESULT_VARIABLE _result ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    file(REMOVE "${OUTPUT}.new")
    message(FATAL_ERROR "mquickjs-table.cmake: ${TOOL} ${ARGS} failed (${_result}):\n${_error}")
endif()
file(RENAME "${OUTPUT}.new" "${OUTPUT}")
