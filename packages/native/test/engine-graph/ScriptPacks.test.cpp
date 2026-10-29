// Script node packs (docs/modular-graph-phase-c1-design.md sections 4 to 7): a pack Engine::loadScriptPack loads runs
// beside the built-in pack's nodes in one document, in runtimes of its own, so what one pack does to its realm (a
// getter on Object.prototype, Array.prototype.push replaced, a global) no node of another pack sees, its memory limit
// and its promise jobs fault only its own nodes, and it loads while an audio thread runs blocks. Its id (never one of
// the app's, "rp" and under), its types' namespace, its source and its definitions are checked, and a refusal changes
// nothing. The control thread's calls into a pack, its load and its settings checks, are held to the call deadline on
// a clock of their own. Driven on an Engine, whose built-in pack is the production bundle, the toy packs inline JS
// source; and on a ScriptRig, where the host's binder, its allocation counts and the built-in pack's deadline are what
// is checked.
//
// Run via `pnpm test:plugin engine-graph`, or `build/bin/retroplug-engine-graph-test "[packs]"`.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <compare>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "GraphDocs.hpp"
#include "ScriptRig.hpp"
#include "graph/Descriptor.hpp"
#include "host/engine/Engine.hpp"
#include "host/script/ScriptHost.hpp"

using rp::host::ScriptHost;
using rp::host::test::DocExtras;
using rp::host::test::docNode;
using rp::host::test::docWire;
using rp::host::test::graphRequest;
using rp::host::test::stereoDocument;

namespace {

constexpr double        kRate = 48000.0;
constexpr std::uint32_t kFrames = 256;

// What every toy pack's source starts with. The production bundle, which is every Engine's built-in pack, carries no
// authoring layer, so a toy defines its nodes as plain objects in the shape defineNode makes
// (packages/retroplug/src/graph/defineNode.ts: id, descriptor(), parseSettings, layout, create and the flags) and
// appends them to the global the script host reads (packages/retroplug/src/script/register.ts). A node's ports are its
// layout whatever its settings.
const std::string kToy = R"JS(
var toy = (function () {
  var nodes = globalThis.__rp_scriptNodes = globalThis.__rp_scriptNodes || [];
  return {
    define: function (id, ports, create) {
      var layout = { ports: ports, params: [] };
      nodes[nodes.length] = {  // not a push: a pack may replace Array.prototype.push
        id: id,
        descriptor: function () { return { id: id, ports: ports, params: [], settings: { type: "object" } }; },
        parseSettings: function (raw) { return raw === undefined ? {} : raw; },
        layout: function () { return layout; },
        create: create,
        reshapes: false,
        faultOnThrow: false,
      };
    },
    input: function (id) { return { id: id, dir: "in", kind: "events" }; },
    output: function (id) { return { id: id, dir: "out", kind: "events" }; },
  };
})();
)JS";

// A toy pack's source: kToy, then `body`.
std::string toys(const std::string& body) { return kToy + body; }

// A node, `id`, that sends each message it gets on with its second byte moved by `by`.
std::string transposer(const std::string& id, int by) {
    return "toy.define('" + id + "', [toy.input('in'), toy.output('out')], function () {\n"
           "  return { process: function (b) {\n"
           "    var ms = b.messages('in');\n"
           "    for (var i = 0; i < ms.length; i++) {\n"
           "      var d = ms[i].data;\n"
           "      b.send('out', ms[i].frame, [d[0], d[1] + (" + std::to_string(by) + "), d[2]]);\n"
           "    }\n"
           "  } };\n"
           "});\n";
}

// A node, `id`, that sends its block count every block, and on its second runs `second` (JS statements, with `kept`
// an array it holds for its life).
std::string counter(const std::string& id, const std::string& second) {
    return "toy.define('" + id + "', [toy.output('out')], function () {\n"
           "  var n = 0, kept = [];\n"
           "  return { process: function (b) {\n"
           "    b.send('out', 0, [++n & 0x7f]);\n"
           "    if (n === 2) { " + second + " }\n"
           "  } };\n"
           "});\n";
}

// A document of `nodes` and `wires` beside the host's stereo pair, placing no system.
Engine::GraphRequest document(std::vector<std::string> nodes, std::vector<std::string> wires) {
    return graphRequest(stereoDocument({}, DocExtras{std::move(nodes), std::move(wires)}), {});
}

// rp.host.midi-out with an input per system in `systems`, in1 up, each sending under its system.
std::string midiOut(const std::string& systems) {
    return docNode("midi-out", "rp.host.midi-out", R"({"systems":[)" + systems + "]}");
}

// The built-in pack's MIDI routing, in `mode`, with an output per system in `systems` (sys-<id>).
std::string routing(const std::string& mode, const std::string& systems) {
    return docNode("routing", "rp.midi.route", R"({"mode":")" + mode + R"(","systems":[)" + systems + "]}");
}

