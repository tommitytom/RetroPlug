# The rp.core node types (graph/nodes/CoreNodes.cpp): routing, delay, constants and the buffer player and recorder.
# Added by retroplug_package(core) (cmake/packages.cmake). Like any pack, it depends on node-sdk alone.
retroplug_package_library(retroplug-core-nodes
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/nodes/CoreNodes.cpp)
target_include_directories(retroplug-core-nodes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-core-nodes PUBLIC retroplug-node-sdk)
