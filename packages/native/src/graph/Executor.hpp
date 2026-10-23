#pragma once

#include <cstdint>

#include "graph/Plan.hpp"

namespace rp::graph {

// Runs one block of a plan. No heap, no exceptions, no locks: the same code drives the audio thread,
// offline renders and the Pico. It walks the steps in order (a coupled group runs prepare, a round-robin
// step until every member reaches the block, then finish), then moves feedback data one block on, then
// empties output queues and rolls control streams for the next block.
//
// A node whose process returns RP_FAULT is bypassed from then on: its audio outputs are silenced every
// block and plan.faults is bumped once, so a wedged node can never take the rest of the graph down.
void runBlock(const PlanView& plan, std::uint32_t frames, const rp_transport& transport);

}  // namespace rp::graph
