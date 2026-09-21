# retroplug_add_test_binary() — the shape every native test binary shares.
#
# There are ten of them (eight run by `pnpm test:plugin`, plus `retroplug-n8-test` and the
# hardware-only `retroplug-n8-hwtest`), and each was spelled out in full: the same
# EXCLUDE_FROM_ALL, the same C++20 pair, the same output directory, the same Catch2 link.
# Nine tenths of each block was boilerplate, which buried the one or two lines that were
# actually specific to the test.
#
# What stays per-target is what genuinely differs: its sources, what it links, what it needs on
# the include path, and any fixture paths baked in as defines.
#
#   retroplug_add_test_binary(NAME     retroplug-audio-test
#                             SOURCES  test/audio/ChannelStreams.test.cpp ...
#                             LIBS     retroplug-backend
#                             INCLUDES ${RP_QUICKJS_INCLUDE}
#                             DEFS     RP_MGB_ROM_PATH="...")
#
# NO_CATCH2 opts out of the Catch2 link for a binary that brings its own main() —
# retroplug-n8-hwtest is the one, a manual harness driven against real hardware.
#
# Sources are interpreted relative to packages/native/ (CMAKE_CURRENT_SOURCE_DIR at the point
# this is included), matching how they were written before.
function(retroplug_add_test_binary)
    cmake_parse_arguments(T "NO_CATCH2" "NAME" "SOURCES;LIBS;INCLUDES;DEFS" ${ARGN})

    if(NOT T_NAME)
        message(FATAL_ERROR "retroplug_add_test_binary: NAME is required")
    endif()
    if(NOT T_SOURCES)
        message(FATAL_ERROR "retroplug_add_test_binary(${T_NAME}): SOURCES is required")
    endif()
    if(T_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "retroplug_add_test_binary(${T_NAME}): unrecognised argument(s): ${T_UNPARSED_ARGUMENTS}")
    endif()

    add_executable(${T_NAME} EXCLUDE_FROM_ALL ${T_SOURCES})

    if(NOT T_NO_CATCH2)
        list(APPEND T_LIBS Catch2::Catch2WithMain)
    endif()
    if(T_LIBS)
        target_link_libraries(${T_NAME} PRIVATE ${T_LIBS})
    endif()
    if(T_INCLUDES)
        target_include_directories(${T_NAME} PRIVATE ${T_INCLUDES})
    endif()
    if(T_DEFS)
        target_compile_definitions(${T_NAME} PRIVATE ${T_DEFS})
    endif()

    set_target_properties(${T_NAME} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endfunction()
