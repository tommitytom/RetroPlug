# Included by packages/native/CMakeLists.txt, which is the only caller. `include()` keeps the
# CURRENT directory scope, so ${CMAKE_CURRENT_SOURCE_DIR} still means packages/native/ here.
#
# --- the node-graph engine (docs/modular-graph-design.md) -------------------------------------------
# retroplug-backend links them: the engine (src/host/engine/Engine.cpp) runs every block on a GraphHost.
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

# retroplug-dsp-nodes: the rp.dsp package (src/graph/nodes/DspNodes.cpp) - gain, biquad, dither, normalize and dpcm,
# each a node around the kernels of src/dsp/ (docs/modular-graph-phase-d1-design.md section 5). A target of its own
# rather than a source of retroplug-graph: retroplug-graph is what every consumer of the engine links (the plugin, the
# CLI, the test hosts), and the package brings the DSP library with it, which retroplug-graph has no business carrying.
# It links retroplug-dsp-kernels alone, never retroplug-dsp-resample, so nothing here reaches r8brain, pthreads or
# RTTI, and it stays core-free like the other packages (no core, txiki, efsw or LVGL): retroplug-graph-test plans and
# runs these nodes with only the graph library and the kernels.
add_library(retroplug-dsp-nodes STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/nodes/DspNodes.cpp)
target_link_libraries(retroplug-dsp-nodes PUBLIC retroplug-graph PRIVATE retroplug-dsp-kernels)
set_target_properties(retroplug-dsp-nodes PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)   # linked into the plugin .so through retroplug-backend

# retroplug-dsp-resample-nodes: the rp.dsp.resample package (src/graph/nodes/DspResampleNodes.cpp), the resampler as a
# node, apart from retroplug-dsp-nodes because its kernel is retroplug-dsp-resample: r8brain, and with it the heap,
# pthreads and RTTI. A build for a target that cannot take those (the Pico's) links retroplug-dsp-nodes and
# retroplug-dsp-kernels alone and leaves dspResamplePackage() out of its registration list, so neither this node nor
# r8brain reaches its binary; the planner's refusal of a `heap` node there is the second line, not the only one.
add_library(retroplug-dsp-resample-nodes STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/nodes/DspResampleNodes.cpp)
target_link_libraries(retroplug-dsp-resample-nodes PUBLIC retroplug-graph PRIVATE retroplug-dsp-resample)
set_target_properties(retroplug-dsp-resample-nodes PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)   # linked into the plugin .so through retroplug-backend
