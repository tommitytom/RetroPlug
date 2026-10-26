// The engine's host I/O nodes and the cart mirror (docs/modular-graph-phase-b3a-design.md sections 3.3 to 3.5),
// beside today's kernel stage: what the hosts stage reaches a plan through rp.host.midi-in and controller-in, what
// a plan sends reaches the buffers the hosts drain through rp.host.midi-out and controller-out, both producers of
// those buffers coexist, rp.n8.cart-mirror feeds the engine's core-byte sink, the Game Boy's serialOut carries
// MI.OUT while the engine's own gather still does, and a full queue's drops reach the engine and the harness RPC.
// Driven on a quiescent Engine, and on a NodeRig where a node's calls on the engine are what is checked.
//
// Run via `pnpm test:plugin engine-graph`.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "../graph/CountingAllocator.hpp"
#include "GraphDocs.hpp"
#include "NodeRig.hpp"
#include "host/dsp/ScriptCompiler.hpp"
#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/graph/HostIoNodes.hpp"
#include "host/rpc/EngineRpcService.hpp"
#include "system/SystemFactory.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

using rp::host::test::DocExtras;
using rp::host::test::docNode;
using rp::host::test::docWire;
using rp::host::test::graphRequest;
using rp::host::test::readFile;
using rp::host::test::stereoDocument;

namespace {

constexpr double kRate = 48000.0;
constexpr std::uint32_t kFrames = 1024;

const char* const kGb = rp::host::test::kGbType;
const char* const kNes = rp::host::test::kNesType;

std::unique_ptr<SameBoySystem> mgb(SystemId id) {
    SameBoyConfig cfg;
    cfg.model = SameBoyModel::DmgB;
    auto s = std::make_unique<SameBoySystem>(id, cfg, readFile(RP_MGB_ROM_PATH));
    s->onActivate(kRate);
    return s;
}

std::unique_ptr<SystemBase> bliptoaster(SystemId id) {
    MesenNesConfig cfg;
    cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
    auto s = std::make_unique<MesenNesSystem>(id, cfg, readFile(RP_BLIPTOASTER_ROM_PATH));
    s->onActivate(kRate);
    return s;
}

// A stand-in kernel, compiled as the control plane compiles the real one: two globals, `processBlock` its body.
void loadKernel(Engine& e, const std::string& processBlock) {
    const auto bc = dsp::compileToBytecode("function setSystems(json) {}\nfunction processBlock(input) {\n" + processBlock + "\n}\n");
    REQUIRE(bc.has_value());
    REQUIRE(e.loadKernel(*bc));
}

// One stereo block; the output, interleaved.
std::vector<float> block(Engine& e, std::uint32_t frames = kFrames) {
    std::vector<float> l(frames), r(frames), out;
    float* outs[2] = {l.data(), r.data()};
    e.processBlock(frames, outs, 2);
    for (std::uint32_t i = 0; i < frames; ++i) {
        out.push_back(l[i]);
        out.push_back(r[i]);
    }
    return out;
}

std::vector<std::uint8_t> noteOn(std::uint8_t key) { return {0x90, key, 100}; }

// A host MIDI-out message as rp.host.midi-out reads it: the emitting system's id, 4 bytes little-endian, then the MIDI.
std::vector<std::uint8_t> tagged(std::uint32_t system, std::vector<std::uint8_t> midi) {
    std::vector<std::uint8_t> out{static_cast<std::uint8_t>(system), static_cast<std::uint8_t>(system >> 8),
                                  static_cast<std::uint8_t>(system >> 16), static_cast<std::uint8_t>(system >> 24)};
    out.insert(out.end(), midi.begin(), midi.end());
    return out;
}

struct Out {
    std::uint32_t             system, frame;
    std::vector<std::uint8_t> data;
    bool operator==(const Out&) const = default;
};
std::vector<Out> midiOut(const Engine& e) {
    std::vector<Out> out;
    for (const auto& m : e.midiOut()) out.push_back({m.system, m.frame, m.data});
    return out;
}

// The host I/O nodes alone, on no systems: `nodes` and `wires`, and the host's stereo pair.
Engine::GraphRequest hostIoOnly(std::vector<std::string> nodes, std::vector<std::string> wires, bool kernel = false) {
    return graphRequest(stereoDocument({}, DocExtras{std::move(nodes), std::move(wires), kernel}), {});
}

// A lone BlipToaster NES rendered for 3.5 s in 1024-frame blocks, MIDI staged by `stage` ahead of each block (with
// the block's first frame): through rp.host.midi-in into its midi input, or through today's path, the kernel stage,
// with a stand-in kernel that forwards every staged message to the NES as core MIDI.
std::vector<float> renderNes(bool viaKernel, const std::function<void(Engine&, std::uint64_t)>& stage) {
    Engine e(kRate);
    REQUIRE(e.parkCore(bliptoaster(1)));
    DocExtras extras;
    extras.kernel = viaKernel;
    if (!viaKernel) {
        extras.nodes = {docNode("midi-in", "rp.host.midi-in")};
        extras.wires = {docWire("midi-in.midi", "sys-1.midi")};
    }
    REQUIRE(e.setGraph(graphRequest(stereoDocument({{1, kNes}}, extras), {1})).ok);
    if (viaKernel)
        loadKernel(e, "for (var i = 0; i < input.midiIn.length; i++) emitCoreMidi(1, input.midiIn[i].frame, input.midiIn[i].data);");
    std::vector<float> out;
    for (std::uint64_t at = 0; at < static_cast<std::uint64_t>(3.5 * kRate); at += kFrames) {
        stage(e, at);
        const auto b = block(e);
        out.insert(out.end(), b.begin(), b.end());
    }
    return out;
}

// The first frame two interleaved stereo renders differ at, or SIZE_MAX.
std::size_t firstDifference(const std::vector<float>& a, const std::vector<float>& b) {
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (a[i] != b[i]) return i / 2;
    return a.size() == b.size() ? SIZE_MAX : std::min(a.size(), b.size()) / 2;
}

// `messages` (frame, MIDI) staged ahead of the first block at or past `seconds`.
using Staged = std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>>;
std::function<void(Engine&, std::uint64_t)> stageAt(std::vector<std::pair<double, Staged>> plan) {
    return [plan = std::move(plan)](Engine& e, std::uint64_t at) {
        for (const auto& [seconds, messages] : plan) {
            const auto from = static_cast<std::uint64_t>(seconds * kRate);
            if (at < from || at >= from + kFrames) continue;
            for (const auto& [frame, bytes] : messages) e.stageMidi(frame, bytes);
        }
    };
}

}  // namespace

