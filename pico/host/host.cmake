# pico/host (docs/modular-graph-phase-e3-design.md section 5): the generic box host, a library with no N8 in it. The
# device rp_host and its two interfaces (DeviceHost), the built-in plan's load and its blocks (BuiltInPlan), the block
# driver (BlockDriver), and the UART MIDI path: the interrupt's ring with its timestamps (UartMidi), the parser
# (pico/midi-in's midi.c) and the staging (MidiStaging). A box product (pico/n8-host) includes this file and links
# rp-box-host; the desktop's tests (packages/native/cmake/tests.cmake) include it for the portable sources, which they
# build for the host.
include_guard(GLOBAL)

set(RP_BOX_HOST_DIR "${CMAKE_CURRENT_LIST_DIR}")
# Everything but the UART's own code, which needs the Pico SDK: what the desktop's tests build as the box does.
set(RP_BOX_HOST_PORTABLE_SOURCES
    "${RP_BOX_HOST_DIR}/BlockDriver.cpp"
    "${RP_BOX_HOST_DIR}/BuiltInPlan.cpp"
    "${RP_BOX_HOST_DIR}/DeviceHost.cpp"
    "${RP_BOX_HOST_DIR}/MidiStaging.cpp"
    "${RP_BOX_HOST_DIR}/../midi-in/midi.c")
set(RP_BOX_HOST_INCLUDES "${RP_BOX_HOST_DIR}" "${RP_BOX_HOST_DIR}/../midi-in")

# In a device project (the Pico SDK's, after pico_sdk_init and the packages), an INTERFACE library, as the SDK's own
# are: its sources compile in the image that links it, with that image's device flags and stack limit.
if(PICO_SDK_VERSION_STRING)
    add_library(rp-box-host INTERFACE)
    target_sources(rp-box-host INTERFACE ${RP_BOX_HOST_PORTABLE_SOURCES} "${RP_BOX_HOST_DIR}/UartMidi.cpp")
    target_include_directories(rp-box-host INTERFACE ${RP_BOX_HOST_INCLUDES})
    target_link_libraries(rp-box-host INTERFACE pico_stdlib hardware_uart retroplug-graph-exec)
endif()
