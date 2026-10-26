// The no-heap claim: once a plan is built, the block path - draining commands, adopting a plan, running
// every core node, feedback copies, a coupled group and the host output - performs no allocation and frees
// nothing. CountingAllocator.cpp replaces global operator new and delete for this binary, and counts what the
// measured blocks allocate and free. The same allocator refuses allocations from refuseAllocationsFrom's size
// up, which is how the planner's out-of-memory path is driven.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "CountingAllocator.hpp"
#include "GraphRig.hpp"

using rp::graph::test::GraphRig;

namespace {

// A coupled member that allocates nothing (the shared rp.test.linked logs to a vector).
struct QuietLinked {
    static constexpr const char* id = "rp.test.quiet-linked";
    struct { rp::link link; } inputs;
    struct { rp::audio_out<rp::mono> out; } outputs;
    std::uint32_t produced = 0, peerCount = 0;
    void set_peers(std::span<rp_node* const> peers) noexcept { peerCount = static_cast<std::uint32_t>(peers.size()); }
    void prepare(const rp::Block&) noexcept { produced = 0; }
    bool step(std::uint32_t target) noexcept {
        if (produced < target) ++produced;
        return produced >= target;
    }
    void finish(const rp::Block& b) noexcept {
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = static_cast<float>(peerCount);
    }
    void process(const rp::Block&) noexcept {}
};

// An output at the caps, whose queue needs 16 MiB of message items. Counts its live instances.
struct BigQueue {
    static constexpr const char* id = "rp.test.big-queue";
    static inline int alive = 0;
    struct { rp::events_out::sized<rp::capacity{.messages = rp::graph::kMaxQueueMessages}> out; } outputs;
    BigQueue() { ++alive; }
    ~BigQueue() { --alive; }
    BigQueue(const BigQueue&) = delete;
    BigQueue& operator=(const BigQueue&) = delete;
    void process(const rp::Block&) noexcept {}
};

const rp_package* noAllocPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<QuietLinked>(), rp::node_type<BigQueue>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.noalloc"})", 2, kTypes};
    return &kPackage;
}

// Every rp.core node, a feedback wire of each kind, a coupled pair, a reshaping node and the host output. The
// `swapped` form changes a setting in place (a live swap), a param value, a coupled group's members, and
// connects an output that was not (so ports_changed fires), reshapes a node (a param it adds gets a document
// value), and adds fresh instances and a feedback wire.
std::string graphDoc(bool swapped, rp::graph::ResourceTable::Id buf, rp::graph::ResourceTable::Id rec) {
    const std::string b = std::to_string(buf), r = std::to_string(rec);
    std::string nodes = R"({"id":"src","type":"rp.test.source"},
        {"id":"p","type":"rp.core.buffer-player",)" + std::string(swapped ? R"("settings":{"loop":false},)" : "") +
        R"("resources":{"sample":"res:)" + b + R"("}},
        {"id":"d","type":"rp.core.delay","settings":{"maxSamples":256},"params":{"time":)" + (swapped ? "40" : "32") + R"(}},
        {"id":"k","type":"rp.core.constant","params":{"value":0.5}},
        {"id":"k2","type":"rp.core.constant","params":{"value":0.25}},
        {"id":"addc","type":"rp.core.add","settings":{"kind":"control"}},
        {"id":"cs","type":"rp.test.sink"},
        {"id":"sum","type":"rp.core.add"},
        {"id":"split","type":"rp.core.split"},
        {"id":"dm","type":"rp.core.downmix"},
        {"id":"join","type":"rp.core.join"},
        {"id":"rl","type":"rp.core.relabel","settings":{"channels":2,"layout":"stereo"}},
        {"id":"mg","type":"rp.core.merge"},
        {"id":"es","type":"rp.test.sink"},
        {"id":"x","type":"rp.test.through"},{"id":"y","type":"rp.test.through"},
        {"id":"rec","type":"rp.core.buffer-recorder","resources":{"target":"res:)" + r + R"("}},
        {"id":"la","type":"rp.test.quiet-linked"},{"id":"lb","type":"rp.test.quiet-linked"},{"id":"ls","type":"rp.test.sink"},
        {"id":"sh","type":"rp.test.shape","settings":)" +
        (swapped ? R"({"ins":["p","q"],"outs":["a","b"],"levels":["K","L"],"knobs":["g","h"],"tag":"b"},)"
                   R"("params":{"g":5})"
                 : R"({"ins":["q"],"outs":["b"],"levels":["L"],"knobs":["h"]})") + R"(},
        {"id":"shs","type":"rp.test.sink"},{"id":"shl","type":"rp.test.sink"},
        {"id":"out","type":"rp.host.audio-out"})";
    std::string wires = R"({"from":"p.out","to":"d.in"},{"from":"p.out","to":"sum.in1"},{"from":"d.out","to":"sum.in2"},
        {"from":"sum.out","to":"split.in"},{"from":"sum.out","to":"dm.in"},
        {"from":"split.out1","to":"join.in1"},{"from":"dm.out","to":"join.in2"},
        {"from":"join.out","to":"out.out.1-2"},{"from":"sum.out","to":"rec.in"},
        {"from":"sum.out","to":"rl.in"},{"from":"rl.out","to":"out.out.3-4"},
        {"from":"k.out","to":"addc.in1"},{"from":"k2.out","to":"addc.in2"},{"from":"addc.out","to":"cs.control","feedback":true},
        {"from":"src.events","to":"mg.in1"},{"from":"mg.out","to":"es.events"},
        {"from":"x.out","to":"y.in"},{"from":"y.out","to":"out.out.5-6"},{"from":"y.out","to":"x.in","feedback":true},
        {"from":"la.link","to":"lb.link"},{"from":"la.out","to":"ls.in"},
        {"from":"src.events","to":"sh.q"},{"from":"sh.b","to":"shs.events"},{"from":"sh.L","to":"shl.control"})";
    if (swapped) {
        nodes += R"(,{"id":"rl2","type":"rp.core.relabel","settings":{"channels":2,"layout":"stereo"}},{"id":"s2","type":"rp.test.sink"},
            {"id":"p2","type":"rp.core.buffer-player"},{"id":"s3","type":"rp.test.sink"},{"id":"fbs","type":"rp.test.sink"},
            {"id":"lc","type":"rp.test.quiet-linked"})";
        wires += R"(,{"from":"sum.out","to":"rl2.in"},{"from":"rl2.out","to":"s2.in"},
            {"from":"rec.buffer","to":"p2.sample"},{"from":"p2.out","to":"s3.in"},
            {"from":"src.events","to":"fbs.events","feedback":true},{"from":"lb.link","to":"lc.link"})";
    }
    return R"({"nodes":[)" + nodes + R"(],"wires":[)" + wires + "]}";
}

