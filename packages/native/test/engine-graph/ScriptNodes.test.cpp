// The script-node runtime (docs/modular-graph-phase-b3a-design.md section 2): TS nodes behind the same C ABI as
// native ones, one runtime per engine. Toy nodes, written here as JS source on the authoring layer the test bundle
// exposes (ScriptRig.hpp), are planned and run beside the graph tests' C++ sources and sinks: what reaches them
// through Block, what they send, the control-side context laying their ports out from settings, a node kept across
// a reshape or swapped or rebuilt, where their instances are made and released, a throw (a refused write onto what a
// node holds among them, while the accessors one hangs on the shared prototypes charge no other, a method only
// Object.prototype holds is adopted by none, and none replaces a method of the frozen Block prototype), the block path's
// allocations, and a running audio thread under all of it. The embedded production bundle and an Engine built
// around a toy bundle close it. A RETROPLUG_PROFILE build (build-prof/, which tools/run-profile.sh configures) adds the
// trace window's cases, [profile]: a script node's process span labelled by its type and closed every way a call ends,
// a label per type in load order, the profile calls refused while an audio thread owns the runtime, and the types past
// the label table's bound.
//
// Run via `pnpm test:plugin engine-graph`; the profile cases, build-prof/bin/retroplug-engine-graph-test "[profile]".

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
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

