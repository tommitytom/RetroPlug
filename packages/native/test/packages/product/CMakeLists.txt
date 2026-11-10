# A product that embeds nothing, configured as a child project by packages/native/cmake/tests.cmake: it generates the
# product source for a package list with no `embed` entry, compiles it, and runs it. No product in the tree has that
# shape (every desktop product lists mgb, whose ROM is an embed), so cmake/packages.cmake's `count = 0` branch would
# otherwise be compiled by nobody. The source is the same one a real product is generated from, so a branch that stops
# compiling, or that answers a count other than 0, fails here.
#
# The package is a fixture: its global properties are what retroplug_package() sets for a manifest with one native entry
# and no `embed`, and its target is a scratch library whose sources and links the branch under test never reads.
cmake_minimum_required(VERSION 3.19)
project(retroplug_product_no_embeds CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# One output directory for every configuration, so the caller names the check's path once (a multi-config generator would
# otherwise put it under a per-config directory).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)  # the per-config suffix is upper case
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_config} "${CMAKE_BINARY_DIR}/bin")
endforeach()

if(NOT RP_ROOT)
    message(FATAL_ERROR "test/packages/product/CMakeLists.txt: pass -DRP_ROOT=<the repository root>")
endif()

# This fixture builds no engine and embeds no bundle, so it declares that it has no bundle machinery: without this,
# retroplug_product() refuses a non-DEVICE product whose bundles are not defined yet (which is the real tree's
# ordering rule, not this fixture's shape).
set(RP_NO_PRODUCT_BUNDLES TRUE)
include("${RP_ROOT}/cmake/packages.cmake")

add_library(rp_fixture_alpha INTERFACE)
add_library(rp_fixture_entry STATIC fixtureEntry.cpp)
# graph/Packages.hpp is the graph runtime's and graph/rp_node.h the node SDK's; the two include roots are all the
# generated source and the fixture's entry read.
set(_rp_include_roots "${RP_ROOT}/packages/graph-runtime/native/src" "${RP_ROOT}/packages/node-sdk/native/src")
target_include_directories(rp_fixture_entry PRIVATE ${_rp_include_roots})

set_property(GLOBAL PROPERTY RP_PACKAGES alpha)
set_property(GLOBAL PROPERTY RP_PACKAGE_alpha_ENTRIES rp_fixture_entry)
set_property(GLOBAL PROPERTY RP_PACKAGE_alpha_TARGETS rp_fixture_alpha rp_fixture_entry)
# No `embed`: _retroplug_embed_entries() finds nothing for this package, and the generated table takes the branch that
# answers a count of 0. Setting the properties explicitly states the fixture's shape rather than leaving it unsaid.
set_property(GLOBAL PROPERTY RP_PACKAGE_alpha_EMBED_IDS "")
set_property(GLOBAL PROPERTY RP_PACKAGE_alpha_EMBED_SYMBOLS "")

add_library(rp_fixture_product STATIC)
retroplug_product(rp_fixture_product PACKAGES alpha)

add_executable(rp_fixture_product_check main.cpp)
target_include_directories(rp_fixture_product PRIVATE ${_rp_include_roots})
target_include_directories(rp_fixture_product_check PRIVATE ${_rp_include_roots})
target_link_libraries(rp_fixture_product_check PRIVATE rp_fixture_product)
