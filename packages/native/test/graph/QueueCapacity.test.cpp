// Queue capacity (docs/modular-graph-phase-b3a-design.md section 2.6): an events or bytes output may declare how many
// messages and payload bytes its queue carries. The planner sizes each output queue from its port and each feedback
// front from the wire's source, a port that declares nothing keeps today's 256 messages and 4096 bytes, and a
// capacity past the caps, on an input, or taking a plan's queues past their budget is refused. What a full queue
// drops is published by the thread that runs the block, for the host to read on the control thread.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iterator>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "GraphRig.hpp"

using namespace rp::graph;
using rp::graph::test::GraphRig;

namespace {

constexpr std::uint32_t kPayloadBytes = 1u << 16;

// Each block, `count` messages of `size` bytes on its events output, whose queue capacity its settings declare (a
// part left 0 keeps the default). `zero` declares an explicit 0 messages and `audio` an audio output with a
// capacity, both of which the C++ layer refuses as the settings. Counts the instances ever made.
struct BurstSettings {
    std::uint32_t count = 1;
    std::uint32_t size = 3;
    std::uint32_t messages = 0;
    std::uint32_t bytes = 0;
    bool          zero = false;
    bool          audio = false;
};

struct Burst {
    static constexpr const char* id = "rp.test.burst";
    using Settings = BurstSettings;
    struct { rp::events_out out; } outputs;

    static void adjust_layout(Layout& l, const Settings& s) {
        if (s.messages) l.ports[0].capacity.messages = s.messages;
        if (s.bytes) l.ports[0].capacity.bytes = s.bytes;
        if (s.zero) l.ports[0].capacity.messages = 0u;
        if (s.audio) {
            PortDesc a{"a", PortDir::Out, PortKind::Audio, ChannelSpec::fixed(1, "mono"), {}, false};
            a.capacity.messages = 4;
            l.ports.push_back(a);
        }
    }

    static inline int made = 0;
    Burst() { ++made; }

    std::uint32_t count = 1, size = 3;
    void on_settings(const Settings& s) noexcept {
        count = s.count;
        size = std::min(s.size, kPayloadBytes);
    }
    void process(const rp::Block&) noexcept {
        static const std::array<std::uint8_t, kPayloadBytes> payload{};
        for (std::uint32_t i = 0; i < count; ++i) outputs.out.push(0, std::span(payload.data(), size));
    }
};

// The same, kept through a settings change that alters its layout (a capacity among them).
struct ReBurst : Burst {
    static constexpr const char* id = "rp.test.reburst";
    static constexpr bool reshapes = true;
    static constexpr bool stateless_prepare = true;
};

// What reached its events input this block: how many messages, and the largest. A capacity its settings name is set
// on its input, which the C++ layer refuses: a wire carries its source's queue.
struct TallySettings {
    std::uint32_t messages = 0;
};

struct Tally {
    static constexpr const char* id = "rp.test.tally";
    using Settings = TallySettings;
    struct { rp::events_in in; } inputs;

    static void adjust_layout(Layout& l, const Settings& s) {
        if (s.messages) l.ports[0].capacity.messages = s.messages;
    }