// One output per `outs` its settings name; refuses more than 4, its reason worded as zod words it.
const char* const kFan = R"JS(
rp.register([rp.defineNode({
  id: "rp.toy.fan",
  settings: rp.s.object({ outs: rp.s.int({ min: 1, max: 4, default: 2 }) }),
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
  settings: rp.s.object({ outs: rp.s.int({ min: 1, max: 4, default: 2 }), tag: rp.s.number(0) }),
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
  settings: rp.s.object({ length: rp.s.int({ min: 0, default: 0 }) }),
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
  settings: rp.s.object({ fail: rp.s.enum(["none", "settings", "ports"], "none"),
                          outs: rp.s.int({ min: 1, max: 2, default: 1 }) }),
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
  settings: rp.s.object({ pad: rp.s.int({ min: 0, max: 2, default: 0 }), refuse: rp.s.bool(false) }),
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

TEST_CASE("a queueMicrotask callback that throws fails its job, which is counted; a promise reaction's throw only rejects its "
          "promise",
          "[script][jobs]") {
    ScriptRig rig(R"JS(
rp.register([rp.defineNode({
  id: "rp.toy.microtask",
  ports: { out: rp.events.out() },
  create: () => {
    let n = 0;
    return {
      process(b) {
        ++n;
        if (n === 1) queueMicrotask(() => { throw new Error("from a microtask"); });
        if (n === 2) Promise.resolve().then(() => { throw new Error("a rejection, not a throw"); });
        b.send("out", 0, [n]);
      },
    };
  },
})]);
)JS");
    REQUIRE(rig.set(doc(node("m", "rp.toy.microtask") + "," + node("p", "rp.test.probe"), wire("m.out", "p.events"))).ok);
    rig.run();
    CHECK(rig.scripts.throwCount() == 1);  // at the block's end, as no one node's
    CHECK_FALSE(rig.host->isFaulted("m"));
    rig.run();
    CHECK(rig.scripts.throwCount() == 1);
    REQUIRE(rig.node<Probe>("p").events.size() == 1);
    CHECK(rig.node<Probe>("p").events[0].data == bytes({2}));
    rig.reclaim();
    INFO(rig.scripts.firstThrow());
    CHECK(rig.scripts.firstThrow().find("a promise job threw: Error: from a microtask") == 0);
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

namespace {

// Hangs a trap, in its first block, on what the block path fills for it: a setter that throws on a message's frame, on
// a control change's value or on the control stream's start, or a message, the messages array, the changes array or
// the control stream frozen whole. Its second block reads that input under a catch, its third uncaught, and its fourth
// not at all; each says whether an exception is pending, and sends its block's number, when it gets that far.
// rp.toy.trap-transport hangs one on the transport, which the host writes before every process call, and sends, as
// does rp.toy.strict-trap-transport, which faults on a throw. rp.toy.trap-audio writes 0.5 into each view of its output
// it can reach, then hangs a setter on the views array's first slot that throws (or runs for ever), or freezes the
// array, which the host writes when the block's length moves; rp.toy.strict-trap-audio is the setter's, faulting on a
// throw. rp.toy.asker, after one, says whether an exception is pending.
const char* const kTrap = R"JS(
const trapTransport = (id, extra) => rp.defineNode({
  id, ...extra,
  ports: { out: rp.events.out() },
  create: () => {
    let armed = false;
    return {
      process(b) {
        if (!armed) Object.defineProperty(b.transport, "tempo", { set() { throw new Error("trapped"); } });
        armed = true;
        b.send("out", 0, [1]);
      },
    };
  },
});
const trapAudio = (id, extra) => rp.defineNode({
  id, ...extra,
  settings: rp.s.object({ on: rp.s.string("setter") }),
  ports: { out: rp.audio.out(rp.channels.stereo()), sent: rp.events.out() },
  create: ({ settings: { on } }) => {
    let armed = false;
    return {
      process(b) {
        const outs = b.audio("out");
        for (let c = 0; c < outs.length; ++c) if (outs[c]) outs[c].fill(0.5);
        if (!armed) {
          armed = true;
          if (on === "frozen") Object.freeze(outs);
          else Object.defineProperty(outs, 0, { configurable: true, get() { return undefined; },
                                                set() { if (on === "spin") for (;;) {} throw new Error("trapped"); } });
        }
        b.send("sent", 0, [1]);
      },
    };
  },
});
rp.register([rp.defineNode({
  id: "rp.toy.trap",
  settings: rp.s.object({ on: rp.s.string("message") }),
  ports: { in: rp.events.in(), level: rp.control.in(), out: rp.events.out() },
  create: ({ settings: { on } }) => {
    let n = 0;
    const read = (b) => (on === "message" || on === "frozen" || on === "frozen-array" ? b.messages("in") : b.control("level"));
    const thrower = { set() { throw new Error("trapped"); } };
    return {
      process(b) {
        ++n;
        if (n === 1) {
          if (on === "message") Object.defineProperty(b.messages("in")[0], "frame", thrower);
          else if (on === "frozen") Object.freeze(b.messages("in")[0]);
          else if (on === "frozen-array") Object.freeze(b.messages("in"));
          else if (on === "change") Object.defineProperty(b.control("level").changes[0], "value", thrower);
          else if (on === "frozen-changes") Object.freeze(b.control("level").changes);
          else if (on === "frozen-stream") Object.freeze(b.control("level"));
          else Object.defineProperty(b.control("level"), "start", thrower);
        } else if (n === 2) {
          try { read(b); __probe("read"); } catch (e) { __probe("caught " + e.name); }
        } else if (n === 3) {
          read(b);
          __probe("read");
        }
        __probe(__pending() ? "pending" : "clear");
        __probe(b.send("out", 0, [n]) ? "sent" : "refused");
      },
    };
  },
}), trapTransport("rp.toy.trap-transport", {}), trapTransport("rp.toy.strict-trap-transport", { faultOnThrow: true }),
trapAudio("rp.toy.trap-audio", {}), trapAudio("rp.toy.strict-trap-audio", { faultOnThrow: true }), rp.defineNode({
  id: "rp.toy.asker",
  ports: { in: rp.events.in() },
  create: () => ({ process(b) { b.messages("in"); __probe(__pending() ? "pending" : "clear"); } }),
})]);
)JS";

// The one realm every script node shares. rp.toy.polluter, in its first block, hangs a getter and a setter that throw
// (saying so first) on Array.prototype's first indices and on Object.prototype for every name the host writes onto
// what it makes, as a node, or the bundle, may; then shows each took, on a fresh object or array. Its settings add the
// names the host reads a node's methods by: hooks "throw" traps them too, and hooks "adopt" makes each a function on
// Object.prototype that says it ran. rp.toy.tidy, planned after it, does nothing a node should not: it passes its
// messages and control changes on, scales its audio by its param and its block's number, reads its transport and says
// whether an exception is pending and how many views it got; with shrink "arrays" it takes back every array it was
// handed once it has read it, every block, draining its messages with shift and emptying its changes and both views
// arrays (a zero length, a splice). It calls no builtin that keeps an array of its own, as JSON.stringify does: QuickJS
// pushes onto one through the prototype chain, so the traps would meet the node's own call, which is no write of the
// host's. rp.toy.hollow's create returns no process; rp.toy.classy's returns a class instance, its process and
// portsChanged on its class's prototype and its onSettings on its base class's. rp.toy.vandal, in its first block,
// tries every way to replace, remove or shadow a method on the Block prototype every node shares, and says whether
// all of it stood.
const char* const kRealm = R"JS(
class Base { onSettings(s) { __probe("classy onSettings " + s.tag); } }
class Classy extends Base {
  process() { __probe("classy process"); }
  portsChanged(connected) { __probe("classy ports " + connected.join(",")); }
}
// Its process and onSettings are getters on its base class, which hand back what the instance holds.
class Accessed { get process() { return this.run; } get onSettings() { return this.swap; } }
class Handled extends Accessed {
  constructor() {
    super();
    this.run = () => __probe("handled process");
    this.swap = (s) => __probe("handled onSettings " + s.tag);
  }
}
rp.register([rp.defineNode({
  id: "rp.toy.polluter",
  settings: rp.s.object({ hooks: rp.s.enum(["none", "throw", "adopt"], "none") }),
  ports: { out: rp.events.out() },
  create: ({ settings: { hooks } }) => {
    let quiet = false, done = false;
    const trap = (name) => ({
      configurable: true,
      get() { if (!quiet) __probe("got " + name); throw new Error("polluted " + name); },
      set(v) { if (!quiet) __probe("set " + name); throw new Error("polluted " + name); },
    });
    const adopted = (name) => ({ configurable: true, writable: true, value: function () { __probe("adopted " + name); } });
    // "value" last: Object.defineProperty reads a descriptor's value through the prototype chain.
    const names = ["frames", "transport", "sampleRate", "tempo", "ppq", "playing", "frame", "data", "flags", "start",
                   "changes", "settings", "maxFrames", "value"];
    const methods = ["process", "onSettings", "portsChanged", "activate"];
    if (hooks === "throw") names.unshift(...methods);
    return {
      process() {
        if (done) return;
        done = true;
        for (let i = 0; i < 8; ++i) Object.defineProperty(Array.prototype, i, trap("[" + i + "]"));
        if (hooks === "adopt")
          for (let k = 0; k < methods.length; ++k) Object.defineProperty(Object.prototype, methods[k], adopted(methods[k]));
        for (let k = 0; k < names.length; ++k) Object.defineProperty(Object.prototype, names[k], trap(names[k]));
        quiet = true;
        let took = 0;
        for (let k = 0; k < names.length; ++k) try { ({})[names[k]] = 1; } catch (e) { ++took; }
        for (let i = 0; i < 8; ++i) try { [][i] = 1; } catch (e) { ++took; }
        quiet = false;
        __probe("polluted " + took);
      },
    };
  },
}), rp.defineNode({
  id: "rp.toy.tidy",
  settings: rp.s.object({ shrink: rp.s.enum(["none", "arrays"], "none"), tag: rp.s.number(0) }),
  ports: { in: rp.events.in(), level: rp.control.in(), audioIn: rp.audio.in(), out: rp.events.out({ messages: 16 }),
           audioOut: rp.audio.out(rp.channels.match("audioIn")), changes: rp.control.out() },
  params: { gain: rp.param.float({ name: "Gain", min: 0, max: 4, default: 0.5 }) },
  create: ({ settings, sampleRate, maxFrames }) => {
    __probe("created " + sampleRate + " " + maxFrames + " " + typeof settings);
    const shrink = settings.shrink === "arrays";
    let n = 0;
    return {
      process(b) {
        ++n;
        const ms = b.messages("in");
        if (shrink) while (ms.length > 0) { const m = ms.shift(); b.send("out", m.frame, m.data, m.flags); }
        else for (let i = 0; i < ms.length; ++i) b.send("out", ms[i].frame, ms[i].data, ms[i].flags);
        const changes = b.control("level").changes;
        for (let k = 0; k < changes.length; ++k) b.emit("changes", changes[k].frame, changes[k].value);
        const g = b.control("gain").start * n;
        const ins = b.audio("audioIn"), outs = b.audio("audioOut");
        for (let c = 0; c < outs.length; ++c) { const i = ins[c], o = outs[c]; for (let f = 0; f < b.frames; ++f) o[f] = i[f] * g; }
        const t = b.transport;
        __probe((__pending() ? "pending " : "clear ") + t.sampleRate + " " + t.tempo + " " + t.playing + " " + ins.length + "/" +
                outs.length);
        if (shrink) {
          changes.length = 0;
          ins.splice(0, ins.length);
          outs.length = 0;
        }
      },
      portsChanged(connected) { __probe("ports " + connected.join(",")); },
    };
  },
}), rp.defineNode({
  id: "rp.toy.hollow",
  ports: { out: rp.events.out() },
  create: () => ({}),
}), rp.defineNode({
  id: "rp.toy.classy",
  settings: rp.s.object({ tag: rp.s.number(0) }),
  ports: { out: rp.events.out() },
  create: () => new Classy(),
}), rp.defineNode({
  id: "rp.toy.handled",
  settings: rp.s.object({ tag: rp.s.number(0) }),
  ports: { out: rp.events.out() },
  create: () => new Handled(),
}), rp.defineNode({
  // A test double: a Proxy with nothing of its own (no getOwnPropertyDescriptor trap) whose get trap answers process
  // and onSettings, as the object create returns ("self") or as that object's prototype ("proto"). Its process says
  // whether the trap was handed the object create returned as the receiver.
  id: "rp.toy.wrapped",
  settings: rp.s.object({ at: rp.s.enum(["self", "proto"], "self"), tag: rp.s.number(0) }),
  ports: { out: rp.events.out() },
  create: ({ settings }) => {
    const handler = {
      get: (target, key, receiver) => {
        if (key === "process") return () => __probe("wrapped process " + (receiver === self));
        if (key === "onSettings") return (s) => __probe("wrapped onSettings " + s.tag);
        return undefined;
      },
    };
    const self = settings.at === "self" ? new Proxy({}, handler) : Object.create(new Proxy({}, handler));
    return self;
  },
}), rp.defineNode({
  // Its prototype is a Proxy over itself, which setPrototypeOf's cycle check cannot see through: the chain past its
  // own process cycles for ever.
  id: "rp.toy.looped",
  ports: { out: rp.events.out() },
  create: () => {
    const t = { process() { __probe("looped process"); } };
    Object.setPrototypeOf(t, new Proxy(t, {}));
    return t;
  },
}), rp.defineNode({
  // Its process on the last of `depth` plain objects, each the prototype of the one before.
  id: "rp.toy.deep",
  settings: rp.s.object({ depth: rp.s.number(1) }),
  ports: { out: rp.events.out() },
  create: ({ settings }) => {
    let o = { process() { __probe("deep process"); } };
    for (let i = 1; i < settings.depth; ++i) o = Object.create(o);
    return o;
  },
}), rp.defineNode({
  id: "rp.toy.vandal",
  ports: { out: rp.events.out() },
  create: () => {
    let done = false;
    return {
      process(b) {
        if (done) return;
        done = true;
        const spy = function () { __probe("spied"); return []; };
        const attempt = (f) => { try { f(); } catch (e) {} };
        const shared = [[Object.getPrototypeOf(b), ["messages", "send", "control", "emit", "audio"]]];
        let intact = true;
        for (let s = 0; s < shared.length; ++s) {
          const owner = shared[s][0], names = shared[s][1];
          for (let k = 0; k < names.length; ++k) {
            const name = names[k], kept = owner[name];
            attempt(() => { owner[name] = spy; });
            attempt(() => Object.defineProperty(owner, name, { value: spy }));
            attempt(() => Object.defineProperty(owner, name, { get: () => spy }));
            attempt(() => { delete owner[name]; });
            attempt(() => Object.defineProperty(kept, "call", { value: spy }));
            attempt(() => Object.setPrototypeOf(kept, { call: spy }));
            intact = intact && owner[name] === kept && Object.isFrozen(kept);
          }
        }
        const proto = Object.getPrototypeOf(b);
        attempt(() => Object.setPrototypeOf(proto, { messages: spy }));
        attempt(() => Object.defineProperty(proto, "extra", { value: spy }));
        intact = intact && Object.isFrozen(proto) && Object.getPrototypeOf(proto) === Object.prototype && !("extra" in proto);
        __probe(intact ? "intact" : "vandalized");
      },
    };
  },
})]);
)JS";

std::vector<std::string> tagsOf(rp::host::test::ProbeLog& log) {
    const std::lock_guard<std::mutex> lock(log.mutex);
    std::vector<std::string> tags;
    for (const auto& call : log.calls) tags.push_back(call.first);
    return tags;
}

// The tags a polluter's accessor, an adopted method or a vandal's spy says when it runs, and a pending exception.
std::vector<std::string> trapsHit(rp::host::test::ProbeLog& log) {
    std::vector<std::string> hit;
    for (const auto& tag : tagsOf(log))
        for (const char* said : {"got ", "set ", "adopted ", "spied", "pending"})
            if (tag.rfind(said, 0) == 0) hit.push_back(tag);
    return hit;
}

// rp.toy.tidy, with these settings, beside `first`, fed by a feed, a constant and a source, its outputs into a probe and
// a sink.
std::string tidyDoc(const std::string& first, const std::string& settings) {
    const std::string nodes = first + "," + node("feed", "rp.test.feed") + "," +
                              node("k", "rp.core.constant", "", R"({"value":0.5})") + "," + node("src", "rp.test.source") + "," +
                              node("t", "rp.toy.tidy", settings) + "," + node("p", "rp.test.probe") + "," + node("sink", "rp.test.sink");
    const std::string wires = wire("feed.midi", "t.in") + "," + wire("k.out", "t.level") + "," + wire("src.out", "t.audioIn") + "," +
                              wire("t.out", "p.events") + "," + wire("t.changes", "p.control") + "," + wire("t.audioOut", "sink.in");
    return doc(nodes, wires);
}

// That document planned, `first` planned alone and run a block already (so the realm is as it left it before the tidy
// node is made), and four blocks of alternating length run, so every block makes the views again: each block's messages,
// its control change and its audio (the source's 1, by the gain param's default and the block's number) reach the tidy
// node's outputs whole, and nothing is charged to it.
void runTidy(ScriptRig& rig, const std::string& first, const std::string& settings) {
    REQUIRE(rig.set(tidyDoc(first, settings)).ok);
    CHECK(rig.log.count("created 48000 256 object") == 1);
    CHECK(rig.log.count("ports true,true,true") == 1);
    auto& feed = rig.node<Feed>("feed");
    const auto& probe = rig.node<Probe>("p");
    const auto& sink = rig.node<gt::Sink>("sink");
    // Its messages array and their pool grow over the first two blocks, and the fourth has no message.
    const std::uint32_t counts[] = {3, 5, 1, 0};
    const std::uint32_t frames[] = {64, 32, 64, 32};
    for (int n = 0; n < 4; ++n) {
        INFO("block " << n + 1);
        std::vector<rp::host::test::Message> sent;
        for (std::uint32_t i = 0; i < counts[n]; ++i) sent.push_back({i, bytes({0x90, static_cast<int>(60 + i), 100})});
        feed.next[Feed::Midi] = sent;
        if (n > 0) REQUIRE(rig.host->setParam("k", "value", 0.25 * n, 10));
        rig.run(frames[n]);
        CHECK(rig.scripts.throwCount() == 0);
        CHECK_FALSE(rig.host->isFaulted("t"));
        CHECK(probe.events == sent);
        CHECK(probe.controlChanges == std::vector<double>{n == 0 ? 0.5 : 0.25 * n});
        CHECK(sink.first[0] == 0.5f * static_cast<float>(n + 1));
        CHECK(sink.last[1] == 0.5f * static_cast<float>(n + 1));
    }
    CHECK(rig.log.count("clear 48000 120 false 2/2") == 4);
}

}  // namespace

TEST_CASE("a write the block path makes onto what a script node holds that fails is the node's throw, and leaves nothing "
          "pending",
          "[script][trap]") {
    struct Case {
        const char* on;
        const char* thrown;
    };
    for (const Case& c : {Case{"message", "Error"}, Case{"frozen", "TypeError"}, Case{"frozen-array", "TypeError"},
                          Case{"change", "Error"}, Case{"frozen-changes", "TypeError"}, Case{"frozen-stream", "TypeError"},
                          Case{"start", "Error"}}) {
        INFO(c.on);
        ScriptRig rig(kTrap);
        REQUIRE(rig.set(doc(node("feed", "rp.test.feed") + "," + node("k", "rp.core.constant", "", R"({"value":0.5})") + "," +
                                node("t", "rp.toy.trap", std::string(R"({"on":")") + c.on + "\"}") + "," + node("p", "rp.test.probe"),
                            wire("feed.midi", "t.in") + "," + wire("k.out", "t.level") + "," + wire("t.out", "p.events")))
                    .ok);
        auto& feed = rig.node<Feed>("feed");
        const auto& probe = rig.node<Probe>("p");
        for (int n = 1; n <= 4; ++n) {
            INFO("block " << n);
            if (n < 4) feed.next[Feed::Midi] = {{static_cast<std::uint32_t>(n), bytes({0x90, 60, 100})}};
            if (n > 1) REQUIRE(rig.host->setParam("k", "value", 0.1 * n, 10));  // a change for the fill to write
            rig.run();
            // Sent, but by the third block, whose read the node does not catch: its throw ends the call first.
            CHECK(probe.events.size() == (n == 3 ? 0u : 1u));
            if (n != 3 && !probe.events.empty()) CHECK(probe.events[0].data == bytes({n}));
            CHECK(rig.scripts.throwCount() == (n < 3 ? 0u : 1u));
        }
        const std::vector<std::string> said{
            "clear", "sent", std::string("caught ") + c.thrown, "clear", "sent", "clear", "sent"};
        CHECK(tagsOf(rig.log) == said);
        CHECK_FALSE(rig.host->isFaulted("t"));
        rig.reclaim();
        INFO(rig.scripts.firstThrow());
        CHECK(rig.scripts.firstThrow().find(std::string("rp.toy.trap threw in process: ") + c.thrown) == 0);
    }
}

TEST_CASE("a write onto a script node's transport that fails is a throw in its process, which then does not run",
          "[script][trap]") {
    ScriptRig rig(kTrap);
    REQUIRE(rig.set(doc(node("t", "rp.toy.trap-transport") + "," + node("a", "rp.toy.asker") + "," + node("p", "rp.test.probe"),
                        wire("t.out", "a.in") + "," + wire("t.out", "p.events")))
                .ok);
    for (int n = 1; n <= 3; ++n) {
        INFO("block " << n);
        rig.run();
        CHECK(rig.node<Probe>("p").events.size() == (n == 1 ? 1u : 0u));  // its first block only
        CHECK(rig.scripts.throwCount() == static_cast<std::uint32_t>(n - 1));
    }
    CHECK(tagsOf(rig.log) == std::vector<std::string>{"clear", "clear", "clear"});
    CHECK_FALSE(rig.host->isFaulted("t"));
    rig.reclaim();
    INFO(rig.scripts.firstThrow());
    CHECK(rig.scripts.firstThrow().find("rp.toy.trap-transport threw in process: Error: trapped") == 0);
}

TEST_CASE("a write onto a faultOnThrow script node's transport that fails faults it, as a throw in its process does",
          "[script][trap][fault-on-throw]") {
    ScriptRig rig(kTrap);
    REQUIRE(rig.set(doc(node("t", "rp.toy.strict-trap-transport") + "," + node("p", "rp.test.probe"), wire("t.out", "p.events"))).ok);
    const auto& probe = rig.node<Probe>("p");
    rig.run();
    CHECK(probe.events.size() == 1);
    CHECK_FALSE(rig.host->isFaulted("t"));
    rig.run();  // the write onto its transport fails before process runs
    CHECK(rig.scripts.throwCount() == 1);
    CHECK(rig.host->isFaulted("t"));
    CHECK(probe.events.empty());
    rig.run();  // bypassed from now on: nothing runs, so nothing more is counted
    CHECK(rig.scripts.throwCount() == 1);
    CHECK(probe.events.empty());
}

TEST_CASE("a refused write onto a script node's audio views is a throw in its process, which then does not run",
          "[script][trap][audio]") {
    struct Case {
        const char*   type;
        const char*   on;
        const char*   first;      // how the first throw is told
        std::uint32_t throws[2];  // after the third block, and the fourth
        bool          faulted[2];
    };
    // The host writes the views when the block's length moves (the third block, 32 frames), and again the block after,
    // since a refused write leaves them all to be made again. The deadline stopping the setter is a throw as any other:
    // faulted on the second in a row, or under faultOnThrow on the first.
    for (const Case& c :
         {Case{"rp.toy.trap-audio", "setter", "rp.toy.trap-audio threw in process: Error: trapped", {1, 2}, {false, false}},
          Case{"rp.toy.trap-audio", "frozen", "rp.toy.trap-audio threw in process: TypeError", {1, 2}, {false, false}},
          Case{"rp.toy.trap-audio", "spin",
               "rp.toy.trap-audio ran past the 50 ms call deadline in process (CPU time), and was stopped", {1, 2}, {false, true}},
          Case{"rp.toy.strict-trap-audio", "setter", "rp.toy.strict-trap-audio threw in process: Error: trapped", {1, 1},
               {true, true}}}) {
        INFO(c.type << " " << c.on);
        ScriptRig rig(kTrap);
        rig.scripts.setCallDeadline(std::chrono::milliseconds(50));
        REQUIRE(rig.set(doc(node("t", c.type, std::string(R"({"on":")") + c.on + "\"}") + "," + node("a", "rp.toy.asker") + "," +
                                node("p", "rp.test.probe") + "," + node("sink", "rp.test.sink"),
                            wire("t.sent", "a.in") + "," + wire("t.sent", "p.events") + "," + wire("t.out", "sink.in")))
                    .ok);
        const auto& probe = rig.node<Probe>("p");
        const auto& sink = rig.node<gt::Sink>("sink");
        rig.run(64, 2);  // the first arms the trap; the second, as long, writes no view, so runs
        CHECK(probe.events.size() == 1);
        CHECK(sink.first[1] == 0.5f);
        CHECK(rig.scripts.throwCount() == 0);
        for (int k = 0; k < 2; ++k) {
            INFO("block " << 3 + k);
            rig.run(32);
            CHECK(probe.events.empty());   // its process did not run
            CHECK(sink.first[0] == 0.0f);  // and its outputs are silent
            CHECK(sink.first[1] == 0.0f);
            CHECK(rig.scripts.throwCount() == c.throws[k]);
            CHECK(rig.host->isFaulted("t") == c.faulted[k]);
        }
        CHECK(tagsOf(rig.log) == std::vector<std::string>(4, "clear"));
        rig.reclaim();
        INFO(rig.scripts.firstThrow());
        CHECK(rig.scripts.firstThrow().find(c.first) == 0);
    }
}

TEST_CASE("the accessors a script node hangs on the shared prototypes run for none of the host's writes, so a node beside it "
          "runs uncharged, even one that takes back every array it is handed",
          "[script][trap][realm]") {
    // With "arrays", every block's fill writes onto slots the tidy node took away (a shift, a zero length, a splice).
    for (const char* shrink : {"none", "arrays"}) {
        INFO("shrink " << shrink);
        ScriptRig rig(kRealm);
        REQUIRE(rig.set(doc(node("x", "rp.toy.polluter"), "")).ok);
        rig.run();
        REQUIRE(rig.log.count("polluted 22") == 1);  // every trap took
        runTidy(rig, node("x", "rp.toy.polluter"), std::string(R"({"shrink":")") + shrink + "\"}");
        CHECK(trapsHit(rig.log).empty());  // none ran, for the host or the tidy node, and nothing was left pending
    }
}

TEST_CASE("the host reads a script node's process and hooks from what its create returned, short of Object.prototype: a "
          "getter there that throws fails no create, and a function there is adopted by no node",
          "[script][trap][realm][hooks]") {
    ScriptRig rig(kRealm);
    SECTION("a getter that throws") {
        const std::string polluter = node("x", "rp.toy.polluter", R"({"hooks":"throw"})");
        REQUIRE(rig.set(doc(polluter, "")).ok);
        rig.run();
        REQUIRE(rig.log.count("polluted 26") == 1);
        runTidy(rig, polluter, "");  // the tidy node has no onSettings and no activate of its own
        CHECK(trapsHit(rig.log).empty());
    }
    SECTION("a function") {
        const std::string polluter = node("x", "rp.toy.polluter", R"({"hooks":"adopt"})");
        REQUIRE(rig.set(doc(polluter, "")).ok);
        rig.run();
        REQUIRE(rig.log.count("polluted 22") == 1);
        runTidy(rig, polluter, "");
        // The tidy node swapped to new settings (it has no onSettings to run), and the hollow and classy nodes made.
        const auto beside = [&](const char* classy) {
            return tidyDoc(polluter + "," + node("h", "rp.toy.hollow") + "," + node("c", "rp.toy.classy", classy), R"({"tag":1})");
        };
        REQUIRE(rig.set(beside("")).ok);
        rig.run(64, 2);
        CHECK(rig.host->isFaulted("h"));  // nothing to run: its create failed
        CHECK_FALSE(rig.host->isFaulted("t"));
        CHECK_FALSE(rig.host->isFaulted("c"));
        CHECK(rig.log.count("classy process") == 2);  // its methods are found on its classes' prototypes
        CHECK(rig.log.count("classy ports false") == 1);
        std::string why;
        CHECK(rig.host->reactivate(44100.0, 256, &why));  // neither the tidy node nor the classy one has an activate to run
        INFO(why);
        REQUIRE(rig.set(beside(R"({"tag":2})")).ok);  // a swap: the classy node's onSettings is its base class's
        CHECK(rig.log.count("classy onSettings 2") == 1);
        CHECK(rig.scripts.throwCount() == 1);
        rig.reclaim();
        CHECK(rig.scripts.firstThrow() == "rp.toy.hollow threw in create: TypeError: create returned no process function");
        CHECK(trapsHit(rig.log).empty());
    }
}

TEST_CASE("the host reads a script node's process and hooks through a getter with the object create returned as `this`, and "
          "through a Proxy as JS reads one, so a wrapper or a test double whose get trap answers them runs",
          "[script][realm][hooks]") {
    struct Case {
        const char* type;
        const char* at;
        const char* process;  // what its process says
        const char* swapped;  // what its onSettings says for the tag 1
    };
    for (const Case& c : {Case{"rp.toy.handled", "", "handled process", "handled onSettings 1"},
                          Case{"rp.toy.wrapped", "self", "wrapped process true", "wrapped onSettings 1"},
                          Case{"rp.toy.wrapped", "proto", "wrapped process true", "wrapped onSettings 1"}}) {
        INFO(c.type << " " << c.at);
        ScriptRig rig(kRealm);
        const auto graph = [&](int tag) {
            const std::string at = *c.at ? std::string(R"("at":")") + c.at + "\"," : "";
            return doc(node("n", c.type, "{" + at + R"("tag":)" + std::to_string(tag) + "}"), "");
        };
        REQUIRE(rig.set(graph(0)).ok);
        rig.run(64, 2);
        CHECK_FALSE(rig.host->isFaulted("n"));
        CHECK(rig.log.count(c.process) == 2);
        REQUIRE(rig.set(graph(1)).ok);  // a swap, which runs its onSettings
        CHECK(rig.log.count(c.swapped) == 1);
        rig.reclaim();
        INFO(rig.scripts.firstThrow());
        CHECK(rig.scripts.throwCount() == 0);
    }
}

TEST_CASE("a script node whose prototype chain cycles through a Proxy, or runs past ScriptHost::kMethodHops plain objects, fails "
          "its create and faults, holding the block thread no longer than a create takes",
          "[script][realm][hooks]") {
    // A hang in either section is the regression: the read of its hooks walked the chain for ever, and since that walk
    // calls no JS, the call deadline never stopped it.
    ScriptRig rig(kRealm);
    SECTION("a cycle through a Proxy") {
        REQUIRE(rig.set(doc(node("l", "rp.toy.looped") + "," + node("c", "rp.toy.classy"), "")).ok);
        rig.run(64, 2);
        CHECK(rig.host->isFaulted("l"));
        CHECK(rig.log.count("looped process") == 0);
        CHECK_FALSE(rig.host->isFaulted("c"));  // the context is as usable after it as before
        CHECK(rig.log.count("classy process") == 2);
        CHECK(rig.scripts.throwCount() == 1);
        rig.reclaim();
        INFO(rig.scripts.firstThrow());
        CHECK(rig.scripts.firstThrow().find("rp.toy.looped threw in create: RangeError: ") == 0);
    }
    SECTION("a chain one past kMethodHops") {
        const auto deep = [](const char* id, int depth) {
            return node(id, "rp.toy.deep", R"({"depth":)" + std::to_string(depth) + "}");
        };
        REQUIRE(rig.set(doc(deep("at", ScriptHost::kMethodHops) + "," + deep("past", ScriptHost::kMethodHops + 1), "")).ok);
        rig.run(64, 2);
        CHECK_FALSE(rig.host->isFaulted("at"));  // its process is on the last object the walk reads
        CHECK(rig.host->isFaulted("past"));
        CHECK(rig.log.count("deep process") == 2);  // the first node's, a block each
        CHECK(rig.scripts.throwCount() == 1);
        rig.reclaim();
        CHECK(rig.scripts.firstThrow() == "rp.toy.deep threw in create: RangeError: the prototype chain is longer than " +
                                              std::to_string(ScriptHost::kMethodHops) + " objects");
    }
}

TEST_CASE("the Block prototype every script node shares, and its methods, are frozen, so no node replaces one for another",
          "[script][trap][realm][block]") {
    ScriptRig rig(kRealm);
    REQUIRE(rig.set(doc(node("v", "rp.toy.vandal"), "")).ok);
    rig.run();
    CHECK(rig.log.count("intact") == 1);
    runTidy(rig, node("v", "rp.toy.vandal"), "");  // its messages, control, audio, send and emit are the host's
    CHECK(trapsHit(rig.log).empty());
}

TEST_CASE("a script node's create and onSettings receive its settings as their JSON form", "[script][settings]") {
    ScriptRig rig(R"JS(
const base = rp.s.object({ name: rp.s.string("abc"), tags: rp.s.array(rp.s.string(""), ["a"]) });
rp.register([rp.defineNode({
  id: "rp.toy.transformed",
  // A schema of its own (defineNode takes any with a safeParse and a toJSONSchema): an `s` one's result, transformed.
  settings: {
    safeParse(raw) {
      const parsed = base.safeParse(raw);
      if (!parsed.success) return parsed;
      return { success: true, data: { name: parsed.data.name.length,      // a number: JSON keeps it
                                      tags: new Set(parsed.data.tags) } }; // a Set: JSON makes it {}
    },
    toJSONSchema: (opts) => base.toJSONSchema(opts),
  },
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

TEST_CASE("faultOnThrow covers a script node's activate: a re-activation whose activate threw faults it at its next "
          "block, and without it the throw is counted and it runs on",
          "[script][fault-on-throw][activate]") {
    ScriptRig rig(R"JS(
const touchy = (id, extra) => rp.defineNode({
  id, ...extra,
  ports: { out: rp.events.out() },
  create: () => ({
    process(b) { b.send("out", 0, [1]); },
    activate(sampleRate) { if (sampleRate === 44100) throw new Error("not at 44100"); },
  }),
});
rp.register([touchy("rp.toy.touchy", { faultOnThrow: true }), touchy("rp.toy.unfazed", {})]);
)JS");
    REQUIRE(rig.set(doc(node("t", "rp.toy.touchy") + "," + node("u", "rp.toy.unfazed") + "," +
                            node("pt", "rp.test.probe") + "," + node("pu", "rp.test.probe"),
                        wire("t.out", "pt.events") + "," + wire("u.out", "pu.events")))
                .ok);
    rig.run();
    REQUIRE(rig.node<Probe>("pt").events.size() == 1);
    std::string why;
    const bool reactivated = rig.host->reactivate(44100.0, 256, &why);
    INFO(why);
    CHECK(reactivated);  // a throw is no refusal: the re-activation stands
    CHECK(rig.scripts.throwCount() == 2);
    rig.run(64, 2);
    CHECK(rig.host->isFaulted("t"));
    CHECK(rig.node<Probe>("pt").events.empty());
    CHECK_FALSE(rig.host->isFaulted("u"));
    CHECK(rig.node<Probe>("pu").events.size() == 1);
    CHECK(rig.scripts.throwCount() == 2);
}

TEST_CASE("a released script instance's cycles are collected when it is released quiescent and when the audio thread hands the "
          "runtime back, never by a block",
          "[script][gc]") {
    ScriptRig rig(R"JS(
rp.register([rp.defineNode({
  id: "rp.toy.cyclic",
  settings: rp.s.object({ tag: rp.s.string("cyclic") }),
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
  settings: rp.s.object({ spinOn: rp.s.enum(["every", "odd"], "every") }),
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
  id: "rp.toy.stalled",  // no faultOnThrow, as the production nodes
  ports: { out: rp.events.out() },
  create: () => {
    let n = 0;
    // Past the 10000 polls between two of the deadline's checks, so each loop holds one; a few ms of CPU at most, far
    // under the deadline even under a sanitizer.
    const work = () => { let x = 0; for (let i = 0; i < 15000; ++i) x += i; return x; };
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
    // In the bundle's order: the MIDI routing, each role's protocol node and the controller session, which buildGraph
    // places (ProtocolNodes.test.cpp holds them to what the DSP kernel they replaced was recorded sending).
    const std::vector<std::string> ids{"rp.midi.route", "rp.mgb.midi",  "rp.n8.midi-to-fifo",   "rp.risa.sync",
                                       "rp.sms.sync",   "rp.lsdj.sync", "rp.controller.session"};
    REQUIRE(host.package()->node_type_count == ids.size());
    for (std::size_t i = 0; i < ids.size(); ++i) CHECK(std::string(host.package()->node_types[i]->id) == ids[i]);
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

TEST_CASE("what a bundle's load leaves in cycles is collected by the first quiescent reclaim, once", "[script][gc]") {
    // The production bundle has left none since B3b took zod and the DSP kernel out of it, so a chunk loaded after it
    // leaves one: an array holding itself and 8 KiB, which its top level drops.
    const std::vector<std::uint8_t> cyclic =
        rp::host::test::compileToys("(function () { const a = [new Float64Array(1024)]; a.push(a); })();");
    ScriptHost host;
    const ScriptHost::Chunk chunks[] = {ScriptHost::embeddedBundle(), cyclic};
    REQUIRE(host.load(chunks));
    const auto loaded = host.audioAllocations();
    host.reclaim();  // no instance released yet: the load's cycles alone
    const auto first = host.audioAllocations();
    CHECK(first.frees > loaded.frees);
    // No GC runs (nothing released since); the count alone could not tell that from a pass that freed nothing.
    host.reclaim();
    CHECK(host.audioAllocations().frees == first.frees);
}

#ifdef RETROPLUG_PROFILE
namespace {

// Script nodes whose process calls end each way one can: a pass-through that reads its input (a marshal span inside its
// js-call), one that does nothing, a throw, a hang the deadline stops, and one that freezes its Block in its first
// block, so every later block's marshalling is refused before its JS runs.
const char* const kSpanned = R"JS(
rp.register([
  rp.defineNode({ id: "rp.toy.span-pass", ports: { in: rp.events.in(), out: rp.events.out() },
                  create: () => ({
                    process(b) {
                      const ms = b.messages("in");
                      for (let i = 0; i < ms.length; ++i) b.send("out", ms[i].frame, ms[i].data);
                    },
                  }) }),
  rp.defineNode({ id: "rp.toy.span-idle", ports: { out: rp.events.out() }, create: () => ({ process() {} }) }),
  rp.defineNode({ id: "rp.toy.span-throw", ports: { out: rp.events.out() },
                  create: () => ({ process() { throw new Error("mid-call"); } }) }),
  rp.defineNode({ id: "rp.toy.span-hang", ports: { out: rp.events.out() }, create: () => ({ process() { for (;;) {} } }) }),
  rp.defineNode({ id: "rp.toy.span-frozen", ports: { out: rp.events.out() },
                  create: () => ({ process(b) { Object.freeze(b); } }) }),
]);
)JS";

// More types than the trace's name table has labels for: with the test bundle's own, the first 1007 of these get one.
const char* const kMany = R"JS(
rp.register(Array.from({ length: 1100 }, (_, i) =>
  rp.defineNode({ id: "rp.toy.many-" + i, ports: { out: rp.events.out() }, create: () => ({ process() {} }) })));
)JS";

std::vector<rp::host::DspTraceSpan> spansOf(const ScriptHost& h, std::uint32_t label) {
    std::vector<rp::host::DspTraceSpan> out;
    for (const auto& s : h.traceSpans())
        if (s.label == label) out.push_back(s);
    return out;
}

// The label a trace names `type`'s process spans by, or kDspSpanLabels when it names none.
std::uint32_t labelOf(const ScriptHost& h, const std::string& type) {
    const auto names = h.traceNames();
    const auto at = std::find(names.begin(), names.end(), type);
    return at == names.end() ? rp::host::kDspSpanLabels : static_cast<std::uint32_t>(at - names.begin());
}

// Each of `inner` inside one of `outer`.
bool nested(const std::vector<rp::host::DspTraceSpan>& inner, const std::vector<rp::host::DspTraceSpan>& outer) {
    return std::all_of(inner.begin(), inner.end(), [&](const auto& i) {
        return std::any_of(outer.begin(), outer.end(), [&](const auto& o) { return o.t0 <= i.t0 && i.t1 <= o.t1; });
    });
}

// How many of `inner` stand inside one of `outer`.
std::size_t countInside(const std::vector<rp::host::DspTraceSpan>& inner, const std::vector<rp::host::DspTraceSpan>& outer) {
    std::size_t n = 0;
    for (const auto& i : inner) n += nested({i}, outer) ? 1 : 0;
    return n;
}

}  // namespace

TEST_CASE("a script node's process span is labelled by its type, holds marshal and then js-call, and closes every way out",
          "[script][profile][trace]") {
    struct Case {
        const char*   type;
        bool          fed;  // a message into its input every block
        std::size_t   calls, jsCalls, marshals;  // over 3 blocks
    };
    // The hang is stopped twice and faulted by the second (no faultOnThrow), so its third block makes no call. The frozen
    // node's second and third blocks are refused at the Block's frames, before their JS runs: a marshal span and no
    // js-call. The pass-through's marshals are the transport's, before its call, and its message's, inside it.
    for (const Case& c : {Case{"rp.toy.span-pass", true, 3, 3, 6}, Case{"rp.toy.span-throw", false, 3, 3, 3},
                          Case{"rp.toy.span-hang", false, 2, 2, 2}, Case{"rp.toy.span-frozen", false, 3, 1, 3}}) {
        INFO(c.type);
        ScriptRig rig(kSpanned);
        rig.scripts.setCallDeadline(std::chrono::milliseconds(50));
        REQUIRE(rig.set(c.fed ? doc(node("feed", "rp.test.feed") + "," + node("s", c.type), wire("feed.midi", "s.in"))
                              : doc(node("s", c.type), ""))
                    .ok);
        rig.scripts.traceReset(true);
        for (int i = 0; i < 3; ++i) {
            if (c.fed) rig.node<Feed>("feed").next[Feed::Midi] = {{0, bytes({0x90, 60, 100})}};
            rig.run(64, 1);
        }
        const std::uint32_t label = labelOf(rig.scripts, c.type);
        REQUIRE(label >= rp::host::DSP_SPAN_TYPE_BASE);
        REQUIRE(label < rp::host::kDspSpanLabels);
        const auto process = spansOf(rig.scripts, label);
        const auto call = spansOf(rig.scripts, rp::host::DSP_SPAN_JSCALL);
        const auto marshal = spansOf(rig.scripts, rp::host::DSP_SPAN_MARSHAL);
        CHECK(process.size() == c.calls);
        CHECK(call.size() == c.jsCalls);
        CHECK(marshal.size() == c.marshals);
        CHECK(rig.scripts.traceSpans().size() == process.size() + call.size() + marshal.size());  // the host's alone
        CHECK(nested(call, process));
        CHECK(nested(marshal, process));
        CHECK(countInside(marshal, call) == (c.fed ? 3u : 0u));
        rig.scripts.traceReset(false);
    }
}

TEST_CASE("script nodes of one type share its span label, each type has its own, and the types are labelled in load order",
          "[script][profile][trace]") {
    ScriptRig rig(kSpanned);
    REQUIRE(rig.set(doc(node("feed", "rp.test.feed") + "," + node("a", "rp.toy.span-pass") + "," + node("b", "rp.toy.span-pass") +
                            "," + node("idle", "rp.toy.span-idle"),
                        wire("feed.midi", "a.in") + "," + wire("a.out", "b.in")))
                .ok);
    rig.scripts.traceReset(true);
    for (int i = 0; i < 3; ++i) {
        rig.node<Feed>("feed").next[Feed::Midi] = {{0, bytes({0x90, 60, 100})}};
        rig.run(64, 1);
    }
    // The test bundle's own type first, then this bundle's, in the order it registered them.
    CHECK(labelOf(rig.scripts, "rp.test.script-echo") == rp::host::DSP_SPAN_TYPE_BASE);
    const std::uint32_t pass = labelOf(rig.scripts, "rp.toy.span-pass"), idle = labelOf(rig.scripts, "rp.toy.span-idle");
    CHECK(pass == rp::host::DSP_SPAN_TYPE_BASE + 1);
    CHECK(idle == pass + 1);
    CHECK(labelOf(rig.scripts, "rp.toy.span-frozen") == idle + 3);
    CHECK(spansOf(rig.scripts, pass).size() == 6);  // a and b, each block
    CHECK(spansOf(rig.scripts, idle).size() == 3);
    CHECK(spansOf(rig.scripts, rp::host::DSP_SPAN_SCRIPT_NODE).empty());
    // One node's process after another's, never inside it.
    auto process = spansOf(rig.scripts, pass);
    for (const auto& s : spansOf(rig.scripts, idle)) process.push_back(s);
    std::sort(process.begin(), process.end(), [](const auto& x, const auto& y) { return x.t0 < y.t0; });
    for (std::size_t i = 1; i < process.size(); ++i) CHECK(process[i - 1].t1 <= process[i].t0);
    CHECK(rig.scripts.throwCount() == 0);
    rig.scripts.traceReset(false);
}

TEST_CASE("the profile calls refuse while an audio thread owns the runtime, and the trace reads read nothing",
          "[script][profile][trace]") {
    ScriptRig rig(kSpanned);
    REQUIRE(rig.set(doc(node("s", "rp.toy.span-idle"), "")).ok);
    REQUIRE(rig.scripts.traceReset(true));
    rig.run();
    const auto spans = rig.scripts.traceSpans();
    const auto names = rig.scripts.traceNames();
    REQUIRE_FALSE(spans.empty());
    REQUIRE_FALSE(names.empty());  // the fixed labels, named as the host is made, and the types', as it loads
    rig.scripts.setAudioThreadOwns(true);
    CHECK_FALSE(rig.scripts.resetAllocStats(true));
    CHECK_FALSE(rig.scripts.runGc().enabled);
    CHECK_FALSE(rig.scripts.traceReset(false));
    CHECK(rig.scripts.traceArmed());  // the refused reset left the window as it was
    CHECK(rig.scripts.traceSpans().empty());
    CHECK(rig.scripts.traceNames().empty());
    CHECK(rig.scripts.allocStats().enabled);  // which never refuses: a benchmark reads enabled to know a profile host
    rig.scripts.setAudioThreadOwns(false);
    CHECK(rig.scripts.traceSpans().size() == spans.size());  // there all along
    CHECK(rig.scripts.traceNames() == names);
    CHECK(rig.scripts.resetAllocStats(false));
    CHECK(rig.scripts.runGc().enabled);
    CHECK(rig.scripts.traceReset(false));
}

TEST_CASE("a type past the trace's name table is traced under the shared script-node label, and the table stays bounded",
          "[script][profile][trace]") {
    ScriptRig rig(kMany);
    REQUIRE(rig.scripts.package()->node_type_count == 1101);  // the test bundle's echo, and these
    REQUIRE(rig.set(doc(node("first", "rp.toy.many-0") + "," + node("last", "rp.toy.many-1006") + "," +
                            node("past", "rp.toy.many-1007") + "," + node("far", "rp.toy.many-1099"),
                        ""))
                .ok);
    rig.scripts.traceReset(true);
    rig.run(64, 2);
    const auto names = rig.scripts.traceNames();
    REQUIRE(names.size() == rp::host::kDspSpanLabels);
    CHECK(names[rp::host::DSP_SPAN_SCRIPT_NODE] == "script-node");
    CHECK(names[rp::host::DSP_SPAN_APU] == "apu-render");
    CHECK(labelOf(rig.scripts, "rp.toy.many-0") == rp::host::DSP_SPAN_TYPE_BASE + 1);
    CHECK(labelOf(rig.scripts, "rp.toy.many-1006") == rp::host::kDspSpanLabels - 1);
    CHECK(labelOf(rig.scripts, "rp.toy.many-1007") == rp::host::kDspSpanLabels);  // named by no label
    CHECK(spansOf(rig.scripts, rp::host::DSP_SPAN_TYPE_BASE + 1).size() == 2);
    CHECK(spansOf(rig.scripts, rp::host::kDspSpanLabels - 1).size() == 2);
    CHECK(spansOf(rig.scripts, rp::host::DSP_SPAN_SCRIPT_NODE).size() == 4);  // past's and far's, each block
    rig.scripts.traceReset(false);
}
#endif
