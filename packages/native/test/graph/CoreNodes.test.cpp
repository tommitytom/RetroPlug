// The rp.core package: explicit combining and conversion, the constant source, delay, and buffer playback
// and recording - plus the planner's resource-binding rules, which these are the first nodes to exercise.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

#include "GraphRig.hpp"

using namespace rp::graph;
using rp::graph::test::GraphRig;
using rp::graph::test::Sink;

namespace {

// One event per block at a set frame, tagged in its second byte.
struct PulseSettings {
    std::uint32_t frame = 0;
    std::uint32_t tag = 0;
};

struct Pulse {
    static constexpr const char* id = "rp.test.pulse";
    using Settings = PulseSettings;
    struct { rp::events_out out; } outputs;
    PulseSettings s;
    void on_settings(const Settings& st) noexcept { s = st; }
    void process(const rp::Block&) noexcept {
        const std::uint8_t msg[3] = {0x90, static_cast<std::uint8_t>(s.tag), 1};
        outputs.out.push(s.frame, msg);
    }
};

// A control stream: `start` from frame 0, then values[i] at frames[i], all in its first block.
struct StepsSettings {
    double                     start = 0.0;
    std::vector<std::uint32_t> frames;
    std::vector<double>        values;
};

struct Steps {
    static constexpr const char* id = "rp.test.steps";
    using Settings = StepsSettings;
    struct { rp::control_out out; } outputs;
    const StepsSettings* s = nullptr;
    bool                 done = false;
    void on_settings(const Settings& st) noexcept { s = &st; }
    void process(const rp::Block&) noexcept {
        if (done) return;
        done = true;
        if (s->start != outputs.out.queue->start) outputs.out.push(0, s->start);
        for (std::size_t i = 0; i < s->frames.size() && i < s->values.size(); ++i) outputs.out.push(s->frames[i], s->values[i]);
    }
};

// What reached a control input this block: its start, every change, and the stream's drop count.
struct ControlLog {
    static constexpr const char* id = "rp.test.control-log";
    struct { rp::control_in in; } inputs;
    double                                       start = 0.0;
    std::vector<std::pair<std::uint32_t, double>> changes;  // reserved: no allocation in process
    std::uint32_t                                dropped = 0;
    double                                       end = 0.0;
    ControlLog() { changes.reserve(256); }
    void process(const rp::Block&) noexcept {
        start = inputs.in.start();
        changes.clear();
        for (const auto& c : inputs.in.changes())
            if (changes.size() < changes.capacity()) changes.emplace_back(c.frame, c.value);
        dropped = inputs.in.queue->dropped;
        end = controlEnd(*inputs.in.queue);
    }
};

const rp_package* coreTestPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Pulse>(), rp::node_type<Steps>(), rp::node_type<ControlLog>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.core"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

struct CoreRig : GraphRig {
    explicit CoreRig(std::uint32_t maxFrames = 64) : GraphRig({48000.0, maxFrames, Target::Desktop}) {
        registry.addPackage(coreTestPackage());
    }

    // An audio buffer whose every sample is value(channel, frame).
    ResourceTable::Id buffer(std::uint32_t channels, std::uint32_t frames, const std::function<float(std::uint32_t, std::uint32_t)>& value) {
        const auto id = resources.createAudioBuffer(channels, frames, 48000.0);
        auto* b = static_cast<rp_audio_buffer*>(resources.get(id)->payload);
        for (std::uint32_t c = 0; c < channels; ++c)
            for (std::uint32_t i = 0; i < frames; ++i) b->data[c][i] = value(c, i);
        return id;
    }

    static std::string res(ResourceTable::Id id) { return "\"res:" + std::to_string(id) + "\""; }
};

using Changes = std::vector<std::pair<std::uint32_t, double>>;

}  // namespace

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