struct Out {
    std::uint32_t             system = 0, frame = 0;
    std::vector<std::uint8_t> data;
    auto operator<=>(const Out&) const = default;
};
using Staged = std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>>;

// One block, `staged` its host MIDI: what the plan sent out, sorted (rp.host.midi-out merges by frame; within one, the
// order is not what these cases hold).
std::vector<Out> block(Engine& e, const Staged& staged = {}) {
    for (const auto& [frame, data] : staged) e.stageMidi(frame, data);
    std::vector<float> l(kFrames), r(kFrames);
    e.processBlock(kFrames, l.data(), r.data());
    std::vector<Out> out;
    for (const auto& m : e.midiOut()) out.push_back({m.system, m.frame, m.data});
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<Out> sorted(std::vector<Out> v) {
    std::sort(v.begin(), v.end());
    return v;
}

// `id` loaded from `source`, required to load (and why not said when it does not).
Engine::ScriptPackResult load(Engine& e, const std::string& id, const std::string& source, std::size_t memoryLimit = 0) {
    auto r = e.loadScriptPack(id, source, memoryLimit);
    UNSCOPED_INFO("loading " << id << ": " << r.error);
    REQUIRE(r.ok);
    return r;
}

// `request` placed, required to plan (and the planner's errors said when it does not).
void place(Engine& e, const Engine::GraphRequest& request) {
    const auto r = e.setGraph(request);
    for (const auto& err : r.errors)
        UNSCOPED_INFO(err.code << " at " << err.nodeId << "." << err.port << ": " << err.message);
    REQUIRE(r.ok);
}

// The engine's packs, each as "<id>#<generation>".
std::vector<std::string> packIds(const Engine& e) {
    std::vector<std::string> ids;
    for (const auto& p : e.scriptPacks()) ids.push_back(p.id + "#" + std::to_string(p.generation));
    return ids;
}

const std::vector<std::uint8_t> kNote{0x90, 60, 100};

}  // namespace