TEST_CASE("rp.host.midi-in hands a NES exactly what today's kernel stage hands it", "[engine-graph][host-io]") {
    // The plugin stages DAW events, then each moved automation slot's CC at frame 0, so a block can be [100, 300, 0].
    // A queue keeps frame order by clamping an earlier frame up, so midi-in sorts first, stably: the NES, whose FIFO
    // stable-sorts by frame, gets the frame-0 note at frame 0, and two frame-0 notes in the order they were staged.
    // BlipToaster plays the last note it is given on a channel, so either order shows. It ignores MIDI for its first
    // second or so (parity/scenes.ts BOOT_MS).
    const Staged ragged{{100, noteOn(60)}, {300, noteOn(62)}, {0, noteOn(64)}};
    const Staged ties{{0, noteOn(67)}, {0, noteOn(69)}};
    const auto viaMidiIn = renderNes(false, stageAt({{1.5, ragged}, {2.5, ties}}));
    const auto today = renderNes(true, stageAt({{1.5, ragged}, {2.5, ties}}));
    CHECK(firstDifference(viaMidiIn, today) == SIZE_MAX);

    // And each order is audible: the frame-0 note clamped behind the others (no sort), or the ties swapped (an
    // unstable one), renders something else from that block on.
    const auto clamped = renderNes(true, stageAt({{1.5, {{100, noteOn(60)}, {300, noteOn(62)}, {300, noteOn(64)}}}, {2.5, ties}}));
    const auto swapped = renderNes(true, stageAt({{1.5, ragged}, {2.5, {{0, noteOn(69)}, {0, noteOn(67)}}}}));
    const auto from = [](double seconds) { return static_cast<std::size_t>(seconds * kRate) / kFrames * kFrames; };
    CHECK(firstDifference(viaMidiIn, clamped) >= from(1.5));
    CHECK(firstDifference(viaMidiIn, clamped) < from(2.5));
    CHECK(firstDifference(viaMidiIn, swapped) >= from(2.5));
    CHECK(firstDifference(viaMidiIn, swapped) != SIZE_MAX);
}

