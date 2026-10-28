#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "graph/rp_node.h"

namespace rp::host {

// The engine's host I/O nodes (docs/modular-graph-phase-b3a-design.md section 3.3), each over the engine's
// rp.engine.host-io/2 service, without which it will not activate:
//
//   rp.host.midi-in         out midi: the host MIDI the engine staged, stable-sorted by frame
//   rp.host.midi-out        {"systems": [<id>, ...]}: inputs in1 to inN, one per entry, whose messages land in
//                           Engine::midiOut tagged with that entry's system, input by input, each in queue order and
//                           none earlier in frame than one before it
//                           (docs/modular-graph-phase-b3b-design.md section 9); two entries may name one system.
//                           With no systems (the default) it has no inputs and sends nothing
//   rp.host.controller-in   out midi: the staged control-surface MIDI, at frame 0; out connected: 1 while a
//                           control surface is attached, from frame 0 of the block
//   rp.host.controller-out  in midi: appended to Engine::controllerOut
//
// The host keeps staging and draining as it does; each input node takes what is staged at the top of its block, and a
// plan without one drops what it would have taken at the block's end. An input node owns its staging whether or not
// its output is wired: it takes everything staged, and an unwired output goes nowhere. So a document holds one
// rp.host.midi-in and one rp.host.controller-in at most (Engine::setGraph refuses a second), and a plan holding one
// should wire it.
const rp_package* hostIoPackage();

// rp.n8.cart-mirror (section 3.5), {"system": <id>}: everything on its midi input, then everything on its bytes
// input (each with its flush flag), handed to the physical N8 the engine feeds, in queue order. The system it names
// is the one the cart mirrors, which the engine reads off the plan: a NES of the document's, one mirror at most.
const rp_package* n8Package();

// The system a cart mirror's settings (JSON) name, read as the node reads them, so the engine and the node accept
// the same settings: a field the node does not know is ignored, a missing or out-of-range system refused (nullopt).
std::optional<std::uint32_t> cartMirrorSystem(std::string_view settings);

// The most inputs rp.host.midi-out's systems form takes: it copies their ids into a fixed array at each commit, which
// must not allocate. A host MIDI out producer for every system an engine holds (SnapshotRegistry::kMaxSlots, 64) and
// a controller's ahead of them fit, with room. Each input's queue is its producer's, sized at that output, since a wire
// carries its source's queue.
inline constexpr std::uint32_t kMidiOutMaxInputs = 128;

// rp.host.midi-in's output: room for a dense block of DAW MIDI, the frame-0 CCs of moved automation and a large
// SysEx (a sample or wave upload to a NES build, which the staging has always taken whole), with margin. The protocol
// nodes' outputs that pass host MIDI on (a NES's midi, a Game Boy's serialIn) hold as many bytes and 8 KiB more for a
// role's own (src/script/nodes/ports.ts CORE_MIDI and SERIAL_IN), so what passes here reaches the system. A message
// past it is dropped and counted (GraphHost::droppedMessages).
inline constexpr std::uint32_t kHostMidiInMessages = 2048;
inline constexpr std::uint32_t kHostMidiInBytes = 65536;
// rp.host.controller-in's: a Launchpad's pad traffic and its replies.
inline constexpr std::uint32_t kControllerInMessages = 1024;
inline constexpr std::uint32_t kControllerInBytes = 16384;

}  // namespace rp::host