std::atomic<int> hostCommandsSeen{0};

// Allocates and frees nothing, as a handler running on the audio thread must not.
void countCommand(void*, const rp::graph::HostCommand&, const void*, bool) {
    hostCommandsSeen.fetch_add(1, std::memory_order_relaxed);
}

rp::test::AllocationCount measure(GraphRig& rig, int blocks) {
    rp::test::beginCountingAllocations();
    rig.run(64, blocks);
    return rp::test::endCountingAllocations();
}

}  // namespace

TEST_CASE("the block path never allocates or frees", "[graph][noalloc]") {
    GraphRig rig;
    REQUIRE(rig.registry.addPackage(noAllocPackage()));
    const auto buf = rig.resources.createAudioBuffer(2, 512, 48000.0);
    const auto rec = rig.resources.createAudioBuffer(2, 512, 48000.0);
    const auto first = rig.set(graphDoc(false, buf, rec));
    INFO(GraphRig::codes(first));
    REQUIRE(first.ok);
    // No warm-up: a node that allocates lazily on its first block is caught too.
    auto m = measure(rig, 2);
    CHECK(m.allocations == 0);
    CHECK(m.frees == 0);

    // A swap is queued (the audio thread "owns" the drain), so the measured blocks also adopt a plan.
    rig.host->setAudioThreadOwns(true);
    const auto second = rig.set(graphDoc(true, buf, rec));
    INFO(GraphRig::codes(second));
    REQUIRE(second.ok);
    REQUIRE(rig.host->setParam("k", "value", 0.75, 5));
    m = measure(rig, 8);
    CHECK(m.allocations == 0);
    CHECK(m.frees == 0);
    rig.host->setAudioThreadOwns(false);
    CHECK(rp::peer_as<QuietLinked>(rig.host->node("la"))->peerCount == 2);  // the peer change was adopted
    const auto& sh = *rp::peer_as<rp::graph::test::Shape>(rig.host->node("sh"));
    CHECK(sh.ranOutputs == 4);    // and the reshape...
    CHECK(sh.knobs['g'] == 5.0);  // ...with the document value of the param it added

    // A host command, and two commit-free plans queued behind it, all applied inside the measured blocks.
    // The same document again changes nothing, so neither plan commits settings and neither waits.
    rig.host->setHostCommandHandler(&countCommand, nullptr);
    rig.host->setAudioThreadOwns(true);
    const std::uint8_t payload = 1;
    REQUIRE(rig.host->pushHostCommand(&payload, sizeof payload));
    REQUIRE(rig.host->setGraph(*rp::graph::parseGraphDocument(graphDoc(true, buf, rec)), std::make_shared<const int>(1)).ok);
    REQUIRE(rig.host->setGraph(*rp::graph::parseGraphDocument(graphDoc(true, buf, rec)), std::make_shared<const int>(2)).ok);
    REQUIRE(rig.host->pushHostCommand(&payload, sizeof payload));
    m = measure(rig, 4);
    CHECK(m.allocations == 0);
    CHECK(m.frees == 0);
    CHECK(hostCommandsSeen.load() == 2);
    CHECK(*static_cast<const int*>(rig.host->hostData()) == 2);
    rig.host->setAudioThreadOwns(false);
}