TEST_CASE("rp.host.midi-in sorts and pushes the staged MIDI without allocating", "[engine-graph][host-io]") {
    Engine e(kRate);
    REQUIRE(e.setGraph(hostIoOnly({docNode("midi-in", "rp.host.midi-in")}, {})).ok);
    for (const std::uint32_t frame : {100u, 300u, 0u, 100u, 0u}) e.stageMidi(frame, noteOn(60));
    float l[64], r[64];
    float* outs[2] = {l, r};
    // No warm-up: a sort that allocates on its first call is caught too.
    rp::test::beginCountingAllocations();
    e.processBlock(64, outs, 2);
    const auto counted = rp::test::endCountingAllocations();
    CHECK(counted.allocations == 0);
    // The staged messages' own bytes, freed as the staging is cleared, as the kernel stage frees them today: each
    // staged message owns its bytes (DspRuntime::MidiIn).
    CHECK(counted.frees == 5);
}

TEST_CASE("rp.host.midi-out lands each message in Engine::midiOut, tagged with the system its prefix names",
          "[engine-graph][host-io]") {
    Engine e(kRate);
    REQUIRE(e.setGraph(hostIoOnly({docNode("midi-in", "rp.host.midi-in"), docNode("midi-out", "rp.host.midi-out")},
                                  {docWire("midi-in.midi", "midi-out.midi")}))
                .ok);
    e.stageMidi(7, tagged(2, {0x90, 60, 100}));
    e.stageMidi(9, tagged(513, {0xF8}));
    e.stageMidi(11, {3, 0, 0});  // too short to name a system: dropped
    block(e);
    CHECK(midiOut(e) == std::vector<Out>{{2, 7, {0x90, 60, 100}}, {513, 9, {0xF8}}});
}

TEST_CASE("the kernel stage and the host I/O nodes share the host's buffers, in plan order", "[engine-graph][host-io]") {
    // Both produce what the hosts drain after a block, so the engine starts each block's buffers empty itself and
    // each producer appends as it runs. A plan holding rp.host.midi-in or controller-in takes what is staged, so the
    // kernel stage then gets none of it. The stand-in kernel reports how many messages it was handed.
    const std::string kernel =
        "emitMidiOut(7, 3, [input.midiIn.length]);\n"
        "emitControllerOut([0xB0, 0, input.controllerIn ? input.controllerIn.length : 0]);";
    const auto controllerOut = [](const Engine& e) { return e.controllerOut(); };
    using Bytes = std::vector<std::vector<std::uint8_t>>;

    SECTION("with the host I/O nodes") {
        Engine e(kRate);
        REQUIRE(e.setGraph(hostIoOnly({docNode("midi-in", "rp.host.midi-in"), docNode("midi-out", "rp.host.midi-out"),
                                       docNode("controller-in", "rp.host.controller-in"),
                                       docNode("controller-out", "rp.host.controller-out")},
                                      {docWire("midi-in.midi", "midi-out.midi"), docWire("controller-in.midi", "controller-out.midi")},
                                      true))
                    .ok);
        loadKernel(e, kernel);
        e.stageMidi(4, tagged(5, {0xF8}));
        e.stageControllerMidi({0x90, 11, 127});
        block(e);
        CHECK(midiOut(e) == std::vector<Out>{{7, 3, {0}}, {5, 4, {0xF8}}});
        CHECK(controllerOut(e) == Bytes{{0xB0, 0, 0}, {0x90, 11, 127}});
        // Nothing staged, and nothing drained in between: the next block starts from empty buffers all the same.
        block(e);
        CHECK(midiOut(e) == std::vector<Out>{{7, 3, {0}}});
        CHECK(controllerOut(e) == Bytes{{0xB0, 0, 0}});
    }
    SECTION("with the kernel after them in plan order") {
        Engine e(kRate);
        REQUIRE(e.setGraph(hostIoOnly({docNode("midi-in", "rp.host.midi-in"), docNode("midi-out", "rp.host.midi-out"),
                                       docNode("kernel", "rp.dsp.kernel")},
                                      {docWire("midi-in.midi", "midi-out.midi")}))
                    .ok);
        loadKernel(e, kernel);
        e.stageMidi(4, tagged(5, {0xF8}));
        block(e);
        CHECK(midiOut(e) == std::vector<Out>{{5, 4, {0xF8}}, {7, 3, {0}}});
    }
    SECTION("without them, the kernel stage gets what is staged, as today") {
        Engine e(kRate);
        REQUIRE(e.setGraph(hostIoOnly({}, {}, true)).ok);
        loadKernel(e, kernel);
        e.stageMidi(0, {0xF8});
        e.stageMidi(0, {0xFA});
        e.stageControllerMidi({0x90, 11, 127});
        block(e);
        CHECK(midiOut(e) == std::vector<Out>{{7, 3, {2}}});
        CHECK(controllerOut(e) == Bytes{{0xB0, 0, 1}});
    }
}

