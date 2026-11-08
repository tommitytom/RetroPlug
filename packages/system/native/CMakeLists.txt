# The system package (docs/modular-graph-phase-f2-design.md section 2): the contract every console implements and the
# engine drives, and the console-neutral plumbing around it. Added by retroplug_package(system) (cmake/packages.cmake);
# a product that lists it registers nothing (its manifest names a target and no entry), but every console's package is
# built on it beside the shell.
#
# Its include root, src/, is where each of these keeps the path it is included by ("system/SystemBase.hpp",
# "transport/MidiTypes.hpp", "util/Gain.hpp"), so a core's source reads the same whichever package it sits in.
#
# It depends on node-sdk and nothing else: DebugTarget.hpp's register and profiler types ride reflect-cpp's
# rfl/Bytestring.hpp, which node-sdk brings (its library links reflect-cpp), and the SystemNode template is written in
# the SDK's node authoring layer (graph/node.hpp). Nothing here includes rpcpp.
retroplug_package_library(retroplug-system
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/SystemBase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/SystemCapabilities.cpp
    # processAlive only, so <windows.h> stays in one TU rather than in every includer of the header (the NES core pulls
    # it in through NesEverdriveFifo.hpp).
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util/ProcessId.cpp)
target_include_directories(retroplug-system PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-system PUBLIC retroplug-node-sdk)
