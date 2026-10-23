// Planner validation: every rejection carries a stable code, and a rejected document changes nothing.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <iterator>
#include <span>

#include "GraphRig.hpp"

using namespace rp::graph;
using rp::graph::test::GraphRig;

namespace {

// A core with two ways to tap its voices, like a NES's pins.* and channels.*: either, never both. It
// records the connected-output mask the planner hands it.
struct Tapped {
    static constexpr const char* id = "rp.test.tapped";
    struct { rp::audio_out<rp::mono> mix; } outputs;
    static void adjust_layout(Layout& l, const rp::detail::NoSettings&) {
        for (const char* port : {"pins.a", "pins.b", "channels.x"})
            l.ports.push_back({port, PortDir::Out, PortKind::Audio, ChannelSpec::fixed(1, "mono"), {}, false});
        l.exclusive.push_back({"pins.*", "channels.*"});
    }
    std::array<std::uint8_t, 4> mask{};
    std::uint32_t               maskCalls = 0;
    void ports_changed(std::span<const std::uint8_t> connected) noexcept {
        for (std::size_t i = 0; i < connected.size() && i < mask.size(); ++i) mask[i] = connected[i];
        ++maskCalls;
    }
    void process(const rp::Block& b) noexcept {
        for (std::uint32_t o = 0; o < 4; ++o)
            for (std::uint32_t i = 0; i < b.frames(); ++i) b.output(o).u.audio.channels[0][i] = 0.0f;
    }
};

// A coupled member with an audio input, so one member can be wired into another.
struct LinkedListener {
    static constexpr const char* id = "rp.test.linked-listener";
    struct { rp::link link; rp::audio_in<rp::mono> in; } inputs;
    struct { rp::audio_out<rp::mono> out; } outputs;
    void prepare(const rp::Block&) noexcept {}
    bool step(std::uint32_t) noexcept { return true; }
    void finish(const rp::Block& b) noexcept {
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = inputs.in[0][i];
    }
    void process(const rp::Block&) noexcept {}
};

// A pack whose create fails after query_memory succeeded (a ROM that will not load, say).
const char* failingDescriptor() { return R"({"id":"rp.test.failing"})"; }
rp_node* failingCreate(const rp_host*, void*, const rp_settings*, rp_error* err) {
    std::strcpy(err->message, "no ROM loaded");
    return nullptr;
}

const rp_package* plannerPackage() {
    static const rp_node_type failing{"rp.test.failing", &failingDescriptor, rp::node_type<test::Sink>()->query_memory,
                                      &failingCreate, rp::node_type<test::Sink>()->destroy};
    static const rp_node_type* const kTypes[] = {rp::node_type<Tapped>(), rp::node_type<LinkedListener>(), &failing};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.planner"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

struct PlannerRig : GraphRig {
    PlannerRig() { registry.addPackage(plannerPackage()); }
};

}  // namespace

TEST_CASE("a valid document plans and reports each node's layout", "[graph][planner]") {
    GraphRig rig;
    const auto r = rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source"},{"id":"out","type":"rp.host.audio-out"}],
                               "wires":[{"from":"src.out","to":"out.out.1-2"}]})");
    INFO(GraphRig::codes(r));
    REQUIRE(r.ok);
    CHECK(r.layouts.at("out").find(R"("id":"out.7-8")") != std::string::npos);
}

TEST_CASE("structural errors", "[graph][planner]") {
    GraphRig rig;
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"a","type":"rp.nope"}],"wires":[]})")) == "unknown-type");
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"a.b","type":"rp.test.sink"}],"wires":[]})")) == "node-id");
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"a b","type":"rp.test.sink"}],"wires":[]})")) == "node-id");
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"sys-3/lsdj-sync","type":"rp.test.sink"}],"wires":[]})")).empty());
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.sink"},{"id":"a","type":"rp.test.sink"}],"wires":[]})")) == "node-id");
    CHECK(GraphRig::codes(rig.set("{not json")) == "document");
}