TEST_CASE("split, join and downmix move channels explicitly, in order", "[graph][core]") {
    CoreRig rig;
    const auto lr = rig.buffer(2, 64, [](std::uint32_t c, std::uint32_t) { return c == 0 ? 0.25f : 0.75f; });
    REQUIRE(rig.set(R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","resources":{"sample":)" + CoreRig::res(lr) + R"(}},
                                  {"id":"split","type":"rp.core.split"},{"id":"join","type":"rp.core.join"},
                                  {"id":"dm","type":"rp.core.downmix"},
                                  {"id":"out","type":"rp.host.audio-out","settings":{"buses":[2,1]}}],
                         "wires":[{"from":"p.out","to":"split.in"},{"from":"p.out","to":"dm.in"},
                                  {"from":"split.out1","to":"join.in2"},{"from":"split.out2","to":"join.in1"},
                                  {"from":"join.out","to":"out.out.1-2"},{"from":"dm.out","to":"out.out.3"}]})").ok);
    rig.run();
    CHECK(rig.outs[0][0] == 0.75f);  // right, joined first
    CHECK(rig.outs[1][0] == 0.25f);
    CHECK(rig.outs[2][0] == 1.0f);  // L + R, the equal-sum downmix
}

TEST_CASE("5.1 downmixes to stereo by ITU-style coefficients and to mono as an equal sum", "[graph][core]") {
    CoreRig rig;  // channel c holds c + 1: L 1, R 2, C 3, LFE 4, Ls 5, Rs 6
    const auto surround = rig.buffer(6, 64, [](std::uint32_t c, std::uint32_t) { return static_cast<float>(c + 1); });
    REQUIRE(rig.set(R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":6,"layout":"5.1"},
                                   "resources":{"sample":)" + CoreRig::res(surround) + R"(}},
                                  {"id":"stereo","type":"rp.core.downmix","settings":{"from":"5.1","to":"stereo"}},
                                  {"id":"mono","type":"rp.core.downmix","settings":{"from":"5.1","to":"mono"}},
                                  {"id":"out","type":"rp.host.audio-out","settings":{"buses":[2,1]}}],
                         "wires":[{"from":"p.out","to":"stereo.in"},{"from":"p.out","to":"mono.in"},
                                  {"from":"stereo.out","to":"out.out.1-2"},{"from":"mono.out","to":"out.out.3"}]})").ok);
    rig.run();
    constexpr float k = 0.70710678f;
    CHECK(rig.outs[0][0] == 1.0f + k * 3.0f + k * 5.0f);  // L + C + Ls, LFE dropped
    CHECK(rig.outs[1][0] == 2.0f + k * 3.0f + k * 6.0f);  // R + C + Rs
    CHECK(rig.outs[2][0] == 21.0f);  // all six, unscaled
}

TEST_CASE("+ sums any number of inputs", "[graph][core]") {
    GraphRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.source","settings":{"value":1}},
                                  {"id":"b","type":"rp.test.source","settings":{"value":2}},
                                  {"id":"c","type":"rp.test.source","settings":{"value":4}},
                                  {"id":"sum","type":"rp.core.add","settings":{"inputs":3}},
                                  {"id":"out","type":"rp.host.audio-out"}],
                         "wires":[{"from":"a.out","to":"sum.in1"},{"from":"b.out","to":"sum.in2"},
                                  {"from":"c.out","to":"sum.in3"},{"from":"sum.out","to":"out.out.1-2"}]})").ok);
    rig.run();
    CHECK(rig.outs[1][63] == 7.0f);
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
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"d","type":"rp.core.delay","settings":{"maxSamples":16777216,"channels":8}}],"wires":[]})")) == "settings-invalid");
    // A wrapped channel sum is refused by the node, with a reason, not left for the planner to trip on.
    const auto wrapped = rig.set(R"({"nodes":[{"id":"s","type":"rp.core.split","settings":{"outputs":[4294967295,2],"layout":"mono"}}],"wires":[]})");
    REQUIRE(GraphRig::codes(wrapped) == "settings-invalid");
    CHECK(wrapped.errors[0].message.find("64") != std::string::npos);
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"r","type":"rp.core.relabel","settings":{"channels":0,"layout":"mono"}}],"wires":[]})")) == "settings-invalid");
    CHECK(GraphRig::codes(rig.set(R"x({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":65,"layout":"discrete(65)"}}],"wires":[]})x")) == "settings-invalid");
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"r","type":"rp.core.buffer-recorder","settings":{"channels":0}}],"wires":[]})")) == "settings-invalid");
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"k","type":"rp.core.constant","settings":{"min":2,"max":1}}],"wires":[]})")) == "settings-invalid");
    CHECK(GraphRig::codes(rig.set(R"x({"nodes":[{"id":"d","type":"rp.core.downmix","settings":{"from":"discrete(65)","to":"mono"}}],"wires":[]})x")) == "settings-invalid");
}

