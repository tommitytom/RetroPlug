#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "graph/rp_node.h"

namespace rp::host {

// The engine's host I/O nodes (docs/modular-graph-phase-b3a-design.md section 3.3), each over the engine's
// rp.engine.host-io/1 service, without which it will not activate:
//
//   rp.host.midi-in         out midi: the host MIDI the engine staged, stable-sorted by frame
//   rp.host.midi-out        in midi: each message's payload is a 4-byte little-endian system id, then the MIDI,
//                           which lands in Engine::midiOut tagged with that system (section 4.4); a message too
//                           short to carry the id is dropped and counted (Engine::droppedMessages)
//   rp.host.controller-in   out midi: the staged control-surface MIDI, at frame 0; out connected: 1 while a
//                           control surface is attached, from frame 0 of the block
//   rp.host.controller-out  in midi: appended to Engine::controllerOut
//
// The host keeps staging and draining as it does; each input node takes what is staged at the top of its block.
// An input node owns its staging whether or not its output is wired: the engine hands the kernel stage none of it
// while the plan holds the node, and an unwired output goes nowhere. So a document holds one rp.host.midi-in and
// one rp.host.controller-in at most (Engine::setGraph refuses a second), and a plan holding one should wire it.
const rp_package* hostIoPackage();

// rp.n8.cart-mirror (section 3.5), {"system": <id>}: everything on its midi input, then everything on its bytes
// input (each with its flush flag), handed to the physical N8 the engine feeds, in queue order. The system it names
// is the one the cart mirrors, which the engine reads off the plan: a NES of the document's, one mirror at most.
const rp_package* n8Package();

// The system a cart mirror's settings (JSON) name, read as the node reads them, so the engine and the node accept
// the same settings: a field the node does not know is ignored, a missing or out-of-range system refused (nullopt).
std::optional<std::uint32_t> cartMirrorSystem(std::string_view settings);

// rp.host.midi-in's output: room for a dense block of DAW MIDI, the frame-0 CCs of moved automation and a large
// SysEx (a sample or wave upload to a NES build, which the staging has always taken whole), with margin. The kernel's
// sys-<id>.midi and serialIn outputs hold as many bytes (src/script/kernelNode.ts KERNEL_CORE_MIDI and
// KERNEL_SERIAL_IN), so what passes here reaches the system. A message past it is dropped and counted
// (GraphHost::droppedMessages).
inline constexpr std::uint32_t kHostMidiInMessages = 2048;
inline constexpr std::uint32_t kHostMidiInBytes = 65536;
// rp.host.controller-in's: a Launchpad's pad traffic and its replies.
inline constexpr std::uint32_t kControllerInMessages = 1024;
inline constexpr std::uint32_t kControllerInBytes = 16384;

}  // namespace rp::host