TEST_CASE("wire errors", "[graph][planner]") {
    GraphRig rig;
    const std::string nodes = R"("nodes":[{"id":"src","type":"rp.test.source"},{"id":"sink","type":"rp.test.sink"},
                                          {"id":"src2","type":"rp.test.source"}])";
    auto doc = [&](const std::string& wires) { return "{" + nodes + R"(,"wires":[)" + wires + "]}"; };
    CHECK(GraphRig::codes(rig.set(doc(R"({"from":"src.nope","to":"sink.in"})"))) == "unknown-port");
    CHECK(GraphRig::codes(rig.set(doc(R"({"from":"sink.in","to":"src.out"})"))) == "wire-direction");
    CHECK(GraphRig::codes(rig.set(doc(R"({"from":"src.out","to":"sink.events"})"))) == "kind-mismatch");
    CHECK(GraphRig::codes(rig.set(doc(R"({"from":"src.out","to":"sink.in"},{"from":"src2.out","to":"sink.in"})"))) == "input-taken");
    CHECK(GraphRig::codes(rig.set(doc(R"({"from":"ghost.out","to":"sink.in"})"))) == "unknown-node");
    CHECK(GraphRig::codes(rig.set(doc(R"({"from":"src","to":"sink.in"})"))) == "wire-endpoint");
}

TEST_CASE("fan-out is free", "[graph][planner]") {
    GraphRig rig;
    const auto r = rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source"},{"id":"a","type":"rp.test.sink"},
                                         {"id":"b","type":"rp.test.sink"}],
                               "wires":[{"from":"src.out","to":"a.in"},{"from":"src.out","to":"b.in"}]})");
    CHECK(r.ok);
}

TEST_CASE("a loop needs a feedback wire, and the error names it", "[graph][planner]") {
    GraphRig rig;
    const std::string nodes = R"("nodes":[{"id":"x","type":"rp.test.through"},{"id":"y","type":"rp.test.through"}])";
    const auto r = rig.set("{" + nodes + R"(,"wires":[{"from":"x.out","to":"y.in"},{"from":"y.out","to":"x.in"}]})");
    REQUIRE(GraphRig::codes(r) == "cycle");
    CHECK(r.errors[0].message.find("x -> y -> x") != std::string::npos);
    CHECK(rig.set("{" + nodes + R"(,"wires":[{"from":"x.out","to":"y.in"},{"from":"y.out","to":"x.in","feedback":true}]})").ok);
}

TEST_CASE("the loop is named whatever order the nodes are listed in", "[graph][planner]") {
    GraphRig rig;
    // c is only downstream of the loop, and listed first.
    const auto r = rig.set(R"({"nodes":[{"id":"c","type":"rp.test.sink"},{"id":"a","type":"rp.test.through"},
                                         {"id":"b","type":"rp.test.through"}],
                               "wires":[{"from":"a.out","to":"b.in"},{"from":"b.out","to":"a.in"},{"from":"a.out","to":"c.in"}]})");
    REQUIRE(GraphRig::codes(r) == "cycle");
    CHECK(r.errors[0].nodeId == "a");
    CHECK(r.errors[0].message.find("(a -> b -> a)") != std::string::npos);
}

TEST_CASE("a data wire between members of one coupled group needs feedback", "[graph][planner]") {
    PlannerRig rig;
    const std::string nodes = R"("nodes":[{"id":"a","type":"rp.test.linked-listener"},{"id":"b","type":"rp.test.linked-listener"}])";
    const auto r = rig.set("{" + nodes + R"(,"wires":[{"from":"a.link","to":"b.link"},{"from":"a.out","to":"b.in"}]})");
    REQUIRE(GraphRig::codes(r) == "cycle");
    CHECK(r.errors[0].message.find("coupled group") != std::string::npos);
    CHECK(rig.set("{" + nodes + R"(,"wires":[{"from":"a.link","to":"b.link"},{"from":"a.out","to":"b.in","feedback":true}]})").ok);
}

TEST_CASE("an exclusive group allows either side, never both", "[graph][planner]") {
    PlannerRig rig;
    const std::string nodes = R"("nodes":[{"id":"t","type":"rp.test.tapped"},{"id":"s1","type":"rp.test.sink"},
                                          {"id":"s2","type":"rp.test.sink"}])";
    CHECK(rig.set("{" + nodes + R"(,"wires":[{"from":"t.pins.a","to":"s1.in"},{"from":"t.mix","to":"s2.in"}]})").ok);
    CHECK(rig.set("{" + nodes + R"(,"wires":[{"from":"t.channels.x","to":"s1.in"}]})").ok);
    const auto both = rig.set("{" + nodes + R"(,"wires":[{"from":"t.pins.a","to":"s1.in"},{"from":"t.channels.x","to":"s2.in"}]})");
    REQUIRE(GraphRig::codes(both) == "exclusive-ports");
    CHECK(both.errors[0].nodeId == "t");
}

