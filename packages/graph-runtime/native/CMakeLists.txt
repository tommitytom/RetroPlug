# The graph runtime (docs/modular-graph-design.md, docs/modular-graph-phase-e1-design.md section 4): what turns a graph
# document into a plan and runs it. Added by retroplug_package(graph-runtime) (cmake/packages.cmake); retroplug-backend
# links it, as the engine runs every block on a GraphHost.
#
# retroplug-graph-exec: the no-heap executor and the plan loader (docs/modular-graph-phase-e3-design.md section 4). No
# heap, no exceptions and no dependencies beyond libc's memcpy / memset / strlen (it is C++20 over the C ABI in
# graph/rp_node.h), which is what lets the same code run a plan on the Pico: the device builds take it, and a device
# image loads its built-in plan with it. It takes node-sdk's headers for the ABI and the queue views, not node-sdk's
# library, which links reflect-cpp.
set(_rp_graph_exec_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/Executor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/PlanLoader.cpp)
retroplug_package_library(retroplug-graph-exec ${_rp_graph_exec_sources})
target_include_directories(retroplug-graph-exec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-graph-exec PUBLIC retroplug-node-sdk-headers)

# A device plans nothing (the desktop planned and validated the plan before export), so the device build
# (RP_DEVICE_BUILD, cmake/packages.cmake) takes the executor and the loader alone.
if(RP_DEVICE_BUILD)
    return()
endif()

# retroplug-graph-exec-device: the same sources compiled as a device compiles them (RP_DEVICE, no exceptions, no RTTI),
# into an object library nothing links, in the desktop's default build: what a <target>-device check is for a device
# entry (cmake/packages.cmake), for the one library every device image takes that is no package entry. A line the
# device cannot build fails the ordinary build and CI, not only the device toolchain's.
add_library(retroplug-graph-exec-device OBJECT ${_rp_graph_exec_sources})
set_target_properties(retroplug-graph-exec-device PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(retroplug-graph-exec-device PRIVATE RP_DEVICE)
if(NOT MSVC)
    target_compile_options(retroplug-graph-exec-device PRIVATE -fno-exceptions -fno-rtti)
endif()
target_include_directories(retroplug-graph-exec-device PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-graph-exec-device PRIVATE retroplug-node-sdk-headers)

# retroplug-graph: everything the control thread needs to turn a graph document into a plan - the registry, resources,
# the planner, GraphHost, the exporter that writes a plan for a device (PlanExport.hpp) - plus the node types the
# runtime implements itself (rp.host.audio-out, the manifest's rp_host_package).
retroplug_package_library(retroplug-graph
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/NodeRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/ResourceTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/PlanStorage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/Planner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/GraphHost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/HostNodes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/PlanExport.cpp)
target_link_libraries(retroplug-graph PUBLIC retroplug-graph-exec retroplug-node-sdk)
