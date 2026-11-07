# cmake -DRP_BOUNDARY_FIXTURE=<fixture.json> -P cmake/check-native-boundaries.cmake: runs one package's native
# boundary check (cmake/packages.cmake's _retroplug_check_native_boundaries) in script mode, over the scratch package a
# fixture describes, and fails as the configure would. The configure runs it over the fixtures a package must be refused
# for (packages/native/test/packages/refused-native, held by packages/native/cmake/tests.cmake), so the rule cannot stop
# firing unnoticed.
#
# A fixture is plain data, because the real check reads target properties and script mode has no target:
#
#   { "package": "scratch", "declared": ["node-sdk"], "devDeclared": ["testing"], "test": false,
#     "links": ["scratch-nodes=retroplug-graph"], "includes": ["scratch-nodes=@PACKAGES@/graph-runtime/native/src"],
#     "sources": ["scratch-nodes=@PACKAGES@/graph-runtime/native/foo.cpp"],
#     "unjudged": ["scratch-nodes=$<TARGET_PROPERTY:reflectcpp,INTERFACE_INCLUDE_DIRECTORIES>"],
#     "owners": ["retroplug-graph=graph-runtime", "retroplug-node-sdk=node-sdk"] }
#
# "test" marks the fixture's target a test binary (retroplug_add_test_binary's): only one may link a devDependency.
#
# "@PACKAGES@" is the packages directory, so a fixture's include directory is a real path under this tree's packages/.
cmake_minimum_required(VERSION 3.19)
if(NOT RP_BOUNDARY_FIXTURE)
    message(FATAL_ERROR "check-native-boundaries.cmake: pass -DRP_BOUNDARY_FIXTURE=<fixture.json>")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/packages.cmake")

file(READ "${RP_BOUNDARY_FIXTURE}" _json)

# One member of the fixture as a CMake list: an array of strings, or "" when the fixture leaves it out.
function(_rp_fixture_list out json member)
    string(JSON _count ERROR_VARIABLE _none LENGTH "${json}" ${member})
    set(_values "")
    if(NOT _none)
        if(_count GREATER 0)
            math(EXPR _last "${_count} - 1")
            foreach(_i RANGE ${_last})
                string(JSON _value GET "${json}" ${member} ${_i})
                string(REPLACE "@PACKAGES@" "${RP_PACKAGES_DIR}" _value "${_value}")
                list(APPEND _values "${_value}")
            endforeach()
        endif()
    endif()
    set(${out} "${_values}" PARENT_SCOPE)
endfunction()

string(JSON _package GET "${_json}" package)
_rp_fixture_list(_declared "${_json}" declared)
_rp_fixture_list(_dev "${_json}" devDeclared)
_rp_fixture_list(_links "${_json}" links)
_rp_fixture_list(_includes "${_json}" includes)
_rp_fixture_list(_sources "${_json}" sources)
_rp_fixture_list(_unjudged "${_json}" unjudged)
_rp_fixture_list(_owners "${_json}" owners)
string(JSON _test ERROR_VARIABLE _noTest GET "${_json}" test)
set(_test_arg "")
if(NOT _noTest AND _test)
    set(_test_arg TEST)
endif()
_retroplug_check_native_boundaries(${_package} LINKS ${_links} INCLUDES ${_includes} SOURCES ${_sources}
                                   UNJUDGED ${_unjudged} DECLARED ${_declared} DEV_DECLARED ${_dev} ${_test_arg}
                                   OWNERS ${_owners})
message(STATUS "check-native-boundaries.cmake: ${RP_BOUNDARY_FIXTURE} holds")