TEST_CASE("rp.host.controller-in's connected output follows the engine's flag from frame 0", "[engine-graph][host-io]") {
    using rp::host::test::NodeRig;
    using rp::host::test::Probe;
    NodeRig rig(kRate);
    REQUIRE(rig.set(R"({"nodes":[{"id":"surface","type":"rp.host.controller-in"},{"id":"probe","type":"rp.test.probe"}],
                        "wires":[{"from":"surface.connected","to":"probe.control"}]})")
                .ok);
    Probe& probe = rig.node<Probe>("probe");
    const rp_transport t{kRate, 120.0, 0.0, 0};
    const auto run = [&](bool connected) {
        rig.connected = connected;
        rig.host->processBlock(64, t);
    };
    run(false);
    CHECK(probe.controlAtZero == 0.0);
    CHECK(probe.controlChanges.empty());
    run(true);  // the block it is attached: a change at frame 0
    CHECK(probe.controlAtZero == 1.0);
    CHECK(probe.controlChanges == std::vector<double>{1.0});
    run(true);  // then the value holds, with no change
    CHECK(probe.controlStart == 1.0);
    CHECK(probe.controlChanges.empty());
    run(false);
    CHECK(probe.controlAtZero == 0.0);
}

TEST_CASE("rp.n8.cart-mirror hands the cart every MIDI message, then every byte message, in queue order",
          "[engine-graph][host-io][n8]") {
    using rp::host::test::Feed;
    using rp::host::test::HostCall;
    using rp::host::test::NodeRig;
    NodeRig rig(kRate);
    REQUIRE(rig.set(R"({"nodes":[{"id":"feed","type":"rp.test.feed"},{"id":"cart","type":"rp.n8.cart-mirror","settings":{"system":1}}],
                        "wires":[{"from":"feed.midi","to":"cart.midi"},{"from":"feed.bytes","to":"cart.bytes"}]})")
                .ok);
    Feed& feed = rig.node<Feed>("feed");
    feed.next[Feed::Midi] = {{3, {0x90, 1, 100}}, {8, {}}, {8, {0x80, 1, 0}}};  // the empty one is skipped, as today
    feed.next[Feed::Bytes] = {{1, {0xF2, 0, 0}, RP_MESSAGE_FLUSH}, {5, {0xF8}}};
    rig.host->processBlock(64, rp_transport{kRate, 120.0, 0.0, 0});
    REQUIRE(rig.cart.size() == 4);
    const auto is = [](const HostCall& c, std::uint32_t frame, std::vector<std::uint8_t> data, bool flush) {
        return c.frame == frame && c.data == data && c.flush == flush;
    };
    CHECK(is(rig.cart[0], 3, {0x90, 1, 100}, false));
    CHECK(is(rig.cart[1], 8, {0x80, 1, 0}, false));
    CHECK(is(rig.cart[2], 1, {0xF2, 0, 0}, true));
    CHECK(is(rig.cart[3], 5, {0xF8}, false));
}

TEST_CASE("the engine feeds the cart mirror to its core-byte sink, and reads the system it mirrors off the plan",
          "[engine-graph][host-io][n8]") {
    Engine e(kRate);
    REQUIRE(e.parkCore(bliptoaster(1)));
    const auto mirrored = [](std::string settings) {
        return DocExtras{{docNode("midi-in", "rp.host.midi-in"), docNode("cart", "rp.n8.cart-mirror", std::move(settings))},
                         {docWire("midi-in.midi", "cart.midi")}};
    };
    CHECK(e.cartMirrorSystemId() == 0);
    REQUIRE(e.setGraph(graphRequest(stereoDocument({{1, kNes}}, mirrored(R"({"system":1})")), {1})).ok);
    CHECK(e.cartMirrorSystemId() == 1);

    // With no sink (the CLI, the tests), nothing goes anywhere.
    e.stageMidi(0, noteOn(1));
    block(e);
    std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> cart;
    e.setCoreByteSink([&](std::uint32_t frame, const std::uint8_t* d, std::size_t n, bool) { cart.push_back({frame, {d, d + n}}); });
    e.stageMidi(5, noteOn(2));
    block(e);
    REQUIRE(cart.size() == 1);
    CHECK(cart[0].first == 5);
    CHECK(cart[0].second == noteOn(2));

    // One mirror at most, naming one of the document's systems; and none is 0.
    auto two = mirrored(R"({"system":1})");
    two.nodes.push_back(docNode("cart-2", "rp.n8.cart-mirror", R"({"system":1})"));
    auto refused = e.setGraph(graphRequest(stereoDocument({{1, kNes}}, two), {1}));
    REQUIRE_FALSE(refused.ok);
    CHECK(refused.errors.at(0).code == "cart-mirror");
    refused = e.setGraph(graphRequest(stereoDocument({{1, kNes}}, mirrored(R"({"system":9})")), {1}));
    REQUIRE_FALSE(refused.ok);
    CHECK(refused.errors.at(0).code == "cart-mirror");
    CHECK(e.cartMirrorSystemId() == 1);
    REQUIRE(e.setGraph(graphRequest(stereoDocument({{1, kNes}}), {1})).ok);
    CHECK(e.cartMirrorSystemId() == 0);
}

