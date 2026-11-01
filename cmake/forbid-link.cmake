# --- retroplug_forbid_linking(<target>): no target in the tree may link a vendored target we compile ourselves --------
#
# Included by the root CMakeLists.txt and called after every target exists. RetroPlug builds r8brain's one non-header
# source (deps/r8brain/r8bbase.cpp) inside retroplug-dsp-resample, on its throwing allocator
# (packages/native/src/dsp/R8bAllocator.hpp, R8bBase.cpp): that file defines the filter, FFT and frac-bank caches every
# r8brain class shares, so building it twice would put two definitions of each class in the program and let the linker
# keep whichever copy it reaches first, silently, with no diagnostic. The dep's own target stays in the tree for its
# include directory; linking it is a configure error here rather than an ODR hazard at run time.
#
# A CMake target that does not exist cannot be linked by accident at all, so dropping the vendored target would enforce
# the same thing - but it would also drop its include interface and the one place that says the dep is still there for
# its headers. This check keeps both and makes the rule it states its own test.
function(retroplug_forbid_linking vendored)
    retroplug_forbid_linking_tree("${CMAKE_SOURCE_DIR}" "${vendored}")
endfunction()

function(retroplug_forbid_linking_tree dir vendored)
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _targets)
        if(NOT _target STREQUAL vendored)
            get_target_property(_links "${_target}" LINK_LIBRARIES)
            if(_links)
                list(FIND _links "${vendored}" _at)
                if(NOT _at EQUAL -1)
                    message(FATAL_ERROR
                        "${_target} links the vendored '${vendored}' target.\n"
                        "Every r8brain class in this program is built on rp::dsp::R8bAllocator, with r8brain's static "
                        "members compiled by ${CMAKE_SOURCE_DIR}/packages/native/src/dsp/R8bBase.cpp. Linking the "
                        "vendored library puts a second definition of each of those classes (on r8brain's default "
                        "malloc hooks) in the program, and the linker keeps one copy with no diagnostic. Use its "
                        "include directory instead, and see packages/native/src/dsp/R8bAllocator.hpp.")
                endif()
            endif()
        endif()
    endforeach()
    get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sub IN LISTS _subdirs)
        retroplug_forbid_linking_tree("${_sub}" "${vendored}")
    endforeach()
endfunction()