    std::uint32_t messages = 0, largest = 0;
    void process(const rp::Block&) noexcept {
        messages = 0;
        largest = 0;
        for (const auto& m : inputs.in.messages()) {
            ++messages;
            largest = std::max(largest, m.size);
        }
    }
};

const rp_package* capacityPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Burst>(), rp::node_type<ReBurst>(),
                                                 rp::node_type<Tally>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.capacity"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

struct CapacityRig : GraphRig {
    CapacityRig() { registry.addPackage(capacityPackage()); }
};

// A burst `b` with these settings, wired to a tally `t` (through a feedback wire if asked).
std::string doc(const std::string& burst, const std::string& tally = "{}", bool feedback = false,
                const std::string& type = "rp.test.burst") {
    return R"({"nodes":[{"id":"b","type":")" + type + R"(","settings":)" + burst +
           R"(},{"id":"t","type":"rp.test.tally","settings":)" + tally + R"(}],"wires":[{"from":"b.out","to":"t.in")" +
           (feedback ? R"(,"feedback":true)" : "") + "}]}";
}

// The caps: a queue of 1 Mi messages and 16 MiB, 32 MiB in all.
const std::string kAtCaps = R"({"messages":1048576,"bytes":16777216})";

// Bursts b1..bn, each with these settings and wired to its own tally (wire k through a feedback wire if `feedback`
// names k).
std::string bursts(const std::vector<std::string>& settings, int feedback = 0) {
    std::string nodes, wires;
    for (std::size_t k = 1; k <= settings.size(); ++k) {
        const std::string n = std::to_string(k);
        nodes += std::string(k > 1 ? "," : "") + R"({"id":"b)" + n + R"(","type":"rp.test.burst","settings":)" +
                 settings[k - 1] + R"(},{"id":"t)" + n + R"(","type":"rp.test.tally"})";
        wires += std::string(k > 1 ? "," : "") + R"({"from":"b)" + n + R"(.out","to":"t)" + n + R"(.in")" +
                 (static_cast<int>(k) == feedback ? R"(,"feedback":true)" : "") + "}";
    }
    return R"({"nodes":[)" + nodes + R"(],"wires":[)" + wires + "]}";
}

}  // namespace

TEST_CASE("a port that declares two messages drops the third, and the drop reaches the host", "[graph][capacity]") {
    CapacityRig rig;
    REQUIRE(rig.set(doc(R"({"count":3,"messages":2})")).ok);
    rig.run();
    CHECK(rig.node<Tally>("t").messages == 2);
    CHECK(rig.host->droppedMessages() == 1);
    CHECK(rig.host->droppedMessages("b") == 1);  // charged to the node whose output it is
    CHECK(rig.host->droppedMessages("t") == 0);
    CHECK(rig.host->droppedMessages("nope") == 0);
    // Each block's drops are published once: two more blocks are two more drops.
    rig.run(64, 2);
    CHECK(rig.host->droppedMessages() == 3);
    CHECK(rig.host->droppedMessages("b") == 3);
}

TEST_CASE("a port that declares 64 KiB carries a 60 KiB message the default drops", "[graph][capacity]") {
    CapacityRig rig;
    REQUIRE(rig.set(doc(R"({"size":61440,"bytes":65536})")).ok);
    rig.run();
    CHECK(rig.node<Tally>("t").largest == 61440);
    CHECK(rig.host->droppedMessages() == 0);

    REQUIRE(rig.set(doc(R"({"size":61440})")).ok);
    rig.run();
    CHECK(rig.node<Tally>("t").messages == 0);
    CHECK(rig.host->droppedMessages() == 1);
}

TEST_CASE("a port that declares nothing keeps 256 messages and 4096 bytes", "[graph][capacity]") {
    CapacityRig rig;
    const SetGraphResult r = rig.set(doc(R"({"count":257,"size":1})"));
    REQUIRE(r.ok);
    CHECK(r.layouts.at("b").find("capacity") == std::string::npos);  // nothing declared, nothing spelt
    rig.run();
    CHECK(rig.node<Tally>("t").messages == 256);
    CHECK(rig.host->droppedMessages() == 1);

    REQUIRE(rig.set(doc(R"({"size":4096})")).ok);
    rig.run();
    CHECK(rig.node<Tally>("t").largest == 4096);
    CHECK(rig.host->droppedMessages() == 1);

    REQUIRE(rig.set(doc(R"({"size":4097})")).ok);
    rig.run();
    CHECK(rig.node<Tally>("t").messages == 0);
    CHECK(rig.host->droppedMessages() == 2);
}

