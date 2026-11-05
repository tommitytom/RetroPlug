# rp-device-check (docs/modular-graph-phase-e1-design.md section 7): a Pico SDK firmware for the RP2350 that runs every
# portable node type of the packages packages.cmake lists (`core`, `dsp`, `n8`, `device-io` and `devtest`), built in
# device mode (RP_DEVICE, no exceptions, no RTTI), from its default settings as msgpack, and a multi-node plan loaded
# by the plan loader (plan.json, docs/modular-graph-phase-e3-design.md section 7), and links with a trap on the heap,
# on exceptions and on RTTI. Its own CMake project on the SDK's toolchain, built in a build dir of its own, never part
# of the desktop's build/: tools/build-device-check.sh configures and builds it (pnpm device:check), and CI's linux-x64
# job runs that script.
#
# Inputs, given by the script:
#   PICO_SDK_PATH          the Pico SDK (2.1.1; no submodules needed)
#   RP_DEVICE_CHECK_CASES  the header rp-device-check-cases wrote (cases/main.cpp): each type's default settings as
#                          msgpack and its layout.
#   RP_EXPORT_PLAN_TOOL    retroplug-export-plan, which exports plan.json for this image's arena at build time. With
#                          the cases header, the two things this project takes from the desktop's build.
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
# them, and the stack limit (cmake/device-image.cmake, included first for it). node-sdk and graph-runtime are what
# every package and the check build on; the list is packages.cmake's.
#
# The runtime with pico_malloc taken out of pico_runtime, the link trap on the heap, on exceptions and on RTTI, and the
# stack limit (cmake/device-image.cmake, which says why each is what it is; the box's firmware links under the same).
include("${CMAKE_CURRENT_SOURCE_DIR}/packages.cmake")
set(RP_DEVICE_BUILD ON)
set(RP_DEVICE_COMPILE_OPTIONS
    "$<TARGET_PROPERTY:pico_standard_link,INTERFACE_COMPILE_OPTIONS>"
    "$<TARGET_PROPERTY:pico_cxx_options,INTERFACE_COMPILE_OPTIONS>")
set(RP_DEVICE_IMAGE rp-device-check)
include("${RP_REPO}/cmake/device-image.cmake")
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

# The plan the check runs (plan.cmake: plan.json and what it is exported for), exported by retroplug-export-plan, a
# desktop tool of the desktop build: it plans plan.json for the RP2350 with the desktop's builds of this image's own
# packages' types (packages.cmake's list, the product below), sizes it with their device builds against the image's
# arena, and writes the blob as a header, with the arena that sizing gave it. A plan this image could not run or hold
# fails here, at build time. The header is rewritten only when its bytes change.
include("${CMAKE_CURRENT_SOURCE_DIR}/plan.cmake")
set(RP_EXPORT_PLAN_TOOL "${RP_REPO}/build/export-plan/retroplug-export-plan" CACHE FILEPATH
    "retroplug-export-plan, from the desktop build (cmake --build build --target retroplug-export-plan)")
if(NOT EXISTS "${RP_EXPORT_PLAN_TOOL}")
    message(FATAL_ERROR "rp-device-check: no plan exporter at RP_EXPORT_PLAN_TOOL (\"${RP_EXPORT_PLAN_TOOL}\"); "
                        "build it in the desktop build (cmake --build build --target retroplug-export-plan), as "
                        "tools/build-device-check.sh does, or pass -DRP_EXPORT_PLAN_TOOL=<path>")
endif()
set(_rp_plan_header "${CMAKE_BINARY_DIR}/generated/DeviceCheckPlan.hpp")
string(REPLACE ";" "," _rp_plan_packages "${RP_DEVICE_CHECK_PACKAGES}")
add_custom_command(
    OUTPUT  "${_rp_plan_header}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/generated"
    COMMAND "${RP_EXPORT_PLAN_TOOL}" "${RP_DEVICE_CHECK_PLAN}" "${_rp_plan_header}"
            --arena ${RP_DEVICE_CHECK_ARENA_BYTES} --sample-rate ${RP_DEVICE_CHECK_PLAN_SAMPLE_RATE}
            --max-frames ${RP_DEVICE_CHECK_PLAN_MAX_FRAMES} --packages ${_rp_plan_packages} --header kDeviceCheckPlan
    DEPENDS "${RP_DEVICE_CHECK_PLAN}" "${RP_EXPORT_PLAN_TOOL}" "${CMAKE_CURRENT_SOURCE_DIR}/plan.cmake"
            "${CMAKE_CURRENT_SOURCE_DIR}/packages.cmake"
    COMMENT "retroplug-export-plan: the device check's plan (plan.json)"
    VERBATIM)

rp_device_executable(rp-device-check main.cpp DeviceCheck.cpp N8Protocol.cpp "${_rp_plan_header}")
target_include_directories(rp-device-check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(rp-device-check PRIVATE RP_DEVICE_CHECK_ARENA_BYTES=${RP_DEVICE_CHECK_ARENA_BYTES})
# A 4 KB main stack (the SDK's default is 2 KB): all of SCRATCH_Y, which holds nothing else, as the box's has. The
# deepest chain is the plan's load: main, runPlan, loadPlan with its run-up inlined, its layout, a node's query_memory
# and its settings' decode, about 2.7 KB summed from -fstack-usage; a create (2.2 KB), the per-type checks (2.0 KB), the
# protocol run (2.0 KB) and a block (0.6 KB) are shallower. No interrupt frame is added on top: the check enables none
# while it runs (polled UART stdio, busy waits, time_us_32 with no alarm, no USB), and the 1.3 KB left would hold one.
# The image measures the real depth itself (main.cpp paints the stack at boot and prints its high-water mark).
# Every frame is held to 1 KB at build time (-Wstack-usage, cmake/device-image.cmake). At run time the stack guard
# (PICO_USE_STACK_GUARDS) holds it: on the RP2350's Arm cores the SDK sets the M33's MSPLIM to the stack's bottom at
# boot, so a push past it faults (a HardFault: no plan line, a dark LED, and CFSR's STKOF set, main.cpp says how to
# read it) rather than running on into SCRATCH_X below, which this image leaves empty (it links no pico_multicore, so
# there is no core 1 stack to reserve), where nothing would notice.
target_compile_definitions(rp-device-check PRIVATE PICO_STACK_SIZE=0x1000 PICO_USE_STACK_GUARDS=1)
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
