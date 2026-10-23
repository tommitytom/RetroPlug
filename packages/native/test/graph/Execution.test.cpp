// Running graphs through GraphHost: buffer assignment, events, feedback, coupled groups, faults, params,
// and the state a plan swap must keep (docs/modular-graph-design.md section 5.3).
#include <catch2/catch_test_macros.hpp>

#include <iterator>
#include <limits>
#include <string>

#include "GraphRig.hpp"

using namespace rp::graph;
using rp::graph::test::GraphRig;
using rp::graph::test::Linked;
using rp::graph::test::Sink;
using rp::graph::test::Source;
using rp::graph::test::Through;

namespace {

// Publishes its Value param on a control output each block; keeps what its Steps param last read.
struct Knob {
    static constexpr const char* id = "rp.test.knob";
    struct { rp::control_out out; } outputs;
    struct {
        rp::float_param<"Value", rp::range{-100, 100, 0}> value;
        rp::int_param<"Steps", rp::range{0, 8, 0}>        steps;
    } params;
    double steps = 0.0;
    void process(const rp::Block&) noexcept {
        outputs.out.push(0, params.value.start());
        steps = params.steps.start();
    }
};

// Publishes the buffer it is bound to as its output each block, until it faults on block `failAt`.
struct WriterSettings {
    std::uint32_t failAt = 0;  // 0: never
};

struct Writer {
    static constexpr const char* id = "rp.test.writer";
    using Settings = WriterSettings;
    struct { rp::resource_write<"audio-buffer"> target; } inputs;
    struct { rp::resource_out<"audio-buffer"> out; } outputs;
    std::uint32_t failAt = 0, blocks = 0;
    void on_settings(const Settings& s) noexcept { failAt = s.failAt; }
    rp_status process(const rp::Block&) noexcept {
        if (failAt && ++blocks >= failAt) return RP_FAULT;
        outputs.out.set(inputs.target.get());
        return RP_OK;
    }
};

// Reads the first sample of whatever buffer it is handed (so ASan sees a freed one).
struct Reader {
    static constexpr const char* id = "rp.test.reader";
    struct { rp::resource_in<"audio-buffer"> in; } inputs;
    const rp_resource* seen = nullptr;
    float              first = 0.0f;
    void process(const rp::Block&) noexcept {
        seen = inputs.in.get();
        if (seen) first = static_cast<const rp_audio_buffer*>(seen->payload)->data[0][0];
    }
};

// Refuses to activate beyond `limit` frames, at all after its first activation (`once`), or at fewer frames
// than it last had (`noShrink`). Keeps the block size it was last activated with.
struct PickySettings {
    std::uint32_t limit = 0;
    bool          once = false;
    bool          noShrink = false;
};

struct Picky {
    static constexpr const char* id = "rp.test.picky";
    using Settings = PickySettings;
    PickySettings s;
    std::uint32_t activations = 0, processed = 0, frames = 0;
    void on_settings(const Settings& st) noexcept { s = st; }
    bool activate(double, std::uint32_t maxFrames) {
        if ((s.once && activations > 0) || (s.limit && maxFrames > s.limit) || (s.noShrink && maxFrames < frames))
            return false;
        ++activations;
        frames = maxFrames;
        return true;
    }
    void process(const rp::Block&) noexcept { ++processed; }
};

int g_countedDestroyed = 0;

struct Counted {
    static constexpr const char* id = "rp.test.counted";
    ~Counted() { ++g_countedDestroyed; }
    void process(const rp::Block&) noexcept {}
};

const rp_package* execPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Knob>(), rp::node_type<Writer>(), rp::node_type<Reader>(),
                                                 rp::node_type<Picky>(), rp::node_type<Counted>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.exec"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

struct ExecRig : GraphRig {
    ExecRig() { registry.addPackage(execPackage()); }
};

}  // namespace

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
    CHECK(rig.outs[0][0] == 0.0f);  // nothing ran, and the host's buffer was not left as it was
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
    ExecRig rig;
    g_countedDestroyed = 0;
    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.sink"},{"id":"b","type":"rp.test.counted"}],"wires":[]})").ok);
    CHECK(rig.host->instanceCount() == 2);
    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.sink"}],"wires":[]})").ok);
    CHECK(rig.host->instanceCount() == 1);
    CHECK(rig.host->node("b") == nullptr);
    rig.host->reclaim();  // the replaced plan held the last reference
    CHECK(g_countedDestroyed == 1);
}

