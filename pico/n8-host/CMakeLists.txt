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

# --- the N8 pack, as a device builds it (docs/modular-graph-phase-e2-design.md section 8): the firmware runs the
# pack's one Edio (packages/n8), compiled as rp-device-check compiles its packages (cmake/packages.cmake's device mode:
# RP_DEVICE, no exceptions, no RTTI, the SDK's code-generation flags), with reflect-cpp's headers at the desktop's pin
# for graph/node.hpp. node-sdk is what every package builds on; graph-runtime carries the product registration's
# header.
include("${RP_REPO}/cmake/reflectcpp-pin.cmake")
set(RP_DEVICE_BUILD ON)
set(RP_DEVICE_COMPILE_OPTIONS
    "$<TARGET_PROPERTY:pico_standard_link,INTERFACE_COMPILE_OPTIONS>"
    "$<TARGET_PROPERTY:pico_cxx_options,INTERFACE_COMPILE_OPTIONS>")
include("${RP_REPO}/cmake/packages.cmake")
retroplug_package(node-sdk)
retroplug_package(graph-runtime)
retroplug_package(n8)

# --- the runtime with pico_malloc taken out, and the link trap on the heap, on exceptions and on RTTI over the whole
# image (cmake/device-image.cmake): the firmware's own code, TinyUSB, Pico-PIO-USB and the pack alike.
set(RP_DEVICE_IMAGE n8-host)
include("${RP_REPO}/cmake/device-image.cmake")

# The firmware's own sources, its includes, libraries and options, for the image and for its trap probe alike.
function(n8_host_setup target)
    # So TinyUSB finds tusb_config.h, and the build finds the shared midi.h.
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/../midi-in
    )
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
    UsbCdcTransport.cpp
    # Stage-1 MIDI parser, reused as-is (the bridge forwards its decoded messages).
    ${CMAKE_CURRENT_LIST_DIR}/../midi-in/midi.c
)
if(NOT RP_NATIVE_USB)
    list(APPEND _n8_host_sources SofTimer.cpp pio_usb_root.c)
endif()
rp_device_executable(n8_host ${_n8_host_sources})
n8_host_setup(n8_host)
# A 4 KB main stack: all of SCRATCH_Y, which holds nothing else. The box's deepest chain (the main loop, a boot through
# bootRom and box.cfg's save, a transport wait, TinyUSB's callbacks and printf, with the UART and SOF interrupts
# nested on top) comes to about 2.3 KB summed pessimistically, past the SDK's 2 KB default. What the linker checks is
# the reservation: it refuses scratch code that would overlap the reserved 4 KB. The stack's real depth is still the
# hand sum, with no limit at run time. Core 1's reservation (PICO_CORE1_STACK_SIZE, which defaults to this one) stays
# its 2 KB in SCRATCH_X: core 1 never runs, and SCRATCH_X keeps room for scratch code.
target_compile_definitions(n8_host PRIVATE PICO_STACK_SIZE=0x1000 PICO_CORE1_STACK_SIZE=0x800)
# The box's product: the N8 pack's device entry (rp.n8.nes), registered as a device registers it, and the pack's
# portable library (the Edio, its menu commands and operations) linked in.
retroplug_product(n8-host DEVICE PACKAGES n8 TARGETS n8_host)
target_link_libraries(n8_host PRIVATE retroplug-graph-exec)

# Experiment knobs, each a plain #define in main.cpp with a default: N8_MENU_TRIES (how many
# '*t' attempts before giving up on the menu).
foreach(knob N8_MENU_TRIES N8_APU_LAT N8_EXP_VOL N8_TEST_HOOKS)
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
