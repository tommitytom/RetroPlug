# The graph runtime (docs/modular-graph-design.md, docs/modular-graph-phase-e1-design.md section 4): what turns a graph
# document into a plan and runs it. Added by retroplug_package(graph-runtime) (cmake/packages.cmake); retroplug-backend
# links it, as the engine runs every block on a GraphHost.
#
# retroplug-graph-exec: the no-heap executor. No heap, no exceptions and no dependencies beyond libc's memcpy / memset
# (it is C++20 over the C ABI in graph/rp_node.h), which is what lets the same code run a plan on the Pico later. It
# takes node-sdk's headers for the ABI and the queue views, not node-sdk's library, which links reflect-cpp.
retroplug_package_library(retroplug-graph-exec
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/Executor.cpp)
target_include_directories(retroplug-graph-exec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-graph-exec PUBLIC retroplug-node-sdk-headers)

# A device plans nothing (the desktop planned and validated the plan before export), so the device build
# (RP_DEVICE_BUILD, cmake/packages.cmake) takes the executor alone.
if(RP_DEVICE_BUILD)
    return()
endif()

# retroplug-graph: everything the control thread needs to turn a graph document into a plan - the registry, resources,
# the planner, GraphHost - plus the node types the runtime implements itself (rp.host.audio-out, the manifest's
# rp_host_package).
retroplug_package_library(retroplug-graph
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/NodeRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/ResourceTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/PlanStorage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/Planner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/GraphHost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/HostNodes.cpp)
target_link_libraries(retroplug-graph PUBLIC retroplug-graph-exec retroplug-node-sdk)