TEST_CASE("core settings schemas name their structs the same under every compiler", "[graph][core]") {
    const std::string add = corePackage()->node_types[0]->descriptor();
    CHECK(add.find(R"("$ref":"#/$defs/rp__graph__core__AddSettings")") != std::string::npos);
    CHECK(add.find("anonymous") == std::string::npos);
}

TEST_CASE("merge orders events from several inputs by frame, ties in input order", "[graph][core]") {
    CoreRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.pulse","settings":{"frame":10,"tag":1}},
                                  {"id":"b","type":"rp.test.pulse","settings":{"frame":5,"tag":2}},
                                  {"id":"c","type":"rp.test.pulse","settings":{"frame":10,"tag":3}},
                                  {"id":"m","type":"rp.core.merge","settings":{"inputs":3}},{"id":"sink","type":"rp.test.sink"}],
                         "wires":[{"from":"a.out","to":"m.in1"},{"from":"b.out","to":"m.in2"},{"from":"c.out","to":"m.in3"},
                                  {"from":"m.out","to":"sink.events"}]})").ok);
    rig.run();
    const auto& sink = rig.node<Sink>("sink");
    REQUIRE(sink.eventCount == 3);
    CHECK(sink.eventFrames[0] == 5);
    CHECK(sink.eventBytes[0] == 2);
    CHECK(sink.eventFrames[1] == 10);
    CHECK(sink.eventBytes[1] == 1);
    CHECK(sink.eventFrames[2] == 10);
    CHECK(sink.eventBytes[2] == 3);
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

TEST_CASE("control + emits the sum at every frame where it changes", "[graph][core]") {
    CoreRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.steps","settings":{"start":1,"frames":[10,20],"values":[2,3]}},
                                  {"id":"b","type":"rp.test.steps","settings":{"start":10,"frames":[20,30],"values":[20,30]}},
                                  {"id":"c","type":"rp.test.steps","settings":{"start":100}},
                                  {"id":"sum","type":"rp.core.add","settings":{"inputs":3,"kind":"control"}},
                                  {"id":"log","type":"rp.test.control-log"}],
                         "wires":[{"from":"a.out","to":"sum.in1"},{"from":"b.out","to":"sum.in2"},{"from":"c.out","to":"sum.in3"},
                                  {"from":"sum.out","to":"log.in"}]})").ok);
    rig.run();
    // frame 0 folds the three starting values (each pushed at frame 0) into one change; frame 20 moves two
    // inputs at once and is still one change
    CHECK(rig.node<ControlLog>("log").changes == Changes{{0, 111}, {10, 112}, {20, 123}, {30, 133}});
    rig.run();
    CHECK(rig.node<ControlLog>("log").start == 133);
    CHECK(rig.node<ControlLog>("log").changes.empty());
}

TEST_CASE("a control stream that overflows still ends on the right value", "[graph][core]") {
    CoreRig rig(128);  // 128 change frames: more than a stream's 64 changes hold
    std::string fa, va, fb, vb;
    for (int i = 0; i < 64; ++i) {
        fa += (i ? "," : "") + std::to_string(i * 2);
        va += (i ? "," : "") + std::to_string(i);
        fb += (i ? "," : "") + std::to_string(i * 2 + 1);
        vb += (i ? "," : "") + std::to_string(i * 100);
    }
    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.test.steps","settings":{"frames":[)" + fa + R"(],"values":[)" + va + R"(]}},
                                  {"id":"b","type":"rp.test.steps","settings":{"frames":[)" + fb + R"(],"values":[)" + vb + R"(]}},
                                  {"id":"sum","type":"rp.core.add","settings":{"kind":"control"}},
                                  {"id":"log","type":"rp.test.control-log"}],
                         "wires":[{"from":"a.out","to":"sum.in1"},{"from":"b.out","to":"sum.in2"},
                                  {"from":"sum.out","to":"log.in"}]})").ok);
    rig.run(128);
    const auto& log = rig.node<ControlLog>("log");
    CHECK(log.dropped > 0);
    CHECK(log.end == 63 + 6300);  // the last change is never the one that is lost
    CHECK(log.changes.back() == std::pair<std::uint32_t, double>{127, 6363});
}

