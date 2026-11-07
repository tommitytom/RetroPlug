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
#   genex        alpha links $<BUILD_INTERFACE:beta's helper>, which it does not declare: refused
#   genex-declared  $<LINK_ONLY:> and $<BUILD_INTERFACE:> around beta's target and include directory: holds
#   interface-include  alpha's INTERFACE_INCLUDE_DIRECTORIES is beta's tree: refused
#   source-outside  alpha's target compiles beta's source: refused
#   alias        alpha links rp::beta, an alias of beta's target, which alpha does not declare: refused
#   alias-declared  the same alias with beta declared: holds (an alias is judged as the target behind it)
#   copt         alpha's compile options add -Ibeta's tree: refused like an include directory would be
#   copt-genex   the same behind $<$<COMPILE_LANGUAGE:CXX>:...>: still the literal, so refused
#   copt-relative  the same spelled relative to alpha's own directory: the compiler resolves a relative flag from the
#                build generator's working directory, which this check cannot know, so it is refused as unjudged
#   copt-iquote  -iquote, and copt-idirafter -idirafter, copt-isystem -isystem, copt-external /external:I and copt-shell
#                SHELL:-I: every spelling an include flag takes, each judged like -I
#   link-path    alpha links a library by its path inside beta's tree: refused like a target of beta's
#   link-path-shell  the same for a library in the shell's tree
#   link-options-dir  alpha's LINK_OPTIONS add -L into beta's tree: refused like an include directory
#   link-options-name  alpha's LINK_OPTIONS name a library of beta's with -l: refused
#   link-options-name-shell  the same for a library of the shell's
#   unjudged     alpha links a generator expression the check cannot evaluate: refused
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
# What alpha's manifest declares: beta for the cases that reach it and must hold, nothing for the ones that must be
# refused (a reach it does not declare).
if(RP_GATHER_CASE STREQUAL "declared" OR RP_GATHER_CASE STREQUAL "genex-declared"
   OR RP_GATHER_CASE STREQUAL "alias-declared")
    set_property(GLOBAL PROPERTY RP_PACKAGE_alpha_DECLARATIONS beta)
else()
    set_property(GLOBAL PROPERTY RP_PACKAGE_alpha_DECLARATIONS "")
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
