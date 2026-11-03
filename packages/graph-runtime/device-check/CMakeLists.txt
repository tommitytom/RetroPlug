# rp-device-check (docs/modular-graph-phase-e1-design.md section 7): a Pico SDK firmware for the RP2350 that runs every
# portable node type of `core` and `dsp`, built in device mode (RP_DEVICE, no exceptions, no RTTI), from its default
# settings as msgpack, and links with a trap on the heap and on exceptions. Its own CMake project on the SDK's
# toolchain, built in a build dir of its own, never part of the desktop's build/: tools/build-device-check.sh
# configures and builds it (pnpm device:check), and CI's linux-x64 job runs that script.
#
# Inputs, both given by the script:
#   PICO_SDK_PATH          the Pico SDK (2.1.1; no submodules needed)
#   RP_DEVICE_CHECK_CASES  the header rp-device-check-cases wrote (cases/main.cpp): each type's default settings as
#                          msgpack and its layout. The one thing this project takes from the desktop's build.
#   RP_REFLECTCPP_SOURCE_DIR  optional: a checkout of reflect-cpp at the pinned version; without it, it is fetched.
cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2)
endif()
if(NOT DEFINED PICO_PLATFORM)
    set(PICO_PLATFORM rp2350-arm-s)
endif()
if(NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif()
if(NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
    message(FATAL_ERROR "rp-device-check: PICO_SDK_PATH (\"${PICO_SDK_PATH}\") is not a Pico SDK")
endif()
set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "The Pico SDK" FORCE)
# Before project(): it picks the SDK's toolchain (arm-none-eabi, the RP2350's Cortex-M33 flags, the FPU on).
include("${PICO_SDK_PATH}/pico_sdk_init.cmake")

project(rp_device_check C CXX ASM)
pico_sdk_init()

get_filename_component(RP_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# --- reflect-cpp, header-only here: the field reflection graph/fields.hpp compiles against (nothing of it is linked).
# The version is the desktop's pin, read from where the desktop pins it (rpcpp's CPMAddPackage), so the two builds
# cannot drift apart. A checkout the caller names (the desktop build's, which CI has already fetched) must be that
# version; otherwise a shallow clone of its tag.
file(READ "${RP_REPO}/deps/dpf.js/deps/rpcpp/CMakeLists.txt" _rp_rpcpp)
string(REGEX MATCH "GITHUB_REPOSITORY[ \t]+getml/reflect-cpp[ \t\r\n]+VERSION[ \t]+([0-9.]+)" _rp_pin "${_rp_rpcpp}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "rp-device-check: no reflect-cpp pin in deps/dpf.js/deps/rpcpp/CMakeLists.txt")
endif()
set(_rp_reflect_version "${CMAKE_MATCH_1}")
if(RP_REFLECTCPP_SOURCE_DIR)
    file(READ "${RP_REFLECTCPP_SOURCE_DIR}/CMakeLists.txt" _rp_reflect)
    string(REGEX MATCH "project\\(reflectcpp VERSION ([0-9.]+)" _rp_found "${_rp_reflect}")
    if(NOT CMAKE_MATCH_1 STREQUAL _rp_reflect_version)
        message(FATAL_ERROR "rp-device-check: ${RP_REFLECTCPP_SOURCE_DIR} is reflect-cpp ${CMAKE_MATCH_1}, and the "
                            "desktop pins ${_rp_reflect_version}")
    endif()
    set(_rp_reflect_dir "${RP_REFLECTCPP_SOURCE_DIR}")
else()
    include(FetchContent)
    # SOURCE_SUBDIR names a directory the checkout does not have, so the clone is made available without its own
    # CMakeLists being added: the headers are all this build takes.
    FetchContent_Declare(rp_reflectcpp
        GIT_REPOSITORY https://github.com/getml/reflect-cpp.git
        GIT_TAG        v${_rp_reflect_version}
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  rp-headers-only)
    FetchContent_MakeAvailable(rp_reflectcpp)
    set(_rp_reflect_dir "${rp_reflectcpp_SOURCE_DIR}")
endif()
set(RP_REFLECTCPP_INCLUDE_DIR "${_rp_reflect_dir}/include")

# --- the packages, in device mode (cmake/packages.cmake): each defines its device targets alone, compiled as a device
# compiles graph/node.hpp, with the SDK's code-generation flags (each function and object in a section of its own, so
# --gc-sections drops what the image never calls; no unwind tables; no __cxa_atexit), as the image's own sources have
# them. node-sdk and graph-runtime are what every package and the check build on; the list is packages.cmake's.
include("${CMAKE_CURRENT_SOURCE_DIR}/packages.cmake")
set(RP_DEVICE_BUILD ON)
set(RP_DEVICE_COMPILE_OPTIONS
    "$<TARGET_PROPERTY:pico_standard_link,INTERFACE_COMPILE_OPTIONS>"
    "$<TARGET_PROPERTY:pico_cxx_options,INTERFACE_COMPILE_OPTIONS>")
include("${RP_REPO}/cmake/packages.cmake")
retroplug_package(node-sdk)
retroplug_package(graph-runtime)
foreach(_rp_package IN LISTS RP_DEVICE_CHECK_PACKAGES)
    retroplug_package(${_rp_package})
endforeach()

if(NOT RP_DEVICE_CHECK_CASES OR NOT EXISTS "${RP_DEVICE_CHECK_CASES}")
    message(FATAL_ERROR "rp-device-check: RP_DEVICE_CHECK_CASES names no header (\"${RP_DEVICE_CHECK_CASES}\"); "
                        "tools/build-device-check.sh writes one with the desktop's rp-device-check-cases")
endif()
# Copied in, so a new header re-runs the configure and rebuilds what includes it.
configure_file("${RP_DEVICE_CHECK_CASES}" "${CMAKE_BINARY_DIR}/generated/DeviceCheckCases.hpp" COPYONLY)

# --- the runtime: the SDK's pico_stdlib with pico_malloc taken out, and nothing else. pico_malloc is the one library
# of the SDK the trap cannot have: it DEFINES the wrapped malloc family (its own --wrap, a heap behind it), so with it
# in the link a call to malloc resolves to its wrapper, the trap fires only further down, at newlib's malloc.o, naming
# that rather than the object that called it, and check-trap.cmake fails the build on the wrapper's definition. The
# rest links under the full trap: stdio over the UART and pico_printf among them (the SDK wraps puts and printf to its
# own implementation, which allocates nothing), and stdio over USB too (TinyUSB's device CDC, scratch-built against
# SDK 2.1.1 with its submodules; this image leaves it out, as CI's SDK has none).
#
# Both lists are read from the SDK's own targets rather than copied, so an SDK that adds a runtime library brings it
# here too: pico_runtime's sources, link options and libraries less pico_malloc (rp-device-runtime), and pico_stdlib's
# with that in place of pico_runtime (rp-device-stdlib). A list that no longer holds the name it replaces stops the
# configure, as the SDK has moved under it.
if(NOT PICO_SDK_VERSION_STRING STREQUAL "2.1.1")
    message(WARNING "rp-device-check: built and checked against Pico SDK 2.1.1, and this is ${PICO_SDK_VERSION_STRING}")
endif()
function(_rp_derive_library name from drop)
    get_target_property(_libraries ${from} INTERFACE_LINK_LIBRARIES)
    if(NOT drop IN_LIST _libraries)
        message(FATAL_ERROR "rp-device-check: ${from} no longer links ${drop} (it links ${_libraries}); the Pico SDK's "
                            "runtime has changed, and ${name} must be derived from it again")
    endif()
    list(REMOVE_ITEM _libraries ${drop})
    list(APPEND _libraries ${ARGN})
    get_target_property(_sources ${from} INTERFACE_SOURCES)
    get_target_property(_options ${from} INTERFACE_LINK_OPTIONS)
    add_library(${name} INTERFACE)
    target_link_libraries(${name} INTERFACE ${_libraries})
    if(_sources)
        target_sources(${name} INTERFACE ${_sources})
    endif()
    if(_options)
        target_link_options(${name} INTERFACE ${_options})
    endif()
endfunction()
_rp_derive_library(rp-device-runtime pico_runtime pico_malloc)
_rp_derive_library(rp-device-stdlib pico_stdlib pico_runtime rp-device-runtime)

# --- the link trap. Every symbol through which code reaches the heap or throws is wrapped, and no __wrap_ symbol is
# defined, so a reference to any of them in what the image links is an undefined __wrap_ reference: a link error
# naming the object (and the function) that made it. Every object is compiled with a section a function, so
# --gc-sections (pico_standard_link) means it names what the image keeps, not every function an object happens to
# hold. The C heap is newlib's, and newlib reaches it through its reentrant _r forms as well as the plain ones, so both
# are listed, and sbrk, the break under it; operator new and delete, the C++ runtime's throw path and the
# std::__throw_* helpers the standard headers call are read from the device's own libstdc++ (every variant of each its
# multilib defines), so a variant is never missed for being spelt differently. The function-local static guards
# (__cxa_guard_*) are wrapped too: libsupc++ implements them with a throw, and a device image has no business with
# them (the device product's registration is written to need none).
set(_rp_trapped
    malloc calloc realloc free memalign aligned_alloc posix_memalign valloc pvalloc reallocf reallocarray
    malloc_usable_size
    _malloc_r _calloc_r _realloc_r _free_r _memalign_r _valloc_r _pvalloc_r _reallocf_r _malloc_usable_size_r
    sbrk _sbrk _sbrk_r
    __cxa_guard_acquire __cxa_guard_release __cxa_guard_abort)
separate_arguments(_rp_cxx_flags UNIX_COMMAND "${CMAKE_CXX_FLAGS}")
execute_process(COMMAND ${CMAKE_CXX_COMPILER} ${_rp_cxx_flags} -print-file-name=libstdc++.a
                OUTPUT_VARIABLE _rp_libstdcxx OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT IS_ABSOLUTE "${_rp_libstdcxx}" OR NOT EXISTS "${_rp_libstdcxx}")
    message(FATAL_ERROR "rp-device-check: no libstdc++.a for these flags (${CMAKE_CXX_FLAGS}): \"${_rp_libstdcxx}\"")
endif()
# A toolchain upgraded in place re-runs the configure, so the list is read from the library the link uses.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_rp_libstdcxx}")
execute_process(COMMAND ${CMAKE_NM} -g --defined-only "${_rp_libstdcxx}" OUTPUT_VARIABLE _rp_nm)
set(_rp_cxx_names
    "_Znw[A-Za-z0-9_]+" "_Zna[A-Za-z0-9_]+"  # operator new and new[]
    "_Zdl[A-Za-z0-9_]+" "_Zda[A-Za-z0-9_]+"  # operator delete and delete[]
    "_ZSt[0-9]+__throw_[A-Za-z0-9_]+"        # std::__throw_*
    "__cxa_(throw|rethrow|allocate_exception|allocate_dependent_exception|free_exception|free_dependent_exception)"
    "__cxa_(init_primary_exception|throw_bad_array_new_length|bad_cast|bad_typeid)")
list(JOIN _rp_cxx_names "|" _rp_cxx_names)
string(REGEX MATCHALL "[ \t][TW] (${_rp_cxx_names})\n" _rp_cxx "${_rp_nm}")
list(TRANSFORM _rp_cxx REPLACE "^[ \t][TW] ([^\n]+)\n$" "\\1")
list(REMOVE_DUPLICATES _rp_cxx)
foreach(_rp_required _Znwj _ZdlPv __cxa_throw __cxa_allocate_exception _ZSt17__throw_bad_allocv)
    if(NOT _rp_required IN_LIST _rp_cxx)
        message(FATAL_ERROR "rp-device-check: ${_rp_libstdcxx} defines no ${_rp_required}; the trap would miss it")
    endif()
endforeach()
list(APPEND _rp_trapped ${_rp_cxx})
list(LENGTH _rp_trapped _rp_trapped_count)
message(STATUS "rp-device-check: the link trap wraps ${_rp_trapped_count} symbols")
set(_rp_trap_list "${CMAKE_BINARY_DIR}/trapped-symbols.txt")
list(JOIN _rp_trapped "\n" _rp_trapped_lines)
file(WRITE "${_rp_trap_list}" "${_rp_trapped_lines}\n")

# An executable of this project as the device builds it: the device-mode flags, the runtime and the trap. One --wrap a
# symbol: -Wl, splits at commas, so a joined list would hand the linker the symbols as input files.
function(_rp_device_executable target)
    add_executable(${target} ${ARGN})
    set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    target_compile_definitions(${target} PRIVATE RP_DEVICE)
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>")
    target_link_libraries(${target} PRIVATE rp-device-stdlib)
    list(TRANSFORM _rp_trapped PREPEND "LINKER:--wrap=" OUTPUT_VARIABLE _wrap)
    target_link_options(${target} PRIVATE ${_wrap})
endfunction()

_rp_device_executable(rp-device-check main.cpp DeviceCheck.cpp)
target_include_directories(rp-device-check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/generated")
# The device check's product: the device entries of packages.cmake's list, which the case generator's product lists
# too. The image's entries reach main.cpp, which refuses a cases header made for other ones.
retroplug_product(rp-device-check PACKAGES ${RP_DEVICE_CHECK_PACKAGES} DEVICE)
get_property(_rp_entries GLOBAL PROPERTY RP_DEVICE_PRODUCT_rp-device-check_ENTRIES)
list(JOIN _rp_entries " " _rp_entries)
target_compile_definitions(rp-device-check PRIVATE "RP_DEVICE_CHECK_ENTRIES=\"${_rp_entries}\"")
target_link_libraries(rp-device-check PRIVATE retroplug-graph-exec)
target_link_options(rp-device-check PRIVATE "LINKER:-Map=${CMAKE_BINARY_DIR}/rp-device-check.map")
# The result goes out on the UART (stdio, GP0 at 115200), which also keeps stdio and printf in the image the trap
# judges. pico_minimize_runtime drops the runtime services the check uses none of (the default alarm pool, the FPGA
# check, the auto-initialised mutexes), for size: the trap needs none of it dropped, as stdio is linked, so the default
# panic prints through the SDK's puts. (Without pico_stdio a panic's puts is newlib's, whose buffers come from malloc;
# that is the image to avoid, and why stdio is not left out to save its size.)
pico_enable_stdio_uart(rp-device-check 1)
pico_enable_stdio_usb(rp-device-check 0)
pico_minimize_runtime(rp-device-check INCLUDE FLOAT DOUBLE PRINTF PANIC)

# None of the wrapped symbols may be defined either: a library that defined one (pico_malloc defines the malloc
# family's) would turn the trap into a heap. Checked on the linked image, which fails the build when one is.
add_custom_command(TARGET rp-device-check POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DIMAGE=$<TARGET_FILE:rp-device-check> -DTRAPPED=${_rp_trap_list}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check-trap.cmake"
    VERBATIM)

# The trap's positive control: an image built exactly as rp-device-check is, whose one source allocates with new and
# with malloc. It must NOT link, and its link error must name its own object for both: tools/build-device-check.sh
# builds it on every run and fails unless it fails that way, so a trap that stopped reaching the link (an option moved
# to a target nothing links, a toolchain that ignores --wrap) fails CI rather than passing everything.
_rp_device_executable(rp-device-trap-probe trap-probe.cpp)
set_target_properties(rp-device-trap-probe PROPERTIES EXCLUDE_FROM_ALL ON)
pico_enable_stdio_uart(rp-device-trap-probe 1)
pico_enable_stdio_usb(rp-device-trap-probe 0)
pico_minimize_runtime(rp-device-trap-probe INCLUDE FLOAT DOUBLE PRINTF PANIC)
