// Running graphs through GraphHost: buffer assignment, events, feedback, coupled groups, faults, params,
// and the state a plan swap must keep (docs/modular-graph-design.md section 5.3).
#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "GraphRig.hpp"

using namespace rp::graph;
using rp::graph::test::GraphRig;
using rp::graph::test::Linked;
using rp::graph::test::Sink;
using rp::graph::test::Source;
using rp::graph::test::Through;

TEST_CASE("audio-out writes wired buses and silences the rest", "[graph][exec]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"value":0.25}},
                                  {"id":"out","type":"rp.host.audio-out"}],
                         "wires":[{"from":"src.out","to":"out.out.3-4"}]})").ok);
    rig.run();
    CHECK(rig.outs[2][10] == 0.25f);
    CHECK(rig.outs[3][63] == 0.25f);
    CHECK(rig.outs[0][0] == 0.0f);  // nothing wired to out.1-2: the host bus gets silence, never stale data
}

TEST_CASE("buffers are reused, but an output never aliases its own input", "[graph][exec]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"value":1}},
                                  {"id":"t1","type":"rp.test.through"},{"id":"t2","type":"rp.test.through"},
                                  {"id":"t3","type":"rp.test.through"},{"id":"out","type":"rp.host.audio-out"}],
                         "wires":[{"from":"src.out","to":"t1.in"},{"from":"t1.out","to":"t2.in"},
                                  {"from":"t2.out","to":"t3.in"},{"from":"t3.out","to":"out.out.1-2"}]})").ok);
    rig.run();
    CHECK(rig.outs[0][0] == 4.0f);
    CHECK_FALSE(rig.node<Through>("t1").aliased);
    CHECK_FALSE(rig.node<Through>("t2").aliased);
    CHECK_FALSE(rig.node<Through>("t3").aliased);
}

TEST_CASE("events reach their consumer and are emptied between blocks", "[graph][exec]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source"},{"id":"sink","type":"rp.test.sink"}],
                         "wires":[{"from":"src.events","to":"sink.events"}]})").ok);
    rig.run(64, 3);
    CHECK(rig.node<Sink>("sink").eventCount == 1);
    CHECK(rig.node<Sink>("sink").eventBytes[0] == 0);  // block 3 starts at frame 128; its note carries 128 & 0x7f
}

TEST_CASE("a feedback wire delivers its value one block late", "[graph][exec]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source"},{"id":"sink","type":"rp.test.sink"}],
                         "wires":[{"from":"src.events","to":"sink.events","feedback":true}]})").ok);
    rig.run();
    CHECK(rig.node<Sink>("sink").eventCount == 0);
    rig.run();
    CHECK(rig.node<Sink>("sink").eventCount == 1);
    CHECK(rig.node<Sink>("sink").eventBytes[0] == 0);  // the note the source sent in the FIRST block
}

TEST_CASE("a coupled group runs its members in lockstep", "[graph][exec]") {
    GraphRig rig;
    rp::graph::test::linkLog().clear();
    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.linked","settings":{"name":"a"}},
                                  {"id":"b","type":"rp.test.linked","settings":{"name":"b"}}],
                         "wires":[{"from":"a.link","to":"b.link"}]})").ok);
    rig.run(4);
    const std::vector<std::string> expected = {"a", "b", "a", "b", "a", "b", "a", "b"};
    CHECK(rp::graph::test::linkLog() == expected);
    CHECK(rig.node<Linked>("a").peerCount == 1);
}

TEST_CASE("coupled peers are updated when the group changes", "[graph][exec]") {
    GraphRig rig;
    const std::string three = R"({"nodes":[{"id":"a","type":"rp.test.linked"},{"id":"b","type":"rp.test.linked"},
                                            {"id":"c","type":"rp.test.linked"}],
                                  "wires":[{"from":"a.link","to":"b.link"},{"from":"b.link","to":"c.link"}]})";
    REQUIRE(rig.set(three).ok);
    rig.run();
    CHECK(rig.node<Linked>("a").peerCount == 2);
    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.linked"},{"id":"b","type":"rp.test.linked"}],
                         "wires":[{"from":"a.link","to":"b.link"}]})").ok);
    rig.run();
    CHECK(rig.node<Linked>("a").peerCount == 1);
}

