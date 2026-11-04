# rp-device-check (docs/modular-graph-phase-e1-design.md section 7): a Pico SDK firmware for the RP2350 that runs every
# portable node type of the packages packages.cmake lists (`core`, `dsp` and `n8`), built in device mode (RP_DEVICE, no
# exceptions, no RTTI), from its default settings as msgpack, and links with a trap on the heap, on exceptions and on
# RTTI. Its own CMake project on the SDK's toolchain, built in a build dir of its own, never part of the desktop's
# build/: tools/build-device-check.sh configures and builds it (pnpm device:check), and CI's linux-x64 job runs that
# script.
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

# --- reflect-cpp, header-only here: the field reflection graph/fields.hpp compiles against (nothing of it is linked),
# at the desktop's pin (cmake/reflectcpp-pin.cmake, which the box's firmware uses too).
include("${RP_REPO}/cmake/reflectcpp-pin.cmake")

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

# --- the runtime with pico_malloc taken out of pico_runtime, and the link trap on the heap, on exceptions and on RTTI
# (cmake/device-image.cmake, which says why each is what it is; the box's firmware links under the same trap).
set(RP_DEVICE_IMAGE rp-device-check)
include("${RP_REPO}/cmake/device-image.cmake")

rp_device_executable(rp-device-check main.cpp DeviceCheck.cpp N8Protocol.cpp)
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

# The trap's second half: the linked image defines no wrapper and refers to none (cmake/check-trap.cmake).
rp_device_trap_check(rp-device-check)

# The trap's positive control: an image built exactly as rp-device-check is, whose one source allocates with new and
# with malloc. It must NOT link, and its link error must name its own object for both: tools/build-device-check.sh
# builds it on every run and fails unless it fails that way, so a trap that stopped reaching the link (an option moved
# to a target nothing links, a toolchain that ignores --wrap) fails CI rather than passing everything.
rp_device_executable(rp-device-trap-probe trap-probe.cpp)
set_target_properties(rp-device-trap-probe PROPERTIES EXCLUDE_FROM_ALL ON)
pico_enable_stdio_uart(rp-device-trap-probe 1)
pico_enable_stdio_usb(rp-device-trap-probe 0)
pico_minimize_runtime(rp-device-trap-probe INCLUDE FLOAT DOUBLE PRINTF PANIC)
