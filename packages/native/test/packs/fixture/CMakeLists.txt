# The fixture pack (docs/downloadable-packs-m1-plan.md): a pack built in this tree, from FixtureNodes.cpp, and its
# deliberately broken variants, each built from the same source and differing from the good pack in one way. Each is
# staged under build/packs/<id>/1.0.0/ (cmake/packs.cmake), and retroplug-packs-test loads them from there. No product
# lists any of them: a pack is loaded, never linked.
#
#   packfixture               the good pack: rp.packfixture.tone and .gain, over a static library as a package's are
#   packfixture-dependent     requires.packs ["packfixture"]: loads after it, refused without it
#   packfixture-api-newer     built for the next API level: refused, "update RetroPlug"
#   packfixture-api-older     built for the previous one: refused, "update the pack"
#   packfixture-api-mismatch  its pack.json says this level, its library reports the next: refused by the entry
#   packfixture-undeclared    registers a third type its provides.nodeTypes leaves out
#   packfixture-overdeclared  declares a third type in provides.nodeTypes that its library does not register
#   packfixture-throws        a library whose entry throws (throws/: its own directory, for MSVC's /EHs)
#   packfixture-pair          two packages from one library (FixtureSecond.cpp), the second taking the good pack's types
#   packfixture-tshalf        a manifest with a TS half beside its native one, which M1 does not load
#   packfixture-duplicate     registers rp.core.add, a built-in's type
#   packfixture-clone         registers the good pack's two types under another package id
#   packfixture-noplatform    a manifest with no library for any platform
#   packfixture-noentry       a library that exports no rp_pack_entry

set(_rp_fixture_dir ${CMAKE_CURRENT_SOURCE_DIR})
set(_rp_fixture_source ${_rp_fixture_dir}/FixtureNodes.cpp)

# The good pack's types as a static library, as a package's native target is: its module links it, and
# retroplug-packs-test links it too, to hold two copies of one package in one process (the module's stays hidden).
retroplug_package_library(retroplug-packfixture-nodes ${_rp_fixture_source})
target_link_libraries(retroplug-packfixture-nodes PUBLIC retroplug-node-sdk)

retroplug_pack_module(retroplug-packfixture-pack
    MANIFEST ${_rp_fixture_dir}/manifests/packfixture.json
    LIBS     retroplug-packfixture-nodes
    ENTRIES  rp_packfixture_package)

# A variant: the fixture source compiled straight into its module, its types (PREFIX) and its package (PACKAGE) named
# after the variant unless it says otherwise. PAIR adds FixtureSecond.cpp's package after the first; SOURCES adds files
# of the variant's own.
function(_rp_fixture_variant id)
    cmake_parse_arguments(V "PAIR" "PREFIX;PACKAGE" "DEFINES;ARGS;SOURCES" ${ARGN})
    if(NOT V_PREFIX)
        set(V_PREFIX "rp.${id}")
    endif()
    if(NOT V_PACKAGE)
        set(V_PACKAGE "rp.${id}")
    endif()
    set(_sources ${_rp_fixture_source} ${V_SOURCES})
    set(_entries rp_packfixture_package)
    if(V_PAIR)
        list(APPEND _sources ${_rp_fixture_dir}/FixtureSecond.cpp)
        list(APPEND _entries rp_packfixture_second_package)
    endif()
    retroplug_pack_module(retroplug-${id}-pack
        MANIFEST ${_rp_fixture_dir}/manifests/${id}.json
        SOURCES  ${_sources}
        LIBS     retroplug-node-sdk
        DEFINES  RP_PACKFIXTURE_PREFIX="${V_PREFIX}" RP_PACKFIXTURE_PACKAGE="${V_PACKAGE}" ${V_DEFINES}
        ENTRIES  ${_entries}
        ${V_ARGS})
    set_property(GLOBAL APPEND PROPERTY RP_PACKFIXTURE_TARGETS retroplug-${id}-pack)
endfunction()

math(EXPR _rp_level_newer "${RP_API_LEVEL} + 1")
math(EXPR _rp_level_older "${RP_API_LEVEL} - 1")
set_property(GLOBAL PROPERTY RP_PACKFIXTURE_TARGETS retroplug-packfixture-pack)
_rp_fixture_variant(packfixture-dependent)
_rp_fixture_variant(packfixture-api-newer ARGS API_LEVEL ${_rp_level_newer})
_rp_fixture_variant(packfixture-api-older ARGS API_LEVEL ${_rp_level_older})
_rp_fixture_variant(packfixture-api-mismatch ARGS ENTRY_API_LEVEL ${_rp_level_newer})
_rp_fixture_variant(packfixture-undeclared DEFINES RP_PACKFIXTURE_EXTRA_TYPE)
_rp_fixture_variant(packfixture-duplicate DEFINES RP_PACKFIXTURE_TONE_ID="rp.core.add")
_rp_fixture_variant(packfixture-clone PREFIX rp.packfixture)
_rp_fixture_variant(packfixture-noentry ARGS NO_ENTRY)
_rp_fixture_variant(packfixture-overdeclared)
add_subdirectory(throws)
_rp_fixture_variant(packfixture-pair PAIR)
# The manifests alone: retroplug_pack_module defines no target for them.
retroplug_pack_module(retroplug-packfixture-noplatform-pack
    MANIFEST ${_rp_fixture_dir}/manifests/packfixture-noplatform.json
    NO_LIBRARY)
retroplug_pack_module(retroplug-packfixture-tshalf-pack
    MANIFEST ${_rp_fixture_dir}/manifests/packfixture-tshalf.json
    NO_LIBRARY)
