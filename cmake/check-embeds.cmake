# cmake -DRP_EMBED_ALPHA=<package.json> -DRP_EMBED_BETA=<package.json> [-DRP_EMBED_ALPHA_NAME=<package>]
#       -DRP_EMBED_BETA_NAME=<package> -P cmake/check-embeds.cmake: reads two manifests as retroplug_package() reads them,
# in script mode, under the package names it is given (alpha and beta unless told otherwise, since the generated
# array's symbol is the package's and the id's), and asks for the embedded-resource table a product listing both would
# carry (cmake/packages.cmake's _retroplug_embed_entries), so the rules that a product may not carry one id twice, nor
# two entries whose symbols run together, are held by a fixture the way the manifest rules are
# (packages/native/cmake/tests.cmake): a rule that stopped firing fails the configure rather than going unnoticed.
cmake_minimum_required(VERSION 3.19)
if(NOT RP_EMBED_ALPHA OR NOT RP_EMBED_BETA)
    message(FATAL_ERROR "check-embeds.cmake: pass -DRP_EMBED_ALPHA=<package.json> -DRP_EMBED_BETA=<package.json>")
endif()
if(NOT RP_EMBED_ALPHA_NAME)
    set(RP_EMBED_ALPHA_NAME alpha)
endif()
if(NOT RP_EMBED_BETA_NAME)
    set(RP_EMBED_BETA_NAME beta)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/packages.cmake")
_retroplug_read_manifest(${RP_EMBED_ALPHA_NAME} "${RP_EMBED_ALPHA}")
_retroplug_read_manifest(${RP_EMBED_BETA_NAME} "${RP_EMBED_BETA}")
_retroplug_embed_entries(demo _rp_entries ${RP_EMBED_ALPHA_NAME} ${RP_EMBED_BETA_NAME})
message(STATUS "check-embeds.cmake: ${RP_EMBED_ALPHA} and ${RP_EMBED_BETA} read; the table holds ${_rp_entries}")
