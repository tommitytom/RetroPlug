# The host-io pack's native half (docs/modular-graph-phase-f2-design.md section 2): the host's own I/O as node types,
# rp.host.midi-in and rp.host.midi-out, and the engine service they read (rp.engine.host-io/3, host/graph/HostServices.hpp)
# with the staging bounds the engine sizes its queues from. Added by retroplug_package(host-io) (cmake/packages.cmake).
#
# It depends on the node SDK alone and includes nothing of the shell's; the engine implements the service, and the
# engine's own translation unit reads the bounds, which is the shell's edge on this package and not this package's on the
# shell.
#
# Its include root is src/, and the include paths keep their text: "host/graph/HostIoNodes.hpp" and
# "host/graph/HostServices.hpp" are what the engine and the tests spell.

retroplug_package_library(retroplug-host-io
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/graph/HostIoNodes.cpp)
target_include_directories(retroplug-host-io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-host-io PUBLIC retroplug-node-sdk)