TEST_CASE("a faulting node is bypassed and counted, the rest keeps running", "[graph][exec]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"f","type":"rp.test.faulty","settings":{"failAt":2}},
                                  {"id":"src","type":"rp.test.source","settings":{"value":0.5,"channels":1}},
                                  {"id":"out","type":"rp.host.audio-out","settings":{"buses":[1,1]}}],
                         "wires":[{"from":"f.out","to":"out.out.1"},{"from":"src.out","to":"out.out.2"}]})").ok);
    rig.run();
    CHECK(rig.outs[0][0] == 1.0f);
    rig.run(64, 3);
    CHECK(rig.outs[0][0] == 0.0f);
    CHECK(rig.outs[1][0] == 0.5f);
    CHECK(rig.host->faultCount() == 1);
}

TEST_CASE("a faulted node stays bypassed through a plan swap", "[graph][exec][swap]") {
    GraphRig rig;
    auto doc = [](const std::string& extra) {
        return R"({"nodes":[{"id":"f","type":"rp.test.faulty","settings":{"failAt":1}},
                            {"id":"out","type":"rp.host.audio-out","settings":{"buses":[1,1]}})" + extra +
               R"(],"wires":[{"from":"f.out","to":"out.out.1"}]})";
    };
    REQUIRE(rig.set(doc("")).ok);
    rig.run();
    REQUIRE(rig.host->isFaulted("f"));
    REQUIRE(rig.set(doc(R"(,{"id":"extra","type":"rp.test.sink"})")).ok);  // an unrelated edit
    rig.run(64, 2);
    CHECK(rig.host->isFaulted("f"));
    CHECK(rig.node<rp::graph::test::Faulty>("f").blocks == 1);  // never called again
    CHECK(rig.host->faultCount() == 1);
    CHECK(rig.outs[0][0] == 0.0f);
    CHECK_FALSE(rig.host->isFaulted("extra"));
    CHECK_FALSE(rig.host->isFaulted("ghost"));
}

TEST_CASE("removing and re-adding a faulted node builds it fresh", "[graph][exec][swap]") {
    GraphRig rig;
    const std::string with = R"({"nodes":[{"id":"f","type":"rp.test.faulty","settings":{"failAt":2}},
                                           {"id":"out","type":"rp.host.audio-out","settings":{"buses":[1,1]}}],
                                 "wires":[{"from":"f.out","to":"out.out.1"}]})";
    REQUIRE(rig.set(with).ok);
    rig.run(64, 2);
    REQUIRE(rig.host->isFaulted("f"));
    REQUIRE(rig.set(R"({"nodes":[{"id":"out","type":"rp.host.audio-out","settings":{"buses":[1,1]}}],"wires":[]})").ok);
    REQUIRE(rig.set(with).ok);
    CHECK_FALSE(rig.host->isFaulted("f"));
    rig.run();
    CHECK(rig.outs[0][0] == 1.0f);
    CHECK(rig.host->faultCount() == 1);  // the count is cumulative
}

TEST_CASE("a live param rides the next block; document params apply on adoption", "[graph][exec]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"value":0.5},"params":{"gain":2}},
                                  {"id":"out","type":"rp.host.audio-out"}],
                         "wires":[{"from":"src.out","to":"out.out.1-2"}]})").ok);
    rig.run();
    CHECK(rig.outs[0][0] == 1.0f);
    REQUIRE(rig.host->setParam("src", "gain", 4.0));
    rig.run();  // the change rides this block's stream; Source reads gain at block start, so it shows next block
    rig.run();
    CHECK(rig.outs[0][0] == 2.0f);
    REQUIRE(rig.host->setParam("src", "gain", 1e9));  // clamped to the declared 0..10
    rig.run();
    rig.run();
    CHECK(rig.outs[0][0] == 5.0f);
    CHECK_FALSE(rig.host->setParam("src", "gain", std::numeric_limits<double>::quiet_NaN()));
    CHECK_FALSE(rig.host->setParam("src", "nope", 1.0));
    CHECK_FALSE(rig.host->setParam("ghost", "gain", 1.0));
}

TEST_CASE("a swap keeps every reused instance's state", "[graph][exec][swap]") {
    GraphRig rig;
    const std::string a = R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"value":0.5}},
                                       {"id":"out","type":"rp.host.audio-out"}],
                              "wires":[{"from":"src.out","to":"out.out.1-2"}]})";
    const std::string b = R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"value":0.5}},
                                       {"id":"extra","type":"rp.test.sink"},{"id":"out","type":"rp.host.audio-out"}],
                              "wires":[{"from":"src.out","to":"out.out.5-6"},{"from":"src.out","to":"extra.in"}]})";
    REQUIRE(rig.set(a).ok);
    rig.run(64, 3);
    REQUIRE(rig.node<Source>("src").frames == 192);
    REQUIRE(rig.set(b).ok);  // rewire and add a node
    rig.run(64, 1);
    CHECK(rig.node<Source>("src").frames == 256);
    CHECK(rig.outs[4][0] == 0.5f);
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"value":0.75}},
                                  {"id":"out","type":"rp.host.audio-out"}],
                         "wires":[{"from":"src.out","to":"out.out.1-2"}]})").ok);  // a live-swappable setting
    rig.run(64, 1);
    CHECK(rig.node<Source>("src").frames == 320);
    CHECK(rig.outs[0][0] == 0.75f);
}

