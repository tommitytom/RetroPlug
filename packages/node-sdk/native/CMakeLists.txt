# The node SDK (docs/modular-graph-phase-e1-design.md section 4): the C ABI (graph/rp_node.h), the C++ authoring layer
# (graph/node.hpp) and what that header compiles against: the descriptor writer, canonical JSON, the graph document
# types it names and the queue views (graph/Queue.hpp), with the single-producer ring the runtime and the device links
# share (transport/SpscRing.hpp). Added by retroplug_package(node-sdk) (cmake/packages.cmake).
#
# Its include root, src/, is where every one of these keeps the path it is included by ("graph/node.hpp"), so a node's
# source is the same whichever package it sits in. A pack depends on this package and nothing else.
#
# retroplug-node-sdk-headers: the include root alone, no library. What a target takes when it needs the ABI, the queue
# views or the ring and must not link reflect-cpp: the no-heap executor (graph-runtime's retroplug-graph-exec), and the
# device links' tests.
add_library(retroplug-node-sdk-headers INTERFACE)
target_include_directories(retroplug-node-sdk-headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# retroplug-node-sdk: the description library. Links reflect-cpp (settings parsing, JSON Schema).
retroplug_package_library(retroplug-node-sdk
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/CanonicalJson.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/Descriptor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/GraphDocument.cpp)
target_link_libraries(retroplug-node-sdk PUBLIC retroplug-node-sdk-headers reflectcpp::reflectcpp)