TEST_CASE("a pack's nodes run beside the built-in pack's in one document, and its load lists its types with their "
          "descriptors",
          "[script][packs]") {
    Engine e(kRate);
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1"});
    const std::size_t builtIn = e.nodeTypeCount();
    const auto loaded = load(e, "test.shift", toys(transposer("test.shift.up", 12) + transposer("test.shift.down", -12)));
    CHECK(loaded.error.empty());
    CHECK(loaded.errors.empty());
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.shift#1"});
    CHECK(e.nodeTypeCount() == builtIn + 2);
    // Its types in registration order, each with its descriptor as the registry holds it: canonical, every field
    // spelled (the name, never given, is the id).
    REQUIRE(loaded.types.size() == 2);
    CHECK(loaded.types[0].id == "test.shift.up");
    CHECK(loaded.types[1].id == "test.shift.down");
    for (const auto& t : loaded.types) {
        INFO(t.descriptor);
        const auto d = rp::graph::parseDescriptor(t.descriptor);
        REQUIRE(d);
        CHECK(d->id == t.id);
        CHECK(d->name == t.id);
        REQUIRE(d->layout.ports.size() == 2);
        CHECK(d->layout.ports[0].id == "in");
        CHECK(d->layout.ports[0].dir == rp::graph::PortDir::In);
        CHECK(d->layout.ports[0].kind == rp::graph::PortKind::Events);
        CHECK(d->layout.ports[1].id == "out");
        CHECK(d->layout.ports[1].dir == rp::graph::PortDir::Out);
        CHECK(t.descriptor == rp::graph::descriptorJson(*d));
    }

    // Host MIDI through the built-in routing, by channel: channel 0 to system 1 through the pack's node, which moves its
    // note up an octave, and channel 1 to system 2 straight, its channel rewritten to 0 (the routing's own job).
    place(e, document({docNode("midi-in", "rp.host.midi-in"), routing("midiChannelToInstance", "1,2"),
                       docNode("up", "test.shift.up"), midiOut("1,2")},
                      {docWire("midi-in.midi", "routing.midiIn"), docWire("routing.sys-1", "up.in"),
                       docWire("up.out", "midi-out.in1"), docWire("routing.sys-2", "midi-out.in2")}));
    CHECK(block(e, {{3, {0x90, 60, 100}}, {5, {0x91, 64, 90}}}) ==
          sorted({{1, 3, {0x90, 72, 100}}, {2, 5, {0x90, 64, 90}}}));
    CHECK(block(e).empty());
    CHECK(block(e, {{0, {0x80, 61, 0}}}) == std::vector<Out>{{1, 0, {0x80, 73, 0}}});
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

TEST_CASE("what one pack does to its realm reaches no node of another: a getter on Object.prototype, Array.prototype.push "
          "replaced, a global",
          "[script][packs]") {
    // Each pack's probe sends what it sees as a SysEx: whether a property only pack A's getter answers is there,
    // whether a push pushes, and whether pack A's global is set.
    const std::string probe = R"JS(
var probe = function (id) {
  toy.define(id, [toy.output("out")], function () {
    return { process: function (b) {
      var getter = ({}).onlyAGetterAnswers === undefined ? 0 : 1;
      var a = [], pushed = a.push(5);
      var push = pushed === 1 && a.length === 1 && a[0] === 5 ? 1 : 0;
      var global = typeof globalThis.leakedByA === "undefined" ? 0 : 1;
      b.send("out", 0, [0xF0, getter, push, global, 0xF7]);
    } };
  });
};
)JS";
    Engine e(kRate);
    load(e, "test.a", toys(probe + R"JS(
probe("test.a.probe");
Object.defineProperty(Object.prototype, "onlyAGetterAnswers", { get: function () { return 7; }, configurable: true });
Array.prototype.push = function () { return -1; };
globalThis.leakedByA = 42;
)JS"));
    load(e, "test.b", toys(probe + R"JS(probe("test.b.probe");)JS"));
    // The built-in routing beside them, placed after both loads, by channel: its create builds its rewritten views with
    // a push, so a realm pack A had reached would leave it none to send channel 1's note with.
    place(e, document({docNode("midi-in", "rp.host.midi-in"), routing("midiChannelToInstance", "3,4"),
                       docNode("a", "test.a.probe"), docNode("b", "test.b.probe"), midiOut("1,2,4")},
                      {docWire("midi-in.midi", "routing.midiIn"), docWire("a.out", "midi-out.in1"),
                       docWire("b.out", "midi-out.in2"), docWire("routing.sys-4", "midi-out.in3")}));
    for (int n = 0; n < 3; ++n) {
        INFO("block " << n);
        // Pack A's probe sees all three in its own realm; pack B's, none.
        CHECK(block(e, {{7, {0x91, 64, 100}}}) ==
              sorted({{1, 0, {0xF0, 1, 0, 1, 0xF7}}, {2, 0, {0xF0, 0, 1, 0, 0xF7}}, {4, 7, {0x90, 64, 100}}}));
    }
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

namespace {

// `id` from `source` refused, `why` in the error, changing nothing: no pack added, the registry as it was, and a
// document naming `type` (one the source defines, or would) refused as naming no type.
void refused(Engine& e, const std::string& id, const std::string& source, const std::string& why, const std::string& type) {
    const auto packsBefore = packIds(e);
    const std::size_t typesBefore = e.nodeTypeCount();
    const auto r = e.loadScriptPack(id, source);
    INFO(id << ": " << r.error);
    CHECK_FALSE(r.ok);
    CHECK(r.error.find(why) != std::string::npos);
    CHECK(r.types.empty());
    CHECK(r.errors.empty());
    CHECK(packIds(e) == packsBefore);
    CHECK(e.nodeTypeCount() == typesBefore);
    if (type.empty()) return;
    const auto placed = e.setGraph(document({docNode("n", type)}, {}));
    CHECK_FALSE(placed.ok);
    REQUIRE_FALSE(placed.errors.empty());
    CHECK(placed.errors[0].code == "unknown-type");
}

}  // namespace

TEST_CASE("a pack refused changes nothing: a non-plain id, the built-in pack's, one of the app's, one loaded, a type "
          "outside its namespace, a compile error, a definition that cannot be read, one the registry refuses",
          "[script][packs][refused]") {
    Engine e(kRate);
    refused(e, "test pack", toys(transposer("test pack.up", 12)), "must be non-empty, with no whitespace", "test pack.up");
    refused(e, "rp.script", toys(transposer("rp.script.up", 12)), "rp.script is the built-in pack, which is not reloadable",
            "rp.script.up");
    // "rp" and the ids under it are the app's to give: its built-in pack's and its native packages' (rp.core, rp.host,
    // ...), and a pack named "rp.midi" would hold the built-in rp.midi.route's namespace.
    for (const std::string app : {"rp", "rp.mine", "rp.core", "rp.midi"}) {
        INFO(app);
        refused(e, app, toys(transposer(app + ".up", 1)), "is the app's to give: \"rp\" and every id under \"rp.\"",
                app + ".up");
    }

    load(e, "test.once", toys(transposer("test.once.first", 1)));
    refused(e, "test.once", toys(transposer("test.once.second", 2)), "pack \"test.once\" is already loaded",
            "test.once.second");

    // A type id is the pack's id, a dot and a name: not another prefix, the bare id, or the id and a dot.
    for (const std::string stray : {"test.other.stray", "test.nsx.stray", "test.ns", "test.ns."}) {
        INFO(stray);
        refused(e, "test.ns", toys(transposer("test.ns.fine", 1) + transposer(stray, 2)),
                "(" + stray + ") is outside pack \"test.ns\": its type ids are \"test.ns.<name>\"", "test.ns.fine");
    }

    // The compile error's line, in the source as given (no toy prelude here).
    refused(e, "test.syntax", "var a = 1;\nvar b = 2;\nvar c = ;\n", "the source does not compile: line 3: SyntaxError: ",
            "test.syntax.up");
    refused(e, "test.syntax",
            toys("toy.define('test.syntax.up', [toy.output('out')], function () {\n  return { process: function (b) {\n"),
            "SyntaxError", "test.syntax.up");

    // A definition with no descriptor: the pack's other types go with it.
    refused(e, "test.nodesc", toys(transposer("test.nodesc.fine", 1) + R"JS(
globalThis.__rp_scriptNodes[globalThis.__rp_scriptNodes.length] = {
  id: "test.nodesc.bare", parseSettings: function (r) { return r || {}; }, layout: function () { return { ports: [] }; },
  create: function () { return { process: function () {} }; },
};
)JS"),
            "test.nodesc.bare: its descriptor: the definition has no descriptor", "test.nodesc.fine");
    // A top level that throws.
    refused(e, "test.throws", toys(transposer("test.throws.up", 1) + "throw new Error('not today');"),
            "chunk 0: Error: not today", "test.throws.up");

    // What the registry refuses, once the pack is built: a descriptor the planner could not use, and a type another
    // pack holds, which two packs' namespaces can both hold where one's id runs on into the other's ("test.nest" and
    // "test.nest.x" can both name "test.nest.x.up"). The pack goes; the type stays the first pack's.
    refused(e, "test.baddesc", toys(R"JS(
toy.define("test.baddesc.odd", [{ id: "in", dir: "in", kind: "smoke" }], function () {
  return { process: function () {} };
});
)JS"),
            "the registry refused the pack's types: ", "test.baddesc.odd");
    load(e, "test.nest", toys(transposer("test.nest.x.up", 1)));
    refused(e, "test.nest.x", toys(transposer("test.nest.x.up", 2)), "node type \"test.nest.x.up\" is already registered",
            "");
    place(e, document({docNode("midi-in", "rp.host.midi-in"), docNode("nest", "test.nest.x.up"), midiOut("1")},
                      {docWire("midi-in.midi", "nest.in"), docWire("nest.out", "midi-out.in1")}));
    CHECK(block(e, {{2, kNote}}) == std::vector<Out>{{1, 2, {0x90, 61, 100}}});  // the first pack's: a semitone, not two

    // Each id refused once its pack was built loads clean now: neither the script host nor the registry kept it.
    load(e, "test.ns", toys(transposer("test.ns.fine", 1)));
    load(e, "test.syntax", toys(transposer("test.syntax.up", 1)));
    load(e, "test.nodesc", toys(transposer("test.nodesc.fine", 1)));
    load(e, "test.baddesc", toys(transposer("test.baddesc.odd", 1)));
    load(e, "test.nest.x", toys(transposer("test.nest.x.down", -1)));
    // And an id JSON must escape is its package's id as it is: its manifest spells it as JSON does.
    const auto quoted = load(e, "test\"q", toys(transposer("test\"q.up", 1)));
    REQUIRE(quoted.types.size() == 1);
    CHECK(quoted.types[0].id == "test\"q.up");
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.once#1", "test.nest#1", "test.ns#1",
                                                 "test.syntax#1", "test.nodesc#1", "test.baddesc#1", "test.nest.x#1",
                                                 "test\"q#1"});

    // The pack that was loaded when a second load of its id was refused runs its own code.
    place(e, document({docNode("midi-in", "rp.host.midi-in"), docNode("once", "test.once.first"), midiOut("1")},
                      {docWire("midi-in.midi", "once.in"), docWire("once.out", "midi-out.in1")}));
    CHECK(block(e, {{2, kNote}}) == std::vector<Out>{{1, 2, {0x90, 61, 100}}});
    CHECK(e.scriptThrows() == 0);
}

