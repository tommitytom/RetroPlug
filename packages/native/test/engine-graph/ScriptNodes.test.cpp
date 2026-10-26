// The script-node runtime (docs/modular-graph-phase-b3a-design.md section 2): TS nodes behind the same C ABI as
// native ones, one runtime per engine. Toy nodes, written here as JS source on the authoring layer the test bundle
// exposes (ScriptRig.hpp), are planned and run beside the graph tests' C++ sources and sinks: what reaches them
// through Block, what they send, the control-side context laying their ports out from settings, a node kept across
// a reshape or swapped or rebuilt, where their instances are made and released, a throw, the block path's
// allocations, and a running audio thread under all of it. The embedded production bundle and an Engine built
// around a toy bundle close it.
//
// Run via `pnpm test:plugin engine-graph`.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "../graph/CountingAllocator.hpp"
#include "ScriptRig.hpp"
#include "host/engine/Engine.hpp"

using rp::host::ScriptHost;
using rp::host::test::Feed;
using rp::host::test::Probe;
using rp::host::test::ScriptRig;
namespace gt = rp::graph::test;

namespace {

std::string doc(const std::string& nodes, const std::string& wires) {
    return R"({"nodes":[)" + nodes + R"(],"wires":[)" + wires + "]}";
}
std::string node(const std::string& id, const std::string& type, const std::string& settings = "", const std::string& params = "") {
    return R"({"id":")" + id + R"(","type":")" + type + "\"" + (settings.empty() ? "" : R"(,"settings":)" + settings) +
           (params.empty() ? "" : R"(,"params":)" + params) + "}";
}
std::string wire(const std::string& from, const std::string& to) {
    return R"({"from":")" + from + R"(","to":")" + to + "\"}";
}

std::vector<std::uint8_t> bytes(std::initializer_list<int> b) {
    std::vector<std::uint8_t> v;
    for (int x : b) v.push_back(static_cast<std::uint8_t>(x));
    return v;
}

std::vector<std::uint8_t> pattern(std::size_t n, int seed) {
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<std::uint8_t>((i * 7 + seed) & 0xff);
    return v;
}

// Every byte of `s` part of a whole UTF-8 sequence: none cut short.
bool wholeUtf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const auto lead = static_cast<unsigned char>(s[i]);
        const std::size_t n = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xe ? 3 : (lead >> 3) == 0x1e ? 4 : 0;
        if (n == 0 || i + n > s.size()) return false;
        for (std::size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xc0) != 0x80) return false;
        i += n;
    }
    return true;
}

// Events and bytes straight through, counting the sends its 4-message output refuses.
const char* const kPass = R"JS(
rp.register([rp.defineNode({
  id: "rp.toy.pass",
  ports: { in: rp.events.in(), bin: rp.bytes.in(), out: rp.events.out({ messages: 4 }), bout: rp.bytes.out(),
           refused: rp.control.out() },
  create: () => {
    let refused = 0;
    return {
      process(b) {
        const ms = b.messages("in");
        for (let i = 0; i < ms.length; ++i) if (!b.send("out", ms[i].frame, ms[i].data, ms[i].flags)) ++refused;
        const bs = b.messages("bin");
        for (let i = 0; i < bs.length; ++i) b.send("bout", bs[i].frame, bs[i].data, bs[i].flags);
        b.emit("refused", 0, refused);
      },
    };
  },
})]);
)JS";

// Audio scaled, through the views of its own copies, by a param and a control input, whose changes it emits.
const char* const kGain = R"JS(
rp.register([rp.defineNode({
  id: "rp.toy.gain",
  ports: { in: rp.audio.in(), out: rp.audio.out(rp.channels.match("in")), level: rp.control.in(), changes: rp.control.out() },
  params: { gain: rp.param.float({ name: "Gain", min: 0, max: 4, default: 1 }) },
  create: () => ({
    process(b) {
      const level = b.control("level");
      const g = b.control("gain").start * level.start;
      const ins = b.audio("in"), outs = b.audio("out");
      if (ins.length !== 2 || outs.length !== 2 || ins[0].length !== b.frames) throw new Error("views are the block's");
      for (let c = 0; c < outs.length; ++c) {
        const i = ins[c], o = outs[c];
        for (let f = 0; f < b.frames; ++f) o[f] = i[f] * g * (c + 1);
      }
      for (let k = 0; k < level.changes.length; ++k) b.emit("changes", level.changes[k].frame, level.changes[k].value);
    },
  }),
})]);
)JS";

// One output per `outs` its settings name; refuses more than 4 with zod's reason.
const char* const kFan = R"JS(
rp.register([rp.defineNode({
  id: "rp.toy.fan",
  settings: rp.z.object({ outs: rp.z.number().int().min(1).max(4).default(2) }),
  ports: (s) => { const p = { in: rp.events.in() }; for (let k = 1; k <= s.outs; ++k) p["out" + k] = rp.events.out(); return p; },
  create: ({ settings }) => ({
    process(b) {
      const ms = b.messages("in");
      for (let k = 1; k <= settings.outs; ++k) for (let i = 0; i < ms.length; ++i) b.send("out" + k, ms[i].frame, ms[i].data);
    },
  }),
})]);
)JS";

// A count of its blocks on every output, [count, k] on out<k>, its outputs listed last first, so a port's position
// moves when their number does. `counter` reshapes; `plain` does not, and also sends its settings' tag.
const char* const kCounters = R"JS(
const outsFor = (s) => { const p = {}; for (let k = s.outs; k >= 1; --k) p["out" + k] = rp.events.out(); return p; };
const counted = (reshapes) => rp.defineNode({
  id: reshapes ? "rp.toy.counter" : "rp.toy.plain",
  ...(reshapes ? { reshapes: true } : {}),
  settings: rp.z.object({ outs: rp.z.number().int().min(1).max(4).default(2), tag: rp.z.number().default(0) }),
  ports: outsFor,
  create: ({ settings }) => {
    let outs = settings.outs, tag = settings.tag, count = 0;
    const name = (reshapes ? "counter " : "plain ") + tag;
    const token = __token(name);
    __probe("create " + name);
    return {
      process: (b) => {
        ++count;
        for (let k = 1; k <= outs; ++k) b.send("out" + k, 0, [count & 0x7f, k, tag & 0x7f]);
        void token;
      },
      onSettings: (s) => {
        outs = s.outs;
        tag = s.tag;
        __probe("onSettings " + (reshapes ? "counter " : "plain ") + s.outs + "/" + s.tag);
      },
    };
  },
});
rp.register([counted(true), counted(false)]);
)JS";

// A throw on every second block; one that latches on its first; one that answers "fault".
const char* const kThrowers = R"JS(
const thrower = (id, extra, at) => rp.defineNode({
  id, ...extra,
  ports: { out: rp.events.out() },
  create: () => {
    let n = 0;
    return {
      process: (b) => {
        ++n;
        b.send("out", 0, [n]);
        if (at === "fault") return n === 2 ? "fault" : undefined;
        if (n % at === 0) throw new Error("boom " + n);
      },
    };
  },
});
rp.register([thrower("rp.toy.thrower", {}, 2), thrower("rp.toy.latch", { faultOnThrow: true }, 2),
             thrower("rp.toy.quitter", {}, "fault")]);
)JS";

// The steady block path: indexed loops, every port read and written, nothing a block makes.
const char* const kSteady = R"JS(
rp.register([rp.defineNode({
  id: "rp.toy.steady",
  ports: { in: rp.events.in(), audioIn: rp.audio.in(), level: rp.control.in(),
           out: rp.events.out(), audioOut: rp.audio.out(rp.channels.match("audioIn")), peak: rp.control.out() },
  params: { gain: rp.param.float({ name: "Gain", min: 0, max: 4, default: 1 }) },
  create: () => ({
    process(b) {
      const ms = b.messages("in");
      for (let i = 0; i < ms.length; ++i) b.send("out", ms[i].frame, ms[i].data, ms[i].flags);
      const g = b.control("gain").start * b.control("level").start;
      const ins = b.audio("audioIn"), outs = b.audio("audioOut");
      for (let c = 0; c < outs.length; ++c) { const i = ins[c], o = outs[c]; for (let f = 0; f < b.frames; ++f) o[f] = i[f] * g; }
      b.emit("peak", 0, g);
      b.transport.ppq;
    },
  }),
}), rp.defineNode({
  id: "rp.toy.idiomatic",
  ports: { in: rp.events.in(), out: rp.events.out() },
  create: () => ({
    process(b) {
      for (const m of b.messages("in")) b.send("out", m.frame, [m.data[0], m.data[1], m.data[2]]);
    },
  }),
})]);
)JS";

