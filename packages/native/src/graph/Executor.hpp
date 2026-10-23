#pragma once

#include <cstdint>

#include "graph/Plan.hpp"

namespace rp::graph {

// A coupled group still not done after this many round-robin passes per frame has a member that is not
// making progress, and every member that is still not done is faulted. The bound is far beyond any real core (a
// SameBoy step is about one instruction, some 20 passes per sample); it is the v1 guard against a wedged
// member spinning the audio thread forever.
constexpr std::uint32_t kMaxGroupPassesPerFrame = 4096;

// Runs one block of a plan. No heap, no exceptions, no locks: the same code drives the audio thread,
// offline renders and the Pico. It walks the steps in order (a coupled group runs prepare, a round-robin
// step until every member reaches the block, then finish), then moves feedback data one block on, then
// empties output queues and rolls control streams for the next block.
//
// A node that faults (process returns RP_FAULT, step returns RP_STEP_FAULT, or its group exceeds the pass
// bound) is bypassed from then on: its outputs are silenced every block (audio zeroed, queues emptied,
// control changes dropped) and plan.faults is bumped once. Its fault flag lives on the instance, so a plan
// swap does not revive it.
void runBlock(const PlanView& plan, std::uint32_t frames, const rp_transport& transport);

}  // namespace rp::graph