TEST_CASE("a constant changing at every frame still ends on its last value", "[graph][core]") {
    CoreRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"k","type":"rp.core.constant","params":{"value":0.5}},{"id":"log","type":"rp.test.control-log"}],
                         "wires":[{"from":"k.out","to":"log.in"}]})").ok);
    for (std::uint32_t i = 0; i < 64; ++i) REQUIRE(rig.host->setParam("k", "value", i / 100.0, i));
    rig.run();
    CHECK(rig.node<ControlLog>("log").end == 0.63);
    CHECK(rig.node<ControlLog>("log").dropped == 0);  // the frame-0 change replaced the start; nothing overflowed
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

TEST_CASE("a delay's time takes effect at the frame it changes", "[graph][core]") {
    CoreRig rig;
    const auto ramp = rig.buffer(2, 64, [](std::uint32_t, std::uint32_t i) { return static_cast<float>(i); });
    REQUIRE(rig.set(R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","resources":{"sample":)" + CoreRig::res(ramp) + R"(}},
                                  {"id":"d","type":"rp.core.delay","settings":{"maxSamples":100}},
                                  {"id":"out","type":"rp.host.audio-out"}],
                         "wires":[{"from":"p.out","to":"d.in"},{"from":"d.out","to":"out.out.1-2"}]})").ok);
    REQUIRE(rig.host->setParam("d", "time", 5, 10));
    rig.run();
    CHECK(rig.outs[0][9] == 9.0f);   // time 0
    CHECK(rig.outs[0][10] == 5.0f);  // time 5 from frame 10
    CHECK(rig.outs[0][63] == 58.0f);
}

TEST_CASE("the player loops, stops, and starts a new buffer from its beginning", "[graph][core]") {
    CoreRig rig;
    const auto four = rig.buffer(1, 4, [](std::uint32_t, std::uint32_t i) { return static_cast<float>(i + 1); });
    const auto two = rig.buffer(1, 2, [](std::uint32_t, std::uint32_t i) { return static_cast<float>(10 + i); });
    auto doc = [&](ResourceTable::Id b, bool loop) {
        return R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"loop":)" + std::string(loop ? "true" : "false") +
               R"(},"resources":{"sample":)" + CoreRig::res(b) + R"(}},{"id":"out","type":"rp.host.audio-out"}],
                    "wires":[{"from":"p.out","to":"out.out.1-2"}]})";
    };
    REQUIRE(rig.set(doc(four, true)).ok);
    rig.run();
    CHECK(rig.outs[0][0] == 1.0f);
    CHECK(rig.outs[0][5] == 2.0f);  // a buffer shorter than the block wraps inside it
    CHECK(rig.outs[1][0] == 0.0f);  // a mono buffer leaves the right channel silent
    REQUIRE(rig.set(doc(four, false)).ok);
    rig.run();
    CHECK(rig.outs[0][0] == 1.0f);  // (64 is a multiple of 4, so it is back at the start)
    CHECK(rig.outs[0][3] == 4.0f);
    CHECK(rig.outs[0][4] == 0.0f);  // one-shot: silent after the end
    REQUIRE(rig.set(doc(four, true)).ok);
    rig.run();
    CHECK(rig.outs[0][0] == 1.0f);  // looping turned back on at the end starts again
    REQUIRE(rig.set(doc(two, true)).ok);
    rig.run();
    CHECK(rig.outs[0][0] == 10.0f);  // a different buffer starts from its first frame
    CHECK(rig.outs[0][1] == 11.0f);
}