TEST_CASE("a capacity past the caps is refused as the node's settings", "[graph][capacity]") {
    CapacityRig rig;
    SetGraphResult r = rig.set(doc(R"({"messages":1048577})"));
    REQUIRE_FALSE(r.ok);
    CHECK(GraphRig::codes(r) == "settings-invalid");
    CHECK(r.errors[0].nodeId == "b");
    CHECK(r.errors[0].message == R"(port "out": "capacity.messages" must be a whole number from 1 to 1048576)");

    r = rig.set(doc(R"({"bytes":16777217})"));
    REQUIRE_FALSE(r.ok);
    CHECK(GraphRig::codes(r) == "settings-invalid");
    CHECK(r.errors[0].message == R"(port "out": "capacity.bytes" must be a whole number from 1 to 16777216)");

    CHECK(rig.set(doc(R"({"messages":1048576,"bytes":16777216})")).ok);  // the caps themselves
}

TEST_CASE("a wire carries its source's capacity, and an input that declares one is refused", "[graph][capacity]") {
    CapacityRig rig;
    REQUIRE(rig.set(doc(R"({"count":8,"messages":8})")).ok);
    rig.run();
    CHECK(rig.node<Tally>("t").messages == 8);
    CHECK(rig.host->droppedMessages() == 0);

    REQUIRE(rig.set(doc(R"({"count":8,"messages":2})")).ok);
    rig.run();
    CHECK(rig.node<Tally>("t").messages == 2);
    CHECK(rig.host->droppedMessages() == 6);

    // A capacity on the input would bound nothing, so it is refused as the node's settings, and the graph runs on.
    const SetGraphResult r = rig.set(doc(R"({"count":8,"messages":2})", R"({"messages":64})"));
    REQUIRE_FALSE(r.ok);
    CHECK(GraphRig::codes(r) == "settings-invalid");
    CHECK(r.errors[0].nodeId == "t");
    CHECK(r.errors[0].message == R"(port "in": only an output declares a capacity: a wire carries its source's queue)");
    rig.run();
    CHECK(rig.node<Tally>("t").messages == 2);
}

TEST_CASE("a feedback wire's front is sized from its source, and resized with it", "[graph][capacity]") {
    CapacityRig rig;
    REQUIRE(rig.set(doc(R"({"count":300,"messages":512})", "{}", true)).ok);
    rig.run();
    CHECK(rig.node<Tally>("t").messages == 0);  // a block behind
    rig.run();
    CHECK(rig.node<Tally>("t").messages == 300);

    // More than the old front holds. A front kept at 512 would hand the tally 512 and lose the rest unseen, since a
    // front is only ever filled from its own source's queue.
    REQUIRE(rig.set(doc(R"({"count":800,"messages":1024})", "{}", true)).ok);
    rig.run(64, 2);
    CHECK(rig.node<Tally>("t").messages == 800);
    CHECK(rig.host->droppedMessages() == 0);
}

TEST_CASE("a feedback wire's source counts what its full queue drops", "[graph][capacity]") {
    CapacityRig rig;
    REQUIRE(rig.set(doc(R"({"count":3,"messages":2})", "{}", true)).ok);
    rig.run(64, 2);
    CHECK(rig.node<Tally>("t").messages == 2);  // the first block's two, a block behind
    CHECK(rig.host->droppedMessages("b") == 2);
    CHECK(rig.host->droppedMessages() == 2);
}

TEST_CASE("a feedback front keeps its block through an edit, and carries it while quiescent when its capacity "
          "changes", "[graph][capacity]") {
    CapacityRig rig;
    REQUIRE(rig.set(doc(R"({"count":5,"messages":512})", "{}", true)).ok);
    rig.run();  // five in flight

    // The audio thread owns the plan from here (this thread stands in for it: each run() adopts what was published).
    // The source is rebuilt, its settings changed, at the same capacity: the front is the one it had, five and all.
    rig.host->setAudioThreadOwns(true);
    REQUIRE(rig.set(doc(R"({"count":6,"messages":512})", "{}", true)).ok);
    rig.run();
    CHECK(rig.node<Tally>("t").messages == 5);

    // The capacity grows while it runs: the old front is the audio thread's to fill, so the new one starts empty.
    REQUIRE(rig.set(doc(R"({"count":7,"messages":1024})", "{}", true)).ok);
    rig.run();
    CHECK(rig.node<Tally>("t").messages == 0);
    rig.host->setAudioThreadOwns(false);

    // Quiescent, it grows again: the seven in flight are carried into the new front.
    REQUIRE(rig.set(doc(R"({"count":9,"messages":2048})", "{}", true)).ok);
    rig.run();
    CHECK(rig.node<Tally>("t").messages == 7);
    CHECK(rig.host->droppedMessages() == 0);

    // And it shrinks below what is in flight: four of the nine are carried, and the five that do not fit are the
    // source's drops, charged to the instance its id names now.
    REQUIRE(rig.set(doc(R"({"count":2,"messages":4})", "{}", true)).ok);
    CHECK(rig.host->droppedMessages("b") == 5);
    CHECK(rig.host->droppedMessages() == 5);
    rig.run();
    CHECK(rig.node<Tally>("t").messages == 4);
    rig.run();
    CHECK(rig.node<Tally>("t").messages == 2);
    CHECK(rig.host->droppedMessages() == 5);
}

