cmake_minimum_required(VERSION 3.13)

# Default to the Pico 2 (RP2350) - the board in the hardware lab. Override with
# -DPICO_BOARD=pico for an original RP2040 Pico. Must be set before pico_sdk_init.
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2)
endif()

include(pico_sdk_import.cmake)

# Pico-PIO-USB (sekigon-gonnoc) - the low-level PIO USB implementation, cloned
# alongside the SDK. Override with -DPICO_PIO_USB_PATH=... if it lives elsewhere.
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

# pico_pio_usb (INTERFACE lib) comes from Pico-PIO-USB.
add_subdirectory(${PICO_PIO_USB_PATH} pico_pio_usb)

add_executable(n8_host
    main.c
    # TinyUSB's PIO-USB host controller driver, compiled directly (as the
    # Pico-PIO-USB host example does) so it builds regardless of SDK gating.
    ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c
)

# Tell Pico-PIO-USB to use the TinyUSB integration.
target_compile_definitions(n8_host PRIVATE PIO_USB_USE_TINYUSB)

# So TinyUSB finds tusb_config.h.
target_include_directories(n8_host PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(n8_host
    pico_stdlib
    pico_pio_usb
    tinyusb_host
)

# Console (printf) over UART0 (GP0/GP1) for the debug probe; native USB stays off
# (the native port is unused - the host runs on PIO-USB).
pico_enable_stdio_uart(n8_host 1)
pico_enable_stdio_usb(n8_host 0)

pico_add_extra_outputs(n8_host)
