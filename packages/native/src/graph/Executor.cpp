#include "graph/Executor.hpp"

#include <cassert>
#include <cstring>

#include "graph/Queue.hpp"

namespace rp::graph {

namespace {

// What a bypassed node leaves on its outputs: silence, empty queues, no control changes (a control stream
// keeps its start value, since that is state, not something this block produced) and no resource handle.
// The last matters for safety: a resource output slot outlives plans, so a writer that faulted would
// otherwise keep publishing a handle that only an old plan pinned, and readers would see it freed.
void silenceOutputs(const rp_process& p, std::uint32_t frames) {
    for (std::uint32_t i = 0; i < p.output_count; ++i) {
        const rp_port_view& v = p.outputs[i];
        switch (v.kind) {
            case RP_PORT_AUDIO:
                for (std::uint32_t c = 0; c < v.u.audio.channel_count; ++c)
                    std::memset(v.u.audio.channels[c], 0, frames * sizeof(float));
                break;
            case RP_PORT_EVENTS:
            case RP_PORT_BYTES:
                clearQueue(*v.u.queue);
                break;
            case RP_PORT_CONTROL:
                v.u.control->count = 0;
                break;
            case RP_PORT_RESOURCE:
                if (v.u.resource) *v.u.resource = nullptr;
                break;
            case RP_PORT_LINK:
            default:
                break;
        }
    }
}

// Flag the node (once) and count it.
void markFaulted(const PlanView& plan, PlanStep& s) {
    if (!s.faulted->exchange(true, std::memory_order_relaxed) && plan.faults)
        plan.faults->fetch_add(1, std::memory_order_relaxed);
}

bool isFaulted(const PlanStep& s) { return s.faulted->load(std::memory_order_relaxed); }

void runNode(const PlanView& plan, PlanStep& s, std::uint32_t frames) {
    if (!isFaulted(s) && s.node->vt->process(s.node, &s.process) != RP_OK) markFaulted(plan, s);
    if (isFaulted(s)) silenceOutputs(s.process, frames);
}

void runGroup(const PlanView& plan, std::span<PlanStep> group, std::uint32_t frames) {
    for (auto& m : group) {
        assert(m.step && "every member of a coupled group implements rp.step/1");
        if (!isFaulted(m)) m.step->prepare(m.node, &m.process);
    }
    // Round-robin until every member has produced the block. Each step() advances a member by a bounded
    // amount (an instruction, a scanline), which is what keeps linked cores in lockstep.
    const std::uint64_t maxPasses = std::uint64_t(frames ? frames : 1) * kMaxGroupPassesPerFrame;
    for (std::uint64_t pass = 0;; ++pass) {
        bool allDone = true;
        for (auto& m : group) {
            if (isFaulted(m)) continue;
            const rp_step_result r = m.step->step(m.node, frames);
            if (r == RP_STEP_BELOW) allDone = false;
            else if (r != RP_STEP_DONE) markFaulted(plan, m);  // RP_STEP_FAULT, or a value we do not know
        }
        if (allDone) break;
        if (pass + 1 >= maxPasses) {
            // Out of passes: whoever is still not done is not making progress. Asking again is safe by
            // contract - a member that finished just answers RP_STEP_DONE.
            for (auto& m : group)
                if (!isFaulted(m) && m.step->step(m.node, frames) != RP_STEP_DONE) markFaulted(plan, m);
            break;
        }
    }
    for (auto& m : group) {
        if (isFaulted(m)) silenceOutputs(m.process, frames);
        else m.step->finish(m.node, &m.process);
    }
}

void moveFeedback(const FeedbackCopy& f, std::uint32_t frames) {
    switch (f.kind) {
        case RP_PORT_AUDIO:
            for (std::uint32_t c = 0; c < f.channels; ++c) {
                std::memcpy(f.frontChannels[c], f.backChannels[c], frames * sizeof(float));
                // A shorter block than the last must not leave the last block's tail to be replayed.
                if (f.capacity > frames)
                    std::memset(f.frontChannels[c] + frames, 0, (f.capacity - frames) * sizeof(float));
            }
            break;
        case RP_PORT_EVENTS:
        case RP_PORT_BYTES:
            clearQueue(*f.frontQueue);
            appendQueue(*f.frontQueue, *f.backQueue);
            break;
        case RP_PORT_CONTROL: {
            rp_control_queue& front = *f.frontControl;
            const rp_control_queue& back = *f.backControl;
            front.start = back.start;
            front.count = 0;
            for (std::uint32_t i = 0; i < back.count; ++i)
                pushControl(front, back.changes[i].frame, back.changes[i].value);
            break;
        }
        case RP_PORT_LINK:
        case RP_PORT_RESOURCE:
        default:
            break;
    }
}

}  // namespace

void runBlock(const PlanView& plan, std::uint32_t frames, const rp_transport& transport) {
    for (auto& s : plan.steps) {
        assert(s.faulted && "every plan step points at its instance's fault flag");
        s.process.frames = frames;
        s.process.transport = &transport;
    }
    for (std::size_t i = 0; i < plan.steps.size();) {
        PlanStep& s = plan.steps[i];
        if (s.groupSize > 1) {
            assert(i + s.groupSize <= plan.steps.size());
            runGroup(plan, plan.steps.subspan(i, s.groupSize), frames);
            i += s.groupSize;
        } else {
            runNode(plan, s, frames);
            ++i;
        }
    }
    for (const auto& f : plan.feedback) moveFeedback(f, frames);
    for (rp_queue* q : plan.queuesToClear) clearQueue(*q);
    for (rp_control_queue* c : plan.controlsToRoll) rollControl(*c);
}

}  // namespace rp::graph