// What a node may do with the audio it is handed, none of which may reach anything but its own copy: keep the
// Float32Array[] across blocks and plans, transfer a view's buffer away, write an input in place, swap the array's
// element for one of its own, or keep a view in module state past its instance's release.
const char* const kAudioHazards = R"JS(
globalThis.stashed = null;
rp.register([
rp.defineNode({
  id: "rp.toy.cacher",   // the array from its first block, every block after
  ports: { in: rp.audio.in(), out: rp.audio.out(rp.channels.match("in")) },
  create: () => {
    let outs = null;
    return {
      process(b) {
        if (!outs) outs = b.audio("out");
        const o = outs[0];
        for (let f = 0; f < b.frames; ++f) o[f] = 0.25;
      },
    };
  },
}),
rp.defineNode({
  id: "rp.toy.transfer",  // length 0: its output's buffer transferred away once, at its length; n: to n bytes, every block
  settings: rp.z.object({ length: rp.z.number().int().min(0).default(0) }),
  ports: { out: rp.audio.out(rp.channels.stereo()) },
  create: ({ settings }) => {
    let taken = null;
    return {
      process(b) {
        const o = b.audio("out")[0];
        if (settings.length > 0) return void o.buffer.transfer(settings.length);
        if (!taken) return void new Float32Array((taken = o.buffer.transfer())).fill(1);
        for (let f = 0; f < b.frames; ++f) o[f] = 0.25;
        new Float32Array(taken)[0] = 1;  // what it took is its own memory now
      },
    };
  },
}),
rp.defineNode({
  id: "rp.toy.scribbler",  // writes its input in place, and sends on what it reads back
  ports: { in: rp.audio.in(), out: rp.audio.out(rp.channels.stereo()) },
  create: () => ({
    process(b) {
      const ins = b.audio("in"), o = b.audio("out")[0];
      for (let c = 0; c < ins.length; ++c) for (let f = 0; f < b.frames; ++f) ins[c][f] = 0.5;
      for (let f = 0; f < b.frames; ++f) o[f] = ins.length ? ins[0][f] : -1;
    },
  }),
}),
rp.defineNode({
  id: "rp.toy.mutator",   // swaps the array's element for one of its own, and goes on writing the one it was handed
  ports: { in: rp.audio.in(), out: rp.audio.out(rp.channels.match("in")) },
  create: () => {
    let kept = null;
    return {
      process(b) {
        const outs = b.audio("out");
        if (!kept) { kept = outs[0]; outs[0] = new Float32Array(b.frames); }
        for (let f = 0; f < b.frames; ++f) kept[f] = 0.75;
      },
    };
  },
}),
rp.defineNode({
  id: "rp.toy.stasher",   // keeps a view of its output in module state
  ports: { out: rp.audio.out(rp.channels.stereo()) },
  create: () => ({ process(b) { globalThis.stashed = b.audio("out")[0]; } }),
}),
rp.defineNode({
  id: "rp.toy.user",      // writes through the module-level stash, whoever made it
  ports: { out: rp.events.out() },
  create: () => ({ process(b) { const s = globalThis.stashed; if (s) for (let i = 0; i < s.length; ++i) s[i] = 9; } }),
}),
]);
)JS";

// One node twice, the first faulting on a throw: its onSettings refuses settings that say fail "settings", and its
// portsChanged throws once settings that say fail "ports" are its.
const char* const kBrittle = R"JS(
const brittle = (id, extra) => rp.defineNode({
  id, ...extra, reshapes: true,
  settings: rp.z.object({ fail: rp.z.enum(["none", "settings", "ports"]).default("none"),
                          outs: rp.z.number().int().min(1).max(2).default(1) }),
  ports: (s) => (s.outs === 1 ? { out1: rp.events.out() } : { out1: rp.events.out(), out2: rp.events.out() }),
  create: ({ settings }) => {
    let fail = settings.fail;
    return {
      process(b) { b.send("out1", 0, [1]); },
      onSettings(s) { if (s.fail === "settings") throw new Error("cannot take these"); fail = s.fail; },
      portsChanged() { if (fail === "ports") throw new Error("cannot rewire"); },
    };
  },
});
rp.register([brittle("rp.toy.brittle", { faultOnThrow: true }), brittle("rp.toy.sturdy", {})]);
)JS";

// A message far longer than any buffer it is kept in, of two- and three-byte characters after `pad` ASCII letters: its
// ports refuse settings that say refuse, and its process throws it.
const char* const kWordy = R"JS(
const wordy = (pad) => "x".repeat(pad) + "\u00e9\u20ac".repeat(200);
rp.register([rp.defineNode({
  id: "rp.toy.wordy",
  settings: rp.z.object({ pad: rp.z.number().int().min(0).max(2).default(0), refuse: rp.z.boolean().default(false) }),
  ports: (s) => { if (s.refuse) throw new Error(wordy(s.pad)); return { out: rp.events.out() }; },
  create: ({ settings }) => ({ process() { throw new Error(wordy(settings.pad)); } }),
})]);
)JS";

// Nothing to do: for tests that need instances and nothing else.
const char* const kIdle = R"JS(
rp.register([rp.defineNode({ id: "rp.toy.idle", ports: { out: rp.events.out() }, create: () => ({ process() {} }) })]);
)JS";

}  // namespace

TEST_CASE("a script node's messages arrive in order with their flags, and a send past capacity is refused and counted",
          "[script]") {
    ScriptRig rig(kPass);
    const auto r = rig.set(doc(node("feed", "rp.test.feed") + "," + node("pass", "rp.toy.pass") + "," + node("probe", "rp.test.probe"),
                               wire("feed.midi", "pass.in") + "," + wire("feed.bytes", "pass.bin") + "," +
                                   wire("pass.out", "probe.events") + "," + wire("pass.bout", "probe.bytes") + "," +
                                   wire("pass.refused", "probe.control")));
    INFO(ScriptRig::codes(r));
    REQUIRE(r.ok);
    auto& feed = rig.node<Feed>("feed");
    const std::vector<rp::host::test::Message> midi{{0, bytes({0x90, 60, 100})}, {5, bytes({0x80, 60, 0})},
                                                    {5, bytes({0x90, 62, 1})},   {9, bytes({0xb0, 1, 2})},
                                                    {12, bytes({0x90, 64, 3})},  {20, bytes({0xf8})}};
    feed.next[Feed::Midi] = midi;
    feed.next[Feed::Bytes] = {{3, bytes({1, 2, 3}), 0}, {7, bytes({4}), RP_MESSAGE_FLUSH}, {7, {}, 0}};
    rig.run();
    const auto& probe = rig.node<Probe>("probe");
    REQUIRE(probe.events.size() == 4);  // the first four, in order: the output holds four
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(probe.events[i].frame == midi[i].frame);
        CHECK(probe.events[i].data == midi[i].data);
    }
    REQUIRE(probe.bytes.size() == 3);
    CHECK(probe.bytes[0].data == bytes({1, 2, 3}));
    CHECK(probe.bytes[1].flags == RP_MESSAGE_FLUSH);
    CHECK(probe.bytes[1].frame == 7);
    CHECK(probe.bytes[2].data.empty());
    CHECK(probe.controlAtZero == 2.0);                // the two sends `out` refused
    CHECK(rig.host->droppedMessages("pass") == 2);   // and the queue counted them
    // A shorter block reuses the arrays: one message, the rest gone.
    feed.next[Feed::Midi] = {{30, bytes({0x90, 70, 7})}};
    rig.run();
    REQUIRE(probe.events.size() == 1);
    CHECK(probe.events[0].frame == 30);
    CHECK(probe.events[0].data == bytes({0x90, 70, 7}));
    CHECK(probe.bytes.empty());
    CHECK(rig.scripts.throwCount() == 0);
}

TEST_CASE("a script node reads a param and a control stream, emits control, and writes audio through views of its own "
          "copies",
          "[script]") {
    ScriptRig rig(kGain);
    REQUIRE(rig.set(doc(node("src", "rp.test.source") + "," + node("k", "rp.core.constant", "", R"({"value":0.5})") + "," +
                            node("g", "rp.toy.gain", "", R"({"gain":3})") + "," + node("sink", "rp.test.sink"),
                        wire("src.out", "g.in") + "," + wire("k.out", "g.level") + "," + wire("g.out", "sink.in") + "," +
                            wire("g.changes", "sink.control")))
                .ok);
    rig.run();
    const auto& sink = rig.node<gt::Sink>("sink");
    // The constant's first value is a change at frame 0 (a node cannot set a control output's start), so the level
    // starts at 0 and the change is passed on.
    CHECK(sink.first[0] == 0.0f);
    CHECK(sink.controlChanges == 1);
    CHECK(sink.controlEnd == 0.5);
    rig.run();
    CHECK(sink.first[0] == 1.5f);  // the source's 1.0, times the gain param's 3, times the level's 0.5
    CHECK(sink.last[0] == 1.5f);
    CHECK(sink.first[1] == 3.0f);  // channel 2 doubled
    CHECK(sink.controlChanges == 0);
    REQUIRE(rig.host->setParam("k", "value", 0.25, 10));
    REQUIRE(rig.host->setParam("g", "gain", 2));
    rig.run();
    CHECK(sink.first[0] == 1.5f);     // both values are changes this block (the gain's at frame 0), so the starts hold
    CHECK(sink.controlChanges == 1);  // the level's change at frame 10, passed on
    CHECK(sink.controlEnd == 0.25);
    rig.run(32);  // a shorter block: new views, as long as it
    CHECK(sink.first[0] == 0.5f);     // the gain's 2, the level's 0.25
    CHECK(sink.last[0] == 0.5f);
    CHECK(sink.controlChanges == 0);
    CHECK(rig.scripts.throwCount() == 0);
}

