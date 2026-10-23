// The executor alone, over nodes written directly against the C ABI (no authoring layer, no planner): a
// hand-built PlanView proves the block walk, coupled-group lockstep, feedback and fault bypass.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <string>
#include <vector>

#include "graph/Executor.hpp"
#include "graph/Queue.hpp"

using namespace rp::graph;

namespace {

// A raw C-ABI node. Its optional log records the calls the executor made: its name for each process()
// or advancing step(), '<' for prepare and '>' for finish.
struct RawNode {
    rp_node           header;
    float             value = 0.0f;
    int               blocks = 0;          // process() calls
    int               failAt = -1;         // process() faults on this call (0-based) and any later one
    int               stepFailAt = -1;     // step() faults once `produced` reaches this
    bool              stepGarbage = false; // step() answers a value that is not a known result
    bool              wedged = false;      // step() never makes progress
    bool              emitThenFault = false;  // push an event and a control change, then fault
    std::string*      log = nullptr;
    char              name = '?';
    std::uint32_t     produced = 0;
    std::atomic<bool> faulted{false};      // the instance-owned flag a plan step points at
};

RawNode* raw(rp_node* n) { return reinterpret_cast<RawNode*>(n); }

rp_status rawProcess(rp_node* n, const rp_process* p) {
    RawNode* self = raw(n);
    if (self->log) *self->log += self->name;
    if (self->emitThenFault) {
        const std::uint8_t note[] = {0x90, 60, 100};
        pushMessage(*p->outputs[1].u.queue, 0, note);
        pushControl(*p->outputs[2].u.control, 0, 0.5);
        return RP_FAULT;
    }
    if (self->failAt >= 0 && self->blocks++ >= self->failAt) return RP_FAULT;
    const rp_audio_view& out = p->outputs[0].u.audio;
    for (std::uint32_t c = 0; c < out.channel_count; ++c)
        for (std::uint32_t i = 0; i < p->frames; ++i) out.channels[c][i] = self->value;
    return RP_OK;
}

void rawPrepare(rp_node* n, const rp_process*) {
    raw(n)->produced = 0;
    if (raw(n)->log) *raw(n)->log += '<';
}

rp_step_result rawStep(rp_node* n, std::uint32_t target) {
    RawNode* self = raw(n);
    if (self->stepFailAt >= 0 && self->produced >= static_cast<std::uint32_t>(self->stepFailAt)) return RP_STEP_FAULT;
    if (self->stepGarbage) return 7u;
    if (self->wedged) return RP_STEP_BELOW;
    if (self->produced < target) {
        ++self->produced;
        if (self->log) *self->log += self->name;
    }
    return self->produced >= target ? RP_STEP_DONE : RP_STEP_BELOW;
}

void rawFinish(rp_node* n, const rp_process*) {
    if (raw(n)->log) *raw(n)->log += '>';
}

const rp_step_interface kStep{&rawPrepare, &rawStep, &rawFinish};

// Entries the executor never calls are left null in this test.
const rp_node_vtable kVtable{nullptr, nullptr, nullptr, nullptr, &rawProcess, nullptr, nullptr, nullptr, nullptr};

struct OneOutput {
    float        samples[16]{};
    float*       channel = samples;
    rp_port_view view{};
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
    s.faulted = &n.faulted;
    return s;
}

void makeGroup(PlanStep* first, std::uint32_t size) {
    first->groupSize = size;
    for (std::uint32_t i = 0; i < size; ++i) first[i].step = &kStep;
}

const rp_transport kTransport{48000.0, 120.0, 0.0, 0};

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

TEST_CASE("a faulting node is silenced from then on, never called again, and counted once", "[graph][executor]") {
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
    CHECK(a.blocks == 2);  // the third block bypassed process() entirely
    CHECK(faults.load() == 1);
    CHECK(a.faulted.load());
}

TEST_CASE("a faulting node's queue outputs are emptied before downstream reads them", "[graph][executor]") {
    RawNode a{{&kVtable}};
    a.emitThenFault = true;
    float samples[8]{};
    float* channel = samples;
    rp_message items[4]{};
    std::uint8_t bytes[16]{};
    rp_queue events{items, 0, 4, bytes, 0, 16, 0};
    rp_control_change changes[4]{};
    rp_control_queue control{0.25, changes, 0, 4, 0};
    rp_port_view outs[3]{};
    outs[0].kind = RP_PORT_AUDIO;
    outs[0].u.audio = rp_audio_view{&channel, 1};
    outs[1].kind = RP_PORT_EVENTS;
    outs[1].u.queue = &events;
    outs[2].kind = RP_PORT_CONTROL;
    outs[2].u.control = &control;
    PlanStep steps[1];
    steps[0].node = &a.header;
    steps[0].process.outputs = outs;
    steps[0].process.output_count = 3;
    steps[0].faulted = &a.faulted;
    PlanView view;
    view.steps = steps;
    runBlock(view, 8, kTransport);
    CHECK(events.count == 0);
    CHECK(control.count == 0);
    CHECK(control.start == 0.25);  // the stream's value is state; only this block's changes are dropped
}

TEST_CASE("a coupled group interleaves its members, block after block", "[graph][executor]") {
    std::string log;
    RawNode a{{&kVtable}}, b{{&kVtable}};
    a.log = b.log = &log;
    a.name = 'a';
    b.name = 'b';
    OneOutput oa, ob;
    PlanStep steps[] = {stepFor(a, oa), stepFor(b, ob)};
    makeGroup(steps, 2);
    PlanView view;
    view.steps = steps;
    runBlock(view, 3, kTransport);
    runBlock(view, 3, kTransport);
    CHECK(log == "<<ababab>><<ababab>>");
}

TEST_CASE("plain nodes and a coupled group run in plan order", "[graph][executor]") {
    std::string log;
    RawNode x{{&kVtable}}, a{{&kVtable}}, b{{&kVtable}}, y{{&kVtable}};
    x.log = a.log = b.log = y.log = &log;
    x.name = 'x';
    a.name = 'a';
    b.name = 'b';
    y.name = 'y';
    OneOutput ox, oa, ob, oy;
    PlanStep steps[] = {stepFor(x, ox), stepFor(a, oa), stepFor(b, ob), stepFor(y, oy)};
    makeGroup(&steps[1], 2);
    PlanView view;
    view.steps = steps;
    runBlock(view, 2, kTransport);
    CHECK(log == "x<<abab>>y");
}

TEST_CASE("a member that faults through step is bypassed while its peer finishes", "[graph][executor]") {
    std::string log;
    RawNode a{{&kVtable}}, b{{&kVtable}};
    a.log = b.log = &log;
    a.name = 'a';
    b.name = 'b';
    a.stepFailAt = 1;
    OneOutput oa, ob;
    oa.samples[0] = 7.0f;
    PlanStep steps[] = {stepFor(a, oa), stepFor(b, ob)};
    makeGroup(steps, 2);
    std::atomic<std::uint32_t> faults{0};
    PlanView view;
    view.steps = steps;
    view.faults = &faults;
    runBlock(view, 3, kTransport);
    CHECK(log == "<<abbb>");
    CHECK(a.faulted.load());
    CHECK_FALSE(b.faulted.load());
    CHECK(oa.samples[0] == 0.0f);
    CHECK(faults.load() == 1);
}

TEST_CASE("an unknown step result is treated as a fault, as an unknown process result is", "[graph][executor]") {
    RawNode a{{&kVtable}}, b{{&kVtable}};
    a.stepGarbage = true;
    OneOutput oa, ob;
    PlanStep steps[] = {stepFor(a, oa), stepFor(b, ob)};
    makeGroup(steps, 2);
    PlanView view;
    view.steps = steps;
    runBlock(view, 2, kTransport);
    CHECK(a.faulted.load());
    CHECK_FALSE(b.faulted.load());
}

TEST_CASE("a wedged member is faulted once its group passes the bound; a finished peer is not", "[graph][executor]") {
    RawNode a{{&kVtable}}, b{{&kVtable}};
    a.wedged = true;
    OneOutput oa, ob;
    PlanStep steps[] = {stepFor(a, oa), stepFor(b, ob)};
    makeGroup(steps, 2);
    std::atomic<std::uint32_t> faults{0};
    PlanView view;
    view.steps = steps;
    view.faults = &faults;
    runBlock(view, 2, kTransport);
    CHECK(a.faulted.load());
    CHECK_FALSE(b.faulted.load());
    CHECK(faults.load() == 1);
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

    const std::uint8_t later[] = {0x90, 64, 100};
    pushMessage(back, 9, later);
    runBlock(view, 16, kTransport);
    REQUIRE(front.count == 1);  // last block's message, replacing the one before
    CHECK(front.items[0].frame == 9);
}

TEST_CASE("audio feedback zeroes the tail a shorter block leaves", "[graph][executor]") {
    float backSamples[8], frontSamples[8];
    float* backCh = backSamples;
    float* frontCh = frontSamples;
    FeedbackCopy copy;
    copy.kind = RP_PORT_AUDIO;
    copy.backChannels = &backCh;
    copy.frontChannels = &frontCh;
    copy.channels = 1;
    copy.capacity = 8;
    PlanView view;
    view.feedback = std::span<const FeedbackCopy>(&copy, 1);
    std::fill(std::begin(backSamples), std::end(backSamples), 2.0f);
    runBlock(view, 8, kTransport);
    CHECK(frontSamples[7] == 2.0f);
    std::fill(std::begin(backSamples), std::end(backSamples), 1.0f);
    runBlock(view, 4, kTransport);
    CHECK(frontSamples[3] == 1.0f);
    CHECK(frontSamples[4] == 0.0f);  // not the 2.0 the previous, longer block left there
}

TEST_CASE("control feedback carries the start value and the changes", "[graph][executor]") {
    rp_control_change backChanges[4]{}, frontChanges[4]{};
    rp_control_queue back{0.5, backChanges, 0, 4, 0};
    rp_control_queue front{0.0, frontChanges, 0, 4, 0};
    pushControl(back, 3, 0.25);
    FeedbackCopy copy;
    copy.kind = RP_PORT_CONTROL;
    copy.backControl = &back;
    copy.frontControl = &front;
    rp_control_queue* roll[] = {&back};
    PlanView view;
    view.feedback = std::span<const FeedbackCopy>(&copy, 1);
    view.controlsToRoll = roll;
    runBlock(view, 8, kTransport);
    CHECK(front.start == 0.5);
    REQUIRE(front.count == 1);
    CHECK(front.changes[0].frame == 3);
    CHECK(front.changes[0].value == 0.25);
    CHECK(back.start == 0.25);  // the producer's own stream rolled on
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
