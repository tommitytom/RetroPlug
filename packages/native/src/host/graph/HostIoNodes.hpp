#pragma once

#include <cstdint>

#include "graph/rp_node.h"

namespace rp::host {

// The engine's host I/O nodes (docs/modular-graph-phase-b3a-design.md section 3.3), each over the engine's
// rp.engine.host-io/1 service, without which it will not activate:
//
//   rp.host.midi-in         out midi: the host MIDI the engine staged, stable-sorted by frame
//   rp.host.midi-out        in midi: each message's payload is a 4-byte little-endian system id, then the MIDI,
//                           which lands in Engine::midiOut tagged with that system (section 4.4)
//   rp.host.controller-in   out midi: the staged control-surface MIDI, at frame 0; out connected: 1 while a
//                           control surface is attached, from frame 0 of the block
//   rp.host.controller-out  in midi: appended to Engine::controllerOut
//
// The host keeps staging and draining as it does; each input node takes what is staged at the top of its block.
const rp_package* hostIoPackage();

// rp.n8.cart-mirror (section 3.5), {"system": <id>}: everything on its midi input, then everything on its bytes
// input (each with its flush flag), handed to the physical N8 the engine feeds, in queue order. The system it names
// is the one the cart mirrors, which the engine reads off the plan.
const rp_package* n8Package();

// rp.host.midi-in's output: room for a dense block of DAW MIDI, the frame-0 CCs of moved automation and a SysEx
// (a wave upload), with margin. A message past it is dropped and counted (GraphHost::droppedMessages).
inline constexpr std::uint32_t kHostMidiInMessages = 2048;
inline constexpr std::uint32_t kHostMidiInBytes = 32768;
// rp.host.controller-in's: a Launchpad's pad traffic and its replies.
inline constexpr std::uint32_t kControllerInMessages = 1024;
inline constexpr std::uint32_t kControllerInBytes = 16384;

}  // namespace rp::host