TEST_CASE("an allocation the planner cannot make is a refused plan, not an exception", "[graph][noalloc]") {
    GraphRig rig;
    REQUIRE(rig.registry.addPackage(noAllocPackage()));
    const std::string small = R"({"nodes":[{"id":"src","type":"rp.test.source"},{"id":"s","type":"rp.test.sink"}],)"
                              R"("wires":[{"from":"src.events","to":"s.events"}]})";
    const std::string big = R"({"nodes":[{"id":"src","type":"rp.test.source"},{"id":"s","type":"rp.test.sink"},)"
                            R"({"id":"q","type":"rp.test.big-queue"},{"id":"qs","type":"rp.test.sink"}],)"
                            R"("wires":[{"from":"src.events","to":"s.events"},{"from":"q.out","to":"qs.events"}]})";
    REQUIRE(rig.set(small).ok);
    rig.run();

    // Within the plan's queue budget, but more than this thread may have: refused, with what it made undone.
    rp::graph::SetGraphResult r;
    rp::test::refuseAllocationsFrom(std::size_t{8} << 20);
    CHECK_NOTHROW(r = rig.set(big));
    rp::test::refuseAllocationsFrom(SIZE_MAX);
    CHECK_FALSE(r.ok);
    CHECK(GraphRig::codes(r) == "out-of-memory");
    CHECK(BigQueue::alive == 0);
    CHECK(rig.host->instanceCount() == 2);
    rig.run();
    CHECK(rp::peer_as<rp::graph::test::Sink>(rig.host->node("s"))->eventCount == 1);  // the old plan runs on

    REQUIRE(rig.set(big).ok);  // with the memory to hand, the same document plans
    CHECK(BigQueue::alive == 1);
}

TEST_CASE("an allocation that fails after a settings swap was prepared releases the swap", "[graph][noalloc]") {
    GraphRig rig;
    REQUIRE(rig.registry.addPackage(noAllocPackage()));
    // The source is reused with a new value, a live swap: its prepared settings are the plan's to commit or release.
    auto doc = [](const char* value, bool big) {
        return std::string(R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"value":)") + value +
               R"(}},{"id":"s","type":"rp.test.sink"})" +
               (big ? R"(,{"id":"q","type":"rp.test.big-queue"},{"id":"qs","type":"rp.test.sink"})" : "") +
               R"(],"wires":[{"from":"src.events","to":"s.events"})" +
               (big ? R"(,{"from":"q.out","to":"qs.events"})" : "") + "]}";
    };
    const auto value = [&] { return rp::peer_as<rp::graph::test::Source>(rig.host->node("src"))->value; };
    REQUIRE(rig.set(doc("0.5", false)).ok);
    rig.run();

    // The allocation fails in build(), after reconcile prepared the swap and before the plan took it over. The swap is
    // released (an ASan run of this binary sees it neither leak nor freed twice) and the old value runs on.
    rp::graph::SetGraphResult r;
    rp::test::refuseAllocationsFrom(std::size_t{8} << 20);
    CHECK_NOTHROW(r = rig.set(doc("0.25", true)));
    rp::test::refuseAllocationsFrom(SIZE_MAX);
    CHECK(GraphRig::codes(r) == "out-of-memory");
    rig.run();
    CHECK(value() == 0.5f);

    REQUIRE(rig.set(doc("0.25", true)).ok);  // prepared afresh, and committed
    rig.run();
    CHECK(value() == 0.25f);
}