TEST_CASE("a node is told which outputs are connected, on the first plan and on each change", "[graph][planner]") {
    PlannerRig rig;
    const std::string nodes = R"("nodes":[{"id":"t","type":"rp.test.tapped"},{"id":"s1","type":"rp.test.sink"})";
    REQUIRE(rig.set("{" + nodes + R"(],"wires":[{"from":"t.pins.a","to":"s1.in"}]})").ok);
    rig.run();
    CHECK(rig.node<Tapped>("t").mask == std::array<std::uint8_t, 4>{0, 1, 0, 0});
    CHECK(rig.node<Tapped>("t").maskCalls == 1);
    REQUIRE(rig.set("{" + nodes + R"(,{"id":"other","type":"rp.test.sink"}],"wires":[{"from":"t.pins.a","to":"s1.in"}]})").ok);
    rig.run();
    CHECK(rig.node<Tapped>("t").maskCalls == 1);  // unchanged mask, no call
    REQUIRE(rig.set("{" + nodes + R"(],"wires":[{"from":"t.channels.x","to":"s1.in"}]})").ok);
    rig.run();
    CHECK(rig.node<Tapped>("t").mask == std::array<std::uint8_t, 4>{0, 0, 0, 1});
    CHECK(rig.node<Tapped>("t").maskCalls == 2);
}

TEST_CASE("a node whose create fails is reported, and its memory is freed", "[graph][planner]") {
    PlannerRig rig;  // (the ASan build is what checks "freed")
    const auto r = rig.set(R"({"nodes":[{"id":"f","type":"rp.test.failing"}],"wires":[]})");
    REQUIRE(GraphRig::codes(r) == "settings-invalid");
    CHECK(r.errors[0].message == "no ROM loaded");
}

TEST_CASE("channel counts and layouts must agree", "[graph][planner]") {
    GraphRig rig;
    const auto mismatch = rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"channels":1}},
                                                {"id":"t","type":"rp.test.through"}],
                                      "wires":[{"from":"src.out","to":"t.in"}]})");
    REQUIRE(GraphRig::codes(mismatch) == "channel-mismatch");
    CHECK(mismatch.errors[0].message.find("mono (1)") != std::string::npos);
}

TEST_CASE("capability flags are checked against the target", "[graph][planner]") {
    GraphRig pico({48000.0, 64, Target::Pico});
    CHECK(GraphRig::codes(pico.set(R"({"nodes":[{"id":"h","type":"rp.test.heavy"}],"wires":[]})")) == "capability");
    GraphRig desktop;
    CHECK(desktop.set(R"({"nodes":[{"id":"h","type":"rp.test.heavy"}],"wires":[]})").ok);
}

TEST_CASE("a linked node must implement rp.step/1", "[graph][planner]") {
    GraphRig rig;
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.linked"},{"id":"b","type":"rp.test.nostep"}],
                                      "wires":[{"from":"a.link","to":"b.link"}]})")) == "link-without-step");
}

TEST_CASE("params: unknown ids and wrong types", "[graph][planner]") {
    GraphRig rig;
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"s","type":"rp.test.source","params":{"nope":1}}],"wires":[]})")) == "unknown-param");
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"s","type":"rp.test.source","params":{"gain":"loud"}}],"wires":[]})")) == "param-value");
    CHECK(rig.set(R"({"nodes":[{"id":"s","type":"rp.test.source","params":{"gain":2}}],"wires":[]})").ok);
}

TEST_CASE("settings errors come back from the node with its reason", "[graph][planner]") {
    GraphRig rig;
    const auto r = rig.set(R"({"nodes":[{"id":"s","type":"rp.test.source","settings":{"channels":"two"}}],"wires":[]})");
    REQUIRE(GraphRig::codes(r) == "settings-invalid");
    CHECK(r.errors[0].message.find("channels") != std::string::npos);
}

TEST_CASE("a rejected document leaves the running graph untouched", "[graph][planner]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"value":0.5}},{"id":"out","type":"rp.host.audio-out"}],
                         "wires":[{"from":"src.out","to":"out.out.1-2"}]})").ok);
    rig.run();
    REQUIRE(rig.outs[0][0] == 0.5f);
    const auto before = rig.host->instanceCount();
    CHECK_FALSE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source"},{"id":"new","type":"rp.test.sink"},{"id":"bad","type":"rp.nope"}],"wires":[]})").ok);
    CHECK(rig.host->instanceCount() == before);
    rig.run();
    CHECK(rig.outs[0][0] == 0.5f);
}
