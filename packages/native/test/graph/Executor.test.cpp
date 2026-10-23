// The executor alone, over nodes written directly against the C ABI (no authoring layer, no planner): a
// hand-built PlanView proves the block walk, coupled-group lockstep, feedback and fault bypass.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>
#include <vector>

#include "graph/Executor.hpp"
#include "graph/Queue.hpp"

using namespace rp::graph;

namespace {

// A raw C-ABI node: writes `value` to its one audio output, or faults once `failAt` blocks have run.
struct RawNode {
    rp_node       header;
    float         value = 0.0f;
    int           blocks = 0;
    int           failAt = -1;
    std::string*  log = nullptr;
    char          name = '?';
    std::uint32_t produced = 0;
};

rp_status rawProcess(rp_node* n, const rp_process* p) {
    auto* self = reinterpret_cast<RawNode*>(n);
    if (self->failAt >= 0 && self->blocks++ >= self->failAt) return RP_FAULT;
    const rp_audio_view& out = p->outputs[0].u.audio;
    for (std::uint32_t c = 0; c < out.channel_count; ++c)
        for (std::uint32_t i = 0; i < p->frames; ++i) out.channels[c][i] = self->value;
    return RP_OK;
}

void rawPrepare(rp_node* n, const rp_process*) { reinterpret_cast<RawNode*>(n)->produced = 0; }
bool rawStep(rp_node* n, std::uint32_t target) {
    auto* self = reinterpret_cast<RawNode*>(n);
    if (self->produced < target) {
        ++self->produced;
        *self->log += self->name;
    }
    return self->produced >= target;
}
void rawFinish(rp_node*, const rp_process*) {}

const rp_step_interface kStep{&rawPrepare, &rawStep, &rawFinish};

const rp_node_vtable kVtable{nullptr, nullptr, nullptr, nullptr, &rawProcess, nullptr, nullptr, nullptr, nullptr};

struct OneOutput {
    float                   samples[16]{};
    float*                  channel = samples;
    rp_port_view            view{};
    OneOutput() {
        view.kind = RP_PORT_AUDIO;
        view.u.audio = rp_audio_view{&channel, 1};
    }
};

PlanStep stepFor(RawNode& n, OneOutput& out) {
    PlanStep s;
    s.node = &n.header;
    s.process.outputs = &out.view;
    s.process.output_count = 1;
    return s;
}

const rp_transport kTransport{48000.0, 120.0, 0.0, false};

}  // namespace

TEST_CASE("runBlock runs every step and fills its outputs", "[graph][executor]") {
    RawNode a{{&kVtable}, 0.25f}, b{{&kVtable}, 0.5f};
    OneOutput oa, ob;
    PlanStep steps[] = {stepFor(a, oa), stepFor(b, ob)};
    PlanView view;
    view.steps = steps;
    runBlock(view, 16, kTransport);
    CHECK(oa.samples[15] == 0.25f);
    CHECK(ob.samples[0] == 0.5f);
    CHECK(steps[0].process.frames == 16);
    CHECK(steps[0].process.transport == &kTransport);
}

TEST_CASE("a faulting node is silenced from then on and counted once", "[graph][executor]") {
    RawNode a{{&kVtable}, 1.0f};
    a.failAt = 1;
    OneOutput oa;
    PlanStep steps[] = {stepFor(a, oa)};
    std::atomic<std::uint32_t> faults{0};
    PlanView view;
    view.steps = steps;
    view.faults = &faults;
    runBlock(view, 8, kTransport);
    CHECK(oa.samples[0] == 1.0f);
    runBlock(view, 8, kTransport);
    CHECK(oa.samples[0] == 0.0f);
    oa.samples[0] = 7.0f;
    runBlock(view, 8, kTransport);
    CHECK(oa.samples[0] == 0.0f);
    CHECK(faults.load() == 1);
    CHECK(steps[0].faulted);
}

TEST_CASE("a coupled group interleaves its members' steps", "[graph][executor]") {
    std::string log;
    RawNode a{{&kVtable}}, b{{&kVtable}};
    a.log = b.log = &log;
    a.name = 'a';
    b.name = 'b';
    OneOutput oa, ob;
    PlanStep steps[] = {stepFor(a, oa), stepFor(b, ob)};
    steps[0].groupSize = 2;
    steps[0].step = steps[1].step = &kStep;
    PlanView view;
    view.steps = steps;
    runBlock(view, 3, kTransport);
    CHECK(log == "ababab");
}

TEST_CASE("feedback moves one block on; output queues and control streams reset", "[graph][executor]") {
    rp_message items[4]{}, frontItems[4]{};
    std::uint8_t bytes[16]{}, frontBytes[16]{};
    rp_queue back{items, 0, 4, bytes, 0, 16, 0};
    rp_queue front{frontItems, 0, 4, frontBytes, 0, 16, 0};
    rp_control_change changes[4]{};
    rp_control_queue control{0.0, changes, 0, 4, 0};

    const std::uint8_t note[] = {0x90, 60, 100};
    pushMessage(back, 5, note);
    pushControl(control, 2, 0.75);

    FeedbackCopy copy;
    copy.kind = RP_PORT_EVENTS;
    copy.backQueue = &back;
    copy.frontQueue = &front;
    rp_queue* clear[] = {&back};
    rp_control_queue* roll[] = {&control};
    PlanView view;
    view.feedback = std::span<const FeedbackCopy>(&copy, 1);
    view.queuesToClear = clear;
    view.controlsToRoll = roll;
    runBlock(view, 16, kTransport);

    CHECK(back.count == 0);
    REQUIRE(front.count == 1);
    CHECK(front.items[0].frame == 5);
    CHECK(messageData(front, front.items[0])[1] == 60);
    CHECK(control.start == 0.75);
    CHECK(control.count == 0);
}

TEST_CASE("queues drop, and count, what does not fit", "[graph][executor]") {
    rp_message items[2]{};
    std::uint8_t bytes[4]{};
    rp_queue q{items, 0, 2, bytes, 0, 4, 0};
    const std::uint8_t three[] = {1, 2, 3};
    CHECK(pushMessage(q, 0, three));
    CHECK_FALSE(pushMessage(q, 1, three));  // bytes full
    CHECK(q.dropped == 1);
    CHECK(pushMessage(q, 0, std::span<const std::uint8_t>(three, 1)));  // an earlier frame is clamped up
    CHECK(q.items[1].frame == 0);
    CHECK_FALSE(pushMessage(q, 3, std::span<const std::uint8_t>()));  // items full
    CHECK(q.dropped == 2);
}
