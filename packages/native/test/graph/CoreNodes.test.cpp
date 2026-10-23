// The rp.core package: explicit combining and conversion, the constant source, delay, and buffer playback
// and recording - plus the planner's resource-binding rules, which these are the first nodes to exercise.
#include <catch2/catch_test_macros.hpp>

#include "GraphRig.hpp"

using namespace rp::graph;
using rp::graph::test::GraphRig;
using rp::graph::test::Sink;

TEST_CASE("+ sums its inputs in input order", "[graph][core]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.source","settings":{"value":0.25}},
                                  {"id":"b","type":"rp.test.source","settings":{"value":0.5}},
                                  {"id":"sum","type":"rp.core.add"},{"id":"out","type":"rp.host.audio-out"}],
                         "wires":[{"from":"a.out","to":"sum.in1"},{"from":"b.out","to":"sum.in2"},
                                  {"from":"sum.out","to":"out.out.1-2"}]})").ok);
    rig.run();
    CHECK(rig.outs[0][10] == 0.75f);
    CHECK(rig.outs[1][63] == 0.75f);
}

TEST_CASE("+ insists every input matches the first", "[graph][core]") {
    GraphRig rig;
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.source"},
                                                {"id":"b","type":"rp.test.source","settings":{"channels":1}},
                                                {"id":"sum","type":"rp.core.add"}],
                                      "wires":[{"from":"a.out","to":"sum.in1"},{"from":"b.out","to":"sum.in2"}]})")) == "channel-mismatch");
}

TEST_CASE("split, join and downmix move channels explicitly", "[graph][core]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"value":0.5}},
                                  {"id":"split","type":"rp.core.split"},
                                  {"id":"dm","type":"rp.core.downmix"},
                                  {"id":"join","type":"rp.core.join"},
                                  {"id":"out","type":"rp.host.audio-out"}],
                         "wires":[{"from":"src.out","to":"split.in"},{"from":"src.out","to":"dm.in"},
                                  {"from":"split.out1","to":"join.in1"},{"from":"dm.out","to":"join.in2"},
                                  {"from":"join.out","to":"out.out.1-2"}]})").ok);
    rig.run();
    CHECK(rig.outs[0][0] == 0.5f);  // left of the source
    CHECK(rig.outs[1][0] == 1.0f);  // L + R, the equal-sum downmix
}

TEST_CASE("5.1 downmixes to stereo by ITU-style coefficients and to mono as an equal sum", "[graph][core]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"value":0.5,"channels":6}},
                                  {"id":"surround","type":"rp.core.relabel","settings":{"channels":6,"layout":"5.1"}},
                                  {"id":"stereo","type":"rp.core.downmix","settings":{"from":"5.1","to":"stereo"}},
                                  {"id":"mono","type":"rp.core.downmix","settings":{"from":"5.1","to":"mono"}},
                                  {"id":"out","type":"rp.host.audio-out","settings":{"buses":[2,1]}}],
                         "wires":[{"from":"src.out","to":"surround.in"},{"from":"surround.out","to":"stereo.in"},
                                  {"from":"surround.out","to":"mono.in"},{"from":"stereo.out","to":"out.out.1-2"},
                                  {"from":"mono.out","to":"out.out.3"}]})").ok);
    rig.run();
    constexpr float k = 0.70710678f;
    CHECK(rig.outs[0][0] == 0.5f + k * 0.5f + k * 0.5f);  // L + C + Ls, LFE dropped
    CHECK(rig.outs[1][0] == 0.5f + k * 0.5f + k * 0.5f);
    CHECK(rig.outs[2][0] == 3.0f);  // all six, unscaled
}

TEST_CASE("join fixes a mono-into-stereo mismatch; relabel asserts a layout", "[graph][core]") {
    GraphRig rig;
    CHECK(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"channels":1}},
                                {"id":"j","type":"rp.core.join"},{"id":"t","type":"rp.test.through"}],
                      "wires":[{"from":"src.out","to":"j.in1"},{"from":"src.out","to":"j.in2"},
                               {"from":"j.out","to":"t.in"}]})").ok);
    CHECK(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"channels":6}},
                                {"id":"r","type":"rp.core.relabel"},{"id":"dm","type":"rp.core.downmix","settings":{"from":"5.1","to":"stereo"}}],
                      "wires":[{"from":"src.out","to":"r.in"},{"from":"r.out","to":"dm.in"}]})").ok);
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"channels":6}},
                                                {"id":"dm","type":"rp.core.downmix","settings":{"from":"5.1","to":"stereo"}}],
                                      "wires":[{"from":"src.out","to":"dm.in"}]})")) == "channel-mismatch");
}

