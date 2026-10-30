// Script node packs (docs/modular-graph-phase-c1-design.md sections 4 to 7): a pack Engine::loadScriptPack loads runs
// beside the built-in pack's nodes in one document, in runtimes of its own, so what one pack does to its realm (a
// getter on Object.prototype, Array.prototype.push replaced, a global) no node of another pack sees, its memory limit
// and its promise jobs fault only its own nodes, and it loads while an audio thread runs blocks. Its id (never one of
// the app's, "rp" and under), its types' namespace, its source and its definitions are checked, and a refusal leaves
// the types a document can name and the running plan as they were (a load that got as far as being built still uses up
// a generation number, and the reclaim every load makes first may still free retired generations). Loaded again, it
// reloads: its placed nodes rebuild on the new code while every other node keeps its instance and its state (the
// running plan's tap included), a reload whose replan is refused leaves the types a document can name and the running
// plan as they were, the generation it built discarded: freed within the call, or, when under an audio thread the
// refused plan made a fresh instance of it, held until that instance's record comes back; and the generation a reload
// replaced is freed on the control thread once its records are back: within the call when no audio thread runs, else at
// the first reclaim after, every load reclaiming first. The control thread's calls into a pack, its load and its
// settings checks, are held to the call deadline on a clock of their own. Driven on an Engine, whose built-in pack is
// the production bundle, the toy packs inline JS source; and on a ScriptRig, where the host's binder, its allocation
// counts and the built-in pack's deadline are what is checked.
//
// Run via `pnpm test:plugin engine-graph`, or `build/bin/retroplug-engine-graph-test "[packs]"`.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <compare>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "GraphDocs.hpp"
#include "NodeRig.hpp"
#include "ScriptRig.hpp"
#include "graph/Descriptor.hpp"
#include "host/engine/DspCommand.hpp"
#include "host/engine/Engine.hpp"
#include "host/script/ScriptHost.hpp"
#include "system/mesen/MesenNesSystem.hpp"

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

    // Host MIDI through the built-in routing, by channel: channel 0 to system 1 through the pack's node, which moves
    // its note up an octave, and channel 1 to system 2 straight, its channel rewritten to 0 (the routing's own job).
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

TEST_CASE("a pack refused changes nothing: a non-plain id, the built-in pack's, one of the app's, a type outside its "
          "namespace, a compile error, a definition that cannot be read, one the registry refuses",
          "[script][packs][refused]") {
    Engine e(kRate);
    refused(e, "test pack", toys(transposer("test pack.up", 12)), "must be non-empty, with no whitespace", "test pack.up");
    refused(e, "rp.script", toys(transposer("rp.script.up", 12)),
            "pack id \"rp.script\" is the built-in pack, which is not reloadable", "rp.script.up");
    // "rp" and the ids under it are the app's to give: its built-in pack's and its native packages' (rp.core, rp.host,
    // ...), and a pack named "rp.midi" would hold the built-in rp.midi.route's namespace.
    for (const std::string app : {"rp", "rp.mine", "rp.core", "rp.midi"}) {
        INFO(app);
        refused(e, app, toys(transposer(app + ".up", 1)), "is the app's to give: \"rp\" and every id under \"rp.\"",
                app + ".up");
    }

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

    // Each id refused once its pack was built loads clean now: neither the script host nor the registry kept it, but
    // for its count of loads, so each is numbered past those refused (test.ns's four, one each of the others; a
    // compile error builds nothing, so test.syntax's count none).
    load(e, "test.ns", toys(transposer("test.ns.fine", 1)));
    load(e, "test.syntax", toys(transposer("test.syntax.up", 1)));
    load(e, "test.nodesc", toys(transposer("test.nodesc.fine", 1)));
    load(e, "test.baddesc", toys(transposer("test.baddesc.odd", 1)));
    load(e, "test.nest.x", toys(transposer("test.nest.x.down", -1)));
    // And an id JSON must escape is its package's id as it is: its manifest spells it as JSON does.
    const auto quoted = load(e, "test\"q", toys(transposer("test\"q.up", 1)));
    REQUIRE(quoted.types.size() == 1);
    CHECK(quoted.types[0].id == "test\"q.up");
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.nest#1", "test.ns#5", "test.syntax#1",
                                                 "test.nodesc#2", "test.baddesc#2", "test.nest.x#2", "test\"q#1"});
    CHECK(e.scriptThrows() == 0);
}

