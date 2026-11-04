# cmake -DRP_MANIFEST=<package.json> -P cmake/check-manifest.cmake: reads one manifest as retroplug_package() reads a
# package's (cmake/packages.cmake's _retroplug_read_manifest), in script mode, and fails as the configure would. The
# configure runs it over the scratch manifests a configure must refuse (packages/native/test/packages/refused, held by
# packages/native/cmake/tests.cmake), so a rule that stopped firing fails the configure rather than going unnoticed.
cmake_minimum_required(VERSION 3.19)
if(NOT RP_MANIFEST)
    message(FATAL_ERROR "check-manifest.cmake: pass -DRP_MANIFEST=<package.json>")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/packages.cmake")
_retroplug_read_manifest(scratch "${RP_MANIFEST}")
message(STATUS "check-manifest.cmake: ${RP_MANIFEST} reads")