TEST_CASE("a feedback wire's capacity changes while the audio thread runs", "[graph][capacity]") {
    // Each edit replaces the front: it cannot be read across while the audio thread fills it, so the new one starts
    // empty. A carry here would be a race, which the ThreadSanitizer run of this binary is there to catch.
    CapacityRig rig;
    REQUIRE(rig.set(doc(R"({"count":300,"messages":512})", "{}", true, "rp.test.reburst")).ok);
    rig.host->setAudioThreadOwns(true);
    std::atomic<bool> stop{false};
    std::thread audio([&] {
        while (!stop.load(std::memory_order_acquire)) rig.host->processBlock(64, rig.transport);
    });
    int ok = 0;
    for (int i = 0; i < 100; ++i) {
        const char* settings = i % 2 ? R"({"count":300,"messages":1024})" : R"({"count":300,"messages":512})";
        ok += rig.set(doc(settings, "{}", true, i % 3 ? "rp.test.reburst" : "rp.test.burst")).ok;
        rig.host->reclaim();
    }
    stop.store(true, std::memory_order_release);
    audio.join();
    rig.host->setAudioThreadOwns(false);
    CHECK(ok == 100);
    CHECK(rig.host->droppedMessages() == 0);
    rig.run(64, 2);
    CHECK(rig.node<Tally>("t").messages == 300);
}

TEST_CASE("a node reshaped across a capacity change keeps its count, and its queue takes the new size",
          "[graph][capacity][reshape]") {
    CapacityRig rig;
    REQUIRE(rig.set(doc(R"({"count":3,"messages":2})", "{}", false, "rp.test.reburst")).ok);
    rig.run();
    rp_node* const before = rig.host->node("b");
    CHECK(rig.host->droppedMessages("b") == 1);

    REQUIRE(rig.set(doc(R"({"count":3,"messages":1})", "{}", false, "rp.test.reburst")).ok);
    rig.run();
    CHECK(rig.host->node("b") == before);  // reshaped, not rebuilt
    CHECK(rig.node<Tally>("t").messages == 1);
    CHECK(rig.host->droppedMessages("b") == 3);  // its instance's count goes on
    CHECK(rig.host->droppedMessages() == 3);
}

TEST_CASE("a capacity adjust_layout sets on a port with no queue, or at 0, is refused as the settings",
          "[graph][capacity]") {
    CapacityRig rig;
    SetGraphResult r = rig.set(doc(R"({"zero":true})"));
    REQUIRE_FALSE(r.ok);
    CHECK(GraphRig::codes(r) == "settings-invalid");
    CHECK(r.errors[0].message == R"(port "out": "capacity.messages" must be a whole number from 1 to 1048576)");

    r = rig.set(doc(R"({"audio":true})"));
    REQUIRE_FALSE(r.ok);
    CHECK(GraphRig::codes(r) == "settings-invalid");
    CHECK(r.errors[0].message == R"(port "a": only events and bytes ports have "capacity")");
}