TEST_CASE("a pack's node that allocates past its pack's memory limit faults, while another pack's node and a built-in one "
          "run on",
          "[script][packs][memory]") {
    Engine e(kRate);
    // Pack A held to 16 MiB, its node reaching for 32 on its second block; pack B at the default, its node taking 24 and
    // keeping them, more than pack A may hold.
    constexpr std::size_t kLimit = std::size_t{16} << 20;
    const auto mebibytes = [](int n) {
        return "for (var k = 0; k < " + std::to_string(n) + "; k++) kept[k] = new Float64Array(1 << 17);";
    };
    load(e, "test.hog", toys(counter("test.hog.grows", mebibytes(32))), kLimit);
    load(e, "test.big", toys(counter("test.big.holds", mebibytes(24))));
    place(e, document({docNode("midi-in", "rp.host.midi-in"), routing("sendToAll", "3"), docNode("hog", "test.hog.grows"),
                       docNode("big", "test.big.holds"), midiOut("1,2,3")},
                      {docWire("midi-in.midi", "routing.midiIn"), docWire("hog.out", "midi-out.in1"),
                       docWire("big.out", "midi-out.in2"), docWire("routing.sys-3", "midi-out.in3")}));
    CHECK(block(e, {{4, kNote}}) == sorted({{1, 0, {1}}, {2, 0, {1}}, {3, 4, kNote}}));
    // The hog runs out: faulted at once, so its block is silent, its throw the out-of-memory error it let go.
    CHECK(block(e, {{4, kNote}}) == sorted({{2, 0, {2}}, {3, 4, kNote}}));
    CHECK(e.faultCount() == 1);
    CHECK(e.scriptThrows() == 1);
    for (std::uint8_t n = 3; n <= 6; ++n) {
        INFO("block " << int{n});
        CHECK(block(e, {{4, kNote}}) == sorted({{2, 0, {n}}, {3, 4, kNote}}));
    }
    CHECK(e.faultCount() == 1);
    CHECK(e.scriptThrows() == 1);
}

