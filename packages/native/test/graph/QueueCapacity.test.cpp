// Queue capacity (docs/modular-graph-phase-b3a-design.md section 2.6): an events or bytes port may declare how many
// messages and payload bytes its queue carries. The planner sizes each output queue from its port and each feedback
// front from the wire's source, a port that declares nothing keeps today's 256 messages and 4096 bytes, and a
// capacity past the caps is refused. What a full queue drops is published by the thread that runs the block, for
// the host to read on the control thread.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iterator>
#include <span>
#include <string>
#include <thread>

#include "GraphRig.hpp"

using namespace rp::graph;
using rp::graph::test::GraphRig;

namespace {

constexpr std::uint32_t kPayloadBytes = 1u << 16;

// Each block, `count` messages of `size` bytes on its events output, whose queue capacity its settings declare (a
// part left 0 keeps the default).
struct BurstSettings {
    std::uint32_t count = 1;
    std::uint32_t size = 3;
    std::uint32_t messages = 0;
    std::uint32_t bytes = 0;
};

struct Burst {
    static constexpr const char* id = "rp.test.burst";
    using Settings = BurstSettings;
    struct { rp::events_out out; } outputs;

    static void adjust_layout(Layout& l, const Settings& s) {
        if (s.messages) l.ports[0].capacity.messages = s.messages;
        if (s.bytes) l.ports[0].capacity.bytes = s.bytes;
    }

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

// What reached its events input this block: how many messages, and the largest. Its input declares the capacity
// its settings name, which bounds nothing: a wire carries its source's queue.
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
    static const rp_node_type* const kTypes[] = {rp::node_type<Burst>(), rp::node_type<Tally>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.capacity"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

struct CapacityRig : GraphRig {
    CapacityRig() { registry.addPackage(capacityPackage()); }
};

// A burst `b` with these settings, wired to a tally `t` (through a feedback wire if asked).
std::string doc(const std::string& burst, const std::string& tally = "{}", bool feedback = false) {
    return R"({"nodes":[{"id":"b","type":"rp.test.burst","settings":)" + burst +
           R"(},{"id":"t","type":"rp.test.tally","settings":)" + tally + R"(}],"wires":[{"from":"b.out","to":"t.in")" +
           (feedback ? R"(,"feedback":true)" : "") + "}]}";
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

TEST_CASE("a wire carries its source's capacity, whatever its input declares", "[graph][capacity]") {
    CapacityRig rig;
    REQUIRE(rig.set(doc(R"({"count":8,"messages":8})", R"({"messages":1})")).ok);
    rig.run();
    CHECK(rig.node<Tally>("t").messages == 8);
    CHECK(rig.host->droppedMessages() == 0);

    REQUIRE(rig.set(doc(R"({"count":8,"messages":2})", R"({"messages":64})")).ok);
    rig.run();
    CHECK(rig.node<Tally>("t").messages == 2);
    CHECK(rig.host->droppedMessages() == 6);
}

TEST_CASE("a feedback wire's front is sized from its source, and resized with it", "[graph][capacity]") {
    CapacityRig rig;
    // The tally's input declares one message; the front takes the source's 512.
    REQUIRE(rig.set(doc(R"({"count":300,"messages":512})", R"({"messages":1})", true)).ok);
    rig.run();
    CHECK(rig.node<Tally>("t").messages == 0);  // a block behind
    rig.run();
    CHECK(rig.node<Tally>("t").messages == 300);

    // More than the old front holds. A front kept at 512 would hand the tally 512 and lose the rest unseen, since a
    // front is only ever filled from its own source's queue.
    REQUIRE(rig.set(doc(R"({"count":800,"messages":1024})", R"({"messages":1})", true)).ok);
    rig.run(64, 2);
    CHECK(rig.node<Tally>("t").messages == 800);
    CHECK(rig.host->droppedMessages() == 0);
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