TEST_CASE("core settings are validated with a reason", "[graph][core]") {
    GraphRig rig;
    const auto r = rig.set(R"({"nodes":[{"id":"j","type":"rp.core.join","settings":{"inputs":[1,1],"layout":"5.1"}}],"wires":[]})");
    REQUIRE(GraphRig::codes(r) == "settings-invalid");
    CHECK(r.errors[0].message.find("5.1") != std::string::npos);
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"d","type":"rp.core.downmix","settings":{"from":"stereo","to":"5.1"}}],"wires":[]})")) == "settings-invalid");
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"a","type":"rp.core.add","settings":{"inputs":0}}],"wires":[]})")) == "settings-invalid");
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"d","type":"rp.core.delay","settings":{"maxSamples":16777217}}],"wires":[]})")) == "settings-invalid");
}

TEST_CASE("core settings schemas name their structs the same under every compiler", "[graph][core]") {
    const std::string add = corePackage()->node_types[0]->descriptor();
    CHECK(add.find(R"("$ref":"#/$defs/rp__graph__core__AddSettings")") != std::string::npos);
    CHECK(add.find("anonymous") == std::string::npos);
}

TEST_CASE("merge orders events from several inputs by frame", "[graph][core]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.source"},{"id":"b","type":"rp.test.source"},
                                  {"id":"m","type":"rp.core.merge"},{"id":"sink","type":"rp.test.sink"}],
                         "wires":[{"from":"a.events","to":"m.in1"},{"from":"b.events","to":"m.in2"},
                                  {"from":"m.out","to":"sink.events"}]})").ok);
    rig.run();
    CHECK(rig.node<Sink>("sink").eventCount == 2);
}

TEST_CASE("a constant drives a param, and keeps its live value across a swap", "[graph][core]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"k","type":"rp.core.constant","settings":{"min":0,"max":10},"params":{"value":3}},
                                  {"id":"sink","type":"rp.test.sink"}],
                         "wires":[{"from":"k.out","to":"sink.control"}]})").ok);
    rig.run();
    CHECK(rig.node<Sink>("sink").controlEnd == 3.0);
    REQUIRE(rig.host->setParam("k", "value", 7.0, 10));
    rig.run();
    CHECK(rig.node<Sink>("sink").controlStart == 3.0);
    CHECK(rig.node<Sink>("sink").controlEnd == 7.0);

    REQUIRE(rig.set(R"({"nodes":[{"id":"k","type":"rp.core.constant","settings":{"min":0,"max":10},"params":{"value":3}},
                                  {"id":"src","type":"rp.test.source","settings":{"value":1}},
                                  {"id":"out","type":"rp.host.audio-out"}],
                         "wires":[{"from":"k.out","to":"src.gain"},{"from":"src.out","to":"out.out.1-2"}]})").ok);
    rig.run(64, 2);
    CHECK(rig.outs[0][0] == 7.0f);  // the document still says 3, but 3 was already applied: the live 7 stands
}

TEST_CASE("control + sums streams at every change frame", "[graph][core]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"k1","type":"rp.core.constant","params":{"value":0.25}},
                                  {"id":"k2","type":"rp.core.constant","params":{"value":0.5}},
                                  {"id":"sum","type":"rp.core.add","settings":{"kind":"control"}},
                                  {"id":"sink","type":"rp.test.sink"}],
                         "wires":[{"from":"k1.out","to":"sum.in1"},{"from":"k2.out","to":"sum.in2"},
                                  {"from":"sum.out","to":"sink.control"}]})").ok);
    rig.run();
    CHECK(rig.node<Sink>("sink").controlEnd == 0.75);
    REQUIRE(rig.host->setParam("k2", "value", 1.0, 20));
    rig.run();
    CHECK(rig.node<Sink>("sink").controlEnd == 1.25);
}

