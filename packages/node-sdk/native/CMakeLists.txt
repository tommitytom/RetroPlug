# The node SDK (docs/modular-graph-phase-e1-design.md section 4): the C ABI (graph/rp_node.h), the C++ authoring layer
# (graph/node.hpp) and what that header compiles against: the descriptor writer, canonical JSON, the graph document
# types it names and the queue views (graph/Queue.hpp), with the single-producer ring the runtime and the device links
# share (transport/SpscRing.hpp). Added by retroplug_package(node-sdk) (cmake/packages.cmake).
#
# Its include root, src/, is where every one of these keeps the path it is included by ("graph/node.hpp"), so a node's
# source is the same whichever package it sits in. A pack depends on this package and nothing else.
#
# graph/node.hpp is built two ways (docs/modular-graph-phase-e1-design.md section 6): the desktop's, which links the
# library below, and a device-mode build (RP_DEVICE, no exceptions, no RTTI), which is headers alone: the portable
# core (graph/node_core.hpp, node_device.hpp and the msgpack settings decode) and reflect-cpp's compile-time field
# reflection, which is header-only and allocates nothing. retroplug-device-mode-test builds it on the desktop.
#
# retroplug-node-sdk-headers: the include root alone, no library. What a target takes when it needs the ABI, the queue
# views or the ring and must not link reflect-cpp: the no-heap executor (graph-runtime's retroplug-graph-exec), and the
# device links' tests.
add_library(retroplug-node-sdk-headers INTERFACE)
target_include_directories(retroplug-node-sdk-headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# retroplug-reflect-headers: reflect-cpp's include root alone, as SYSTEM headers, and no library: what a device-mode
# build of the SDK takes for the compile-time field reflection (graph/fields.hpp), which is header-only and allocates
# nothing. SYSTEM for the reason packages/native/cmake/warnings.cmake marks reflect-cpp so: its templates instantiate
# in our translation units, and our warning level is not its. The device-mode test's objects and every package's
# device check (cmake/packages.cmake) take it, and the device build will.
add_library(retroplug-reflect-headers INTERFACE)
target_include_directories(retroplug-reflect-headers SYSTEM INTERFACE
    $<TARGET_PROPERTY:reflectcpp,INTERFACE_INCLUDE_DIRECTORIES>)

# retroplug-node-sdk: the description library. Links reflect-cpp (settings parsing, JSON Schema), and holds the JSON to
# msgpack settings converter (graph/JsonToMsgpack.hpp), the desktop half of RP_SETTINGS_MSGPACK.
retroplug_package_library(retroplug-node-sdk
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/CanonicalJson.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/Descriptor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/GraphDocument.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/JsonToMsgpack.cpp)
target_link_libraries(retroplug-node-sdk PUBLIC retroplug-node-sdk-headers reflectcpp::reflectcpp)
