#pragma once

#include <cstdint>

#include "graph/rp_node.h"

namespace rp::host {

// The rp.launchpad package: a Novation Launchpad as a control surface on nodes of its own
// (docs/modular-graph-phase-c5-design.md section 5), through the host's surface link (rp.host.devices/1,
// "launchpad").
//
//   rp.launchpad.surface-in   in: the device's MIDI, drained from the link at the start of the block, each message at
//                             frame 0 (a control surface's timing is not musical); connected: 1 while the device is
//                             attached, from frame 0 of the block. Its first block after it binds the link discards what
//                             the link already holds (presses sent while no node read the device) rather than
//                             replaying them; a reactivate that keeps the handle is not a first block.
//   rp.launchpad.surface-out  out: every message handed to the link, in queue order, in the block it was queued.
//
// The two event ports are what a controller session reads and writes. Two nodes rather than one because a surface
// both feeds a session and follows it: one node with both directions would be a node-level cycle the planner refuses,
// and the only wire that breaks a cycle is a feedback wire, which delivers the block before. Split in two, every
// stream is in its own block and the plan holds no feedback wire.
//
// One device, one link: at most one node of either type per plan (the types are unique; the planner refuses a second).
// Two input nodes would drain the one receive ring between them, and two output nodes would split the device's LED
// frames, so the second is a refusal rather than a silently split stream.
const rp_package* launchpadPackage();

// rp.launchpad.surface-in's `in` output: a Launchpad's pad traffic and its replies, up to the bound a reader holds in
// one block.
inline constexpr std::uint32_t kSurfaceInMessages = 1024;
inline constexpr std::uint32_t kSurfaceInBytes = 16384;

}  // namespace rp::host
