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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/PackageMemory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/HostNodes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/PlanExport.cpp)
target_link_libraries(retroplug-graph PUBLIC retroplug-graph-exec retroplug-node-sdk)

# The MicroQuickJS script host (docs/modular-graph-phase-e4-design.md section 4, src/script/MqjsHost.hpp): a device
# script pack's nodes on the engine the box runs, built here for the desktop (no device image links it yet).
#
# rp_mqjs_stdlib: RetroPlug's standard library for the engine (src/script/rp_mqjs.h), its tables generated at the host's
# width and the box's 32 bits (cmake/mquickjs.cmake). retroplug-mqjs-engine: the engine with those tables, compiled
# from src/script/rp_mqjs_engine.c (mquickjs.c whole, then RetroPlug's reads of its JSContext), a C library a binary
# links once, whichever builds of the host it holds (RP_DEVICE_MODE_SHARED: retroplug_device_mode_objects links
# it as it is, rather than compiling it again as a device would, since it names nothing in namespace rp).
# retroplug-mqjs-host: the host, which a script pack's registration (cmake/packages.cmake) links.
set(_rp_script_dir ${CMAKE_CURRENT_SOURCE_DIR}/src/script)
retroplug_mquickjs_stdlib(rp_mqjs_stdlib SOURCE ${_rp_script_dir}/rp_mqjs_stdlib.c)
retroplug_mquickjs_engine(retroplug-mqjs-engine STDLIB rp_mqjs_stdlib SOURCES ${_rp_script_dir}/rp_mqjs_table.c
                          ENGINE ${_rp_script_dir}/rp_mqjs_engine.c INCLUDES ${_rp_script_dir})
set_target_properties(retroplug-mqjs-engine PROPERTIES RP_DEVICE_MODE_SHARED ON)
retroplug_package_library(retroplug-mqjs-host ${_rp_script_dir}/MqjsHost.cpp)
target_include_directories(retroplug-mqjs-host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-mqjs-host PUBLIC retroplug-mqjs-engine retroplug-node-sdk-headers
                                          PRIVATE retroplug-node-sdk)

# retroplug-mqjs-host-device: the host compiled as a device compiles it (RP_DEVICE, no exceptions, no RTTI), into an
# object library nothing links, as retroplug-graph-exec-device is for the loader: a line the device cannot build fails
# the ordinary build.
add_library(retroplug-mqjs-host-device OBJECT ${_rp_script_dir}/MqjsHost.cpp)
set_target_properties(retroplug-mqjs-host-device PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(retroplug-mqjs-host-device PRIVATE RP_DEVICE)
if(NOT MSVC)
    target_compile_options(retroplug-mqjs-host-device PRIVATE -fno-exceptions -fno-rtti)
endif()
target_include_directories(retroplug-mqjs-host-device PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                                                              ${CMAKE_SOURCE_DIR}/deps/mquickjs)
target_link_libraries(retroplug-mqjs-host-device PRIVATE retroplug-node-sdk-headers)

# retroplug-mqjs-run (../mqjs-run/main.cpp): the device-pack toolchain's runner. It runs a pack's transcript and holds
# the pack to its declared heap on RetroPlug's own standard library and this host, as the desktop and the box run it,
# rather than on upstream's REPL. Built by name: a script pack's toolchain target depends on it.
add_executable(retroplug-mqjs-run EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/../mqjs-run/main.cpp)
set_target_properties(retroplug-mqjs-run PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
                                                    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_link_libraries(retroplug-mqjs-run PRIVATE retroplug-mqjs-host retroplug-node-sdk)
