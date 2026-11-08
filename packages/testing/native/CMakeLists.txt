# The native half of @retroplug/testing: what test binaries of more than one package share, so no test binary reaches
# another package's test tree (this package's ts/ tree is the TS harness's half). Added by retroplug_package(testing)
# (cmake/packages.cmake), which no product lists: a test binary takes it through its package's devDependency on
# @retroplug/testing, and the native boundary check reads the same devDependencies.
#
# An OBJECT library, not a static one: CountingAllocator.cpp replaces the global operator new and delete, and an archive
# member is linked only when it answers an undefined symbol, which an allocation never does. Every object goes into each
# binary that links the target.
#
# src/ is the include root, so the helpers keep the paths they are included by ("graph/CountingAllocator.hpp") wherever
# a test sits. LinkRomModel.hpp is header-only: the reliable link's ROM rules, driven by both halves' test binaries
# (the N8 pack's and the Mesen package's).
#
# EXCLUDE_FROM_ALL: nothing the default build needs links it (only test binaries, which are built by name), and a library
# that did would be refused by the configure (retroplug_check_test_helpers). A test binary that links it still builds it.
add_library(retroplug-testing OBJECT EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/CountingAllocator.cpp)
set_target_properties(retroplug-testing PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
# PUBLIC: the helpers keep the paths they are included by, in the library's own source and in every test binary's.
target_include_directories(retroplug-testing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(retroplug-testing PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/bigobj>)
endif()
