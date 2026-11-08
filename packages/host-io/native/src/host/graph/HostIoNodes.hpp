#pragma once

#include <cstdint>

#include "graph/rp_node.h"

namespace rp::host {

// The engine's host I/O nodes (docs/modular-graph-phase-b3a-design.md section 3.3), each over the engine's
// rp.engine.host-io/3 service, without which it will not activate:
//
//   rp.host.midi-in         out midi: the host MIDI the engine staged, stable-sorted by frame
//   rp.host.midi-out        {"systems": [<id>, ...]}: inputs in1 to inN, one per entry, whose messages land in
//                           Engine::midiOut tagged with that entry's system, merged by frame, ties in input order,
//                           none earlier in frame than one before it and none past the block's last frame
//                           (docs/modular-graph-phase-b3b-design.md section 9, docs/lsdj-serial-out-design.md
//                           section 5); two entries may name one system. With no systems (the default) it has no
//                           inputs and sends nothing
//
// The host keeps staging and draining as it does; the input node takes what is staged at the top of its block, and a
// plan without one drops what it would have taken at the block's end. It owns its staging whether or not its output
// is wired: it takes everything staged, and an unwired output goes nowhere. So a document holds one rp.host.midi-in at
// most (Engine::setGraph refuses a second), and a plan holding one should wire it. A document holds one
// rp.host.midi-out at most too (Engine::setGraph refuses a second), which keeps Engine::midiOut frame-sorted: a second
// would append its own merge behind the first's.
//
// A control surface's traffic is not the host's: rp.launchpad.surface-in takes what its device sent and
// rp.launchpad.surface-out writes back to it (packages/launchpad's SurfaceNodes.hpp), through the device link a host
// installs.
//
// The package itself (rp_host_io_package, the entry its manifest names) is no declaration of this header's: it has no
// caller but the generated registration, as each console package's is its own translation unit's.

// The most inputs rp.host.midi-out's systems form takes: it copies their ids into a fixed array at each commit, which
// must not allocate. A host MIDI out producer for every system an engine holds (SnapshotRegistry::kMaxSlots, 64) and
// a controller's ahead of them fit, with room. Each input's queue is its producer's, sized at that output, since a wire
// carries its source's queue.
inline constexpr std::uint32_t kMidiOutMaxInputs = 128;

// rp.host.midi-in's output: room for a dense block of DAW MIDI, the frame-0 CCs of moved automation and a large
// SysEx (a sample or wave upload to a NES build, which the staging has always taken whole), with margin. The protocol
// nodes' outputs that pass host MIDI on (a NES's midi, a Game Boy's serialIn) hold as many bytes and 8 KiB more for a
// role's own (packages/core/ts/ports.ts CORE_MIDI and SERIAL_IN), so what passes here reaches the system. A message
// past it is dropped and counted (GraphHost::droppedMessages).
inline constexpr std::uint32_t kHostMidiInMessages = 2048;
inline constexpr std::uint32_t kHostMidiInBytes = 65536;

}  // namespace rp::host

// The package's entry, named in packages/native/package.json: what a product's generated registration calls
// (rp_register_packages, graph/Packages.hpp).
extern "C" const rp_package* rp_host_io_package(void);
