cmake_minimum_required(VERSION 3.20)

# Default to the Pico 2 (RP2350) - the board in the hardware lab. Override with
# -DPICO_BOARD=pico for an original RP2040 Pico. Must be set before pico_sdk_init.
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2)
endif()

# -DRP_NATIVE_USB=ON drives the N8 over the RP2350's SILICON USB host controller (native
# rhport 0, the Pico's own USB D+/D- pins) instead of the software PIO-USB (rhport 1 on
# GP2/GP3). Native is a hardware host like a PC's - use it to test whether the N8's cart
# FIFO write works there (it does not over PIO-USB; see ../pico-n8-fifo-write-bug.md). The
# wiring differs: see README.md.
option(RP_NATIVE_USB "Use the RP2350 native USB host instead of PIO-USB" OFF)

include(pico_sdk_import.cmake)

# Pico-PIO-USB (sekigon-gonnoc) - only needed for the PIO-USB path: -DPICO_PIO_USB_PATH, else
# $PICO_PIO_USB_PATH, else the devcontainer's checkout.
if(NOT DEFINED PICO_PIO_USB_PATH)
    if(DEFINED ENV{PICO_PIO_USB_PATH})
        set(PICO_PIO_USB_PATH "$ENV{PICO_PIO_USB_PATH}")
    else()
        set(PICO_PIO_USB_PATH /workspaces/Pico-PIO-USB)
    endif()
endif()