TEST_CASE("a buffer swapped in mid-play starts from its first frame", "[graph][core]") {
    CoreRig rig;
    const auto three = rig.buffer(1, 3, [](std::uint32_t, std::uint32_t i) { return static_cast<float>(i + 1); });
    const auto other = rig.buffer(1, 7, [](std::uint32_t, std::uint32_t i) { return static_cast<float>(20 + i); });
    auto doc = [&](ResourceTable::Id b, bool loop) {
        return R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"loop":)" + std::string(loop ? "true" : "false") +
               R"(},"resources":{"sample":)" + CoreRig::res(b) + R"(}},{"id":"out","type":"rp.host.audio-out"}],
                    "wires":[{"from":"p.out","to":"out.out.1-2"}]})";
    };
    REQUIRE(rig.set(doc(three, true)).ok);
    rig.run();  // 64 frames of a 3-frame loop: it stops one frame in
    REQUIRE(rig.set(doc(other, true)).ok);
    rig.run();  // (and 64 frames of a 7-frame loop stop one frame in too)
    CHECK(rig.outs[0][0] == 20.0f);
    REQUIRE(rig.set(doc(three, false)).ok);  // a one-shot too: no loop wrap to hide a missed restart
    rig.run();
    CHECK(rig.outs[0][0] == 1.0f);
}

TEST_CASE("the recorder starts at the frame Record turns on and stops at the buffer's end", "[graph][core]") {
    CoreRig rig;
    const auto ramp = rig.buffer(1, 64, [](std::uint32_t, std::uint32_t i) { return static_cast<float>(i); });
    const auto target = rig.resources.createAudioBuffer(1, 40, 48000.0);
    const auto other = rig.resources.createAudioBuffer(1, 8, 48000.0);
    auto doc = [&](ResourceTable::Id t) {
        return R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":1,"layout":"mono"},
                             "resources":{"sample":)" + CoreRig::res(ramp) + R"(}},
                            {"id":"rec","type":"rp.core.buffer-recorder","settings":{"channels":1},"params":{"record":false},
                             "resources":{"target":)" + CoreRig::res(t) + R"(}}],
                   "wires":[{"from":"p.out","to":"rec.in"}]})";
    };
    auto sample = [&](ResourceTable::Id id, std::uint32_t i) {
        return static_cast<const rp_audio_buffer*>(rig.resources.get(id)->payload)->data[0][i];
    };
    REQUIRE(rig.set(doc(target)).ok);
    rig.run();
    CHECK(sample(target, 0) == 0.0f);  // Record off: nothing written
    REQUIRE(rig.host->setParam("rec", "record", 1, 32));
    rig.run();
    CHECK(sample(target, 0) == 32.0f);  // the input at the frame Record turned on
    CHECK(sample(target, 31) == 63.0f);
    rig.run();
    CHECK(sample(target, 32) == 0.0f);  // the next block carried on from where it was
    CHECK(sample(target, 39) == 7.0f);  // and stopped at the buffer's last frame
    REQUIRE(rig.host->setParam("rec", "record", 0, 0));
    rig.run();
    REQUIRE(rig.host->setParam("rec", "record", 1, 0));
    rig.run();
    CHECK(sample(target, 0) == 0.0f);  // turned on again: back to the first frame, overwriting
    CHECK(sample(target, 39) == 39.0f);
    REQUIRE(rig.set(doc(other)).ok);
    rig.run();
    CHECK(sample(other, 0) == 0.0f);  // a new target starts from its first frame
    CHECK(sample(other, 7) == 7.0f);
}

TEST_CASE("a writer is bound to its buffer, never wired", "[graph][core]") {
    GraphRig rig;
    const auto buf = rig.resources.createAudioBuffer(2, 64, 48000.0);
    CHECK(GraphRig::codes(rig.set(R"({"nodes":[{"id":"rec","type":"rp.core.buffer-recorder","resources":{"target":"res:)" +
                                  std::to_string(buf) + R"("}},{"id":"rec2","type":"rp.core.buffer-recorder"}],
                                      "wires":[{"from":"rec.buffer","to":"rec2.target"}]})")) == "resource-write-wired");
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
