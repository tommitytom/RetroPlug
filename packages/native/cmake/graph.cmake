# Included by packages/native/CMakeLists.txt, which is the only caller. `include()` keeps the
# CURRENT directory scope, so ${CMAKE_CURRENT_SOURCE_DIR} still means packages/native/ here.
#
# --- the node-graph engine (docs/modular-graph-design.md) -------------------------------------------
# Phase A builds these and tests them; nothing in the shipping build links them yet.
#
# retroplug-graph-exec: the no-heap executor. It has no dependencies at all (freestanding C++20 over the
# C ABI in graph/rp_node.h), which is what lets the same code run a plan on the Pico later.
add_library(retroplug-graph-exec STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/Executor.cpp)
target_include_directories(retroplug-graph-exec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(retroplug-graph-exec PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)   # will be linked into the plugin .so