TEST_CASE("a pack's node that allocates past its pack's memory limit faults, while another pack's node and a built-in one "
          "run on",
          "[script][packs][memory]") {
    Engine e(kRate);
    // Pack A held to 16 MiB, its node reaching for 32 on its second block; pack B at the default, its node taking 24
    // and keeping them, more than pack A may hold.
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
    // The chain's node first, so the other's checkpoint follows its leftovers in every block: a pack-mate's would run
    // them.
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

namespace {

// JS that queues a chain of `jobs` promise jobs, one link each, the first from where it runs; 0, a chain that never
// ends.
std::string jobChain(int jobs) {
    return "(function () { var n = 0, link = function () { if (" + std::to_string(jobs) +
           " === 0 || ++n < " + std::to_string(jobs) + ") Promise.resolve().then(link); };\n"
           "  Promise.resolve().then(link); })();\n";
}

}  // namespace

TEST_CASE("a load whose promise jobs do not settle in the 256 a load runs is refused, changing nothing: its top "
          "level's, a descriptor's, a definition's read after the last descriptor, what its collection queues, in "
          "either runtime, the built-in pack's load too",
          "[script][packs][jobs]") {
    constexpr const char* kUnsettled = "promise jobs did not settle in the 256 a load runs";
    Engine e(kRate);
    // In each runtime a load runs at most 256 of what its top level queued: a chain that long settles, one more does
    // not.
    load(e, "test.settles", toys(transposer("test.settles.up", 1) + jobChain(256)));
    refused(e, "test.unsettled", toys(transposer("test.unsettled.up", 1) + jobChain(257)),
            std::string("its top level's ") + kUnsettled, "test.unsettled.up");
    refused(e, "test.endless", toys(transposer("test.endless.up", 1) + jobChain(0)),
            std::string("its top level's ") + kUnsettled, "test.endless.up");
    // A descriptor that starts one, each time it is asked.
    refused(e, "test.descchain", toys(transposer("test.descchain.up", 1) + R"JS(
var d = globalThis.__rp_scriptNodes, plain = d[d.length - 1].descriptor;
d[d.length - 1].descriptor = function () { )JS" + jobChain(0) + R"JS( return plain(); };
)JS"),
            std::string("test.descchain.up: its descriptor's ") + kUnsettled, "test.descchain.up");
    // A getter on a flag the load reads after the last descriptor's drain, which starts one.
    refused(e, "test.flagchain", toys(transposer("test.flagchain.up", 1) + R"JS(
var f = globalThis.__rp_scriptNodes;
Object.defineProperty(f[f.length - 1], "faultOnThrow", { get: function () { )JS" + jobChain(0) + R"JS( return false; } });
)JS"),
            std::string("its definitions' ") + kUnsettled + ", in the control context", "test.flagchain.up");
    // A FinalizationRegistry callback that starts one, its target a cycle the top level drops, which the loaded pack's
    // collection finds (a threshold GC on the way would find it earlier, and be read as the stretch it ran in; this
    // small a heap reaches none).
    refused(e, "test.finchain", toys(transposer("test.finchain.up", 1) + R"JS(
var registry = globalThis.registry = new FinalizationRegistry(function () { )JS" + jobChain(0) + R"JS( });
(function () { var cycle = {}; cycle.self = cycle; registry.register(cycle, 0); })();
)JS"),
            std::string("its collection's ") + kUnsettled + ", in the audio context", "test.finchain.up");
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.settles#1"});
    CHECK(e.scriptThrows() == 0);

    // The audio runtime's drain alone (the control side's settles): what the binder says of the context it runs in.
    rp::host::test::ScriptRig rig;
    const auto audioOnly =
        rp::host::test::compileToys(toys(transposer("test.audiochain.up", 1) + "if (__audioSide) " + jobChain(0)));
    const ScriptHost::Chunk audioChunk = audioOnly;
    std::string error;
    CHECK(rig.scripts.loadPack("test.audiochain", {&audioChunk, 1}, 0, &error) == nullptr);
    INFO(error);
    CHECK(error == std::string("its top level's ") + kUnsettled + ", in the audio context");
    CHECK(rig.scripts.packs().size() == 1);

    // The audio descriptors' drain alone: a descriptor, asked in the audio context only, that starts a chain. (The
    // descriptor tests above stop on the control side first.)
    const auto audioDescriptor = rp::host::test::compileToys(toys(transposer("test.audiodesc.up", 1) + R"JS(
if (__audioSide) {
  var d = globalThis.__rp_scriptNodes, plain = d[d.length - 1].descriptor;
  d[d.length - 1].descriptor = function () { )JS" + jobChain(0) + R"JS( return plain(); };
}
)JS"));
    const ScriptHost::Chunk descriptorChunk = audioDescriptor;
    error.clear();
    CHECK(rig.scripts.loadPack("test.audiodesc", {&descriptorChunk, 1}, 0, &error) == nullptr);
    INFO(error);
    CHECK(error == std::string("its descriptors' ") + kUnsettled + ", in the audio context");
    CHECK(rig.scripts.packs().size() == 1);

    // The built-in pack's load, refused as a loaded pack's is: its package then holds no type.
    ScriptHost host;
    const auto chained = rp::host::test::compileToys(toys(transposer("test.builtin.up", 1) + jobChain(0)));
    const ScriptHost::Chunk chunks[] = {ScriptHost::embeddedBundle(), chained};
    error.clear();
    CHECK_FALSE(host.load(chunks, &error));
    INFO(error);
    CHECK(error == std::string("its top level's ") + kUnsettled + ", in the control context");
    CHECK(host.package()->node_type_count == 0);
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
    const auto patience = std::chrono::steady_clock::now() + std::chrono::seconds(10);  // as waitFor's
    while (loaded.ok && !placed && std::chrono::steady_clock::now() < patience) {
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
    const auto patience = std::chrono::steady_clock::now() + std::chrono::seconds(10);  // as waitFor's
    while (stuck.ok && !placed && std::chrono::steady_clock::now() < patience) {
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
    // a job its top level queued, a definition's descriptor. None is added. Only the first reaches the audio runtime
    // before the load stops: the others hang in code both runtimes would run, and the control runtime, loaded first, is
    // where they stop.
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

TEST_CASE("a pack's control runtime is held to its memory limit as its audio runtime is: a top level allocating past it "
          "there refuses the load, and a parseSettings allocating past it refuses the document placing its node while "
          "the running plan runs on, each saying so",
          "[script][packs][memory]") {
    using rp::host::test::compileToys;
    constexpr std::size_t kLimit = std::size_t{4} << 20;
    // 8 MiB, held until the code that holds it returns.
    const std::string hog = "var kept = []; for (var k = 0; k < 8; k++) kept[k] = new Float64Array(1 << 17);\n";

    // The load: its top level allocates only in the control runtime (what the binder says of the context it runs in),
    // so only that runtime's limit can refuse it. The same pack without it loads under the same limit.
    rp::host::test::ScriptRig rig;
    const auto hogging = compileToys(toys(transposer("test.ctlhog.up", 1) + "if (!__audioSide) { " + hog + "}"));
    const ScriptHost::Chunk hoggingChunk = hogging;
    std::string error;
    CHECK(rig.scripts.loadPack("test.ctlhog", {&hoggingChunk, 1}, kLimit, &error) == nullptr);
    INFO(error);
    CHECK(error.rfind("chunk 0: ", 0) == 0);
    CHECK(error.find("(the load reached the pack's 4 MiB memory limit)") != std::string::npos);
    CHECK(rig.scripts.packs().size() == 1);
    const auto fine = compileToys(toys(transposer("test.ctlhog.up", 1)));
    const ScriptHost::Chunk fineChunk = fine;
    CHECK(rig.scripts.loadPack("test.ctlhog", {&fineChunk, 1}, kLimit, &error) != nullptr);

    // A settings check: test.memset.big's parseSettings holds 8 MiB while it runs; test.memset.catcher's does too, and
    // catches the error it is refused with.
    Engine e(kRate);
    load(e, "test.memset", toys(transposer("test.memset.big", 1) + transposer("test.memset.catcher", 2) +
                                transposer("test.memset.fine", 12) + R"JS(
var defs = globalThis.__rp_scriptNodes, big = defs[defs.length - 3], catcher = defs[defs.length - 2];
var bigPlain = big.parseSettings, catcherPlain = catcher.parseSettings;
big.parseSettings = function (raw) { )JS" + hog + R"JS( return bigPlain(raw); };
catcher.parseSettings = function (raw) { try { )JS" + hog + R"JS( } catch (e) {} return catcherPlain(raw); };
)JS"),
         kLimit);
    place(e, routedTo());
    CHECK(block(e, {{2, kNote}}) == std::vector<Out>{{1, 2, kNote}});
    const auto r = e.setGraph(routedTo("test.memset.big"));
    CHECK_FALSE(r.ok);
    REQUIRE(r.errors.size() == 1);
    INFO(r.errors[0].message);
    CHECK(r.errors[0].code == "settings-invalid");
    CHECK(r.errors[0].nodeId == "node");
    CHECK(r.errors[0].message == "test.memset.big's parseSettings reached its pack's 4 MiB memory limit: out of memory");
    for (int n = 0; n < 3; ++n) CHECK(block(e, {{2, kNote}}) == std::vector<Out>{{1, 2, kNote}});
    // The pack is no worse for it: its fine type plans and runs.
    place(e, routedTo("test.memset.fine"));
    CHECK(block(e, {{2, kNote}}) == sorted({{1, 2, kNote}, {2, 2, {0x90, 72, 100}}}));
    // A check that catches the refused allocation runs on, there being no node to fault: the document is planned, and
    // no throw is counted.
    place(e, routedTo("test.memset.catcher"));
    CHECK(block(e, {{2, kNote}}) == sorted({{1, 2, kNote}, {2, 2, {0x90, 62, 100}}}));
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

TEST_CASE("a settings check whose promise jobs do not settle in the 256 it runs refuses its document while the running "
          "plan runs on, and a chain that never settles, staying queued, refuses every later check of its pack until "
          "the pack is reloaded",
          "[script][packs][jobs]") {
    constexpr const char* kUnsettled =
        "'s settings check: its pack's promise jobs did not settle in the 256 a settings check runs";
    // Each a transposer whose parseSettings starts a chain of promise jobs, 256 long, 257, or one that never ends, and
    // one that starts none.
    const auto chained = [](const std::string& id, int by, int jobs) {
        return transposer(id, by) + "(function () { var d = globalThis.__rp_scriptNodes, n = d[d.length - 1], plain = "
                                    "n.parseSettings;\n  n.parseSettings = function (raw) { " +
               jobChain(jobs) + " return plain(raw); };\n})();\n";
    };
    const std::string source = toys(chained("test.loose.long", 1, 256) + chained("test.loose.over", 2, 257) +
                                    chained("test.loose.endless", 3, 0) + transposer("test.loose.fine", 12));
    const auto refusedCheck = [](Engine& e, const std::string& type) {
        const auto r = e.setGraph(routedTo(type));
        CHECK_FALSE(r.ok);
        REQUIRE(r.errors.size() == 1);
        INFO(r.errors[0].message);
        CHECK(r.errors[0].code == "settings-invalid");
        CHECK(r.errors[0].nodeId == "node");
        CHECK(r.errors[0].message == type + kUnsettled);
    };
    Engine e(kRate);
    load(e, "test.loose", source);
    place(e, routedTo());
    CHECK(block(e, {{2, kNote}}) == std::vector<Out>{{1, 2, kNote}});
    // 256 settle in each check (the planner's layout_for, then the fresh instance's create).
    place(e, routedTo("test.loose.long"));
    const std::vector<Out> longRuns = sorted({{1, 2, kNote}, {2, 2, {0x90, 61, 100}}});
    CHECK(block(e, {{2, kNote}}) == longRuns);
    // 257 do not: refused, the running plan running on. The one left waits in the pack's control runtime, and the next
    // check runs it with its own.
    refusedCheck(e, "test.loose.over");
    CHECK(block(e, {{2, kNote}}) == longRuns);
    place(e, routedTo("test.loose.fine"));
    const std::vector<Out> fineRuns = sorted({{1, 2, kNote}, {2, 2, {0x90, 72, 100}}});
    CHECK(block(e, {{2, kNote}}) == fineRuns);
    // A chain that never ends: refused, and QuickJS cannot drop what it leaves queued, so every later check of the pack
    // finds it unsettled too, a type that starts none among them.
    refusedCheck(e, "test.loose.endless");
    refusedCheck(e, "test.loose.long");
    refusedCheck(e, "test.loose.fine");
    CHECK(block(e, {{2, kNote}}) == fineRuns);
    // A reload frees the runtime the chain lived in: its replan checks the placed node in the new generation's.
    load(e, "test.loose", source);
    CHECK(block(e, {{2, kNote}}) == fineRuns);
    place(e, routedTo("test.loose.long"));
    CHECK(block(e, {{2, kNote}}) == longRuns);
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.loose#2"});
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
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

TEST_CASE("a pack the registry refuses is discarded with nothing in any audio-side count: neither what its load "
          "allocated nor what freeing it frees, which is the control thread's",
          "[script][packs]") {
    using rp::host::test::compileToys;
    rp::host::test::ScriptRig rig;
    // A descriptor the registry cannot use, and a type id another pack holds ("test.nest" and "test.nest.x" can both
    // name "test.nest.x.up"), each pack built and joined before the registry is asked.
    const std::vector<std::pair<std::string, std::string>> refusals{
        {"test.odd", R"JS(
toy.define("test.odd.smoke", [{ id: "in", dir: "in", kind: "smoke" }], function () {
  return { process: function () {} };
});
)JS"},
        {"test.nest.x", transposer("test.nest.x.up", 1)},
    };
    const auto holding = compileToys(toys(transposer("test.nest.x.up", 2)));
    const ScriptHost::Chunk holdingChunk = holding;
    std::string error;
    const rp_package* holder = rig.scripts.loadPack("test.nest", {&holdingChunk, 1}, 0, &error);
    REQUIRE(holder != nullptr);
    REQUIRE(rig.registry.addPackage(holder, &error));
    for (const auto& [id, body] : refusals) {
        INFO(id);
        const auto before = rig.scripts.audioAllocations();
        const auto bytecode = compileToys(toys(body));
        const ScriptHost::Chunk chunk = bytecode;
        const rp_package* package = rig.scripts.loadPack(id, {&chunk, 1}, 0, &error);
        INFO(error);
        REQUIRE(package != nullptr);
        CHECK_FALSE(rig.registry.addPackage(package, &error));
        rig.scripts.discardPack(package);
        const auto after = rig.scripts.audioAllocations();
        CHECK(after.allocs == before.allocs);
        CHECK(after.reallocs == before.reallocs);
        CHECK(after.frees == before.frees);
        CHECK(after.bytes == before.bytes);
        CHECK(rig.scripts.packs().size() == 2);  // the built-in pack and the holder's
    }
}

TEST_CASE("the script host says which generation of an id is current, which is what makes a load of it a reload: none "
          "before its first load or for the built-in pack, a discarded generation never, a retired one no longer",
          "[script][packs][reload]") {
    using rp::host::test::compileToys;
    rp::host::test::ScriptRig rig;
    const auto bytecode = compileToys(toys(transposer("test.which.up", 1)));
    const ScriptHost::Chunk chunk = bytecode;
    std::string error;
    CHECK(rig.scripts.currentPack("test.which") == nullptr);
    CHECK(rig.scripts.currentPack("rp.script") == nullptr);
    const rp_package* first = rig.scripts.loadPack("test.which", {&chunk, 1}, 0, &error);
    INFO(error);
    REQUIRE(first != nullptr);
    CHECK(rig.scripts.currentPack("test.which") == first);
    CHECK(rig.scripts.currentPack("test.whic") == nullptr);
    CHECK(rig.scripts.currentPack("test.which.up") == nullptr);
    // A generation that never stood leaves the one before it current.
    const rp_package* refused = rig.scripts.loadPack("test.which", {&chunk, 1}, 0, &error);
    REQUIRE(refused != nullptr);
    rig.scripts.discardPack(refused);
    CHECK(rig.scripts.currentPack("test.which") == first);
    // One that replaced it is current in its place.
    const rp_package* next = rig.scripts.loadPack("test.which", {&chunk, 1}, 0, &error);
    REQUIRE(next != nullptr);
    rig.scripts.retirePack(first);
    CHECK(rig.scripts.currentPack("test.which") == next);
    const auto packs = rig.scripts.packs();
    REQUIRE(packs.size() == 2);
    CHECK(packs[1].generation == 3);
}

namespace {

// A node, `id`, that sends [tag, n] every block, n its block count: which code runs, and whether its instance, and so
// its count, carried on.
std::string tally(const std::string& id, int tag) {
    return "toy.define('" + id + "', [toy.output('out')], function () {\n"
           "  var n = 0;\n"
           "  return { process: function (b) { b.send('out', 0, [" + std::to_string(tag) + ", ++n & 0x7f]); } };\n"
           "});\n";
}

// The pack `id` at `generation` among `packs`, or null.
const ScriptHost::PackInfo* findPack(const std::vector<ScriptHost::PackInfo>& packs, const std::string& id,
                                     std::uint32_t generation) {
    for (const auto& p : packs)
        if (p.id == id && p.generation == generation) return &p;
    return nullptr;
}

// Until `done()`, for at most ten seconds (the block thread is someone else); whether it came.
template <class Done>
bool waitUntil(Done&& done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return done();
}

}  // namespace

TEST_CASE("a pack reloads while its nodes run: they rebuild on its new code, fresh, while another pack's node and a "
          "built-in one keep their instances and their state, and the old generation is freed within the call",
          "[script][packs][reload]") {
    Engine e(kRate);
    e.setControllerConnected(true);
    load(e, "test.keep", toys(tally("test.keep.count", 1)));
    load(e, "test.code", toys(transposer("test.code.shift", 12) + tally("test.code.count", 2)));
    // Beside them the built-in controller session, which takes the control surface (Programmer mode, a full paint) on
    // the block it first sees one attached, and only then: a session made again takes it again.
    const auto request = [](const std::string& session) {
        return document({docNode("midi-in", "rp.host.midi-in"), docNode("shift", "test.code.shift"),
                         docNode("keep", "test.keep.count"), docNode("count", "test.code.count"), midiOut("1,2,3"),
                         docNode("surface-in", "rp.host.controller-in"), docNode(session, "rp.controller.session"),
                         docNode("surface-out", "rp.host.controller-out")},
                        {docWire("midi-in.midi", "shift.in"), docWire("shift.out", "midi-out.in1"),
                         docWire("keep.out", "midi-out.in2"), docWire("count.out", "midi-out.in3"),
                         docWire("surface-in.midi", session + ".controllerIn"),
                         docWire("surface-in.connected", session + ".controllerConnected"),
                         docWire(session + ".controllerOut", "surface-out.midi")});
    };
    place(e, request("session"));
    CHECK(block(e, {{3, kNote}}) == sorted({{1, 3, {0x90, 72, 100}}, {2, 0, {1, 1}}, {3, 0, {2, 1}}}));
    CHECK_FALSE(e.controllerOut().empty());
    for (std::uint8_t n = 2; n <= 3; ++n) {
        INFO("block " << int{n});
        CHECK(block(e) == sorted({{2, 0, {1, n}}, {3, 0, {2, n}}}));
        CHECK(e.controllerOut().empty());
    }
    const std::uint32_t instances = e.scriptInstances();
    const std::size_t types = e.nodeTypeCount();

    const auto reloaded = load(e, "test.code", toys(transposer("test.code.shift", -12) + tally("test.code.count", 3)));
    CHECK(reloaded.error.empty());
    CHECK(reloaded.errors.empty());
    REQUIRE(reloaded.types.size() == 2);
    CHECK(reloaded.types[0].id == "test.code.shift");
    CHECK(reloaded.types[1].id == "test.code.count");
    // No audio thread runs, so the replan released the old generation's nodes there and then, and the generation went
    // within the call: its runtimes, and its entries in the registry.
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.keep#1", "test.code#2"});
    CHECK(e.retiredNodeTypeCount() == 0);
    CHECK(e.nodeTypeCount() == types);
    CHECK(e.scriptInstances() == instances);
    const auto packs = e.scriptPacks();
    REQUIRE(packs.size() == 3);
    CHECK(packs[2].standing == ScriptHost::PackStanding::Current);
    CHECK(packs[2].records == 2);
    CHECK(packs[1].records == 1);
    CHECK(packs[0].records == 1);  // the session

    // The pack's nodes on the new code, counting from 1 again; the other pack's count carrying on, unbroken; the
    // session not taking the surface again.
    CHECK(block(e, {{3, kNote}}) == sorted({{1, 3, {0x90, 48, 100}}, {2, 0, {1, 4}}, {3, 0, {3, 1}}}));
    CHECK(e.controllerOut().empty());
    CHECK(block(e) == sorted({{2, 0, {1, 5}}, {3, 0, {3, 2}}}));
    CHECK(e.controllerOut().empty());
    // Where a session made again does take it: the same document with the session under another id.
    place(e, request("session2"));
    CHECK(block(e) == sorted({{2, 0, {1, 6}}, {3, 0, {3, 3}}}));
    CHECK_FALSE(e.controllerOut().empty());
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

namespace {

// A reload of `id` from `source` refused, changing nothing: `why` in its error and the planner's first error `code` at
// `node` (no planner error when `code` is empty: refused before any replan), no types listed, and the packs, the
// registry and the script instances as they were.
void refusedReload(Engine& e, const std::string& id, const std::string& source, const std::string& why,
                   const std::string& code = "", const std::string& node = "") {
    const auto packsBefore = packIds(e);
    const std::size_t typesBefore = e.nodeTypeCount();
    const std::uint32_t instances = e.scriptInstances();
    const auto r = e.loadScriptPack(id, source);
    INFO(id << ": " << r.error);
    CHECK_FALSE(r.ok);
    CHECK(r.error.find(why) != std::string::npos);
    CHECK(r.types.empty());
    if (code.empty()) {
        CHECK(r.errors.empty());
    } else {
        REQUIRE_FALSE(r.errors.empty());
        CHECK(r.errors[0].code == code);
        CHECK(r.errors[0].nodeId == node);
        CHECK(r.error.find(r.errors[0].message) != std::string::npos);
    }
    CHECK(packIds(e) == packsBefore);
    CHECK(e.nodeTypeCount() == typesBefore);
    CHECK(e.retiredNodeTypeCount() == 0);
    CHECK(e.scriptInstances() == instances);
}

}  // namespace

TEST_CASE("a reload refused changes nothing, the old code running on with its state: the new settings check refusing "
          "the document's settings, a type the document places gone, a type another pack holds, a source that does not "
          "load; and a type a reload adds places",
          "[script][packs][reload]") {
    Engine e(kRate);
    load(e, "test.code", toys(transposer("test.code.shift", 12) + tally("test.code.count", 2)));
    load(e, "test.code.sub", toys(tally("test.code.sub.x", 5)));
    place(e, document({docNode("midi-in", "rp.host.midi-in"), docNode("shift", "test.code.shift"),
                       docNode("count", "test.code.count"), midiOut("1,2")},
                      {docWire("midi-in.midi", "shift.in"), docWire("shift.out", "midi-out.in1"),
                       docWire("count.out", "midi-out.in2")}));
    // The old code, its instances kept: up an octave, and [2, n] with n carrying on.
    std::uint8_t n = 0;
    const auto oldCodeRuns = [&] {
        ++n;
        CHECK(block(e, {{3, kNote}}) == sorted({{1, 3, {0x90, 72, 100}}, {2, 0, {2, n}}}));
    };
    oldCodeRuns();

    // Every type kept, but test.code.shift's settings check wants a gain, which the document's settings do not give.
    refusedReload(e, "test.code", toys(transposer("test.code.shift", 7) + tally("test.code.count", 3) + R"JS(
globalThis.__rp_scriptNodes[0].parseSettings = function (raw) {
  if (!raw || typeof raw.gain !== "number") throw new Error("test.code.shift takes a gain");
  return raw;
};
)JS"),
                  "test.code.shift takes a gain", "settings-invalid", "shift");
    oldCodeRuns();
    // test.code.count gone.
    refusedReload(e, "test.code", toys(transposer("test.code.shift", 7)),
                  "no node type \"test.code.count\" is registered", "unknown-type", "count");
    oldCodeRuns();
    // A type in its namespace that the pack whose id runs on into it holds (test.code.sub's).
    refusedReload(e, "test.code",
                  toys(transposer("test.code.shift", 7) + tally("test.code.count", 3) + tally("test.code.sub.x", 6)),
                  "the registry refused the pack's types: node type \"test.code.sub.x\" is already registered");
    oldCodeRuns();
    // Refused before it is a generation: a compile error, a top level that throws, a type outside its namespace.
    refusedReload(e, "test.code", "var x = ;", "the source does not compile: line 1: SyntaxError");
    refusedReload(e, "test.code", toys(transposer("test.code.shift", 7) + "throw new Error('not this one');"),
                  "chunk 0: Error: not this one");
    refusedReload(e, "test.code", toys(transposer("test.other.shift", 7)), "is outside pack \"test.code\"");
    oldCodeRuns();
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.code#1", "test.code.sub#1"});

    // One that adds a type: the placed nodes rebuild on it (the count from 1 again), and the new type places.
    const auto added = load(e, "test.code", toys(transposer("test.code.shift", 12) + tally("test.code.count", 2) +
                                                tally("test.code.extra", 9)));
    REQUIRE(added.types.size() == 3);
    CHECK(added.types[2].id == "test.code.extra");
    // Numbered past the five refused above that were built: the generations the id's loads gave never come again.
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.code.sub#1", "test.code#7"});
    CHECK(block(e, {{3, kNote}}) == sorted({{1, 3, {0x90, 72, 100}}, {2, 0, {2, 1}}}));
    place(e, document({docNode("count", "test.code.count"), docNode("extra", "test.code.extra"), midiOut("2,3")},
                      {docWire("count.out", "midi-out.in1"), docWire("extra.out", "midi-out.in2")}));
    CHECK(block(e) == sorted({{2, 0, {2, 2}}, {3, 0, {9, 1}}}));
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

TEST_CASE("a placed node whose settings check fails refuses a reload of another pack the running document places "
          "beside it, that pack's old code running on, until the failing node's own pack is reloaded",
          "[script][packs][reload][jobs]") {
    // test.stuck's node starts a chain of promise jobs that never ends, in its pack's control runtime, when its
    // settings ask for one, so it places while they do not.
    const std::string stuck =
        toys(transposer("test.stuck.shift", 12) +
             "(function () { var d = globalThis.__rp_scriptNodes, n = d[d.length - 1], plain = n.parseSettings;\n"
             "  n.parseSettings = function (raw) { if (raw && raw.endless) " +
             jobChain(0) + " return plain(raw); };\n})();\n");
    const auto request = [](const std::string& settings) {
        return document({docNode("midi-in", "rp.host.midi-in"), routing("sendToAll", "1,2"),
                         docNode("stuck", "test.stuck.shift", settings), docNode("other", "test.other.shift"),
                         midiOut("1,2")},
                        {docWire("midi-in.midi", "routing.midiIn"), docWire("routing.sys-1", "stuck.in"),
                         docWire("stuck.out", "midi-out.in1"), docWire("routing.sys-2", "other.in"),
                         docWire("other.out", "midi-out.in2")});
    };
    const std::string unsettled =
        "test.stuck.shift's settings check: its pack's promise jobs did not settle in the 256 a settings check runs";
    Engine e(kRate);
    load(e, "test.stuck", stuck);
    load(e, "test.other", toys(transposer("test.other.shift", 1)));
    place(e, request(""));
    // test.stuck's node up an octave, test.other's up `by`.
    const auto runs = [&](int by) {
        CHECK(block(e, {{2, kNote}}) ==
              sorted({{1, 2, {0x90, 72, 100}}, {2, 2, {0x90, static_cast<std::uint8_t>(60 + by), 100}}}));
    };
    runs(1);
    // Asked for the chain: refused, the running plan running on, and the chain left queued in test.stuck's control
    // runtime, which QuickJS cannot drop it from.
    const auto endless = e.setGraph(request(R"({"endless":true})"));
    CHECK_FALSE(endless.ok);
    REQUIRE_FALSE(endless.errors.empty());
    CHECK(endless.errors[0].code == "settings-invalid");
    CHECK(endless.errors[0].nodeId == "stuck");
    CHECK(endless.errors[0].message == unsettled);
    runs(1);
    // A reload of test.other plans the running document again, whose check of the placed test.stuck node, settings
    // that start nothing, finds that chain unsettled: refused with that node's error, test.other's old code running on.
    refusedReload(e, "test.other", toys(transposer("test.other.shift", 2)),
                  "the running document does not plan with the reloaded pack: settings-invalid at stuck: " + unsettled,
                  "settings-invalid", "stuck");
    runs(1);
    // test.stuck reloaded, its parseSettings starting no chain: its replan checks the node in the new generation's
    // control runtime, and the runtime the chain lived in goes with the old one. Then test.other reloads.
    load(e, "test.stuck", toys(transposer("test.stuck.shift", 12)));
    runs(1);
    load(e, "test.other", toys(transposer("test.other.shift", 2)));
    runs(2);
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.stuck#2", "test.other#3"});
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

TEST_CASE("fifty reloads of a placed pack, blocks run between them, leave one generation, the instance count steady "
          "and the registry as it was",
          "[script][packs][reload]") {
    Engine e(kRate);
    load(e, "test.loop", toys(tally("test.loop.count", 0)));
    place(e, document({docNode("count", "test.loop.count"), midiOut("1")}, {docWire("count.out", "midi-out.in1")}));
    CHECK(block(e) == std::vector<Out>{{1, 0, {0, 1}}});
    const std::uint32_t instances = e.scriptInstances();
    const std::size_t types = e.nodeTypeCount();
    for (int k = 1; k <= 50; ++k) {
        INFO("reload " << k);
        load(e, "test.loop", toys(tally("test.loop.count", k)));
        const auto tag = static_cast<std::uint8_t>(k);
        CHECK(block(e) == std::vector<Out>{{1, 0, {tag, 1}}});
        CHECK(block(e) == std::vector<Out>{{1, 0, {tag, 2}}});
        CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.loop#" + std::to_string(k + 1)});
        CHECK(e.scriptInstances() == instances);
        CHECK(e.nodeTypeCount() == types);
        CHECK(e.retiredNodeTypeCount() == 0);
    }
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

TEST_CASE("under an audio thread, a reload's old generation is retired until its records come back, then freed by a "
          "reclaim on the control thread, and reloads in a row free every generation they replace",
          "[script][packs][reload][threads]") {
    Engine e(kRate);
    load(e, "test.live", toys(tally("test.live.count", 0)));
    place(e, document({docNode("count", "test.live.count"), midiOut("1")}, {docWire("count.out", "midi-out.in1")}));
    e.setAudioThreadOwns(true);
    // The audio thread: blocks, each noting the tag of the code its node ran.
    std::atomic<bool> running{true};
    std::atomic<int>  lastTag{-1};
    std::thread       audio([&] {
        std::vector<float> l(kFrames), r(kFrames);
        while (running.load()) {
            e.drainCommands();
            e.processBlock(kFrames, l.data(), r.data());
            for (const auto& m : e.midiOut())
                if (m.system == 1 && !m.data.empty()) lastTag.store(m.data[0]);
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });
    const bool started = waitUntil([&] { return lastTag.load() == 0; });
    // A reload, tried again while its replan finds the ring busy (which the audio thread drains).
    const auto reload = [&](int tag) {
        Engine::ScriptPackResult r;
        const auto patience = std::chrono::steady_clock::now() + std::chrono::seconds(10);  // as waitUntil's
        while (std::chrono::steady_clock::now() < patience) {
            r = e.loadScriptPack("test.live", toys(tally("test.live.count", tag)));
            if (r.ok || r.errors.empty() || r.errors[0].code != "busy") break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            e.reclaim();
        }
        return r;
    };
    const auto first = reload(1);
    // The plan holding the old generation's node is the audio thread's until it adopts the replan and hands the old
    // one back, so the generation is retired, its record still out, and its entries still in the registry.
    const auto held = e.scriptPacks();
    const ScriptHost::PackInfo* old = findPack(held, "test.live", 1);
    const std::size_t retiredTypes = e.retiredNodeTypeCount();
    // A reclaim frees the plan handed back, which queues its node's release; a later block releases it; and the reclaim
    // after that deletes the record, and frees the generation with it.
    const auto onlyCurrent = [&] {
        e.reclaim();
        const auto packs = e.scriptPacks();
        return packs.size() == 2 && packs[1].standing == ScriptHost::PackStanding::Current && packs[1].records == 1 &&
               e.retiredNodeTypeCount() == 0;
    };
    const bool freedFirst = waitUntil(onlyCurrent);
    const bool ranFirst = waitUntil([&] { return lastTag.load() == 1; });
    // Twenty in a row, a reclaim between each, so each frees what it can while the next is loaded and planned.
    bool reloaded = true;
    for (int k = 2; k <= 21; ++k) {
        reloaded = reload(k).ok && reloaded;
        e.reclaim();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const bool freedAll = waitUntil(onlyCurrent);
    const bool ranLast = waitUntil([&] { return lastTag.load() == 21; });
    running = false;
    audio.join();
    e.setAudioThreadOwns(false);
    e.reclaim();
    CHECK(started);
    INFO(first.error);
    REQUIRE(first.ok);
    REQUIRE(old != nullptr);
    CHECK(old->standing == ScriptHost::PackStanding::Retired);
    CHECK(old->records == 1);
    CHECK(retiredTypes == 1);
    CHECK(freedFirst);
    CHECK(ranFirst);
    CHECK(reloaded);
    CHECK(freedAll);
    CHECK(ranLast);
    CHECK(packIds(e).size() == 2);
    CHECK(e.scriptInstances() == 1);
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

TEST_CASE("a reload refused under an audio thread after its replan made a fresh instance leaves its generation listed, "
          "discarded, until that instance's record comes back, and the block thread's release of it, then the reclaim "
          "that finds it back, frees the generation while the old code runs on",
          "[script][packs][reload]") {
    Engine e(kRate);
    load(e, "test.held", toys(tally("test.held.count", 1)));
    place(e, document({docNode("count", "test.held.count"), midiOut("1")}, {docWire("count.out", "midi-out.in1")}));
    CHECK(block(e) == std::vector<Out>{{1, 0, {1, 1}}});
    const std::uint32_t instances = e.scriptInstances();
    const std::size_t types = e.nodeTypeCount();
    // The test thread is the block thread from here: a push only queues, so the ring fills, and the replan, planned
    // with a fresh instance of the new generation's node, finds no room for its swap.
    e.setAudioThreadOwns(true);
    DspCommand bpm;
    bpm.kind = DspCommand::Kind::SetBpm;
    bpm.setBpm = {120.0};
    while (e.pushCommand(bpm)) {
    }
    const auto refused = e.loadScriptPack("test.held", toys(tally("test.held.count", 2)));
    INFO(refused.error);
    CHECK_FALSE(refused.ok);
    REQUIRE(refused.errors.size() == 1);
    CHECK(refused.errors[0].code == "busy");
    // A planner error with no node says so with none named.
    CHECK(refused.error == "the running document does not plan with the reloaded pack: busy: the command ring is full");
    CHECK(refused.types.empty());
    // That instance went with the refused plan, its release queued to the block thread, so its record is out: the new
    // generation is held, discarded, while the registry has the old one's entries back, none of them retired.
    const auto held = e.scriptPacks();
    REQUIRE(held.size() == 3);
    CHECK(held[1].id == "test.held");
    CHECK(held[1].generation == 1);
    CHECK(held[1].standing == ScriptHost::PackStanding::Current);
    CHECK(held[1].records == 1);
    CHECK(held[2].id == "test.held");
    CHECK(held[2].generation == 2);
    CHECK(held[2].standing == ScriptHost::PackStanding::Discarded);
    CHECK(held[2].records == 1);
    CHECK(e.retiredNodeTypeCount() == 0);
    CHECK(e.nodeTypeCount() == types);

    // The ring drained, then a block, whose top releases that instance in the discarded generation's runtime (which
    // must still be there), and the reclaim that deletes its record frees the generation with it.
    e.drainCommands();
    CHECK(block(e) == std::vector<Out>{{1, 0, {1, 2}}});
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.held#1", "test.held#2"});
    e.reclaim();
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.held#1"});
    CHECK(e.retiredNodeTypeCount() == 0);
    CHECK(e.nodeTypeCount() == types);
    CHECK(e.scriptInstances() == instances);
    // The type found is the old generation's again: a second node of it, placed now, runs the old code beside the
    // first, which keeps its instance.
    place(e, document({docNode("count", "test.held.count"), docNode("count2", "test.held.count"), midiOut("1,2")},
                      {docWire("count.out", "midi-out.in1"), docWire("count2.out", "midi-out.in2")}));
    e.drainCommands();
    CHECK(block(e) == sorted({{1, 0, {1, 3}}, {2, 0, {1, 1}}}));
    e.setAudioThreadOwns(false);
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

TEST_CASE("under an audio thread, a load reclaims first: the generation a reload replaced is freed by the first load "
          "after a block has released its node, a load its compile refuses and one of another id among them",
          "[script][packs][reload]") {
    Engine e(kRate);
    load(e, "test.back", toys(tally("test.back.count", 1)));
    place(e, document({docNode("count", "test.back.count"), midiOut("1")}, {docWire("count.out", "midi-out.in1")}));
    CHECK(block(e) == std::vector<Out>{{1, 0, {1, 1}}});
    // The test thread is the block thread from here, and nothing reclaims but the loads, as in the plugin and SDL,
    // which reclaim only inside setGraph and a load.
    e.setAudioThreadOwns(true);
    const auto drained = [&] {
        e.drainCommands();
        return block(e);
    };
    const auto held = [&](std::uint32_t generation) {
        const auto packs = e.scriptPacks();
        const ScriptHost::PackInfo* p = findPack(packs, "test.back", generation);
        return p && p->standing == ScriptHost::PackStanding::Retired && p->records == 1;
    };
    // The first reload's replan is queued, so the generation it replaced is retired with its node's record out; the
    // block adopts the replan and hands the plan that held that node back.
    load(e, "test.back", toys(tally("test.back.count", 2)));
    CHECK(held(1));
    CHECK(drained() == std::vector<Out>{{1, 0, {2, 1}}});
    // The second reload's reclaim frees that plan, which queues the node's release to the block thread: its record is
    // not back, so the first generation is still held, and the second is retired beside it.
    load(e, "test.back", toys(tally("test.back.count", 3)));
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.back#1", "test.back#2", "test.back#3"});
    CHECK(held(1));
    CHECK(held(2));
    CHECK(e.retiredNodeTypeCount() == 2);
    // A block releases that node at its top and hands its record back, which no reclaim has found yet.
    CHECK(drained() == std::vector<Out>{{1, 0, {3, 1}}});
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.back#1", "test.back#2", "test.back#3"});
    // A load its compile refuses still reclaims first: the record found back frees the first generation, its retired
    // entries with it, and the plan holding the second's node, handed back at that block, is freed, queuing its node's
    // release to the block thread.
    const auto typo = e.loadScriptPack("test.back", "var x = ;");
    CHECK_FALSE(typo.ok);
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.back#2", "test.back#3"});
    CHECK(held(2));
    CHECK(e.retiredNodeTypeCount() == 1);
    CHECK(drained() == std::vector<Out>{{1, 0, {3, 2}}});
    // And a load of another id, which replans nothing, frees the second.
    load(e, "test.beside", toys(tally("test.beside.count", 9)));
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.back#3", "test.beside#1"});
    CHECK(e.retiredNodeTypeCount() == 0);
    CHECK(drained() == std::vector<Out>{{1, 0, {3, 3}}});
    e.setAudioThreadOwns(false);
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

TEST_CASE("a pack's generation numbers only increase: every load of its id takes the next, refused or not, and however "
          "soon a refused one was freed, so no number names two sources",
          "[script][packs][reload]") {
    Engine e(kRate);
    load(e, "test.gen", toys(tally("test.gen.count", 1)));
    place(e, document({docNode("count", "test.gen.count"), midiOut("1")}, {docWire("count.out", "midi-out.in1")}));
    CHECK(block(e) == std::vector<Out>{{1, 0, {1, 1}}});
    // Generation 2, its replan refused (the type the document places gone), freed within the call; generation 3, its
    // load refused (its top level throws).
    CHECK_FALSE(e.loadScriptPack("test.gen", toys(tally("test.gen.other", 2))).ok);
    CHECK_FALSE(e.loadScriptPack("test.gen", toys(tally("test.gen.count", 3) + "throw new Error('not this one');")).ok);
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.gen#1"});
    load(e, "test.gen", toys(tally("test.gen.count", 4)));
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.gen#4"});
    CHECK(block(e) == std::vector<Out>{{1, 0, {4, 1}}});

    // Generation 5, refused busy under an audio thread and so held, discarded, until a block releases its fresh
    // instance and a reclaim finds the record back. Its number is spent as it is made, freed or not; the next load,
    // checked here once it is freed, is 6.
    e.setAudioThreadOwns(true);
    DspCommand bpm;
    bpm.kind = DspCommand::Kind::SetBpm;
    bpm.setBpm = {120.0};
    while (e.pushCommand(bpm)) {
    }
    CHECK_FALSE(e.loadScriptPack("test.gen", toys(tally("test.gen.count", 5))).ok);
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.gen#4", "test.gen#5"});
    e.drainCommands();
    block(e);
    e.reclaim();
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.gen#4"});
    e.setAudioThreadOwns(false);
    load(e, "test.gen", toys(tally("test.gen.count", 6)));
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.gen#6"});
    CHECK(block(e) == std::vector<Out>{{1, 0, {6, 1}}});
    // Each id counts its own loads.
    load(e, "test.gen.b", toys(tally("test.gen.b.count", 1)));
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.gen#6", "test.gen.b#1"});
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

TEST_CASE("a reload's replan carries the running plan's tap as it is: neither restored nor armed again",
          "[script][packs][reload]") {
    using rp::host::test::DocSystem;
    using rp::host::test::kNesType;
    using rp::host::test::rearming;
    using rp::host::test::withRoms;
    Engine e(kRate);
    rp::host::test::Roms roms;
    std::vector<DocSystem> systems{{1, kNesType}};
    withRoms(e, systems, roms);
    load(e, "test.tap", toys(tally("test.tap.count", 1)));
    const auto request = graphRequest(stereoDocument(systems,
                                                     DocExtras{{docNode("count", "test.tap.count"), midiOut("1")},
                                                               {docWire("count.out", "midi-out.in1")}}),
                                      {1});
    place(e, rearming(request, 1, 1));  // the tap armed to mode 1 from the core's built mode 0
    auto* nes = dynamic_cast<MesenNesSystem*>(e.findSystem(1));
    REQUIRE(nes != nullptr);
    CHECK(nes->channelExportMode() == 1);
    CHECK(block(e) == std::vector<Out>{{1, 0, {1, 1}}});
    // Moved behind the engine's back, so a replan that restored the tap (to 0) or armed it again (to 1) would show.
    nes->setChannelExportMode(2);
    load(e, "test.tap", toys(tally("test.tap.count", 2)));
    CHECK(block(e) == std::vector<Out>{{1, 0, {2, 1}}});  // the pack's node rebuilt on the new code
    CHECK(nes->channelExportMode() == 2);
    // And the engine still holds what it armed: a re-arm with nothing to arm restores the mode the core had before.
    place(e, rearming(request));
    CHECK(nes->channelExportMode() == 0);
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

#ifdef RETROPLUG_PROFILE
namespace {

// A pack of one node, test.loop.count, a tally that on its first block keeps 32 KiB in its realm (a global), which
// outlives its instance and goes only with its pack's runtimes.
std::string keeper(int tag) {
    return toys("toy.define('test.loop.count', [toy.output('out')], function () {\n"
                "  var n = 0;\n"
                "  return { process: function (b) {\n"
                "    if (n === 0) globalThis.kept = new Float64Array(4096);\n"
                "    b.send('out', 0, [" + std::to_string(tag) + ", ++n & 0x7f]);\n"
                "  } };\n"
                "});\n");
}

}  // namespace

TEST_CASE("a type keeps its span label across its pack's reloads, the trace's name table does not grow, and what each "
          "freed generation's realm kept leaves the live heap with it",
          "[script][packs][reload][profile][trace]") {
    const auto source = keeper;
    Engine e(kRate);
    load(e, "test.loop", source(0));
    place(e, document({docNode("count", "test.loop.count"), midiOut("1")}, {docWire("count.out", "midi-out.in1")}));
    // The label of the node's process spans over one traced block, named by the trace's name table.
    const auto traced = [&] {
        REQUIRE(e.dspTraceReset(true));
        block(e);
        const auto names = e.dspTraceNames();
        std::vector<std::uint32_t> labels;
        for (const auto& span : e.dspTrace())
            if (span.label < names.size() && names[span.label] == "test.loop.count") labels.push_back(span.label);
        REQUIRE(e.dspTraceReset(false));
        REQUIRE(labels.size() == 1);
        return std::pair{labels[0], names.size()};
    };
    const auto [label, names] = traced();
    CHECK(label >= rp::host::DSP_SPAN_TYPE_BASE);
    REQUIRE(e.resetDspAllocStats(false));  // the allocation window, from one generation's realm holding its 32 KiB
    for (int k = 1; k <= 50; ++k) {
        INFO("reload " << k);
        load(e, "test.loop", source(k));
        CHECK(traced() == std::pair{label, names});
    }
    // Fifty generations each kept 32 KiB, and each took it with it: the live heap is where one generation's was.
    CHECK(std::abs(e.dspAllocStats().liveBytesDelta) < 16 * 1024);
}

TEST_CASE("a generation freed under an audio thread leaves what its realm kept in the live heap until the audio thread "
          "hands the runtimes back, which takes it off",
          "[script][packs][reload][profile]") {
    Engine e(kRate);
    load(e, "test.loop", keeper(1));
    place(e, document({docNode("count", "test.loop.count"), midiOut("1")}, {docWire("count.out", "midi-out.in1")}));
    CHECK(block(e) == std::vector<Out>{{1, 0, {1, 1}}});
    REQUIRE(e.resetDspAllocStats(false));  // the allocation window, from the first generation's realm holding its 32 KiB
    // The test thread is the block thread from here. The reload's replan is published, and the old generation retired
    // with its node's record out; the block that adopts the replan hands the old plan back, the reclaim after it frees
    // that plan, queuing the old node's release, the next block releases it, and the reclaim after that finds the
    // record back and frees the generation.
    e.setAudioThreadOwns(true);
    load(e, "test.loop", keeper(2));
    e.drainCommands();
    CHECK(block(e) == std::vector<Out>{{1, 0, {2, 1}}});
    e.reclaim();
    CHECK(block(e) == std::vector<Out>{{1, 0, {2, 2}}});
    e.reclaim();
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.loop#2"});
    // Its runtimes were freed on the control thread while the block thread may have been writing the live heap's count,
    // so what its realm kept is still in it, beside the new generation's 32 KiB, until no audio thread runs.
    CHECK(e.dspAllocStats().liveBytesDelta > 24 * 1024);
    e.setAudioThreadOwns(false);
    CHECK(std::abs(e.dspAllocStats().liveBytesDelta) < 16 * 1024);
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}
#endif