TEST_CASE("a pack's node whose promise chain never settles faults after its two cut shorts, while another pack's node, its "
          "own promises settling, is charged nothing",
          "[script][packs][jobs]") {
    Engine e(kRate);
    load(e, "test.chain", toys(R"JS(
toy.define("test.chain.endless", [toy.output("out")], function () {
  var n = 0, started = false;
  var link = function () { Promise.resolve().then(link); };
  return { process: function (b) {
    b.send("out", 0, [++n & 0x7f]);
    if (!started) { started = true; link(); }
  } };
});
)JS"));
    // Each block it sends its count and what its last block's reaction settled, which ran at the checkpoint after it.
    load(e, "test.calm", toys(R"JS(
toy.define("test.calm.settles", [toy.output("out")], function () {
  var n = 0, settled = 0;
  return { process: function (b) {
    b.send("out", 0, [++n & 0x7f, settled & 0x7f]);
    Promise.resolve(n).then(function (v) { settled = v; });
  } };
});
)JS"));
    // The chain's node first, so the other's checkpoint follows its leftovers in every block: a pack-mate's would run them.
    place(e, document({docNode("midi-in", "rp.host.midi-in"), docNode("chain", "test.chain.endless"),
                       docNode("calm", "test.calm.settles"), routing("sendToAll", "3"), midiOut("1,2,3")},
                      {docWire("midi-in.midi", "routing.midiIn"), docWire("chain.out", "midi-out.in1"),
                       docWire("calm.out", "midi-out.in2"), docWire("routing.sys-3", "midi-out.in3")}));
    CHECK(block(e, {{9, kNote}}) == sorted({{1, 0, {1}}, {2, 0, {1, 0}}, {3, 9, kNote}}));
    CHECK(e.scriptThrows() == 1);  // the chain cut short once: its throw
    CHECK(e.faultCount() == 0);
    CHECK(block(e, {{9, kNote}}) == sorted({{2, 0, {2, 1}}, {3, 9, kNote}}));  // cut short again: faulted, its block silent
    CHECK(e.scriptThrows() == 2);
    CHECK(e.faultCount() == 1);
    for (std::uint8_t n = 3; n <= 8; ++n) {
        INFO("block " << int{n});
        CHECK(block(e, {{9, kNote}}) == sorted({{2, 0, {n, static_cast<std::uint8_t>(n - 1)}}, {3, 9, kNote}}));
    }
    CHECK(e.scriptThrows() == 2);
    CHECK(e.faultCount() == 1);
}

TEST_CASE("a pack loads while an audio thread runs blocks, and the plan that places its node runs it on that thread",
          "[script][packs][threads]") {
    Engine e(kRate);
    place(e, document({docNode("midi-in", "rp.host.midi-in"), routing("sendToAll", "1"), midiOut("1")},
                      {docWire("midi-in.midi", "routing.midiIn"), docWire("routing.sys-1", "midi-out.in1")}));
    e.setAudioThreadOwns(true);
    // The audio thread: each block stages a note, which the built-in routing's script node passes on, and counts the
    // blocks whose MIDI out holds it as it was (system 1) and moved up an octave (system 2), which only the pack's node
    // does.
    std::atomic<bool> running{true};
    std::atomic<int>  moved{0}, plain{0};
    std::thread       audio([&] {
        std::vector<float> l(kFrames), r(kFrames);
        while (running.load()) {
            e.drainCommands();
            e.stageMidi(0, kNote);
            e.processBlock(kFrames, l.data(), r.data());
            for (const auto& m : e.midiOut()) {
                if (m.system == 2 && m.data == std::vector<std::uint8_t>{0x90, 72, 100}) moved.fetch_add(1);
                if (m.system == 1 && m.data == kNote) plain.fetch_add(1);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });
    const auto waitFor = [](const std::atomic<int>& count, int n) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (count.load() < n && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return count.load() >= n;
    };
    const bool started = waitFor(plain, 10);  // the routing's JS runs in the built-in pack's audio runtime meanwhile
    // The pack's top level runs past QuickJS's interrupt poll many times over in each runtime, while the block thread
    // is making calls into the built-in pack's: a handler polled in the load would share the deadline state of those
    // calls (ThreadSanitizer's case), which is why a pack joins it only once loaded.
    const auto loaded =
        e.loadScriptPack("test.live", toys("for (var i = 0; i < 300000; i++) {}\n" + transposer("test.live.up", 12)));
    INFO(loaded.error);
    const auto request = document({docNode("midi-in", "rp.host.midi-in"), routing("sendToAll", "1,2"),
                                   docNode("up", "test.live.up"), midiOut("1,2")},
                                  {docWire("midi-in.midi", "routing.midiIn"), docWire("routing.sys-1", "midi-out.in1"),
                                   docWire("routing.sys-2", "up.in"), docWire("up.out", "midi-out.in2")});
    bool placed = false;
    std::string refusal;
    for (int attempt = 0; loaded.ok && !placed && attempt < 1000; ++attempt) {
        const auto r = e.setGraph(request);
        placed = r.ok;
        if (!r.ok) {
            refusal = r.errors.empty() ? "" : r.errors[0].code + ": " + r.errors[0].message;
            if (refusal.rfind("busy", 0) != 0) break;  // busy is the ring full, which the audio thread drains: again
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        e.reclaim();
    }
    const bool ran = placed && waitFor(moved, 10);
    running = false;
    audio.join();
    e.setAudioThreadOwns(false);
    e.reclaim();
    CHECK(started);
    REQUIRE(loaded.ok);
    INFO(refusal);
    REQUIRE(placed);
    CHECK(ran);
    // And on this thread, quiescent again, the same plan.
    CHECK(block(e, {{6, kNote}}) == sorted({{1, 6, kNote}, {2, 6, {0x90, 72, 100}}}));
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

namespace {

// Three types whose settings check never returns, in parseSettings, in layout, and a third that is fine. Their
// descriptors need neither, so the pack loads, and only a document placing one of the first two asks.
const std::string kStuck = R"JS(
var idle = function () { return { process: function () {} }; };
toy.define("test.stuck.settings", [toy.input("in"), toy.output("out")], idle);
toy.define("test.stuck.layout", [toy.input("in"), toy.output("out")], idle);
var defs = globalThis.__rp_scriptNodes;
defs[defs.length - 2].parseSettings = function () { for (;;) {} };
defs[defs.length - 1].layout = function () { for (;;) {} };
)JS" + transposer("test.stuck.fine", 12);

// The running plan: host MIDI through the built-in routing to the host MIDI out, under system 1, and to `type`, if
// given, whose output goes out under system 2.
Engine::GraphRequest routedTo(const std::string& type = "") {
    const std::string systems = type.empty() ? "1" : "1,2";
    std::vector<std::string> nodes{docNode("midi-in", "rp.host.midi-in"), routing("sendToAll", systems),
                                   midiOut(systems)};
    std::vector<std::string> wires{docWire("midi-in.midi", "routing.midiIn"), docWire("routing.sys-1", "midi-out.in1")};
    if (!type.empty()) {
        nodes.push_back(docNode("node", type));
        wires.push_back(docWire("routing.sys-2", "node.in"));
        wires.push_back(docWire("node.out", "midi-out.in2"));
    }
    return document(std::move(nodes), std::move(wires));
}

// `request` refused because the node's settings check ran past a 50 ms deadline in `where`.
void stoppedCheck(Engine& e, const Engine::GraphRequest& request, const std::string& type, const std::string& where) {
    const auto r = e.setGraph(request);
    CHECK_FALSE(r.ok);
    REQUIRE(r.errors.size() == 1);
    INFO(r.errors[0].message);
    CHECK(r.errors[0].code == "settings-invalid");
    CHECK(r.errors[0].nodeId == "node");
    CHECK(r.errors[0].message == type + "'s " + where + " ran past the 50 ms call deadline (CPU time), and was stopped");
}

constexpr const char* kLoadStopped = "its load ran past the 50 ms call deadline (CPU time), and was stopped";

}  // namespace

TEST_CASE("the control thread's calls into a pack are held to the call deadline: a load that never returns is refused, "
          "and a settings check that never returns refuses its document while the running plan runs on",
          "[script][packs][deadline]") {
    Engine e(kRate);
    e.setScriptCallDeadline(std::chrono::milliseconds(50));
    place(e, routedTo());
    CHECK(block(e, {{2, kNote}}) == std::vector<Out>{{1, 2, kNote}});
    const auto packsBefore = packIds(e);
    const std::size_t typesBefore = e.nodeTypeCount();
    const auto hung = e.loadScriptPack("test.hang", toys(transposer("test.hang.up", 1) + "for (;;) {}"));
    INFO(hung.error);
    CHECK_FALSE(hung.ok);
    CHECK(hung.error.rfind(kLoadStopped, 0) == 0);
    CHECK(packIds(e) == packsBefore);
    CHECK(e.nodeTypeCount() == typesBefore);
    CHECK(block(e, {{2, kNote}}) == std::vector<Out>{{1, 2, kNote}});

    load(e, "test.stuck", toys(kStuck));
    stoppedCheck(e, routedTo("test.stuck.settings"), "test.stuck.settings", "parseSettings");
    stoppedCheck(e, routedTo("test.stuck.layout"), "test.stuck.layout", "layout");
    for (int n = 0; n < 3; ++n) CHECK(block(e, {{2, kNote}}) == std::vector<Out>{{1, 2, kNote}});
    // A pack whose call was stopped is no worse for it: its fine type plans and runs.
    place(e, routedTo("test.stuck.fine"));
    CHECK(block(e, {{2, kNote}}) == sorted({{1, 2, kNote}, {2, 2, {0x90, 72, 100}}}));
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

TEST_CASE("the control side's deadline keeps its own clock while an audio thread is making calls into another pack",
          "[script][packs][deadline][threads]") {
    Engine e(kRate);
    e.setScriptCallDeadline(std::chrono::milliseconds(50));
    place(e, routedTo());
    e.setAudioThreadOwns(true);
    std::atomic<bool> running{true};
    std::atomic<int>  plain{0}, moved{0};
    std::thread       audio([&] {
        std::vector<float> l(kFrames), r(kFrames);
        while (running.load()) {
            e.drainCommands();
            e.stageMidi(0, kNote);
            e.processBlock(kFrames, l.data(), r.data());
            for (const auto& m : e.midiOut()) {
                if (m.system == 1 && m.data == kNote) plain.fetch_add(1);
                if (m.system == 2 && m.data == std::vector<std::uint8_t>{0x90, 72, 100}) moved.fetch_add(1);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });
    const auto waitFor = [](const std::atomic<int>& count, int n) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (count.load() < n && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return count.load() >= n;
    };
    const bool started = waitFor(plain, 10);
    // Each stopped call runs its 50 ms on this thread while the block thread's routing calls run on theirs, each on its
    // own clock: a block's call ends well inside its deadline, and the load and the check are stopped.
    const auto hung = e.loadScriptPack("test.hang", toys(transposer("test.hang.up", 1) + "for (;;) {}"));
    const auto stuck = e.loadScriptPack("test.stuck", toys(kStuck));
    const auto refusedPlan = e.setGraph(routedTo("test.stuck.settings"));
    const int afterRefusal = plain.load();
    const bool ranOn = waitFor(plain, afterRefusal + 10);  // the running plan, as it was
    bool placed = false;
    for (int attempt = 0; stuck.ok && !placed && attempt < 1000; ++attempt) {
        const auto r = e.setGraph(routedTo("test.stuck.fine"));
        placed = r.ok;
        if (!r.ok && (r.errors.empty() || r.errors[0].code != "busy")) break;
        if (!r.ok) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        e.reclaim();
    }
    const bool ranFine = placed && waitFor(moved, 10);
    running = false;
    audio.join();
    e.setAudioThreadOwns(false);
    e.reclaim();
    CHECK(started);
    INFO(hung.error);
    CHECK_FALSE(hung.ok);
    CHECK(hung.error.rfind(kLoadStopped, 0) == 0);
    REQUIRE(stuck.ok);
    CHECK_FALSE(refusedPlan.ok);
    REQUIRE(refusedPlan.errors.size() == 1);
    CHECK(refusedPlan.errors[0].message ==
          "test.stuck.settings's parseSettings ran past the 50 ms call deadline (CPU time), and was stopped");
    CHECK(ranOn);
    CHECK(placed);
    CHECK(ranFine);
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

TEST_CASE("the call deadline holds every pack's control side, the built-in one's too, and a loaded pack's audio runtime "
          "while its load owns it",
          "[script][packs][deadline]") {
    using rp::host::test::compileToys;
    // The built-in pack's settings check: the test bundle and the stuck toys loaded as the rig's one bundle.
    rp::host::test::ScriptRig rig(toys(kStuck));
    rig.scripts.setCallDeadline(std::chrono::milliseconds(50));
    const auto placed = rig.set(R"({"nodes":[{"id":"feed","type":"rp.test.feed"},)"
                                R"({"id":"node","type":"test.stuck.settings"}],)"
                                R"("wires":[{"from":"feed.midi","to":"node.in"}]})");
    CHECK_FALSE(placed.ok);
    REQUIRE(placed.errors.size() == 1);
    CHECK(placed.errors[0].message ==
          "test.stuck.settings's parseSettings ran past the 50 ms call deadline (CPU time), and was stopped");

    // A loaded pack's load, stopped wherever it hangs: its audio runtime's top level (the control side's loaded fine),
    // a job its top level queued, a definition's descriptor. None is added.
    const std::vector<std::pair<std::string, std::string>> hangs{
        {"test.audiohang", transposer("test.audiohang.up", 1) + "if (__audioSide) for (;;) {}"},
        {"test.jobhang", transposer("test.jobhang.up", 1) + "Promise.resolve().then(function () { for (;;) {} });"},
        {"test.deschang",
         transposer("test.deschang.up", 1) +
             "var d = globalThis.__rp_scriptNodes;\nd[d.length - 1].descriptor = function () { for (;;) {} };"},
    };
    for (const auto& [id, body] : hangs) {
        INFO(id);
        const auto bytecode = compileToys(toys(body));
        const ScriptHost::Chunk chunk = bytecode;
        std::string error;
        CHECK(rig.scripts.loadPack(id, {&chunk, 1}, 0, &error) == nullptr);
        INFO(error);
        CHECK(error.rfind(kLoadStopped, 0) == 0);
        CHECK(rig.scripts.packs().size() == 1);
    }

    // The built-in pack's load, stopped as a loaded pack's is: its package then holds no type.
    ScriptHost host;
    host.setCallDeadline(std::chrono::milliseconds(50));
    const auto hang = compileToys(toys(transposer("test.builtin.up", 1) + "for (;;) {}"));
    const ScriptHost::Chunk chunks[] = {ScriptHost::embeddedBundle(), hang};
    std::string error;
    CHECK_FALSE(host.load(chunks, &error));
    INFO(error);
    CHECK(error.rfind(kLoadStopped, 0) == 0);
    CHECK(host.package()->node_type_count == 0);
}

TEST_CASE("a pack is bound by the host's binder in both its contexts, and what its load allocates is in no audio-side "
          "count, where its nodes' are",
          "[script][packs]") {
    using rp::host::test::Feed;
    using rp::host::test::Probe;
    rp::host::test::ScriptRig rig;
    const auto before = rig.scripts.audioAllocations();
    const auto bytecode = rp::host::test::compileToys(toys(R"JS(
__probe("loaded " + (__audioSide ? "audio" : "control"));
var junk = [];
for (var k = 0; k < 64; k++) junk[k] = { k: k };
toy.define("test.bound.pass", [toy.input("in"), toy.output("out")], function () {
  return { process: function (b) {
    var ms = b.messages("in");
    for (var i = 0; i < ms.length; i++) b.send("out", ms[i].frame, ms[i].data);
  } };
});
)JS"));
    const ScriptHost::Chunk chunk = bytecode;
    std::string error;
    const rp_package* package = rig.scripts.loadPack("test.bound", {&chunk, 1}, 0, &error);
    INFO(error);
    REQUIRE(package != nullptr);
    CHECK(rig.log.count("loaded control") == 1);
    CHECK(rig.log.count("loaded audio") == 1);
    const auto loaded = rig.scripts.audioAllocations();
    CHECK(loaded.allocs == before.allocs);  // the load's runtimes, its top level and its GC: the control thread's own
    CHECK(loaded.frees == before.frees);
    CHECK(loaded.bytes == before.bytes);
    const auto packs = rig.scripts.packs();
    REQUIRE(packs.size() == 2);
    CHECK(packs[0].id == "rp.script");
    CHECK(packs[1].id == "test.bound");
    CHECK(packs[1].generation == 1);
    CHECK(package->node_type_count == 1);
    CHECK(std::string(package->manifest_json) == R"({"id":"test.bound"})");

    REQUIRE(rig.registry.addPackage(package, &error));
    REQUIRE(rig.set(R"({"nodes":[{"id":"feed","type":"rp.test.feed"},{"id":"pass","type":"test.bound.pass"},)"
                    R"({"id":"probe","type":"rp.test.probe"}],)"
                    R"("wires":[{"from":"feed.midi","to":"pass.in"},{"from":"pass.out","to":"probe.events"}]})")
                .ok);
    rig.node<Feed>("feed").next[Feed::Midi] = {{2, {0x90, 1, 2}}};
    rig.run();
    REQUIRE(rig.node<Probe>("probe").events.size() == 1);
    CHECK(rig.node<Probe>("probe").events[0].data == std::vector<std::uint8_t>{0x90, 1, 2});
    CHECK(rig.scripts.audioAllocations().allocs > loaded.allocs);  // its node's create and first block, on the block thread
    CHECK(rig.scripts.throwCount() == 0);
}
