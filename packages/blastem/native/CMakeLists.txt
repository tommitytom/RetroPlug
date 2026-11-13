# The blastem package's native half: the Mega Drive core over the vendored BlastEm library, its shim, its ROM gate, the
# build function rp.blastem.md's request carries, and the node type. Added by retroplug_package(blastem)
# (cmake/packages.cmake).

retroplug_package_library(retroplug-blastem
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/blastem/BlastemShim.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/blastem/BlastemAudio.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/blastem/BlastemMdSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/blastem/BlastemMemory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/blastem/BlastemPortDevice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/blastem/BlastemVideo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/blastem/BlastemBackend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/blastem/BlastemNode.cpp)
target_include_directories(retroplug-blastem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# The blip_buf and socket-seam renames, spelled as the core they feed is compiled (cmake/blastem.cmake owns the list and
# the reason it exists). BlastemAudio.cpp calls blip_buf and BlastemShim.cpp answers the socket seam; the rest of the
# target only sees the blip_t typedef, which no rename touches.
target_compile_definitions(retroplug-blastem PRIVATE ${BLASTEM_SEAM_RENAMES})
target_link_libraries(retroplug-blastem
    PUBLIC  retroplug-system    # SystemBase, the build request, the host's system services
            retroplug-node-sdk  # graph/node.hpp
            blastem)            # deps/blastem: the emulator (which carries -lm on the POSIX toolchains)

# The link order is not what makes this correct: the case names rp::blastem::currentInstance, the shim's own symbol, and
# that reference is what pulls BlastemShim.cpp.o (the frontend globals and callbacks blastem calls) out of the package's
# archive, so the core's references meet the shim whatever order the archives are read in. Listing blastem first and
# counting on the linker's order would instead rest on CMake keeping the first of two occurrences of one library, which
# is a policy (CMP0156) away from keeping the last.
retroplug_add_test_binary(
    NAME     retroplug-blastem-test
    SOURCES  test/BlastemBuild.test.cpp test/SyntheticRom.test.cpp test/BlastemBoot.test.cpp
             test/BlastemStop.test.cpp test/BlastemVideo.test.cpp test/BlastemAudio.test.cpp
             test/BlastemPort.test.cpp test/BlastemInvariance.test.cpp test/BlastemInstances.test.cpp
             test/BlastemMemory.test.cpp test/BlastemSaves.test.cpp test/BlastemFaults.test.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test   # SyntheticRom.hpp, a later task's images
    LIBS     retroplug-blastem)                 # blastem is its PUBLIC dependency, and is read after it
