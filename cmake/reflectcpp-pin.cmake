# reflect-cpp for a device project (docs/modular-graph-phase-e2-design.md section 8): the RP2350 images build
# graph/node.hpp against reflect-cpp's headers (the field reflection graph/fields.hpp compiles against; nothing of it is
# linked), and they are their own CMake projects on the Pico SDK's toolchain, so they find it themselves rather than
# through the desktop's CPM. Included by packages/graph-runtime/device-check and pico/n8-host; sets
# RP_REFLECTCPP_INCLUDE_DIR, which packages/node-sdk's device build puts on its include path.
#
# The version is the desktop's pin, read from where the desktop pins it (rpcpp's CPMAddPackage), so the builds cannot
# drift apart. A checkout the caller names in RP_REFLECTCPP_SOURCE_DIR (the desktop build's, which CI has already
# fetched) must be that version; otherwise a shallow clone of its tag.
get_filename_component(_rp_pin_repo "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(READ "${_rp_pin_repo}/deps/dpf.js/deps/rpcpp/CMakeLists.txt" _rp_rpcpp)
string(REGEX MATCH "GITHUB_REPOSITORY[ \t]+getml/reflect-cpp[ \t\r\n]+VERSION[ \t]+([0-9.]+)" _rp_pin "${_rp_rpcpp}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "${PROJECT_NAME}: no reflect-cpp pin in deps/dpf.js/deps/rpcpp/CMakeLists.txt")
endif()
set(_rp_reflect_version "${CMAKE_MATCH_1}")
if(RP_REFLECTCPP_SOURCE_DIR)
    file(READ "${RP_REFLECTCPP_SOURCE_DIR}/CMakeLists.txt" _rp_reflect)
    string(REGEX MATCH "project\\(reflectcpp VERSION ([0-9.]+)" _rp_found "${_rp_reflect}")
    if(NOT CMAKE_MATCH_1 STREQUAL _rp_reflect_version)
        message(FATAL_ERROR "${PROJECT_NAME}: ${RP_REFLECTCPP_SOURCE_DIR} is reflect-cpp ${CMAKE_MATCH_1}, and the "
                            "desktop pins ${_rp_reflect_version}")
    endif()
    set(_rp_reflect_dir "${RP_REFLECTCPP_SOURCE_DIR}")
else()
    include(FetchContent)
    # SOURCE_SUBDIR names a directory the checkout does not have, so the clone is made available without its own
    # CMakeLists being added: the headers are all a device build takes.
    FetchContent_Declare(rp_reflectcpp
        GIT_REPOSITORY https://github.com/getml/reflect-cpp.git
        GIT_TAG        v${_rp_reflect_version}
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  rp-headers-only)
    FetchContent_MakeAvailable(rp_reflectcpp)
    set(_rp_reflect_dir "${rp_reflectcpp_SOURCE_DIR}")
endif()
set(RP_REFLECTCPP_INCLUDE_DIR "${_rp_reflect_dir}/include")
