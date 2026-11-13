# The blastem package's native half: the Mega Drive core over the vendored BlastEm library, its shim, its ROM gate, the
# build function rp.blastem.md's request carries, and the node type. Added by retroplug_package(blastem)
# (cmake/packages.cmake).

retroplug_package_library(retroplug-blastem
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/blastem/BlastemShim.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/blastem/BlastemNode.cpp)
target_include_directories(retroplug-blastem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-blastem
    PUBLIC  retroplug-system    # SystemBase, the build request, the host's system services
            retroplug-node-sdk  # graph/node.hpp
            blastem)            # deps/blastem: the emulator (which carries -lm on the POSIX toolchains)

# The link order is the point of naming blastem here beside the package: an archive's undefined symbols are answered
# only by what the linker reads after it, and nothing in the case names a shim symbol directly, so reading the core
# first lets the shim (retroplug-blastem's own object, holding the frontend globals and callbacks blastem calls) answer
# the core's references instead of leaving them undefined.
retroplug_add_test_binary(
    NAME     retroplug-blastem-test
    SOURCES  test/BlastemBuild.test.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test   # SyntheticRom.hpp, a later task's images
    LIBS     blastem          # deps/blastem: read before the package, so its references reach the shim
         retroplug-blastem)