TEST_CASE("a delay delays by its param, from host-supplied memory", "[graph][core]") {
    GraphRig rig;
    const auto buf = rig.resources.createAudioBuffer(2, 4, 48000.0);
    static_cast<rp_audio_buffer*>(rig.resources.get(buf)->payload)->data[0][0] = 1.0f;  // an impulse, left
    REQUIRE(rig.set(R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"loop":false},"resources":{"sample":"res:)" +
                    std::to_string(buf) + R"("}},
                                  {"id":"d","type":"rp.core.delay","settings":{"maxSamples":100},"params":{"time":70}},
                                  {"id":"out","type":"rp.host.audio-out"}],
                         "wires":[{"from":"p.out","to":"d.in"},{"from":"d.out","to":"out.out.1-2"}]})").ok);
    rig.run(64);
    CHECK(rig.outs[0][0] == 0.0f);
    rig.run(64);
    CHECK(rig.outs[0][6] == 1.0f);  // 70 samples after the impulse at frame 0
}

TEST_CASE("the recorder writes a buffer that readers wired from it see in the same block", "[graph][core]") {
    GraphRig rig;
    const auto target = rig.resources.createAudioBuffer(2, 256, 48000.0);
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"value":0.5}},
                                  {"id":"rec","type":"rp.core.buffer-recorder","resources":{"target":"res:)" +
                    std::to_string(target) + R"("}},
                                  {"id":"p","type":"rp.core.buffer-player"},{"id":"out","type":"rp.host.audio-out"}],
                         "wires":[{"from":"src.out","to":"rec.in"},{"from":"rec.buffer","to":"p.sample"},
                                  {"from":"p.out","to":"out.out.1-2"}]})").ok);
    rig.run(64);
    CHECK(rig.outs[0][0] == 0.5f);
    CHECK(static_cast<const rp_audio_buffer*>(rig.resources.get(target)->payload)->data[1][63] == 0.5f);
}

TEST_CASE("resource bindings are checked", "[graph][core]") {
    GraphRig rig;
    const auto buf = rig.resources.createAudioBuffer(2, 128, 48000.0);
    const auto bytes = rig.resources.createBytes("bytes", {1, 2, 3});
    auto player = [](const std::string& ref) {
        return R"({"id":"p","type":"rp.core.buffer-player","resources":{"sample":")" + ref + R"("}})";
    };
    auto recorder = [&](const std::string& id) {
        return R"({"id":")" + id + R"(","type":"rp.core.buffer-recorder","resources":{"target":"res:)" + std::to_string(buf) + R"("}})";
    };
    auto doc = [](const std::string& nodes) { return R"({"nodes":[)" + nodes + R"(],"wires":[]})"; };
    CHECK(rig.set(doc(player("res:" + std::to_string(buf)))).ok);
    CHECK(GraphRig::codes(rig.set(doc(player("res:" + std::to_string(bytes))))) == "resource-type-mismatch");
    CHECK(GraphRig::codes(rig.set(doc(player("res:999")))) == "unknown-resource");
    CHECK(GraphRig::codes(rig.set(doc(recorder("r1") + "," + recorder("r2")))) == "resource-multiple-writers");
    CHECK(GraphRig::codes(rig.set(doc(recorder("r1") + "," + player("res:" + std::to_string(buf))))) == "resource-reader-unordered");
}

TEST_CASE("a plan pins the resources it binds until it is replaced", "[graph][core]") {
    GraphRig rig;
    const auto buf = rig.resources.createAudioBuffer(2, 8, 48000.0);
    REQUIRE(rig.set(R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","resources":{"sample":"res:)" + std::to_string(buf) +
                    R"("}}],"wires":[]})").ok);
    CHECK(rig.resources.pinCount(buf) == 1);
    CHECK(rig.resources.drop(buf));  // the owner lets go; the plan's pin keeps it alive
    CHECK_FALSE(rig.resources.drop(buf));  // and a duplicate drop cannot take the pin with it
    CHECK(rig.resources.liveCount() == 1);
    // A document still binding it is refused at once, not only after the old plan has been reclaimed.
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"q","type":"rp.core.buffer-player","resources":{"sample":"res:)" +
                                  std::to_string(buf) + R"("}}],"wires":[]})")) == "unknown-resource");
    REQUIRE(rig.set(R"({"nodes":[],"wires":[]})").ok);
    CHECK(rig.resources.liveCount() == 0);
}
