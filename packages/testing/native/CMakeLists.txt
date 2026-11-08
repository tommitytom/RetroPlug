# The native half of @retroplug/testing: what test binaries of more than one package share, so no test binary reaches
# another package's test tree (this package's ts/ tree is the TS harness's half). Added by retroplug_package(testing)
# (cmake/packages.cmake), which no product lists: a test binary takes it through its package's devDependency on
# @retroplug/testing, and the native boundary check reads the same devDependencies.
#
# Two targets, because they are two different things to ask for. retroplug-testing is the headers (INTERFACE), and
# src/ is their include root, so the helpers keep the paths they are included by ("graph/CountingAllocator.hpp",
# "LinkRomModel.hpp") wherever a test sits. LinkRomModel.hpp is header-only: the reliable link's ROM rules, driven by
# both halves' test binaries (the N8 pack's and the Mesen package's), and a test that wants nothing else links this
# library alone.
#
# retroplug-testing-allocator is the OBJECT library holding graph/CountingAllocator.cpp: an object library, not a
# static one, because it replaces the global operator new and delete, and an archive member is linked only when it
# answers an undefined symbol, which an allocation never does. Every object goes into each binary that links it. It is
# a target of its own so a test that only needs a header does not silently run under a replaced allocator (and, on
# Windows, under _aligned_malloc), which is what `retroplug-n8-test` used to do.
#
# EXCLUDE_FROM_ALL: nothing the default build needs links either (only test binaries, which are built by name), and a
# library or product that did would be refused by the configure (retroplug_check_test_helpers). A test binary that
# links one still builds it.
add_library(retroplug-testing INTERFACE)
target_include_directories(retroplug-testing INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(retroplug-testing-allocator OBJECT EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/CountingAllocator.cpp)
set_target_properties(retroplug-testing-allocator PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
# PUBLIC: the allocator's own source includes CountingAllocator.hpp, and every test binary that links it gets the header
# root with it (a no-allocation case includes both).
target_link_libraries(retroplug-testing-allocator PUBLIC retroplug-testing)
if(MSVC)
    target_compile_options(retroplug-testing-allocator PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/bigobj>)
endif()