TEST_CASE("settings compare in canonical form", "[graph][exec][swap]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"value":0.5,"channels":2}}],"wires":[]})").ok);
    rig.run(64, 2);
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{ "channels": 2, "value": 0.50 }}],"wires":[]})").ok);
    rig.run(64, 1);
    CHECK(rig.node<Source>("src").frames == 192);
}

TEST_CASE("a layout-changing setting rebuilds the instance", "[graph][exec][swap]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source"}],"wires":[]})").ok);
    rig.run(64, 2);
    REQUIRE(rig.node<Source>("src").frames == 128);
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source","settings":{"channels":4}}],"wires":[]})").ok);
    rig.run(64, 1);
    CHECK(rig.node<Source>("src").frames == 64);
}

TEST_CASE("re-activation at a new rate preserves state", "[graph][exec][swap]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source"}],"wires":[]})").ok);
    rig.run(64, 2);
    REQUIRE(rig.host->reactivate(96000.0, 64));
    rig.run(64, 1);
    CHECK(rig.node<Source>("src").frames == 192);
}

TEST_CASE("a block longer than the plan's buffers is refused, not overrun", "[graph][exec]") {
    GraphRig rig;  // maxFrames 64
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source"},{"id":"out","type":"rp.host.audio-out"}],
                         "wires":[{"from":"src.out","to":"out.out.1-2"}]})").ok);
    rig.run(256);
    CHECK(rig.outs[0][0] == -99.0f);  // nothing ran
    CHECK(rig.node<Source>("src").frames == 0);
}

TEST_CASE("re-activation at a larger block size re-sizes every buffer, feedback included", "[graph][exec][swap]") {
    GraphRig rig;  // maxFrames 64
    REQUIRE(rig.set(R"({"nodes":[{"id":"x","type":"rp.test.through"},{"id":"y","type":"rp.test.through"},
                                  {"id":"out","type":"rp.host.audio-out"}],
                         "wires":[{"from":"x.out","to":"y.in"},{"from":"y.out","to":"x.in","feedback":true},
                                  {"from":"y.out","to":"out.out.1-2"}]})").ok);
    rig.run(64);  // x reads 0 back, so y = 2
    REQUIRE(rig.outs[0][63] == 2.0f);
    REQUIRE(rig.host->reactivate(48000.0, 256));
    rig.run(256);  // the 64 frames in flight carried over (2 -> 4); the new tail starts from silence (0 -> 2)
    CHECK(rig.outs[0][0] == 4.0f);
    CHECK(rig.outs[0][255] == 2.0f);
    rig.run(256);
    CHECK(rig.outs[0][0] == 6.0f);
    CHECK(rig.outs[0][255] == 4.0f);
}

TEST_CASE("feedback data in flight survives a swap that keeps the wire", "[graph][exec][swap]") {
    GraphRig rig;
    auto doc = [](const std::string& extra) {
        return R"({"nodes":[{"id":"src","type":"rp.test.source"},{"id":"sink","type":"rp.test.sink"})" + extra +
               R"(],"wires":[{"from":"src.events","to":"sink.events","feedback":true}]})";
    };
    REQUIRE(rig.set(doc("")).ok);
    rig.run();  // the source's first note is now in flight
    REQUIRE(rig.set(doc(R"(,{"id":"other","type":"rp.test.sink"})")).ok);
    rig.run();
    CHECK(rig.node<Sink>("sink").eventCount == 1);
    CHECK(rig.node<Sink>("sink").eventBytes[0] == 0);
}

TEST_CASE("removing a node frees it", "[graph][exec]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.sink"},{"id":"b","type":"rp.test.sink"}],"wires":[]})").ok);
    CHECK(rig.host->instanceCount() == 2);
    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.sink"}],"wires":[]})").ok);
    CHECK(rig.host->instanceCount() == 1);
    CHECK(rig.host->node("b") == nullptr);
}
