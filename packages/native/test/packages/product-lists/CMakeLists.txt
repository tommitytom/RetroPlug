# retroplug_product()'s list rules and the subsequence rule the root configure applies, configured as a child project
# by packages/native/cmake/tests.cmake. Each is a refusal of the DECLARATION, so the fixture needs no package and no
# engine: one INTERFACE target for the product to attach to, and the list globals the root configure fills from the
# manifest. PRODUCT_LIST_CASE picks the shape:
#
#   list-both        LIST and PACKAGES together: refused, they are one choice
#   list-unknown     LIST naming a list the manifest does not declare: refused, naming it
#   packages-no-list PACKAGES naming packages that are no list's, in a build that makes bundles: refused, since the
#                    product's four bundles are built from a list's generated tree
#   subsequence-out-of-order  a list that reverses the desktop's order: refused by retroplug_check_product_lists()
#   subsequence-twice        a list naming a package twice: refused as a duplicate, not as "out of order"
cmake_minimum_required(VERSION 3.19)
project(retroplug_product_lists NONE)

if(NOT RP_ROOT)
    message(FATAL_ERROR "test/packages/product-lists/CMakeLists.txt: pass -DRP_ROOT=<the repository root>")
endif()
if(NOT PRODUCT_LIST_CASE)
    message(FATAL_ERROR "test/packages/product-lists/CMakeLists.txt: pass -DPRODUCT_LIST_CASE=<case>")
endif()

include("${RP_ROOT}/cmake/packages.cmake")

# The root configure's globals, as it reads them from the root manifest: a list is only judged against the desktop's.
set(RP_DESKTOP_PACKAGES "alpha;beta")
set(RP_PRODUCT_PACKAGES_desktop "alpha;beta")
set(RP_PRODUCT_PACKAGES_out-of-order "beta;alpha")
set(RP_PRODUCT_PACKAGES_twice "alpha;alpha")
# One list at a time, so the case under test is the one the rule reaches first (the rule walks the names in order).
if(PRODUCT_LIST_CASE STREQUAL "subsequence-twice")
    set(RP_PRODUCT_LIST_NAMES "desktop;twice")
else()
    set(RP_PRODUCT_LIST_NAMES "desktop;out-of-order")
endif()
# A package the product path finds, so `packages-no-list` reaches the bundle rule rather than the package guard: the
# real configure's table is retroplug_package()'s, which this fixture does not call.
set_property(GLOBAL PROPERTY RP_PACKAGES alpha)

# The bundle machinery, which the refusal of a PACKAGES product with no list needs to know the build makes bundles: the
# refusal is retroplug_product()'s and fires before this is called, so a case that reaches the stub has not refused.
function(retroplug_bundles name)
    message(FATAL_ERROR "test/packages/product-lists: retroplug_product() accepted ${name}, whose packages are no list")
endfunction()

if(PRODUCT_LIST_CASE STREQUAL "packages-no-list")
    # The product path runs to the bundle rule, so it needs a target it can add sources and include directories to.
    enable_language(CXX)
    add_library(rp_fixture_product STATIC fixture.cpp)
else()
    # Every other case is refused before the product touches a target.
    add_library(rp_fixture_product INTERFACE)
endif()

if(PRODUCT_LIST_CASE STREQUAL "list-both")
    retroplug_product(rp_fixture_product LIST desktop PACKAGES alpha)
elseif(PRODUCT_LIST_CASE STREQUAL "list-unknown")
    retroplug_product(rp_fixture_product LIST nowhere)
elseif(PRODUCT_LIST_CASE STREQUAL "packages-no-list")
    retroplug_product(rp_fixture_product PACKAGES alpha)
elseif(PRODUCT_LIST_CASE STREQUAL "subsequence-out-of-order")
    retroplug_check_product_lists()
elseif(PRODUCT_LIST_CASE STREQUAL "subsequence-twice")
    retroplug_check_product_lists()
else()
    message(FATAL_ERROR "test/packages/product-lists: no case ${PRODUCT_LIST_CASE}")
endif()
message(STATUS "product-lists/${PRODUCT_LIST_CASE} holds")