project(n8_host C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

pico_sdk_init()

if(NOT DEFINED PICO_TINYUSB_PATH)
    set(PICO_TINYUSB_PATH ${PICO_SDK_PATH}/lib/tinyusb)
endif()

get_filename_component(RP_REPO "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# --- the packages, as a device builds them (docs/modular-graph-phase-e2-design.md section 8, the E3 design's section
# 5): the N8 pack (packages/n8: its one Edio, the menu commands and operations, rp.n8.nes) and the two packages the
# built-in plan names beside it (core, for rp.core.midi-filter, and device-io, for rp.device.midi-in), compiled as
# rp-device-check compiles its packages (cmake/packages.cmake's device mode: RP_DEVICE, no exceptions, no RTTI, the
# SDK's code-generation flags), with reflect-cpp's headers at the desktop's pin for graph/node.hpp. node-sdk is what
# every package builds on; graph-runtime carries the executor, the plan loader and the product registration's header.
#
# The runtime with pico_malloc taken out, the link trap on the heap, on exceptions and on RTTI over the whole image
# (the firmware's own code, TinyUSB, Pico-PIO-USB and the packages alike), and the stack limit on every frame they
# compile (cmake/device-image.cmake), which is included before the packages so that their libraries take it.
include("${RP_REPO}/cmake/reflectcpp-pin.cmake")
set(RP_DEVICE_BUILD ON)
set(RP_DEVICE_COMPILE_OPTIONS
    "$<TARGET_PROPERTY:pico_standard_link,INTERFACE_COMPILE_OPTIONS>"
    "$<TARGET_PROPERTY:pico_cxx_options,INTERFACE_COMPILE_OPTIONS>")
set(RP_DEVICE_IMAGE n8-host)
include("${RP_REPO}/cmake/device-image.cmake")
include("${RP_REPO}/cmake/packages.cmake")
retroplug_package(node-sdk)
retroplug_package(graph-runtime)
retroplug_package(core)
retroplug_package(device-io)
retroplug_package(n8)

# --- the generic box host (../host: the device rp_host, the built-in plan's load and blocks, the block driver, the
# UART MIDI path), and the built-in plan, exported when the firmware is built.
include("${CMAKE_CURRENT_LIST_DIR}/../host/host.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/plan.cmake")

# retroplug-export-plan (packages/graph-runtime/export-plan), a desktop tool of the desktop build: it plans plan.json
# for the RP2350 with the desktop's builds of the box's own packages' types (plan.cmake's N8_HOST_PACKAGES, the product
# below), sizes it with their device builds against the arena below, and writes the blob as a header, so a plan the box
# cannot run or hold fails this build rather than its boot.
# tools/build-box.sh builds it in the desktop build first and passes its path; loop.sh passes the path of the one the
# desktop build last built (it builds no desktop tree itself). The default is the desktop build's own (build/).
set(RP_EXPORT_PLAN_TOOL "${RP_REPO}/build/export-plan/retroplug-export-plan" CACHE FILEPATH
    "retroplug-export-plan, from the desktop build (cmake --build build --target retroplug-export-plan)")
if(NOT EXISTS "${RP_EXPORT_PLAN_TOOL}")
    message(FATAL_ERROR "n8-host: no plan exporter at RP_EXPORT_PLAN_TOOL (\"${RP_EXPORT_PLAN_TOOL}\"); build it "
                        "in the desktop build (cmake --build build --target retroplug-export-plan), as "
                        "tools/build-box.sh does, or pass -DRP_EXPORT_PLAN_TOOL=<path>")
endif()
set(_n8_plan_header "${CMAKE_BINARY_DIR}/generated/n8_host_plan.hpp")
string(REPLACE ";" "," _n8_plan_packages "${N8_HOST_PACKAGES}")
add_custom_command(
    OUTPUT  "${_n8_plan_header}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/generated"
    COMMAND "${RP_EXPORT_PLAN_TOOL}" "${N8_HOST_PLAN}" "${_n8_plan_header}" --arena ${N8_HOST_ARENA_BYTES}
            --sample-rate ${N8_HOST_PLAN_SAMPLE_RATE} --max-frames ${N8_HOST_PLAN_MAX_FRAMES}
            --packages ${_n8_plan_packages} --header kBuiltInPlan
    DEPENDS "${N8_HOST_PLAN}" "${RP_EXPORT_PLAN_TOOL}" "${CMAKE_CURRENT_LIST_DIR}/plan.cmake"
    COMMENT "retroplug-export-plan: the box's built-in plan (plan.json)"
    VERBATIM)

# The firmware's own sources, its includes, libraries and options, for the image and for its trap probe alike.
function(n8_host_setup target)
    # So TinyUSB finds tusb_config.h (the host library carries the shared midi.h).
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
    target_link_libraries(${target} PRIVATE
        pico_stdlib
        hardware_uart
        tinyusb_host
        hardware_watchdog
    )
    if(RP_NATIVE_USB)
        # Native USB host: tinyusb_host already carries hcd_rp2040.c (the RP2040/RP2350 silicon
        # host controller), which activates when CFG_TUH_RPI_PIO_USB=0 (set via USE_NATIVE_USB).
        target_compile_definitions(${target} PRIVATE USE_NATIVE_USB)
    else()
        # PIO-USB host: Pico-PIO-USB + TinyUSB's PIO-USB HCD (compiled directly, as the
        # Pico-PIO-USB host example does, so it builds regardless of SDK gating).
        target_sources(${target} PRIVATE
            ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c)
        target_compile_definitions(${target} PRIVATE PIO_USB_USE_TINYUSB)
        target_link_libraries(${target} PRIVATE pico_pio_usb)
        # pio_usb_host_init calls alarm_pool_create unless its config skips the pool, so the call is linked even
        # though the box's config skips it, and the SDK makes that pool with malloc and calloc. The wrap points the
        # call at a stub that never runs (SofTimer.cpp), which leaves the SDK's pool function, and the heap, out. The
        # wrap also puts every other caller of alarm_pool_create past the link trap, so --cref lets the post-link check
        # below (check-wrap-referrers.cmake) hold that Pico-PIO-USB's init is the only one.
        target_link_options(${target} PRIVATE "LINKER:--wrap=alarm_pool_create_on_timer" "LINKER:--cref")
    endif()
    # -DN8_TUSB_DEBUG=2 traces TinyUSB's attach / enumeration on the console - the first thing to
    # look at when the N8 never mounts (no attach line at all = nothing electrical on D+/D-).
    if(DEFINED N8_TUSB_DEBUG)
        target_compile_definitions(${target} PRIVATE CFG_TUSB_DEBUG=${N8_TUSB_DEBUG})
    endif()
    # Console (printf) over UART0 (GP0/GP1) for the debug probe. Native USB stdio stays off:
    # in the PIO-USB build the native port is unused; in the native-USB build it IS the host
    # port, so it must not be a stdio device.
    pico_enable_stdio_uart(${target} 1)
    pico_enable_stdio_usb(${target} 0)
endfunction()

if(NOT RP_NATIVE_USB)
    if(NOT EXISTS "${PICO_PIO_USB_PATH}/src/pio_usb.h")
        message(FATAL_ERROR "n8-host: PICO_PIO_USB_PATH (\"${PICO_PIO_USB_PATH}\") is not a Pico-PIO-USB checkout "
                            "(5a37a66); -DRP_NATIVE_USB=ON builds without it")
    endif()
    # The firmware is checked against one Pico-PIO-USB commit: its init is what the alarm-pool wrap and the static SOF
    # timer stand in for (SofTimer.cpp), so another commit builds with a warning rather than quietly.
    set(_n8_pio_usb_commit 5a37a66dc5d3fbe0ef3cdbeda923a757440f984f)
    execute_process(COMMAND git -C "${PICO_PIO_USB_PATH}" rev-parse HEAD --show-toplevel
                    OUTPUT_VARIABLE _n8_pio_usb_git OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    string(REPLACE "\n" ";" _n8_pio_usb_git "${_n8_pio_usb_git}")
    list(LENGTH _n8_pio_usb_git _n8_pio_usb_n)
    set(_n8_pio_usb_head "of a commit git cannot read")
    if(_n8_pio_usb_n EQUAL 2)
        # A plain copy inside another work tree would answer that tree's HEAD, so the checkout must be its own.
        list(GET _n8_pio_usb_git 0 _n8_pio_usb_sha)
        list(GET _n8_pio_usb_git 1 _n8_pio_usb_top)
        get_filename_component(_n8_pio_usb_top "${_n8_pio_usb_top}" REALPATH)
        get_filename_component(_n8_pio_usb_dir "${PICO_PIO_USB_PATH}" REALPATH)
        if(_n8_pio_usb_top STREQUAL _n8_pio_usb_dir)
            set(_n8_pio_usb_head "${_n8_pio_usb_sha}")
        endif()
    endif()
    if(NOT _n8_pio_usb_head STREQUAL _n8_pio_usb_commit)
        message(WARNING "n8-host: Pico-PIO-USB at ${PICO_PIO_USB_PATH} is ${_n8_pio_usb_head}, and the firmware is "
                        "built and checked against ${_n8_pio_usb_commit}")
    endif()
    add_subdirectory(${PICO_PIO_USB_PATH} pico_pio_usb)
endif()

set(_n8_host_sources
    main.cpp
    box.cpp
    BoxCartLink.cpp
    CartVolume.cpp
    UsbCdcTransport.cpp
    "${_n8_plan_header}"
)
if(NOT RP_NATIVE_USB)
    list(APPEND _n8_host_sources SofTimer.cpp pio_usb_root.c)
endif()
rp_device_executable(n8_host ${_n8_host_sources})
n8_host_setup(n8_host)
# The image runs from SRAM: the SDK's copy_to_ram binary type, whose boot copies it out of flash (flash then holds only
# the load image and the staged ROMs). Run from flash, every instruction comes through the 16 KB XIP cache, and a
# message's own path (the parse and the staging, the block's three nodes, the cart link's push and drain, the FIFO write
# through the Edio and TinyUSB) runs once a message, so between isolated messages it falls out of the cache and each
# 8-byte line comes back from the QSPI flash (quad EBh at clk_sys/3), about 90 cycles a line. On the rig an isolated
# message's way from its UART poll to its FIFO write took about 11,200 cycles with about 90 refills from flash and
# 3,200 with none from SRAM, its fifoWR call 91 us and 38, and stress.sh's isolated latency (average/worst, us) went
# from 689-742/1192-1248 (four runs; a fifth held a message 12.6 ms behind a console line, as E2 can too) to
# 588-720/1080-1128 (five), where E2's image, half the size, gave 558-653/1083-1097 from flash. It costs SRAM: the
# code and constants sit beside the data, about 305 KiB of the 512 KiB in all where running from flash took about 200
# (the plan's 128 KiB arena among them), which leaves about 200 KiB for a later plan's audio and E4's script heap.
pico_set_binary_type(n8_host copy_to_ram)
# A 4 KB main stack: all of SCRATCH_Y, which holds nothing else. The box's deepest chains, summed pessimistically from
# -fstack-usage: the plan's load at boot (main, BuiltInPlan::load, loadPlan, its layout, a node's query_memory and its
# settings' decode) about 2.5 KB, before the UART and USB interrupts are on; a boot through bootRom and box.cfg's save,
# a transport wait, TinyUSB's callbacks and printf, with the UART and SOF interrupts nested on top, about 2.3 KB; a
# block (main, BuiltInPlan::run, runBlock, rp.n8.nes's process, the link's push) about 0.6 KB. What the linker checks is
# the reservation: it refuses scratch code that would overlap the reserved 4 KB. Every frame is held to 1 KB at build
# time (-Wstack-usage, cmake/device-image.cmake), and the chains are still the hand sum, with no limit at run time.
# In the PIO-USB build, which links pico_multicore (Pico-PIO-USB does), core 1's reservation (PICO_CORE1_STACK_SIZE,
# which defaults to this one) stays its 2 KB in SCRATCH_X: core 1 never runs, and SCRATCH_X keeps room for scratch
# code. The native build links no pico_multicore and reserves no core 1 stack.
target_compile_definitions(n8_host PRIVATE PICO_STACK_SIZE=0x1000 PICO_CORE1_STACK_SIZE=0x800)
# No heap reserved (the SDK's .heap section, 2 KB by default): nothing in the image allocates, which the link trap
# holds, so the reservation would be RAM no one can use. The plan's memory is its arena, a static below.
target_compile_definitions(n8_host PRIVATE PICO_HEAP_SIZE=0)
# The box's product: the device entries of the packages its plan names (rp.core, rp.device and rp.n8), registered as a
# device registers them, which is what the loader holds the plan's types to at boot, and the N8 pack's portable library
# (the Edio, its menu commands and operations) linked in; the generic box host; the plan's header and its arena.
retroplug_product(n8-host DEVICE PACKAGES ${N8_HOST_PACKAGES} TARGETS n8_host)
target_link_libraries(n8_host PRIVATE rp-box-host retroplug-graph-exec)
target_include_directories(n8_host PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(n8_host PRIVATE N8_HOST_ARENA_BYTES=${N8_HOST_ARENA_BYTES})

# Experiment knobs, each a plain #define in main.cpp with a default: N8_MENU_TRIES (how many
# '*t' attempts before giving up on the menu), N8_LAT_TRACE (the latency trace, LatTrace.hpp: a message's way through
# the box and what each part of a pass costs, printed after a second of quiet). The expansion volume is no knob: it is
# rp.n8.nes's `expansionVolume` param, unity unless plan.json sets it, which the cart link writes.
foreach(knob N8_MENU_TRIES N8_APU_LAT N8_TEST_HOOKS N8_LAT_TRACE)
    if(DEFINED ${knob})
        target_compile_definitions(n8_host PRIVATE ${knob}=${${knob}})
    endif()
endforeach()
# The SD path of the ROM to boot (a string, so it is quoted here): -DN8_ROM_PATH=usb-games/x.nes
if(DEFINED N8_ROM_PATH)
    target_compile_definitions(n8_host PRIVATE N8_ROM_PATH="${N8_ROM_PATH}")
endif()

rp_device_trap_check(n8_host)
if(NOT RP_NATIVE_USB)
    add_custom_command(TARGET n8_host POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DMAP=$<TARGET_FILE:n8_host>.map -DSYMBOL=__wrap_alarm_pool_create_on_timer
                -DREFERRER=pio_usb_host.c${CMAKE_C_OUTPUT_EXTENSION} -P "${CMAKE_CURRENT_LIST_DIR}/check-wrap-referrers.cmake"
        VERBATIM)
endif()
# The UF2, BIN, HEX and disassembly beside the ELF (the UF2 needs picotool; -DPICO_NO_PICOTOOL=1 skips it, as
# tools/build-box.sh does, where the ELF is the artifact). loop.sh flashes the ELF.
pico_add_extra_outputs(n8_host)

# The trap's positive control for this image: built as n8_host is, with the firmware's libraries, its one source
# allocating with new and with malloc. It must NOT link, naming its own object for both (tools/build-box.sh builds it
# and fails unless it fails that way), so a trap that stopped reaching this project's link fails rather than passing.
rp_device_executable(n8_host_trap_probe "${RP_REPO}/packages/graph-runtime/device-check/trap-probe.cpp")
set_target_properties(n8_host_trap_probe PROPERTIES EXCLUDE_FROM_ALL ON)
n8_host_setup(n8_host_trap_probe)
