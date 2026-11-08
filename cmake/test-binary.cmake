# retroplug_add_test_binary() — the shape every native test binary shares.
#
# There are fourteen of them (eleven run by `pnpm test:plugin`, two by `pnpm test:n8` and the
# hardware-only `retroplug-n8-hwtest`), and each was spelled out in full: the same
# EXCLUDE_FROM_ALL, the same C++20 pair, the same output directory, the same Catch2 link.
# Nine tenths of each block was boilerplate, which buried the one or two lines that were
# actually specific to the test.
#
# What stays per-target is what genuinely differs: its sources, what it links, what it needs on
# the include path, and any fixture paths baked in as defines.
#
#   retroplug_add_test_binary(NAME     retroplug-audio-test
#                             SOURCES  test/audio/SameBoyStems.test.cpp ...
#                             LIBS     retroplug-backend
#                             INCLUDES ${RP_QUICKJS_INCLUDE}
#                             DEFS     RP_MGB_ROM_PATH="...")
#
# PACKAGES makes the binary a product (retroplug_product, cmake/packages.cmake): one that constructs an Engine lists
# the node packages that engine registers, and gets its generated rp_register_packages(). PROVIDERS names the packages
# of those whose provider the product starts (a test that starts one, or links the desktop half of one), as a product's
# own would.
#
# NO_CATCH2 opts out of the Catch2 link for a binary that brings its own main() —
# retroplug-n8-hwtest is the one, a manual harness driven against real hardware.
#
# Shared by packages/native (cmake/tests.cmake) and the packs (packages/n8/native), so the root
# CMakeLists includes it before the first retroplug_package(). Relative sources resolve against the
# CALLING directory (CMAKE_CURRENT_SOURCE_DIR where the call is made, as add_executable reads it), so
# each caller writes its paths from its own tree.
function(retroplug_add_test_binary)
    cmake_parse_arguments(T "NO_CATCH2" "NAME" "SOURCES;LIBS;INCLUDES;DEFS;PACKAGES;PROVIDERS" ${ARGN})

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

    if(T_PACKAGES)
        retroplug_product(${T_NAME} PACKAGES ${T_PACKAGES} PROVIDERS ${T_PROVIDERS})
    endif()

    set_target_properties(${T_NAME} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        # A test binary may link its package's devDependencies, as a test file may import them (cmake/packages.cmake's
        # boundary check reads this).
        RP_TEST_BINARY ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # A package's test binary sits in the package's directory, so it does not inherit the directory-wide /bigobj that
    # packages/native's own tests get, and reflect-cpp's instantiation passes COFF's 2^16 section limit (C1128) in any
    # TU leaning on it. /bigobj only lifts that limit, so every test binary carries it where it means anything.
    if(MSVC)
        target_compile_options(${T_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/bigobj>)
    endif()
endfunction()
