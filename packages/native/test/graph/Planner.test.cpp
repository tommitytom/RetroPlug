// Planner validation: every rejection carries a stable code, and a rejected document changes nothing.
#include <catch2/catch_test_macros.hpp>

#include "GraphRig.hpp"

using namespace rp::graph;
using rp::graph::test::GraphRig;

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
