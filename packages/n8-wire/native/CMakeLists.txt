# The N8 wire (docs/modular-graph-phase-f5a-design.md section 4): what a real Everdrive N8 Pro cart and the emulated
# NES FIFO that stands in for it have to agree on, and what neither owns - BlipToaster's reliable link, the one sender
# (rp::N8ReliableSender) each side drives over its own transport: the cart's desktop link (packages/n8's N8Link) in wall
# time, the emulator's FIFO role (packages/mesen's NesN8FifoRole) in emulated CPU time - and the expansion-volume range
# the emulated chip and the cart's master-volume register are both set from. Added by retroplug_package(n8-wire)
# (cmake/packages.cmake); `n8` and `mesen` both list it and neither lists the other, so the console and the cartridge
# meet at a package that is nobody's own.
#
# Header-only, so an INTERFACE target: a consumer gets this package's include root, where the two headers keep the paths
# they are included by ("transport/N8ReliableLink.hpp", "n8wire/ExpansionVolume.hpp"), and node-sdk's include root,
# for the link's frame store (util/FixedQueue.hpp, header-only: nothing here links reflect-cpp or any library). A build
# adds it too, between node-sdk and the packages that list it.
add_library(retroplug-n8-wire INTERFACE)
target_include_directories(retroplug-n8-wire INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-n8-wire INTERFACE retroplug-node-sdk-headers)
