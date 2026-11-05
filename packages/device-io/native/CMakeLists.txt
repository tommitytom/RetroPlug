# The rp.device node types (src/device-io/DeviceIoNodes.cpp): a device host's own I/O, which the desktop's rp.host nodes
# are for the engine. Added by retroplug_package(device-io) (cmake/packages.cmake). Like any pack, it depends on
# node-sdk alone; the host interface its node reads is the SDK's (graph/rp_device_midi_in.h). No desktop product lists
# it: the plan exporter and the device builds do.
retroplug_package_library(retroplug-device-io-nodes
    ${CMAKE_CURRENT_SOURCE_DIR}/src/device-io/DeviceIoNodes.cpp)
target_include_directories(retroplug-device-io-nodes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-device-io-nodes PUBLIC retroplug-node-sdk)