TEST_CASE("the control side lays a script node's ports out from its settings, and refuses what its definition refuses",
          "[script]") {
    ScriptRig rig(kFan);
    const auto fan = [](const std::string& settings, const std::string& wires) {
        return doc(node("feed", "rp.test.feed") + "," + node("fan", "rp.toy.fan", settings) + "," + node("probe", "rp.test.probe"),
                   wire("feed.midi", "fan.in") + (wires.empty() ? "" : "," + wires));
    };
    const auto three = rig.set(fan(R"({"outs":3})", wire("fan.out3", "probe.events")));
    INFO(ScriptRig::codes(three));
    REQUIRE(three.ok);
    CHECK(three.layouts.at("fan").find(R"("id":"out3")") != std::string::npos);
    rig.node<Feed>("feed").next[Feed::Midi] = {{4, bytes({0x90, 1, 2})}};
    rig.run();
    REQUIRE(rig.node<Probe>("probe").events.size() == 1);
    CHECK(rig.node<Probe>("probe").events[0].frame == 4);

    CHECK(ScriptRig::codes(rig.set(fan(R"({"outs":3})", wire("fan.out4", "probe.events")))) == "unknown-port");
    const auto refused = rig.set(fan(R"({"outs":9})", ""));
    REQUIRE(ScriptRig::codes(refused) == "settings-invalid");
    CHECK(refused.errors[0].message == R"(node type "rp.toy.fan": settings: outs: Too big: expected number to be <=4)");
    CHECK(ScriptRig::codes(rig.set(fan(R"({"outs":"two"})", ""))) == "settings-invalid");
}

TEST_CASE("the test bundle's own node, defined as a production node is, plans and runs", "[script]") {
    ScriptRig rig;
    REQUIRE(rig.registry.find("rp.test.script-echo"));
    REQUIRE(rig.set(doc(node("feed", "rp.test.feed") + "," + node("echo", "rp.test.script-echo", R"({"copies":2})", R"({"transpose":2})") +
                            "," + node("probe", "rp.test.probe"),
                        wire("feed.midi", "echo.in") + "," + wire("echo.out", "probe.events")))
                .ok);
    rig.node<Feed>("feed").next[Feed::Midi] = {{8, bytes({0x90, 60, 100})}};
    rig.run();
    const auto& probe = rig.node<Probe>("probe");
    REQUIRE(probe.events.size() == 2);
    CHECK(probe.events[0].data == bytes({0x90, 62, 100}));
    CHECK(probe.events[1].frame == 8);
    CHECK(ScriptRig::codes(rig.set(doc(node("echo", "rp.test.script-echo", R"({"copies":0})"), ""))) == "settings-invalid");
}

TEST_CASE("a reshaping script node is kept across a change of its ports: its state goes on, onSettings runs once at the "
          "swap, and its ports rebind by id",
          "[script]") {
    ScriptRig rig(kCounters);
    const auto counter = [](int outs, const std::string& wires) {
        return doc(node("c", "rp.toy.counter", R"({"outs":)" + std::to_string(outs) + "}") + "," + node("p2", "rp.test.probe") +
                       "," + node("p3", "rp.test.probe"),
                   wires);
    };
    REQUIRE(rig.set(counter(2, wire("c.out2", "p2.events"))).ok);
    rp_node* const kept = rig.host->node("c");
    rig.run(64, 3);
    REQUIRE(rig.node<Probe>("p2").events.size() == 1);
    CHECK(rig.node<Probe>("p2").events[0].data == bytes({3, 2, 0}));
    // out2 is the first output now and the second next: it is rebound by id.
    const auto r = rig.set(counter(3, wire("c.out2", "p2.events") + "," + wire("c.out3", "p3.events")));
    INFO(ScriptRig::codes(r));
    REQUIRE(r.ok);
    CHECK(rig.host->node("c") == kept);
    CHECK(r.layouts.at("c").find(R"("id":"out3")") < r.layouts.at("c").find(R"("id":"out2")"));
    rig.run();
    CHECK(rig.node<Probe>("p2").events[0].data == bytes({4, 2, 0}));
    CHECK(rig.node<Probe>("p3").events[0].data == bytes({4, 3, 0}));
    CHECK(rig.log.count("onSettings counter 3/0") == 1);
    CHECK(rig.log.threadsOf("onSettings counter 3/0")[0] == std::this_thread::get_id());  // quiescent: this is the block thread
    // A change that leaves the layout as it is: a swap, so the ports stay as they are and only the settings move.
    const auto tagged = rig.set(doc(node("c", "rp.toy.counter", R"({"outs":3,"tag":7})") + "," + node("p2", "rp.test.probe") +
                                        "," + node("p3", "rp.test.probe"),
                                    wire("c.out2", "p2.events") + "," + wire("c.out3", "p3.events")));
    REQUIRE(tagged.ok);
    CHECK(rig.host->node("c") == kept);
    rig.run();
    CHECK(rig.node<Probe>("p2").events[0].data == bytes({5, 2, 7}));
    CHECK(rig.log.count("onSettings counter 3/7") == 1);
    CHECK(rig.log.count("create counter 0") == 1);
    CHECK(rig.scripts.liveInstances() == 1);
}

TEST_CASE("a script node that does not reshape is swapped while its layout holds and rebuilt when it moves", "[script]") {
    ScriptRig rig(kCounters);
    const auto plain = [](int outs, int tag) {
        return doc(node("n", "rp.toy.plain", R"({"outs":)" + std::to_string(outs) + R"(,"tag":)" + std::to_string(tag) + "}") +
                       "," + node("p", "rp.test.probe"),
                   wire("n.out1", "p.events"));
    };
    REQUIRE(rig.set(plain(1, 0)).ok);
    rp_node* const first = rig.host->node("n");
    rig.run(64, 2);
    REQUIRE(rig.set(plain(1, 5)).ok);  // the same layout: a swap
    CHECK(rig.host->node("n") == first);
    rig.run();
    CHECK(rig.node<Probe>("p").events[0].data == bytes({3, 1, 5}));
    CHECK(rig.log.count("onSettings plain 1/5") == 1);
    REQUIRE(rig.set(plain(2, 5)).ok);  // a port more: rebuilt, so the count starts again
    CHECK(rig.host->node("n") != first);
    rig.run();
    CHECK(rig.node<Probe>("p").events[0].data == bytes({1, 1, 5}));
    CHECK(rig.log.count("create plain 5") == 1);
    CHECK(rig.log.count("released plain 0") == 1);
    CHECK(rig.scripts.liveInstances() == 1);
    CHECK(rig.scripts.throwCount() == 0);
}