TEST_CASE("the Game Boy's serialOut carries what its core captured, and the engine's MI.OUT gather still does",
          "[engine-graph][host-io]") {
    // mGB, with MI.OUT capture armed, has a byte clocked out of its serial port every eight frames (it holds the
    // port in external-clock receive). The port carries this block's bytes at frame 0, here into a cart mirror whose
    // sink records them; the gather hands the same bytes to the kernel stage next block, which a stand-in kernel
    // echoes as host MIDI out.
    Engine e(kRate);
    auto core = mgb(1);
    core->setSerialOutCapture(true);
    SameBoySystem* const gb = core.get();
    REQUIRE(e.parkCore(std::move(core)));
    DocExtras extras{{docNode("cart", "rp.n8.cart-mirror", R"({"system":1})")}, {docWire("sys-1.serialOut", "cart.bytes")}};
    REQUIRE(e.setGraph(graphRequest(stereoDocument({{1, kGb}}, extras), {1})).ok);
    loadKernel(e, "for (var i = 0; i < input.serialOut.length; i++) emitMidiOut(input.serialOut[i].system, 0, [input.serialOut[i].byte]);");
    std::vector<std::uint8_t> carried;
    bool eachAtZero = true;
    e.setCoreByteSink([&](std::uint32_t frame, const std::uint8_t* d, std::size_t n, bool) {
        eachAtZero = eachAtZero && frame == 0 && n == 1;
        carried.insert(carried.end(), d, d + n);
    });
    std::vector<std::uint8_t> last;  // what the core captured last block
    std::size_t captured = 0;
    for (int b = 0; b < 100; ++b) {
        carried.clear();
        block(e);
        std::vector<std::uint8_t> log, gathered;
        for (const auto& entry : gb->serialOutLog_) log.push_back(entry.second);
        for (const auto& m : e.midiOut()) {
            CHECK(m.system == 1);
            gathered.insert(gathered.end(), m.data.begin(), m.data.end());
        }
        INFO("block " << b);
        CHECK(carried == log);
        CHECK(gathered == last);
        last = log;
        captured += log.size();
    }
    CHECK(eachAtZero);
    CHECK(captured > 0);
}

TEST_CASE("a full output queue's drops reach the engine and the harness facet", "[engine-graph][host-io]") {
    Engine           e(kRate);
    SystemFactory    factory;
    QueuedInvoker    invoker(e);
    EngineRpcService rpc(e, factory, invoker);
    REQUIRE(e.setGraph(hostIoOnly({docNode("midi-in", "rp.host.midi-in"), docNode("midi-out", "rp.host.midi-out")},
                                  {docWire("midi-in.midi", "midi-out.midi")}))
                .ok);
    for (int i = 0; i < 3; ++i) e.stageMidi(0, tagged(1, {0xF8}));
    block(e);
    CHECK(e.droppedMessages() == 0);
    CHECK(rpc.droppedMessages() == 0);
    // One block's worth more than midi-in's port holds.
    for (std::uint32_t i = 0; i < rp::host::kHostMidiInMessages + 3; ++i) e.stageMidi(0, tagged(1, {0xF8}));
    block(e);
    CHECK(e.midiOut().size() == rp::host::kHostMidiInMessages);
    CHECK(e.droppedMessages() == 3);
    CHECK(rpc.droppedMessages() == 3);
}
