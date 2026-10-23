#include "graph/Executor.hpp"

#include <cstring>

#include "graph/Queue.hpp"

namespace rp::graph {

namespace {

void silenceOutputs(const rp_process& p, std::uint32_t frames) {
    for (std::uint32_t i = 0; i < p.output_count; ++i) {
        const rp_port_view& v = p.outputs[i];
        if (v.kind != RP_PORT_AUDIO) continue;
        for (std::uint32_t c = 0; c < v.u.audio.channel_count; ++c)
            std::memset(v.u.audio.channels[c], 0, frames * sizeof(float));
    }
}

void fault(const PlanView& plan, PlanStep& s, std::uint32_t frames) {
    if (!s.faulted) {
        s.faulted = true;
        if (plan.faults) plan.faults->fetch_add(1, std::memory_order_relaxed);
    }
    silenceOutputs(s.process, frames);
}

void runNode(const PlanView& plan, PlanStep& s, std::uint32_t frames) {
    if (s.faulted) {
        silenceOutputs(s.process, frames);
        return;
    }
    if (s.node->vt->process(s.node, &s.process) != RP_OK) fault(plan, s, frames);
}

void runGroup(std::span<PlanStep> group, std::uint32_t frames) {
    for (auto& m : group)
        if (!m.faulted) m.step->prepare(m.node, &m.process);
    // Round-robin until every member has produced the block. Each step() call advances a member by a
    // bounded amount (an instruction or a scanline), which is what keeps linked cores in lockstep.
    bool allDone = false;
    while (!allDone) {
        allDone = true;
        for (auto& m : group)
            if (!m.faulted && !m.step->step(m.node, frames)) allDone = false;
    }
    for (auto& m : group) {
        if (m.faulted) silenceOutputs(m.process, frames);
        else m.step->finish(m.node, &m.process);
    }
}

void moveFeedback(const FeedbackCopy& f, std::uint32_t frames) {
    switch (f.kind) {
        case RP_PORT_AUDIO:
            for (std::uint32_t c = 0; c < f.channels; ++c)
                std::memcpy(f.frontChannels[c], f.backChannels[c], frames * sizeof(float));
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
        default:
            break;
    }
}

}  // namespace

void runBlock(const PlanView& plan, std::uint32_t frames, const rp_transport& transport) {
    for (auto& s : plan.steps) {
        s.process.frames = frames;
        s.process.transport = &transport;
    }
    for (std::size_t i = 0; i < plan.steps.size();) {
        PlanStep& s = plan.steps[i];
        if (s.groupSize > 1) {
            runGroup(plan.steps.subspan(i, s.groupSize), frames);
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
