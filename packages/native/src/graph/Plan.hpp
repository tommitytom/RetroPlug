#pragma once

#include <atomic>
#include <cstdint>
#include <span>

#include "graph/rp_node.h"

namespace rp::graph {

// The executor's view of a plan: flat, pre-bound, and owned elsewhere. On the desktop the planner builds
// it inside a PlanStorage (heap, control thread); on the Pico it would point into a static arena. Nothing
// the executor reads from here is ever resized while a block runs.

struct PlanStep {
    rp_node*   node = nullptr;
    rp_process process{};  // inputs / outputs / params already bound; frames + transport set each block
    // > 0 on the FIRST member of a coupled group: the group is this step and the next groupSize - 1.
    std::uint32_t            groupSize = 0;
    const rp_step_interface* step = nullptr;  // set for every member of a coupled group
    // Required. Owned by the node instance, not the plan, so a node that faulted stays bypassed through
    // every later plan swap. Written only by the audio thread; atomic so the control thread may read it.
    std::atomic<bool>*       faulted = nullptr;
};

// A feedback wire's one block of delay: what the producer wrote this block (`back`) becomes what the
// consumer reads next block (`front`). Both buffers belong to the wire, not the plan, so a swap that
// keeps the wire keeps the data in flight.
struct FeedbackCopy {
    rp_port_kind kind = RP_PORT_AUDIO;
    // audio
    float* const* backChannels = nullptr;
    float* const* frontChannels = nullptr;
    std::uint32_t channels = 0;
    std::uint32_t capacity = 0;  // frames each front channel holds; the tail past this block is zeroed
    // events / bytes
    rp_queue* backQueue = nullptr;
    rp_queue* frontQueue = nullptr;
    // control
    rp_control_queue* backControl = nullptr;
    rp_control_queue* frontControl = nullptr;
};

struct PlanView {
    std::span<PlanStep>                steps;
    std::span<rp_queue* const>         queuesToClear;   // every output queue, emptied after the block
    std::span<rp_control_queue* const> controlsToRoll;  // every control output + param stream
    std::span<const FeedbackCopy>      feedback;
    std::atomic<std::uint32_t>*        faults = nullptr;  // bumped once per node that faults
};

}  // namespace rp::graph