TEST_CASE("a node whose type changes under the same id is rebuilt as the new type", "[graph][exec][swap]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"n","type":"rp.test.sink"}],"wires":[]})").ok);
    REQUIRE(rig.set(R"({"nodes":[{"id":"n","type":"rp.test.through"}],"wires":[]})").ok);
    CHECK(rp::peer_as<Through>(rig.host->node("n")) != nullptr);
    CHECK(rp::peer_as<Sink>(rig.host->node("n")) == nullptr);
    CHECK(rig.host->instanceCount() == 1);
}

TEST_CASE("a feedback wire removed and added back starts from silence", "[graph][exec][swap]") {
    GraphRig rig;
    const std::string loop = R"({"nodes":[{"id":"x","type":"rp.test.through"},{"id":"y","type":"rp.test.through"},
                                          {"id":"out","type":"rp.host.audio-out"}],
                                 "wires":[{"from":"x.out","to":"y.in"},{"from":"y.out","to":"x.in","feedback":true},
                                          {"from":"y.out","to":"out.out.1-2"}]})";
    REQUIRE(rig.set(loop).ok);
    rig.run(64, 2);
    REQUIRE(rig.outs[0][0] == 4.0f);
    REQUIRE(rig.set(R"({"nodes":[{"id":"x","type":"rp.test.through"},{"id":"y","type":"rp.test.through"},
                                  {"id":"out","type":"rp.host.audio-out"}],
                         "wires":[{"from":"x.out","to":"y.in"},{"from":"y.out","to":"out.out.1-2"}]})").ok);
    rig.run();
    REQUIRE(rig.set(loop).ok);
    rig.run();
    CHECK(rig.outs[0][0] == 2.0f);  // x read silence, not the 4 the old wire last carried
}

TEST_CASE("a bypassed writer stops publishing its resource handle", "[graph][exec]") {
    ExecRig rig;
    const auto a = rig.resources.createAudioBuffer(1, 4, 48000.0);
    const auto b = rig.resources.createAudioBuffer(1, 4, 48000.0);
    static_cast<rp_audio_buffer*>(rig.resources.get(a)->payload)->data[0][0] = 1.0f;
    auto doc = [](ResourceTable::Id target) {
        return R"({"nodes":[{"id":"w","type":"rp.test.writer","settings":{"failAt":2},"resources":{"target":"res:)" +
               std::to_string(target) + R"("}},{"id":"r","type":"rp.test.reader"}],"wires":[{"from":"w.out","to":"r.in"}]})";
    };
    REQUIRE(rig.set(doc(a)).ok);
    rig.run();
    CHECK(rig.node<Reader>("r").first == 1.0f);
    rig.run();  // the writer faults
    CHECK(rig.node<Reader>("r").seen == nullptr);
    // A is dropped and only the old plan pinned it; once that plan is reclaimed, A is gone.
    REQUIRE(rig.resources.drop(a));
    REQUIRE(rig.set(doc(b)).ok);
    rig.host->reclaim();
    CHECK(rig.resources.liveCount() == 1);
    rig.run();
    CHECK(rig.node<Reader>("r").seen == nullptr);
}

TEST_CASE("a param wired from a control output follows it; setParam leaves it alone", "[graph][exec]") {
    ExecRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"k","type":"rp.test.knob","params":{"value":3}},
                                  {"id":"src","type":"rp.test.source","settings":{"value":0.5}},
                                  {"id":"out","type":"rp.host.audio-out"}],
                         "wires":[{"from":"k.out","to":"src.gain"},{"from":"src.out","to":"out.out.1-2"}]})").ok);
    rig.run(64, 2);
    CHECK(rig.outs[0][0] == 1.5f);
    CHECK_FALSE(rig.host->setParam("src", "gain", 4.0));  // the wire drives it
    REQUIRE(rig.host->setParam("k", "steps", 2.6));
    rig.run(64, 2);  // the change rides one block's stream; start() shows it the block after
    CHECK(rig.node<Knob>("k").steps == 3.0);  // rounded, as a document value would be
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"k","type":"rp.test.knob"},
                                                {"id":"src","type":"rp.test.source","params":{"gain":2}}],
                                      "wires":[{"from":"k.out","to":"src.gain"}]})")) == "input-taken");
}

TEST_CASE("a control feedback wire delivers its changes one block late", "[graph][exec]") {
    ExecRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"k","type":"rp.test.knob","params":{"value":7}},{"id":"s","type":"rp.test.sink"}],
                         "wires":[{"from":"k.out","to":"s.control","feedback":true}]})").ok);
    rig.run();
    CHECK(rig.node<Sink>("s").controlChanges == 0);
    rig.run();
    CHECK(rig.node<Sink>("s").controlChanges == 1);
    CHECK(rig.node<Sink>("s").controlEnd == 7.0);
}

