# Included by packages/native/CMakeLists.txt, which is the only caller. `include()` keeps the
# CURRENT directory scope, so ${CMAKE_CURRENT_SOURCE_DIR} still means packages/native/ here.
#
# --- the node-graph engine (docs/modular-graph-design.md) -------------------------------------------
# Phase A builds these and tests them; nothing in the shipping build links them yet.
#
# retroplug-graph-exec: the no-heap executor. No heap, no exceptions and no dependencies beyond libc's
# memcpy / memset (it is C++20 over the C ABI in graph/rp_node.h), which is what lets the same code run a
# plan on the Pico later.
add_library(retroplug-graph-exec STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/Executor.cpp)
target_include_directories(retroplug-graph-exec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(retroplug-graph-exec PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)   # will be linked into the plugin .so

# retroplug-graph: everything the control thread needs to turn a graph document into a plan - canonical
# JSON, descriptors, the registry, resources, the planner, GraphHost - plus the host-implemented nodes and
# the rp.core package. Links reflect-cpp (settings parsing, JSON Schema).
add_library(retroplug-graph STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/CanonicalJson.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/Descriptor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/GraphDocument.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/NodeRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/ResourceTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/PlanStorage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/Planner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/GraphHost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/HostNodes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/nodes/CoreNodes.cpp)
target_link_libraries(retroplug-graph PUBLIC retroplug-graph-exec reflectcpp::reflectcpp)
set_target_properties(retroplug-graph PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