TEST_CASE("an over-cap capacity on a live node is refused as its settings, and the node runs on", "[graph][capacity]") {
    for (const char* type : {"rp.test.burst", "rp.test.reburst"}) {
        CAPTURE(type);
        CapacityRig rig;
        REQUIRE(rig.set(doc(R"({"count":3,"messages":2})", "{}", false, type)).ok);
        rig.run();
        rp_node* const before = rig.host->node("b");
        const SetGraphResult r = rig.set(doc(R"({"count":3,"messages":2000000})", "{}", false, type));
        REQUIRE_FALSE(r.ok);
        CHECK(GraphRig::codes(r) == "settings-invalid");
        CHECK(r.errors[0].message == R"(port "out": "capacity.messages" must be a whole number from 1 to 1048576)");
        rig.run();
        CHECK(rig.host->node("b") == before);
        CHECK(rig.node<Tally>("t").messages == 2);
    }
}

TEST_CASE("a plan's queues are held to their budget, naming the port that crossed it", "[graph][capacity]") {
    CapacityRig rig;
    // Two queues at the caps are 64 MiB, the budget itself.
    REQUIRE(rig.set(bursts({kAtCaps, kAtCaps})).ok);
    rig.run();
    CHECK(rig.node<Tally>("t2").messages == 1);

    // A third crosses it. Refused before anything is made, so the graph runs on as it was.
    const int made = Burst::made;
    SetGraphResult r = rig.set(bursts({kAtCaps, kAtCaps, kAtCaps}));
    REQUIRE_FALSE(r.ok);
    CHECK(GraphRig::codes(r) == "queue-budget");
    CHECK(r.errors[0].nodeId == "b3");
    CHECK(r.errors[0].port == "out");
    CHECK(r.errors[0].message == R"(the queue of "b3.out" takes the plan's events and bytes queues to )"
                                 R"(100663296 bytes, past their 64 MiB budget; declare smaller capacities)");
    CHECK(Burst::made == made);  // not even b3, which is new
    rig.run();
    CHECK(rig.node<Tally>("t2").messages == 1);

    // A feedback wire's front is a queue of its own, sized as its source's: one wire at the caps is the whole budget,
    // and one more queue at the default 8 KiB crosses it.
    REQUIRE(rig.set(bursts({kAtCaps}, 1)).ok);
    r = rig.set(bursts({"{}", kAtCaps}, 2));
    REQUIRE_FALSE(r.ok);
    CHECK(r.errors[0].nodeId == "b2");
    CHECK(r.errors[0].message.find(R"(the feedback front of "b2.out->t2.in" takes the plan's events and bytes )"
                                   R"(queues to 67117056 bytes)") == 0);

    // Queues at the defaults are 8 KiB each, nowhere near it.
    std::vector<std::string> many(64, "{}");
    REQUIRE(rig.set(bursts(many)).ok);
}

TEST_CASE("an unconnected output's drops lose nothing, so they are not counted", "[graph][capacity]") {
    CapacityRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"b","type":"rp.test.burst","settings":{"count":300}}],"wires":[]})").ok);
    rig.run();
    CHECK(rig.host->droppedMessages() == 0);
}

TEST_CASE("the control thread reads drops while the audio thread publishes them", "[graph][capacity]") {
    CapacityRig rig;
    REQUIRE(rig.set(doc(R"({"count":3,"messages":2})")).ok);
    rig.host->setAudioThreadOwns(true);
    std::atomic<bool> stop{false};
    std::thread audio([&] {
        while (!stop.load(std::memory_order_acquire)) rig.host->processBlock(64, rig.transport);
    });
    // Read, and reclaimed (which reports the dropping node), while the audio thread writes every queue's count.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (rig.host->droppedMessages() < 100 && std::chrono::steady_clock::now() < deadline) {
        rig.host->reclaim();
        std::this_thread::yield();
    }
    stop.store(true, std::memory_order_release);
    audio.join();
    rig.host->setAudioThreadOwns(false);
    CHECK(rig.host->droppedMessages() >= 100);
    CHECK(rig.host->droppedMessages("b") == rig.host->droppedMessages());
}
