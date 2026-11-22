# reflect-cpp (fetched by rpcpp's CPMAddPackage, pinned there) is patched at configure.
# MSVC 14.52 refuses `std::invoke_result_t<decltype(fn<...>), A>` when fn's return type is
# `auto` (C3539), which view_t.hpp, processed_t.hpp and the other aliases in the patch use.
# The tracked patch takes decltype of a CALL instead; same types, no MSVC refusal. Applied
# idempotently, as cmake/sameboy.cmake does; a pin bump that invalidates it fails the configure.
# Include right after dpf.js is added (CPM has set reflectcpp_SOURCE_DIR by then).
find_package(Git QUIET)
if(NOT GIT_FOUND)
    message(FATAL_ERROR "git is required to apply cmake/patches/reflectcpp-msvc-auto-decltype.patch")
endif()
if(NOT reflectcpp_SOURCE_DIR OR NOT EXISTS "${reflectcpp_SOURCE_DIR}/include/rfl/view_t.hpp")
    message(FATAL_ERROR
        "reflectcpp_SOURCE_DIR is unset or has no include/rfl/view_t.hpp: the reflect-cpp patch "
        "must be included after dpf.js (rpcpp's CPMAddPackage) is added.")
endif()
set(_RFL_PATCH "${CMAKE_SOURCE_DIR}/cmake/patches/reflectcpp-msvc-auto-decltype.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_RFL_PATCH}")
# `git apply --reverse --check` succeeds only when the patch is already present.
execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${_RFL_PATCH}"
    WORKING_DIRECTORY "${reflectcpp_SOURCE_DIR}"
    RESULT_VARIABLE _RFL_PATCH_PRESENT
    OUTPUT_QUIET ERROR_QUIET)
if(_RFL_PATCH_PRESENT EQUAL 0)
    message(STATUS "reflect-cpp: MSVC auto-decltype patch already present")
else()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --check "${_RFL_PATCH}"
        WORKING_DIRECTORY "${reflectcpp_SOURCE_DIR}"
        RESULT_VARIABLE _RFL_PATCH_OK
        OUTPUT_VARIABLE _RFL_PATCH_MSG ERROR_VARIABLE _RFL_PATCH_MSG)
    if(NOT _RFL_PATCH_OK EQUAL 0)
        message(FATAL_ERROR
            "The reflect-cpp patch no longer applies to ${reflectcpp_SOURCE_DIR} (did the CPM pin "
            "move?). Regenerate ${_RFL_PATCH}.\n${_RFL_PATCH_MSG}")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply "${_RFL_PATCH}"
        WORKING_DIRECTORY "${reflectcpp_SOURCE_DIR}"
        RESULT_VARIABLE _RFL_PATCH_APPLIED)
    if(NOT _RFL_PATCH_APPLIED EQUAL 0)
        message(FATAL_ERROR "Failed to apply reflect-cpp patch ${_RFL_PATCH}")
    endif()
    message(STATUS "reflect-cpp: applied MSVC auto-decltype patch")
endif()