TEST_CASE("host outputs start every block silent", "[graph][exec]") {
    GraphRig rig;
    rig.run();  // before any plan
    CHECK(rig.outs[0][0] == 0.0f);
    REQUIRE(rig.set(R"({"nodes":[{"id":"src","type":"rp.test.source"},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                         "wires":[{"from":"src.out","to":"out.out.1-2"}]})").ok);
    rig.run();
    CHECK(rig.outs[0][0] == 1.0f);
    CHECK(rig.outs[2][0] == 0.0f);  // beyond the only bus
    CHECK(rig.outs[7][63] == 0.0f);
}

TEST_CASE("a node that will not re-activate leaves the running plan safe", "[graph][exec][swap]") {
    ExecRig rig;  // maxFrames 64
    REQUIRE(rig.set(R"({"nodes":[{"id":"p","type":"rp.test.picky","settings":{"limit":128}},
                                  {"id":"src","type":"rp.test.source"}],"wires":[]})").ok);
    rig.run();
    std::string error;
    CHECK_FALSE(rig.host->reactivate(48000.0, 256, &error));
    CHECK(error.find("\"p\"") != std::string::npos);
    CHECK_FALSE(rig.host->isFaulted("p"));  // back on 64 frames
    CHECK(rig.node<Picky>("p").activations == 2);
    rig.run(256);  // the old plan still refuses what it was not built for
    CHECK(rig.node<Picky>("p").processed == 1);
    rig.run(64);
    CHECK(rig.node<Picky>("p").processed == 2);
    CHECK(rig.node<Source>("src").frames == 128);
}

TEST_CASE("instances switched before a refusal go back to the old size", "[graph][exec][swap]") {
    ExecRig rig;  // maxFrames 64; live instances re-activate in id order, so "a" switches before "z" refuses
    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.picky"},
                                  {"id":"z","type":"rp.test.picky","settings":{"limit":128}}],"wires":[]})").ok);
    CHECK_FALSE(rig.host->reactivate(48000.0, 256));
    CHECK(rig.node<Picky>("a").frames == 64);
    CHECK(rig.node<Picky>("a").activations == 3);  // 64, then 256, then back to 64
    CHECK_FALSE(rig.host->isFaulted("a"));
    CHECK_FALSE(rig.host->isFaulted("z"));
}

TEST_CASE("a switched instance that will not go back is bypassed", "[graph][exec][swap]") {
    ExecRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.picky","settings":{"noShrink":true}},
                                  {"id":"z","type":"rp.test.picky","settings":{"limit":128}}],"wires":[]})").ok);
    CHECK_FALSE(rig.host->reactivate(48000.0, 256));
    CHECK(rig.host->isFaulted("a"));  // stuck at 256 frames under a plan built for 64
    CHECK_FALSE(rig.host->isFaulted("z"));
    CHECK(rig.host->faultCount() == 1);
    rig.run();
    CHECK(rig.node<Picky>("a").processed == 0);
    CHECK(rig.node<Picky>("z").processed == 1);
}

TEST_CASE("a node that will not come back at all is bypassed", "[graph][exec][swap]") {
    ExecRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"p","type":"rp.test.picky","settings":{"once":true}},
                                  {"id":"src","type":"rp.test.source"}],"wires":[]})").ok);
    CHECK_FALSE(rig.host->reactivate(96000.0, 64));
    CHECK(rig.host->isFaulted("p"));
    rig.run();
    CHECK(rig.node<Picky>("p").processed == 0);  // never run deactivated
    CHECK(rig.node<Source>("src").frames == 64);
}

TEST_CASE("reactivate refuses a graph that no longer plans and changes nothing", "[graph][exec][swap]") {
    ExecRig rig;
    const auto buf = rig.resources.createAudioBuffer(1, 4, 48000.0);
    REQUIRE(rig.set(R"({"nodes":[{"id":"r","type":"rp.test.reader","resources":{"in":"res:)" + std::to_string(buf) +
                    R"("}}],"wires":[]})").ok);
    REQUIRE(rig.resources.drop(buf));  // still pinned by the running plan, but no longer bindable
    std::string error;
    CHECK_FALSE(rig.host->reactivate(48000.0, 256, &error));
    CHECK(error.find("no longer plans") != std::string::npos);
    CHECK(rig.host->config().maxFrames == 64);
    CHECK_FALSE(rig.host->reactivate(0.0, 64, &error));
    CHECK(error.find("sample rate") != std::string::npos);
}