TEST_CASE("an instance is created when its plan is adopted and released when its plan is reclaimed, on the block thread",
          "[script]") {
    ScriptRig rig(kCounters);
    const auto plain = [](int tag) {
        return doc(node("n", "rp.toy.plain", R"({"outs":1,"tag":)" + std::to_string(tag) + "}"), "");
    };
    const std::string empty = doc("", "");
    // Quiescent: the control thread runs the blocks, so it creates and releases.
    REQUIRE(rig.set(plain(1)).ok);
    CHECK(rig.log.count("create plain 1") == 1);  // adopted inline, so created already
    REQUIRE(rig.set(empty).ok);
    CHECK(rig.log.threadsOf("released plain 1") == std::vector<std::thread::id>{std::this_thread::get_id()});

    // An audio thread running the blocks: it does both, and the control thread never enters the instances' runtime.
    rig.setAudioThreadOwns(true);
    std::atomic<bool> running{true};
    std::thread::id audioId;
    std::thread audio([&] {
        audioId = std::this_thread::get_id();
        while (running.load()) {
            rig.block(64);
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });
    REQUIRE(rig.set(plain(2)).ok);
    REQUIRE(rig.log.waitFor("create plain 2"));
    REQUIRE(rig.set(doc(node("n", "rp.toy.plain", R"({"outs":1,"tag":3})"), "")).ok);  // a swap
    REQUIRE(rig.log.waitFor("onSettings plain 1/3"));
    REQUIRE(rig.set(doc(node("c", "rp.toy.counter", R"({"outs":1})"), "")).ok);  // plain goes, counter comes
    REQUIRE(rig.set(doc(node("c", "rp.toy.counter", R"({"outs":2})"), "")).ok);  // a reshape
    REQUIRE(rig.log.waitFor("onSettings counter 2/0"));
    REQUIRE(rig.set(empty).ok);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((rig.log.count("released plain 2") < 1 || rig.log.count("released counter 0") < 1) &&
           std::chrono::steady_clock::now() < deadline) {
        rig.reclaim();  // the plans the audio thread handed back: their shims queue the releases it then performs
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    running = false;
    audio.join();
    rig.setAudioThreadOwns(false);
    for (const char* tag : {"create plain 2", "onSettings plain 1/3", "create counter 0", "onSettings counter 2/0",
                            "released plain 2", "released counter 0"}) {
        INFO(tag);
        CHECK(rig.log.threadsOf(tag) == std::vector<std::thread::id>{audioId});
    }
    CHECK(rig.scripts.liveInstances() == 0);
}

TEST_CASE("a throw in a script node is counted and it runs on; one that latches, or answers fault, is bypassed", "[script]") {
    ScriptRig rig(kThrowers);
    REQUIRE(rig.set(doc(node("t", "rp.toy.thrower") + "," + node("l", "rp.toy.latch") + "," + node("q", "rp.toy.quitter") + "," +
                            node("pt", "rp.test.probe") + "," + node("pl", "rp.test.probe") + "," + node("pq", "rp.test.probe"),
                        wire("t.out", "pt.events") + "," + wire("l.out", "pl.events") + "," + wire("q.out", "pq.events")))
                .ok);
    for (int n = 1; n <= 4; ++n) {
        rig.run();
        INFO("block " << n);
        // What the thrower sent before its throw stands, and it runs again the block after.
        REQUIRE(rig.node<Probe>("pt").events.size() == 1);
        CHECK(rig.node<Probe>("pt").events[0].data == bytes({n}));
        // The latch and the quitter fault on their second block: bypassed, so silent, from then on.
        CHECK(rig.node<Probe>("pl").events.size() == (n < 2 ? 1u : 0u));
        CHECK(rig.node<Probe>("pq").events.size() == (n < 2 ? 1u : 0u));
    }
    CHECK(rig.scripts.throwCount() == 3);  // the thrower's two, the latch's one; "fault" is no throw
    CHECK_FALSE(rig.host->isFaulted("t"));
    CHECK(rig.host->isFaulted("l"));
    CHECK(rig.host->isFaulted("q"));
    CHECK(rig.host->faultCount() == 2);
    rig.reclaim();  // logs the first throw
    CHECK(rig.scripts.firstThrow().find("rp.toy.thrower") != std::string::npos);  // first in the plan
    CHECK(rig.scripts.firstThrow().find("boom 2") != std::string::npos);
}

TEST_CASE("the script block path allocates nothing native once warm, and what it allocates in JS is measured", "[script][noalloc]") {
    ScriptRig rig(kSteady);
    const std::string nodes = node("src", "rp.test.source") + "," + node("k", "rp.core.constant", "", R"({"value":0.5})") + "," +
                              node("s", "rp.toy.steady", "", R"({"gain":2})") + "," + node("i", "rp.toy.idiomatic") + "," +
                              node("sink", "rp.test.sink") + "," + node("isink", "rp.test.sink");
    const std::string wires = wire("src.events", "s.in") + "," + wire("src.out", "s.audioIn") + "," + wire("k.out", "s.level") + "," +
                              wire("s.out", "sink.events") + "," + wire("s.audioOut", "sink.in") + "," + wire("s.peak", "sink.control") +
                              "," + wire("src.events", "i.in") + "," + wire("i.out", "isink.events");
    REQUIRE(rig.set(doc(nodes, wires)).ok);
    rig.run(64, 8);  // warm: every array and view made
    REQUIRE(rig.node<gt::Sink>("sink").eventCount == 1);
    REQUIRE(rig.node<gt::Sink>("isink").eventCount == 1);
    CHECK(rig.node<gt::Sink>("sink").first[0] == 1.0f);

    constexpr int kBlocks = 64;
    const auto before = rig.scripts.audioAllocations();
    rp::test::beginCountingAllocations();
    rig.run(64, kBlocks);
    const auto native = rp::test::endCountingAllocations();
    const auto after = rig.scripts.audioAllocations();
    CHECK(native.allocations == 0);
    CHECK(native.frees == 0);
    const double jsAllocs = static_cast<double>(after.allocs + after.reallocs - before.allocs - before.reallocs) / kBlocks;
    const double jsBytes = static_cast<double>(after.bytes - before.bytes) / kBlocks;
    std::printf("[script] steady + idiomatic nodes: %.2f JS allocations (%.1f bytes) per 64-frame block\n", jsAllocs, jsBytes);

    // The same, per node: the steady one alone, then the idiomatic one alone.
    for (const char* only : {"s", "i"}) {
        ScriptRig one(kSteady);
        const bool steady = std::string(only) == "s";
        REQUIRE(one.set(doc(node("src", "rp.test.source") + "," + node("k", "rp.core.constant", "", R"({"value":0.5})") + "," +
                                (steady ? node("s", "rp.toy.steady") : node("i", "rp.toy.idiomatic")) + "," + node("sink", "rp.test.sink"),
                            steady ? wire("src.events", "s.in") + "," + wire("src.out", "s.audioIn") + "," + wire("k.out", "s.level") + "," +
                                         wire("s.out", "sink.events") + "," + wire("s.audioOut", "sink.in")
                                   : wire("src.events", "i.in") + "," + wire("i.out", "sink.events")))
                    .ok);
        one.run(64, 8);
        const auto a0 = one.scripts.audioAllocations();
        one.run(64, kBlocks);
        const auto a1 = one.scripts.audioAllocations();
        const double perBlock = static_cast<double>(a1.allocs + a1.reallocs - a0.allocs - a0.reallocs) / kBlocks;
        std::printf("[script] %s node: %.2f JS allocations (%.1f bytes) per block\n", steady ? "steady" : "idiomatic", perBlock,
                    static_cast<double>(a1.bytes - a0.bytes) / kBlocks);
        if (steady) CHECK(perBlock == 0.0);
    }

    // A plan adopted inside the measured blocks, keeping both script nodes and telling the steady one its outputs
    // changed, with a param set: nothing native either.
    rig.setAudioThreadOwns(true);
    REQUIRE(rig.set(doc(nodes + "," + node("extra", "rp.test.sink"), wires + "," + wire("s.out", "extra.events"))).ok);
    REQUIRE(rig.host->setParam("s", "gain", 3));
    rp::test::beginCountingAllocations();
    rig.run(64, 8);
    const auto swap = rp::test::endCountingAllocations();
    CHECK(swap.allocations == 0);
    CHECK(swap.frees == 0);
    rig.setAudioThreadOwns(false);
    CHECK(rig.node<gt::Sink>("extra").eventCount == 1);
    CHECK(rig.scripts.throwCount() == 0);
}

TEST_CASE("a script node that keeps its audio array across blocks writes its own copy, whatever plan it runs in",
          "[script][audio-copy][cache]") {
    ScriptRig rig(kAudioHazards);
    const std::string base = node("src", "rp.test.source") + "," + node("c", "rp.toy.cacher") + "," + node("sink", "rp.test.sink");
    const std::string wires = wire("src.out", "c.in") + "," + wire("c.out", "sink.in");
    REQUIRE(rig.set(doc(base, wires)).ok);
    rig.run(64, 2);
    const auto& sink = rig.node<gt::Sink>("sink");
    CHECK(sink.first[0] == 0.25f);
    rp_node* const kept = rig.host->node("c");
    // An unrelated node: c is kept and its connected outputs do not move, so nothing tells it of the swap, which frees
    // the plan its first blocks ran in.
    REQUIRE(rig.set(doc(base + "," + node("other", "rp.test.sink"), wires)).ok);
    REQUIRE(rig.host->node("c") == kept);
    rig.run(64, 2);
    CHECK(sink.first[0] == 0.25f);
    CHECK(sink.last[0] == 0.25f);
    CHECK(sink.first[1] == 0.0f);  // a channel it never writes holds silence, not what the plan's buffer last held
    // A re-activation re-plans every buffer too, here at a larger block than the copies were made for.
    std::string why;
    REQUIRE(rig.host->reactivate(44100.0, 512, &why));
    REQUIRE(rig.host->node("c") == kept);
    rig.run(64, 2);
    CHECK(sink.first[0] == 0.25f);
    rig.run(200);  // a longer block: its views are made again, in the array it keeps
    CHECK(sink.last[0] == 0.25f);
    CHECK(rig.scripts.throwCount() == 0);
}

TEST_CASE("a script node that transfers its output's buffer away takes only its own copy: that block is silent, and the next "
          "block's copy is made again",
          "[script][audio-copy][transfer]") {
    for (const int length : {0, 8}) {
        INFO("transferred to " << (length ? std::to_string(length) + " bytes" : std::string("its own length")));
        ScriptRig rig(kAudioHazards);
        const auto graph = [&](const std::string& extra) {
            return doc(node("t", "rp.toy.transfer", R"({"length":)" + std::to_string(length) + "}") + "," +
                           node("sink", "rp.test.sink") + extra,
                       wire("t.out", "sink.in"));
        };
        REQUIRE(rig.set(graph("")).ok);
        rig.run();
        const auto& sink = rig.node<gt::Sink>("sink");
        CHECK(sink.first[0] == 0.0f);  // what it gave away is not the plan's to read
        REQUIRE(rig.set(graph("," + node("x", "rp.test.sink"))).ok);  // a swap, freeing the plan it first ran in
        rig.run(64, 2);
        CHECK(sink.first[0] == (length ? 0.0f : 0.25f));
        CHECK(sink.last[0] == (length ? 0.0f : 0.25f));
        CHECK(rig.scripts.throwCount() == 0);
    }
}

TEST_CASE("a script node's audio inputs are its own copies: writing one reaches neither its source nor the plan's silence",
          "[script][audio-copy][inplace]") {
    ScriptRig rig(kAudioHazards);
    // Unconnected, s's input is the one silent buffer every unconnected input of the plan reads, sink's after it.
    REQUIRE(rig.set(doc(node("s", "rp.toy.scribbler") + "," + node("sink", "rp.test.sink") + "," + node("own", "rp.test.sink"),
                        wire("s.out", "own.in")))
                .ok);
    rig.run(64, 2);
    CHECK(rig.node<gt::Sink>("sink").first[0] == 0.0f);
    CHECK(rig.node<gt::Sink>("sink").last[0] == 0.0f);
    CHECK(rig.node<gt::Sink>("own").first[0] == 0.5f);  // its copy holds what it wrote
    // Wired, its input is the source's output, which `after` reads after it.
    REQUIRE(rig.set(doc(node("src", "rp.test.source") + "," + node("s", "rp.toy.scribbler") + "," + node("after", "rp.test.sink") +
                            "," + node("own", "rp.test.sink"),
                        wire("src.out", "s.in") + "," + wire("src.out", "after.in") + "," + wire("s.out", "own.in")))
                .ok);
    rig.run(64, 2);
    CHECK(rig.node<gt::Sink>("after").first[0] == 1.0f);
    CHECK(rig.node<gt::Sink>("after").last[1] == 1.0f);
    CHECK(rig.node<gt::Sink>("own").first[0] == 0.5f);
    CHECK(rig.scripts.throwCount() == 0);
}

TEST_CASE("a script node that swaps its audio array's element for its own goes on writing the copy it was handed",
          "[script][audio-copy][mutate]") {
    ScriptRig rig(kAudioHazards);
    const std::string nodes = node("src", "rp.test.source") + "," + node("m", "rp.toy.mutator") + "," + node("sink", "rp.test.sink");
    const std::string wires = wire("src.out", "m.in") + "," + wire("m.out", "sink.in");
    REQUIRE(rig.set(doc(nodes, wires)).ok);
    rig.run(64, 2);
    CHECK(rig.node<gt::Sink>("sink").first[0] == 0.75f);
    REQUIRE(rig.set(doc(nodes + "," + node("x", "rp.test.sink"), wires)).ok);  // the plan it first ran in, freed
    rig.run(64, 2);
    CHECK(rig.node<gt::Sink>("sink").first[0] == 0.75f);
    CHECK(rig.scripts.throwCount() == 0);
}

TEST_CASE("a view a script node keeps in module state outlives its instance as its own memory, which nothing reads",
          "[script][audio-copy][stash]") {
    ScriptRig rig(kAudioHazards);
    REQUIRE(rig.set(doc(node("s", "rp.toy.stasher") + "," + node("sink", "rp.test.sink") + "," + node("u", "rp.toy.user"),
                        wire("s.out", "sink.in")))
                .ok);
    rig.run(64, 2);
    REQUIRE(rig.set(doc(node("u", "rp.toy.user") + "," + node("src", "rp.test.source") + "," + node("sink", "rp.test.sink"),
                        wire("src.out", "sink.in")))
                .ok);  // s goes, and its plan with it
    rig.run(64, 3);    // u writes through what s kept
    CHECK(rig.node<gt::Sink>("sink").first[0] == 1.0f);
    CHECK(rig.node<gt::Sink>("sink").last[1] == 1.0f);
    CHECK(rig.scripts.throwCount() == 0);
}

TEST_CASE("a promise reaction a script node queues runs at the end of its block, and its memory is given back", "[script][jobs]") {
    ScriptRig rig(R"JS(
rp.register([rp.defineNode({
  id: "rp.toy.async",
  ports: { reactions: rp.control.out() },
  create: () => {
    let ran = 0;
    return {
      process(b) {
        b.emit("reactions", 0, ran);
        Promise.resolve(1).then(() => { ++ran; });
      },
    };
  },
})]);
)JS");
    REQUIRE(rig.set(doc(node("a", "rp.toy.async") + "," + node("p", "rp.test.probe"), wire("a.reactions", "p.control"))).ok);
    rig.run(64, 4);
    CHECK(rig.node<Probe>("p").controlAtZero == 3.0);  // each block sees every reaction the blocks before it queued
    const auto before = rig.scripts.audioAllocations();
    rig.run(64, 1000);
    const auto after = rig.scripts.audioAllocations();
    CHECK(rig.node<Probe>("p").controlAtZero == 1003.0);
    const auto held = static_cast<std::int64_t>(after.allocs - before.allocs) - static_cast<std::int64_t>(after.frees - before.frees);
    INFO("1000 blocks: " << after.allocs - before.allocs << " allocations, " << after.frees - before.frees << " frees");
    CHECK(held <= 16);  // what a reaction allocates is freed once it has run: nothing grows with the blocks
    CHECK(rig.scripts.throwCount() == 0);
}

TEST_CASE("promise reactions that queue another every time run a bounded number a block, the rest at the next block's end",
          "[script][jobs]") {
    ScriptRig rig(R"JS(
rp.register([rp.defineNode({
  id: "rp.toy.chain",
  ports: { links: rp.control.out() },
  create: () => {
    let links = 0, started = false;
    const link = () => { ++links; Promise.resolve().then(link); };
    return {
      process(b) {
        b.emit("links", 0, links);
        if (!started) { started = true; link(); Promise.resolve().then(() => { throw new Error("a rejection, not a throw"); }); }
      },
    };
  },
})]);
)JS");
    REQUIRE(rig.set(doc(node("c", "rp.toy.chain") + "," + node("p", "rp.test.probe"), wire("c.links", "p.control"))).ok);
    rig.run();
    CHECK(rig.node<Probe>("p").controlAtZero == 0.0);
    CHECK(rig.scripts.jobsCutShort() == 1);
    rig.run();
    // The link process ran, then the block's 256 jobs: 255 more links and the reaction that threw, which rejects its
    // promise rather than failing the job.
    CHECK(rig.node<Probe>("p").controlAtZero == 1.0 + (ScriptHost::kJobsPerBlock - 1));
    rig.run();
    CHECK(rig.node<Probe>("p").controlAtZero == 1.0 + (ScriptHost::kJobsPerBlock - 1) + ScriptHost::kJobsPerBlock);
    CHECK(rig.scripts.jobsCutShort() == 3);
    CHECK(rig.scripts.throwCount() == 0);
    rig.reclaim();  // says so, once
}

TEST_CASE("a script node's payloads grow their buffer when a block's outgrow it, and one JS detached is replaced",
          "[script][payload]") {
    ScriptRig rig(R"JS(
rp.register([rp.defineNode({
  id: "rp.toy.bpass",
  ports: { in: rp.bytes.in(), out: rp.bytes.out({ messages: 16, bytes: 8192 }) },
  create: () => ({
    process(b) {
      const ms = b.messages("in");
      for (let i = 0; i < ms.length; ++i) {
        b.send("out", ms[i].frame, ms[i].data, ms[i].flags);
        if (ms[i].data.length === 7) ms[i].data.buffer.transfer();  // the payload buffer, detached
      }
    },
  }),
})]);
)JS");
    REQUIRE(rig.set(doc(node("feed", "rp.test.feed") + "," + node("p", "rp.toy.bpass") + "," + node("probe", "rp.test.probe"),
                        wire("feed.bytes", "p.in") + "," + wire("p.out", "probe.bytes")))
                .ok);
    auto& feed = rig.node<Feed>("feed");
    const auto& probe = rig.node<Probe>("probe");
    feed.next[Feed::Bytes] = {{1, pattern(100, 1), 0}, {2, pattern(100, 2), 0}};
    rig.run();
    REQUIRE(probe.bytes.size() == 2);
    feed.next[Feed::Bytes] = {{3, pattern(3000, 3), 0}, {4, pattern(5, 4), 0}};  // past the first block's 256-byte buffer
    rig.run();
    REQUIRE(probe.bytes.size() == 2);
    CHECK(probe.bytes[0].data == pattern(3000, 3));
    CHECK(probe.bytes[1].data == pattern(5, 4));
    feed.next[Feed::Bytes] = {{0, pattern(7, 5), 0}};
    rig.run();
    feed.next[Feed::Bytes] = {{0, pattern(9, 6), 0}, {1, pattern(11, 7), 0}};
    rig.run();
    REQUIRE(probe.bytes.size() == 2);
    CHECK(probe.bytes[0].data == pattern(9, 6));
    CHECK(probe.bytes[1].data == pattern(11, 7));
    CHECK(rig.scripts.throwCount() == 0);
}

TEST_CASE("a script node whose create throws faults at its first block, and is released", "[script][create]") {
    ScriptRig rig(R"JS(
rp.register([rp.defineNode({ id: "rp.toy.badcreate", ports: { out: rp.events.out() }, create: () => { throw new Error("no"); } })]);
)JS");
    REQUIRE(rig.set(doc(node("b", "rp.toy.badcreate"), "")).ok);  // planned: create is lazy, so nothing has failed yet
    rig.run(64, 2);
    CHECK(rig.host->isFaulted("b"));
    CHECK(rig.scripts.throwCount() == 1);
    CHECK(rig.scripts.liveInstances() == 0);
    REQUIRE(rig.set(doc("", "")).ok);
    rig.reclaim();
    CHECK(rig.scripts.firstThrow() == "rp.toy.badcreate threw in create: Error: no");
}

TEST_CASE("a re-activation reaches a live script node's activate, a refusal rolls back, and portsChanged hears its outputs",
          "[script][activate]") {
    ScriptRig rig(R"JS(
rp.register([rp.defineNode({
  id: "rp.toy.act",
  ports: { out: rp.events.out() },
  create: ({ sampleRate, maxFrames }) => {
    __probe("create " + sampleRate + "/" + maxFrames);
    return {
      process(b) {},
      activate(sr, mf) { __probe("activate " + sr + "/" + mf); return mf <= 512; },
      portsChanged(c) { __probe("ports " + c.join(",")); },
    };
  },
})]);
)JS");
    REQUIRE(rig.set(doc(node("a", "rp.toy.act") + "," + node("p", "rp.test.probe"), wire("a.out", "p.events"))).ok);
    rig.run();
    CHECK(rig.log.count("create 48000/256") == 1);
    CHECK(rig.log.count("ports true") == 1);
    std::string why;
    CHECK(rig.host->reactivate(44100.0, 512, &why));
    CHECK(rig.log.count("activate 44100/512") == 1);
    CHECK_FALSE(rig.host->reactivate(48000.0, 1024, &why));
    INFO(why);
    CHECK(rig.log.count("activate 48000/1024") == 1);
    CHECK(rig.log.count("activate 44100/512") == 2);  // back on the old
    REQUIRE(rig.set(doc(node("a", "rp.toy.act"), "")).ok);  // its output unwired
    rig.run();
    CHECK(rig.log.count("ports false") == 1);
    CHECK(rig.scripts.throwCount() == 0);
}

TEST_CASE("a script host loads one bundle: a second load is refused, and the first one's types stay registered",
          "[script][load]") {
    ScriptRig rig(kPass);
    const std::vector<std::uint8_t> again = rp::host::test::compileToys(kCounters);
    const ScriptHost::Chunk chunk = again;
    std::string error;
    CHECK_FALSE(rig.scripts.load({&chunk, 1}, &error));
    CHECK(error == "a script host loads one bundle");
    CHECK(rig.scripts.package()->node_type_count == 2);  // the test bundle's echo, and pass
    REQUIRE(rig.set(doc(node("feed", "rp.test.feed") + "," + node("pass", "rp.toy.pass") + "," + node("probe", "rp.test.probe"),
                        wire("feed.midi", "pass.in") + "," + wire("pass.out", "probe.events")))
                .ok);
    rig.node<Feed>("feed").next[Feed::Midi] = {{2, bytes({0x90, 1, 2})}};
    rig.run();
    REQUIRE(rig.node<Probe>("probe").events.size() == 1);
    CHECK(rig.node<Probe>("probe").events[0].data == bytes({0x90, 1, 2}));
}

TEST_CASE("a bundle whose definition differs between the two contexts is refused, and one bad definition refuses the package",
          "[script][load][two-faced]") {
    rp::host::test::ProbeLog log;
    ScriptHost host(&rp::host::test::bindProbes, &log);
    const std::vector<std::uint8_t> toys = rp::host::test::compileToys(R"JS(
rp.register([rp.defineNode({
  id: "rp.toy.twofaced",
  ports: __audioSide ? { out: rp.events.out() } : { out: rp.events.out(), more: rp.events.out() },
  create: () => ({ process() {} }),
})]);
)JS");
    const ScriptHost::Chunk chunks[] = {rp::host::test::scriptTestBundle(), toys};
    std::string error;
    CHECK_FALSE(host.load(chunks, &error));
    INFO(error);
    CHECK(error.find("rp.toy.twofaced") != std::string::npos);
    CHECK(error.find("descriptor") != std::string::npos);
    CHECK(host.package()->node_type_count == 0);  // the test bundle's own echo goes with it
}

TEST_CASE("a send whose data throws while it is read leaves nothing of that message in the queue", "[script][send]") {
    ScriptRig rig(R"JS(
rp.register([rp.defineNode({
  id: "rp.toy.liar",
  ports: { out: rp.events.out() },
  create: () => ({
    process(b) {
      // A note-on whose velocity cannot be read: half of it, zeros after, would be a note-off.
      const torn = { length: 3, 0: 0x90, 1: 60, get 2() { throw new Error("no velocity"); } };
      try { b.send("out", 0, torn); } catch (e) {}
      // One whose read sends another first, so its reservation is no longer the queue's last message.
      const nested = { length: 3, 0: 0x90, get 1() { b.send("out", 1, [0xb0, 1, 2]); throw new Error("no note"); }, 2: 9 };
      try { b.send("out", 2, nested); } catch (e) {}
      b.send("out", 3, [0x80, 60, 0]);
    },
  }),
})]);
)JS");
    REQUIRE(rig.set(doc(node("l", "rp.toy.liar") + "," + node("p", "rp.test.probe"), wire("l.out", "p.events"))).ok);
    rig.run();
    const auto& events = rig.node<Probe>("p").events;
    REQUIRE(events.size() == 2);
    CHECK(events[0].data == bytes({0xb0, 1, 2}));
    CHECK(events[0].frame == 2);  // sent behind the reservation at frame 2, so moved up to it
    CHECK(events[1].data == bytes({0x80, 60, 0}));
    CHECK(events[1].frame == 3);
    CHECK(rig.scripts.throwCount() == 0);  // the node caught them
}

TEST_CASE("a script node's create and onSettings receive its settings as their JSON form", "[script][settings]") {
    ScriptRig rig(R"JS(
rp.register([rp.defineNode({
  id: "rp.toy.transformed",
  settings: rp.z.object({
    name: rp.z.string().default("abc").transform((s) => s.length),                // a number: JSON keeps it
    tags: rp.z.array(rp.z.string()).default(["a"]).transform((t) => new Set(t)),  // a Set: JSON makes it {}
  }),
  ports: { out: rp.events.out() },
  create: ({ settings }) => {
    __probe("create " + JSON.stringify(settings) + " " + (settings.tags instanceof Set));
    return { process() {}, onSettings: (s) => __probe("onSettings " + JSON.stringify(s)) };
  },
})]);
)JS");
    REQUIRE(rig.set(doc(node("t", "rp.toy.transformed"), "")).ok);
    CHECK(rig.log.count(R"(create {"name":3,"tags":{}} false)") == 1);
    REQUIRE(rig.set(doc(node("t", "rp.toy.transformed", R"({"name":"hello"})"), "")).ok);  // a swap
    CHECK(rig.log.count(R"(onSettings {"name":5,"tags":{}})") == 1);
}

TEST_CASE("faultOnThrow covers a script node's onSettings and portsChanged: a reshape whose hook threw faults it at its next "
          "block",
          "[script][fault-on-throw]") {
    for (const char* hook : {"settings", "ports"}) {
        INFO("fail " << hook);
        ScriptRig rig(kBrittle);
        const auto graph = [](const std::string& settings) {
            return doc(node("b", "rp.toy.brittle", settings) + "," + node("s", "rp.toy.sturdy", settings) + "," +
                           node("pb", "rp.test.probe") + "," + node("ps", "rp.test.probe"),
                       wire("b.out1", "pb.events") + "," + wire("s.out1", "ps.events"));
        };
        REQUIRE(rig.set(graph("")).ok);
        rig.run();
        REQUIRE(rig.node<Probe>("pb").events.size() == 1);
        rp_node* const kept = rig.host->node("b");
        // A reshape: onSettings at the commit, then portsChanged for the new layout.
        REQUIRE(rig.set(graph(std::string(R"({"outs":2,"fail":")") + hook + "\"}")).ok);
        CHECK(rig.host->node("b") == kept);
        rig.run(64, 2);
        CHECK(rig.host->isFaulted("b"));
        CHECK(rig.node<Probe>("pb").events.empty());
        CHECK_FALSE(rig.host->isFaulted("s"));  // counted, and it runs on
        CHECK(rig.node<Probe>("ps").events.size() == 1);
        CHECK(rig.scripts.throwCount() == 2);
    }
}

TEST_CASE("a released script instance's cycles are collected when it is released quiescent and when the audio thread hands the "
          "runtime back, never by a block",
          "[script][gc]") {
    ScriptRig rig(R"JS(
rp.register([rp.defineNode({
  id: "rp.toy.cyclic",
  settings: rp.z.object({ tag: rp.z.string().default("cyclic") }),
  ports: { out: rp.events.out() },
  create: ({ settings }) => { const self = { t: __token(settings.tag), n: 0, process(b) { self.n++; } }; return self; },
})]);
)JS");
    REQUIRE(rig.set(doc(node("c", "rp.toy.cyclic"), "")).ok);
    rig.run(64, 2);
    REQUIRE(rig.set(doc("", "")).ok);  // quiescent: released here, and its cycle collected
    CHECK(rig.log.count("released cyclic") == 1);

    rig.setAudioThreadOwns(true);  // this thread plays the audio thread
    REQUIRE(rig.set(doc(node("c", "rp.toy.cyclic", R"({"tag":"owned"})"), "")).ok);
    rig.run(64, 2);
    REQUIRE(rig.set(doc("", "")).ok);
    rig.block();        // adopts the empty plan, handing back the one that held c
    rig.reclaim();      // frees it, queueing c's release
    rig.run(64, 200);   // the first releases it, and none collects its cycle
    CHECK(rig.scripts.liveInstances() == 0);
    CHECK(rig.log.count("released owned") == 0);
    rig.setAudioThreadOwns(false);
    CHECK(rig.log.count("released owned") == 1);
}

TEST_CASE("a long non-ASCII message is cut at a character, never inside one", "[script][utf8]") {
    // After 0, 1 or 2 ASCII letters, one cut by bytes would land inside a character whatever a buffer holds.
    for (int pad = 0; pad <= 2; ++pad) {
        INFO("pad " << pad);
        ScriptRig rig(kWordy);
        const auto refused = rig.set(doc(node("w", "rp.toy.wordy", R"({"refuse":true,"pad":)" + std::to_string(pad) + "}"), ""));
        REQUIRE(ScriptRig::codes(refused) == "settings-invalid");
        CHECK(refused.errors[0].message.size() > 200);
        CHECK(wholeUtf8(refused.errors[0].message));
        REQUIRE(rig.set(doc(node("w", "rp.toy.wordy", R"({"pad":)" + std::to_string(pad) + "}"), "")).ok);
        rig.run();
        const std::string first = rig.scripts.firstThrow();
        CHECK(first.size() > 400);
        CHECK(wholeUtf8(first));
    }
}

TEST_CASE("releases past the ring wait in the control thread's backlog, a destroy allocates nothing, and a block releases a "
          "bounded number",
          "[script][backlog]") {
    ScriptRig rig(kIdle);
    std::string nodes;
    for (int i = 0; i < 300; ++i) nodes += (i ? "," : "") + node("n" + std::to_string(i), "rp.toy.idle");
    rig.setAudioThreadOwns(true);  // this thread plays the audio thread
    REQUIRE(rig.set(doc(nodes, "")).ok);
    rig.block();
    REQUIRE(rig.scripts.liveInstances() == 300);
    REQUIRE(rig.set(doc("", "")).ok);
    rig.block();  // adopts the empty plan, handing back the 300
    rp::test::beginCountingAllocations();
    rig.reclaim();  // frees that plan: 300 destroys, 255 into the ring (a slot short of its 256) and 45 into the backlog
    const auto destroys = rp::test::endCountingAllocations();
    CHECK(destroys.allocations == 0);  // a destroy runs in a destructor, so it may not throw bad_alloc
    constexpr std::uint32_t kPer = ScriptHost::kReleasesPerBlock;
    rig.block();
    CHECK(rig.scripts.liveInstances() == 300 - kPer);
    for (std::uint32_t b = 1; b < (255 + kPer - 1) / kPer; ++b) rig.block();
    CHECK(rig.scripts.liveInstances() == 45);  // the ring drained: the backlog waits for the control thread
    rig.block();
    CHECK(rig.scripts.liveInstances() == 45);
    rig.reclaim();  // deletes the records the blocks handed back, and moves the backlog into the ring
    for (int b = 0; b < 8 && rig.scripts.liveInstances() > 0; ++b) rig.block();
    CHECK(rig.scripts.liveInstances() == 0);
    rig.reclaim();
    rig.setAudioThreadOwns(false);
}

// Runs for ever on every block, or on the odd ones, after its send: no try can catch what stops it.
const char* const kSpinners = R"JS(
const spinner = (id, extra) => rp.defineNode({
  id, ...extra,
  settings: rp.z.object({ spinOn: rp.z.enum(["every", "odd"]).default("every") }),
  ports: { out: rp.events.out() },
  create: ({ settings }) => {
    let n = 0;
    return {
      process(b) {
        b.send("out", 0, [++n]);
        if (settings.spinOn === "every" || n % 2 === 1) for (;;) { try { while (true) {} } catch (e) {} }
      },
    };
  },
});
rp.register([spinner("rp.toy.spinner", {}), spinner("rp.toy.strict-spinner", { faultOnThrow: true })]);
)JS";

TEST_CASE("a script node held off the CPU past the call deadline is not stopped: the deadline counts its own CPU time",
          "[script][deadline][off-cpu]") {
    ScriptRig rig(R"JS(
rp.register([rp.defineNode({
  id: "rp.toy.stalled",  // no faultOnThrow, as the kernel
  ports: { out: rp.events.out() },
  create: () => {
    let n = 0;
    const work = () => { let x = 0; for (let i = 0; i < 50000; ++i) x += i; return x; };  // polls; a few ms of CPU at most
    return {
      process(b) {
        work();
        if (++n === 4) __sleep(150);  // preempted, stopped (Ctrl-Z on a render) or a paused VM, for 150 ms
        work();
        b.send("out", 0, [0x90, 60, 100]);
      },
    };
  },
})]);
)JS");
    rig.scripts.setCallDeadline(std::chrono::milliseconds(100));
    REQUIRE(rig.set(doc(node("k", "rp.toy.stalled") + "," + node("p", "rp.test.probe"), wire("k.out", "p.events"))).ok);
    rig.run(64, 3);
    const auto start = std::chrono::steady_clock::now();
    rig.run();  // the stall: half again the deadline of wall time, next to none of CPU
    CHECK(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(150));
    CHECK(rig.node<Probe>("p").events.size() == 1);
    rig.run();
    CHECK(rig.scripts.throwCount() == 0);
    CHECK_FALSE(rig.host->isFaulted("k"));
    CHECK(rig.node<Probe>("p").events.size() == 1);
}

TEST_CASE("a script node without faultOnThrow that runs past the call deadline is counted once and faulted on the second "
          "expiry in a row",
          "[script][deadline][streak]") {
    ScriptRig rig(kSpinners);
    rig.scripts.setCallDeadline(std::chrono::milliseconds(50));
    REQUIRE(rig.set(doc(node("s", "rp.toy.spinner") + "," + node("p", "rp.test.probe"), wire("s.out", "p.events"))).ok);
    rig.run();
    CHECK(rig.scripts.throwCount() == 1);
    CHECK_FALSE(rig.host->isFaulted("s"));  // design 4.5: the next block runs again
    REQUIRE(rig.node<Probe>("p").events.size() == 1);  // what it sent before it hung stands
    rig.run();
    CHECK(rig.scripts.throwCount() == 2);
    CHECK(rig.host->isFaulted("s"));  // hung again: bypassed, within two deadlines
    CHECK(rig.node<Probe>("p").events.empty());
    rig.run();
    CHECK(rig.scripts.throwCount() == 2);
    rig.reclaim();
    INFO(rig.scripts.firstThrow());
    CHECK(rig.scripts.firstThrow().find("rp.toy.spinner ran past the 50 ms call deadline in process (CPU time), and was stopped") == 0);
}

TEST_CASE("a script node with faultOnThrow that runs past the call deadline faults on the first expiry", "[script][deadline][strict]") {
    ScriptRig rig(kSpinners);
    rig.scripts.setCallDeadline(std::chrono::milliseconds(50));
    REQUIRE(rig.set(doc(node("s", "rp.toy.strict-spinner") + "," + node("p", "rp.test.probe"), wire("s.out", "p.events"))).ok);
    rig.run();
    CHECK(rig.scripts.throwCount() == 1);
    CHECK(rig.host->isFaulted("s"));
    CHECK(rig.node<Probe>("p").events.empty());
}

TEST_CASE("a call that runs to its end between two expiries starts the count again", "[script][deadline][reset]") {
    ScriptRig rig(kSpinners);
    rig.scripts.setCallDeadline(std::chrono::milliseconds(50));
    REQUIRE(rig.set(doc(node("s", "rp.toy.spinner", R"({"spinOn":"odd"})") + "," + node("p", "rp.test.probe"), wire("s.out", "p.events")))
                .ok);
    for (int b = 1; b <= 6; ++b) {
        rig.run();
        INFO("block " << b);
        CHECK(rig.scripts.throwCount() == static_cast<std::uint32_t>((b + 1) / 2));  // every odd block stopped
        CHECK_FALSE(rig.host->isFaulted("s"));
        CHECK(rig.node<Probe>("p").events.size() == 1);
    }
}

TEST_CASE("an endless recursion in a script node throws and it runs on, and a promise reaction that never returns is stopped at "
          "its block's end",
          "[script][deadline]") {
    ScriptRig rig(R"JS(
rp.register([rp.defineNode({
  id: "rp.toy.deep",
  ports: { out: rp.events.out() },
  create: () => { const down = (n) => down(n + 1) + 1; return { process(b) { b.send("out", 0, [2]); down(0); } }; },
}), rp.defineNode({
  id: "rp.toy.slowjob",  // a promise reaction that never returns, once
  ports: { out: rp.events.out() },
  create: () => {
    let queued = false;
    return { process(b) { if (!queued) { queued = true; Promise.resolve().then(() => { for (;;) {} }); } } };
  },
})]);
)JS");
    rig.scripts.setCallDeadline(std::chrono::milliseconds(50));
    REQUIRE(rig.set(doc(node("j", "rp.toy.slowjob"), "")).ok);
    rig.run();
    CHECK(rig.scripts.throwCount() == 1);
    CHECK_FALSE(rig.host->isFaulted("j"));  // a job is no one node's
    rig.reclaim();
    INFO(rig.scripts.firstThrow());
    CHECK(rig.scripts.firstThrow().find("a promise job ran past the 50 ms call deadline (CPU time), and was stopped") == 0);
    REQUIRE(rig.set(doc(node("d", "rp.toy.deep") + "," + node("p", "rp.test.probe"), wire("d.out", "p.events"))).ok);
    rig.run(64, 3);
    CHECK(rig.scripts.throwCount() == 4);  // a stack overflow is an ordinary throw: counted, and it runs on
    CHECK_FALSE(rig.host->isFaulted("d"));
    CHECK(rig.node<Probe>("p").events.size() == 1);
}

TEST_CASE("an audio thread runs script nodes while the control thread plans, reshapes, rebuilds and reclaims them",
          "[script][stress]") {
    ScriptRig rig(std::string(kCounters) + kGain);
    rig.setAudioThreadOwns(true);
    std::atomic<bool> running{true};
    std::thread audio([&] {
        while (running.load()) {
            rig.block(64);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });
    int refused = 0;
    for (int i = 0; i < 300; ++i) {
        std::string nodes = node("c", "rp.toy.counter", R"({"outs":)" + std::to_string(1 + i % 4) + "}") + "," +
                            node("p", "rp.test.probe") + "," + node("src", "rp.test.source") + "," +
                            node("sink", "rp.test.sink");
        std::string wires = wire("c.out1", "p.events");
        if (i % 3 != 0) {
            nodes += "," + node("n", "rp.toy.plain", R"({"outs":)" + std::to_string(1 + (i / 3) % 2) + R"(,"tag":)" + std::to_string(i) + "}");
        }
        if (i % 5 != 0) {
            nodes += "," + node("g", "rp.toy.gain");
            wires += "," + wire("src.out", "g.in") + "," + wire("g.out", "sink.in");
        }
        const auto r = rig.set(doc(nodes, wires));
        if (!r.ok) {
            INFO(ScriptRig::codes(r));
            REQUIRE(ScriptRig::codes(r) == "busy");
            ++refused;
        }
        if (i % 5 != 0) rig.host->setParam("g", "gain", (i % 7) * 0.5);
        rig.reclaim();
        std::this_thread::sleep_for(std::chrono::microseconds(150));
    }
    running = false;
    audio.join();
    rig.setAudioThreadOwns(false);
    CHECK(refused < 300);
    CHECK(rig.scripts.throwCount() == 0);
    // Quiescent again: the last word is a document with one script node, and every other instance is gone.
    REQUIRE(rig.set(doc(node("c", "rp.toy.counter", R"({"outs":1})"), "")).ok);
    rig.reclaim();
    CHECK(rig.scripts.liveInstances() == 1);
}

TEST_CASE("the embedded script bundle loads, and registers the production script types", "[script]") {
    ScriptHost host;
    const ScriptHost::Chunk bundle = ScriptHost::embeddedBundle();
    std::string error;
    REQUIRE(bundle.size() > 0);
    REQUIRE(host.load({&bundle, 1}, &error));
    CHECK(error.empty());
    // The DSP kernel (KernelNode.test.cpp holds it to what it was recorded sending).
    REQUIRE(host.package()->node_type_count == 1);
    CHECK(std::string(host.package()->node_types[0]->id) == "rp.dsp.kernel");
    CHECK(host.package()->abi_version == RP_ABI_VERSION);
}

TEST_CASE("an Engine runs the script types of the bundle it is built with, and counts their throws", "[script]") {
    const std::vector<std::uint8_t> toys = rp::host::test::compileToys(kThrowers);
    const ScriptHost::Chunk chunks[] = {rp::host::test::scriptTestBundle(), toys};
    Engine e(48000.0, chunks);
    Engine::GraphRequest request;
    request.document = doc(node("t", "rp.toy.thrower") + "," + node("out", "rp.host.audio-out", R"({"buses":[1,1]})"), "");
    const auto r = e.setGraph(request);
    INFO((r.errors.empty() ? std::string() : r.errors[0].code + ": " + r.errors[0].message));
    REQUIRE(r.ok);
    std::vector<float> l(256), rr(256);
    for (int i = 0; i < 4; ++i) e.processBlock(256, l.data(), rr.data());
    CHECK(e.scriptThrows() == 2);
    CHECK(e.faultCount() == 0);
    // The embedded bundle's types, in an Engine built the usual way: a document naming a toy type is refused.
    Engine plain;
    CHECK_FALSE(plain.setGraph(request).ok);
}

TEST_CASE("two Engines run script nodes on two threads at once, each in its own runtimes", "[script][two-engines]") {
    const std::vector<std::uint8_t> toys = rp::host::test::compileToys(kThrowers);
    const ScriptHost::Chunk chunks[] = {rp::host::test::scriptTestBundle(), toys};
    std::atomic<int> failures{0};
    auto run = [&] {
        for (int round = 0; round < 3; ++round) {
            Engine e(48000.0, chunks);
            Engine::GraphRequest request;
            request.document = doc(node("t", "rp.toy.thrower") + "," + node("q", "rp.toy.quitter") + "," +
                                       node("out", "rp.host.audio-out", R"({"buses":[1,1]})"),
                                   "");
            if (!e.setGraph(request).ok) ++failures;
            std::vector<float> l(256), r(256);
            for (int i = 0; i < 20; ++i) e.processBlock(256, l.data(), r.data());
            if (e.scriptThrows() != 10) ++failures;
        }
    };
    std::thread a(run), b(run);
    a.join();
    b.join();
    CHECK(failures.load() == 0);
}

TEST_CASE("an Engine torn down with plans still queued creates their script nodes there, then releases them", "[script][teardown]") {
    const std::vector<std::uint8_t> toys = rp::host::test::compileToys(std::string(kThrowers) + R"JS(
rp.register([rp.defineNode({ id: "rp.toy.badcreate", ports: { out: rp.events.out() }, create: () => { throw new Error("no"); } }),
             rp.defineNode({ id: "rp.toy.cyclic", ports: { out: rp.events.out() },
                             create: () => { const self = { big: new Float64Array(1 << 16), process(b) { self.big[0]++; } }; return self; } })]);
)JS");
    const ScriptHost::Chunk chunks[] = {rp::host::test::scriptTestBundle(), toys};
    Engine e(48000.0, chunks);
    e.setAudioThreadOwns(true);  // an audio thread that stops before it runs a block
    Engine::GraphRequest first, second;
    first.document = doc(node("t", "rp.toy.thrower") + "," + node("bad", "rp.toy.badcreate") + "," +
                             node("out", "rp.host.audio-out", R"({"buses":[1,1]})"),
                         "");
    second.document = doc(node("c", "rp.toy.cyclic") + "," + node("out", "rp.host.audio-out", R"({"buses":[1,1]})"), "");
    REQUIRE(e.setGraph(first).ok);
    REQUIRE(e.setGraph(second).ok);  // queued behind it: a plan of fresh instances waits for no adoption
    // The teardown a driver does once its audio thread has stopped (AudioDriverRpcService's destructor): every queued
    // plan adopted here, on this thread, so their script nodes are created here, and the plans replaced released.
    e.discardPending();
    CHECK(e.scriptThrows() == 1);  // bad's create ran
    e.setAudioThreadOwns(false);
    e.reclaim();
    // The Engine goes at the end of the test, its last plan's script node with it, after the GraphHost that holds it.
}
