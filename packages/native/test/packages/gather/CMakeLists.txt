# The native boundary gatherer's own fixture (cmake/packages.cmake's retroplug_check_package_boundaries), driven by
# packages/native/cmake/tests.cmake at every configure. It is a project of its own, configured as a child process, with
# INTERFACE targets under a packages/ tree of its own: the gatherer is the part a plain rule fixture cannot reach, because
# it reads SOURCE_DIR and the build's targets, and script mode has none.
#
# RP_GATHER_CASE picks the shape:
#
#   later        alpha links a helper beta defines AFTER alpha, which alpha does not declare: refused
#   shell        alpha links the shell's backend: refused
#   dev-library  alpha's library links its devDependency: refused
#   declared     alpha links beta's manifest target, declared: holds
#   test-dev     alpha's TEST target links its devDependency: holds
cmake_minimum_required(VERSION 3.19)
project(retroplug_package_gather NONE)

if(NOT RP_GATHER_CASE)
    message(FATAL_ERROR "gather/CMakeLists.txt: pass -DRP_GATHER_CASE=<case>")
endif()
if(NOT RP_ROOT)
    message(FATAL_ERROR "gather/CMakeLists.txt: pass -DRP_ROOT=<the repository root>")
endif()

include("${RP_ROOT}/cmake/packages.cmake")
# The fixture's packages tree, not this checkout's: the gatherer attributes a target by its SOURCE_DIR under this.
set(RP_PACKAGES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/packages")
set_property(GLOBAL PROPERTY RP_PACKAGES alpha beta testing native)
if(RP_GATHER_CASE STREQUAL "later")
    set_property(GLOBAL PROPERTY RP_PACKAGE_alpha_DECLARATIONS "")
else()
    set_property(GLOBAL PROPERTY RP_PACKAGE_alpha_DECLARATIONS beta)
endif()
set_property(GLOBAL PROPERTY RP_PACKAGE_alpha_DEV_DECLARATIONS testing)
set_property(GLOBAL PROPERTY RP_PACKAGE_beta_DECLARATIONS "")
set_property(GLOBAL PROPERTY RP_PACKAGE_beta_DEV_DECLARATIONS "")

add_subdirectory(packages/alpha)
add_subdirectory(packages/beta)
add_subdirectory(packages/testing)
add_subdirectory(packages/native)

retroplug_check_package_boundaries()
message(STATUS "gather/${RP_GATHER_CASE} holds")
