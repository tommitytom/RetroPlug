cmake_minimum_required(VERSION 3.13)

# Locate the Pico SDK (via PICO_SDK_PATH env var, or fetched from git as a fallback).
include(pico_sdk_import.cmake)

project(n8_midi_in C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(midi_in_test
    midi_in_test.c
)

target_link_libraries(midi_in_test
    pico_stdlib
    hardware_uart
)

# Console (printf / stdio) over UART0 (GP0/GP1) for the Raspberry Pi Debug Probe.
# USB stdio is off so the native USB port stays free for later stages.
pico_enable_stdio_uart(midi_in_test 1)
pico_enable_stdio_usb(midi_in_test 0)

# Emit .uf2 / .hex / .bin / .map alongside the .elf.
pico_add_extra_outputs(midi_in_test)
