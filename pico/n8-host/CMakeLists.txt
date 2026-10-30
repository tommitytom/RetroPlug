cmake_minimum_required(VERSION 3.13)

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

# Pico-PIO-USB (sekigon-gonnoc) - only needed for the PIO-USB path.
if(NOT DEFINED PICO_PIO_USB_PATH)
    set(PICO_PIO_USB_PATH /workspaces/Pico-PIO-USB)
endif()

project(n8_host C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

if(NOT DEFINED PICO_TINYUSB_PATH)
    set(PICO_TINYUSB_PATH ${PICO_SDK_PATH}/lib/tinyusb)
endif()

add_executable(n8_host
    main.c
    edio.c
    # Stage-1 MIDI parser, reused as-is (the bridge forwards its decoded messages).
    ${CMAKE_CURRENT_LIST_DIR}/../midi-in/midi.c
)

# So TinyUSB finds tusb_config.h, and the build finds the shared midi.h.
target_include_directories(n8_host PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../midi-in
)

target_link_libraries(n8_host
    pico_stdlib
    hardware_uart
    tinyusb_host
)

if(RP_NATIVE_USB)
    # Native USB host: tinyusb_host already carries hcd_rp2040.c (the RP2040/RP2350 silicon
    # host controller), which activates when CFG_TUH_RPI_PIO_USB=0 (set via USE_NATIVE_USB).
    target_compile_definitions(n8_host PRIVATE USE_NATIVE_USB)
else()
    # PIO-USB host: pull in Pico-PIO-USB + TinyUSB's PIO-USB HCD (compiled directly, as the
    # Pico-PIO-USB host example does, so it builds regardless of SDK gating).
    add_subdirectory(${PICO_PIO_USB_PATH} pico_pio_usb)
    target_sources(n8_host PRIVATE
        ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c)
    target_compile_definitions(n8_host PRIVATE PIO_USB_USE_TINYUSB)
    target_link_libraries(n8_host pico_pio_usb)
endif()

# -DN8_TUSB_DEBUG=2 traces TinyUSB's attach / enumeration on the console - the first thing to
# look at when the N8 never mounts (no attach line at all = nothing electrical on D+/D-).
if(DEFINED N8_TUSB_DEBUG)
    target_compile_definitions(n8_host PRIVATE CFG_TUSB_DEBUG=${N8_TUSB_DEBUG})
endif()

# Experiment knobs, each a plain #define in main.c with a default: N8_MENU_TRIES (how many
# '*t' attempts before giving up on the menu).
foreach(knob N8_MENU_TRIES N8_APU_LAT N8_EXP_VOL)
    if(DEFINED ${knob})
        target_compile_definitions(n8_host PRIVATE ${knob}=${${knob}})
    endif()
endforeach()
# The SD path of the ROM to boot (a string, so it is quoted here): -DN8_ROM_PATH=usb-games/x.nes
if(DEFINED N8_ROM_PATH)
    target_compile_definitions(n8_host PRIVATE N8_ROM_PATH="${N8_ROM_PATH}")
endif()

# Console (printf) over UART0 (GP0/GP1) for the debug probe. Native USB stdio stays off:
# in the PIO-USB build the native port is unused; in the native-USB build it IS the host
# port, so it must not be a stdio device.
pico_enable_stdio_uart(n8_host 1)
pico_enable_stdio_usb(n8_host 0)

pico_add_extra_outputs(n8_host)
