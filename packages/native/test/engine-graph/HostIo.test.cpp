// The engine's host I/O nodes and the cart mirror (docs/modular-graph-phase-b3a-design.md sections 3.3 to 3.5): what
// the hosts stage reaches a plan through rp.host.midi-in and controller-in (a plan without one drops what it would
// take, and a block too long to run drops it all), what a plan sends reaches the buffers the hosts drain through
// rp.host.midi-out and controller-out, rp.n8.cart-mirror feeds the engine's core-byte sink, the Game Boy's serialOut
// carries MI.OUT, and a full queue's drops reach the engine and the harness RPC. A document holds one midi-in, one
// controller-in and one cart mirror at most, the mirror naming a NES. Driven on a quiescent Engine, and on a NodeRig
// where a node's calls on the engine are what is checked.
//
// Run via `pnpm test:plugin engine-graph`.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <quickjs.h>

#include "../graph/CountingAllocator.hpp"
#include "GraphDocs.hpp"
#include "NodeRig.hpp"
#include "TypedRpcServer.h"
#include "codecs/QuickJSCodec.h"
#include "graph/GraphHost.hpp"
#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/graph/HostIoNodes.hpp"
#include "host/rpc/BackendRpcRegistration.hpp"
#include "host/rpc/EngineRpcService.hpp"
#include "system/SystemFactory.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "transports/QuickJSTransport.h"

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
Engine::GraphRequest hostIoOnly(std::vector<std::string> nodes, std::vector<std::string> wires) {
    return graphRequest(stereoDocument({}, DocExtras{std::move(nodes), std::move(wires)}), {});
}

// A lone BlipToaster NES rendered for 3.5 s in 1024-frame blocks, MIDI staged by `stage` ahead of each block (with
// the block's first frame), through rp.host.midi-in into its midi input.
std::vector<float> renderNes(const std::function<void(Engine&, std::uint64_t)>& stage) {
    Engine e(kRate);
    REQUIRE(e.parkCore(bliptoaster(1)));
    const DocExtras extras{{docNode("midi-in", "rp.host.midi-in")}, {docWire("midi-in.midi", "sys-1.midi")}};
    REQUIRE(e.setGraph(graphRequest(stereoDocument({{1, kNes}}, extras), {1})).ok);
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

// rp.host.midi-in wired straight to rp.host.midi-out, so what midi-in hands on lands in Engine::midiOut.
Engine::GraphRequest midiInToOut() {
    return hostIoOnly({docNode("midi-in", "rp.host.midi-in"), docNode("midi-out", "rp.host.midi-out")},
                      {docWire("midi-in.midi", "midi-out.midi")});
}

// All four host I/O nodes, each input wired to its output: what midi-in and controller-in hand on lands in
// Engine::midiOut and controllerOut.
Engine::GraphRequest hostIoLoop() {
    return hostIoOnly({docNode("midi-in", "rp.host.midi-in"), docNode("midi-out", "rp.host.midi-out"),
                       docNode("controller-in", "rp.host.controller-in"), docNode("controller-out", "rp.host.controller-out")},
                      {docWire("midi-in.midi", "midi-out.midi"), docWire("controller-in.midi", "controller-out.midi")});
}

// (frame, staging index) pairs, in the order a block's messages arrive.
using Order = std::vector<std::pair<std::uint32_t, std::uint32_t>>;

// One block's staging at `frames`, through midiInToOut: each message staged under its index as the system id
// midi-out reads, so Engine::midiOut says which message each one was.
Order midiInOrder(Engine& e, const std::vector<std::uint32_t>& frames) {
    for (std::uint32_t i = 0; i < frames.size(); ++i) e.stageMidi(frames[i], tagged(i, {0xF8}));
    block(e);
    Order out;
    for (const auto& m : e.midiOut()) out.push_back({m.frame, m.system});
    return out;
}

// The same staging stable-sorted by frame, as std::stable_sort orders it.
Order stableOrder(const std::vector<std::uint32_t>& frames) {
    Order out;
    for (std::uint32_t i = 0; i < frames.size(); ++i) out.push_back({frames[i], i});
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return out;
}

// A Game Boy (sys-1, MI.OUT capture armed) and a NES (sys-2) that the cart mirror names, the Game Boy's serialOut
// wired to the mirror's bytes input: the core-byte sink sees every byte the port carries. The Game Boy's core.
SameBoySystem* serialOutToCart(Engine& e) {
    auto core = mgb(1);
    core->setSerialOutCapture(true);
    SameBoySystem* const gb = core.get();
    REQUIRE(e.parkCore(std::move(core)));
    REQUIRE(e.parkCore(bliptoaster(2)));
    DocExtras extras{{docNode("cart", "rp.n8.cart-mirror", R"({"system":2})")}, {docWire("sys-1.serialOut", "cart.bytes")}};
    REQUIRE(e.setGraph(graphRequest(stereoDocument({{1, kGb}, {2, kNes}}, extras), {1, 2})).ok);
    return gb;
}

// The bytes a Game Boy's core captured this block.
std::vector<std::uint8_t> serialLog(const SameBoySystem& gb) {
    std::vector<std::uint8_t> log;
    for (const auto& entry : gb.serialOutLog_) log.push_back(entry.second);
    return log;
}

// Set RETROPLUG_SYNC_TRACE, or clear it (null), for the engines constructed after: each reads it as it is built.
void setSyncTraceEnv(const char* value) {
#if defined(_WIN32)
    _putenv_s("RETROPLUG_SYNC_TRACE", value ? value : "");
#else
    if (value) setenv("RETROPLUG_SYNC_TRACE", value, 1);
    else unsetenv("RETROPLUG_SYNC_TRACE");
#endif
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

TEST_CASE("rp.host.midi-in hands a NES its staged MIDI in frame order, ties in staging order", "[engine-graph][host-io]") {
    // The plugin stages DAW events, then each moved automation slot's CC at frame 0, so a block can be [100, 300, 0].
    // A queue keeps frame order by clamping an earlier frame up, so midi-in sorts first, stably: the NES gets what it
    // gets from a host that stages in frame order (the frame-0 note at frame 0), and two frame-0 notes in the order
    // they were staged, as its FIFO, which stable-sorts by frame, always gave them. BlipToaster plays the last note it
    // is given on a channel, so either order shows. It ignores MIDI for its first second or so (parity/scenes.ts
    // BOOT_MS).
    const Staged ragged{{100, noteOn(60)}, {300, noteOn(62)}, {0, noteOn(64)}};
    const Staged ties{{0, noteOn(67)}, {0, noteOn(69)}};
    const auto viaMidiIn = renderNes(stageAt({{1.5, ragged}, {2.5, ties}}));
    const auto inOrder = renderNes(stageAt({{1.5, {{0, noteOn(64)}, {100, noteOn(60)}, {300, noteOn(62)}}}, {2.5, ties}}));
    CHECK(firstDifference(viaMidiIn, inOrder) == SIZE_MAX);

    // And each order is audible: the frame-0 note clamped behind the others (no sort), or the ties swapped (an
    // unstable one), renders something else from that block on.
    const auto clamped = renderNes(stageAt({{1.5, {{100, noteOn(60)}, {300, noteOn(62)}, {300, noteOn(64)}}}, {2.5, ties}}));
    const auto swapped = renderNes(stageAt({{1.5, ragged}, {2.5, {{0, noteOn(69)}, {0, noteOn(67)}}}}));
    const auto from = [](double seconds) { return static_cast<std::size_t>(seconds * kRate) / kFrames * kFrames; };
    CHECK(firstDifference(viaMidiIn, clamped) >= from(1.5));
    CHECK(firstDifference(viaMidiIn, clamped) < from(2.5));
    CHECK(firstDifference(viaMidiIn, swapped) >= from(2.5));
    CHECK(firstDifference(viaMidiIn, swapped) != SIZE_MAX);
}

TEST_CASE("the engine stages host MIDI, and rp.host.midi-in sorts and pushes it, without allocating", "[engine-graph][host-io]") {
    // Both of midi-in's paths: the merge of two runs (the plugin's staging: its DAW events, then automation's CCs at
    // frame 0), and the sort it falls back to for more.
    Engine e(kRate);
    REQUIRE(e.setGraph(hostIoOnly({docNode("midi-in", "rp.host.midi-in")}, {})).ok);
    const std::vector<std::uint8_t> note = noteOn(60), sysex{0xF0, 0x7D, 1, 2, 3, 4, 5, 6, 7, 0xF7};
    float l[64], r[64];
    float* outs[2] = {l, r};
    for (const auto& frames : {std::vector<std::uint32_t>{100, 300, 0, 0}, std::vector<std::uint32_t>{100, 300, 0, 100, 0}}) {
        INFO(frames.size() << " staged");
        // No warm-up: a staging or a path that allocates on its first call is caught too. The staging holds what
        // midi-in carries in a block from the engine's construction.
        rp::test::beginCountingAllocations();
        for (const std::uint32_t frame : frames) e.stageMidi(frame, note);
        e.stageMidi(7, sysex);
        e.stageControllerMidi(note);  // no controller-in: dropped at the block's end
        e.processBlock(64, outs, 2);
        const auto counted = rp::test::endCountingAllocations();
        CHECK(counted.allocations == 0);
        CHECK(counted.frees == 0);
    }
}

TEST_CASE("rp.host.midi-in hands on the staging in frame order, ties in the order they were staged",
          "[engine-graph][host-io]") {
    Engine e(kRate);
    REQUIRE(e.setGraph(midiInToOut()).ok);
    // One run.
    CHECK(midiInOrder(e, {0, 0, 5, 9}) == Order{{0, 0}, {0, 1}, {5, 2}, {9, 3}});
    // Two, as the plugin stages them: a tie within a run keeps its order, and one across the runs goes to the earlier.
    CHECK(midiInOrder(e, {100, 300, 0, 0}) == Order{{0, 2}, {0, 3}, {100, 0}, {300, 1}});
    CHECK(midiInOrder(e, {0, 100, 0, 100}) == Order{{0, 0}, {0, 2}, {100, 1}, {100, 3}});
    CHECK(midiInOrder(e, {7, 7, 7, 0, 7}) == Order{{0, 3}, {7, 0}, {7, 1}, {7, 2}, {7, 4}});
    // Three.
    CHECK(midiInOrder(e, {100, 300, 0, 100, 0}) == Order{{0, 2}, {0, 4}, {100, 0}, {100, 3}, {300, 1}});
    CHECK(midiInOrder(e, {}).empty());
    CHECK(e.droppedMessages() == 0);
}

TEST_CASE("rp.host.midi-in's order is std::stable_sort's by frame, for any staging", "[engine-graph][host-io]") {
    // Stagings of every shape its paths tell apart: one run, two, more; ties within a run and across runs; every
    // frame the same; and runs left unsorted, so a run is any length down to one message.
    Engine e(kRate);
    REQUIRE(e.setGraph(midiInToOut()).ok);
    std::mt19937 rng(20261026);
    const auto pick = [&](std::uint32_t n) { return std::uniform_int_distribution<std::uint32_t>(0, n - 1)(rng); };
    const std::uint32_t spans[] = {1, 3, 16, kFrames};  // the frames a staging draws from; 1: all the same
    for (int trial = 0; trial < 3000; ++trial) {
        const std::uint32_t span = spans[pick(4)];
        const bool sortedRuns = trial % 4 != 3;
        std::vector<std::uint32_t> frames;
        for (std::uint32_t run = 1 + pick(5); run > 0; --run) {
            std::vector<std::uint32_t> part(pick(40));
            for (auto& f : part) f = pick(span);
            if (sortedRuns) std::sort(part.begin(), part.end());
            frames.insert(frames.end(), part.begin(), part.end());
        }
        INFO("trial " << trial << ": " << frames.size() << " messages over " << span << " frames");
        REQUIRE(midiInOrder(e, frames) == stableOrder(frames));
    }
    CHECK(e.droppedMessages() == 0);
}

TEST_CASE("host MIDI staged through the command ring is drained and taken without allocating on the block thread",
          "[engine-graph][host-io]") {
    // The ring carries a channel message inline: drained on the block thread, its bytes are copied into the staging.
    Engine        e(kRate);
    QueuedInvoker invoker(e);
    REQUIRE(e.setGraph(hostIoOnly({docNode("midi-in", "rp.host.midi-in")}, {})).ok);
    float l[64], r[64];
    float* outs[2] = {l, r};
    invoker.setAudioThreadOwns(true);  // pushes queue for the block thread, which this thread is now
    for (const std::uint32_t frame : {9u, 3u, 0u}) invoker.stageMidi(noteOn(60), frame);
    REQUIRE(invoker.stageControllerMidi({0x90, 11, 127}));
    rp::test::beginCountingAllocations();
    invoker.drainInto(e);
    e.processBlock(64, outs, 2);
    const auto counted = rp::test::endCountingAllocations();
    CHECK(counted.allocations == 0);

    // And it was taken: a plan reading midi-in's output gets the next block's messages, and those alone.
    REQUIRE(e.setGraph(hostIoOnly({docNode("midi-in", "rp.host.midi-in"), docNode("midi-out", "rp.host.midi-out")},
                                  {docWire("midi-in.midi", "midi-out.midi")}))
                .ok);
    invoker.stageMidi(tagged(4, {0xF8}), 5);
    invoker.stageMidi(tagged(4, {0xFA}), 2);
    invoker.drainInto(e);
    e.processBlock(64, outs, 2);
    invoker.setAudioThreadOwns(false);
    CHECK(midiOut(e) == std::vector<Out>{{4, 2, {0xFA}}, {4, 5, {0xF8}}});
}

TEST_CASE("rp.host.midi-out lands each message in Engine::midiOut, tagged with the system its prefix names",
          "[engine-graph][host-io]") {
    Engine e(kRate);
    REQUIRE(e.setGraph(hostIoOnly({docNode("midi-in", "rp.host.midi-in"), docNode("midi-out", "rp.host.midi-out")},
                                  {docWire("midi-in.midi", "midi-out.midi")}))
                .ok);
    e.stageMidi(7, tagged(2, {0x90, 60, 100}));
    e.stageMidi(9, tagged(513, {0xF8}));
    e.stageMidi(11, {3, 0, 0});  // too short to name a system: dropped, and counted
    block(e);
    CHECK(midiOut(e) == std::vector<Out>{{2, 7, {0x90, 60, 100}}, {513, 9, {0xF8}}});
    CHECK(e.droppedMessages() == 1);
}

TEST_CASE("each block's host buffers start empty, and a plan with no input node drops what was staged",
          "[engine-graph][host-io]") {
    // The engine empties what the hosts drain at the top of each block, and the plan's output nodes fill it. What is
    // staged belongs to the next block: a plan holding rp.host.midi-in (controller-in) takes it then, and a plan with
    // neither drops it at that block's end rather than carrying it into a later one.
    const auto controllerOut = [](const Engine& e) { return e.controllerOut(); };
    using Bytes = std::vector<std::vector<std::uint8_t>>;
    const auto io = hostIoLoop();

    SECTION("with the host I/O nodes") {
        Engine e(kRate);
        REQUIRE(e.setGraph(io).ok);
        e.stageMidi(4, tagged(5, {0xF8}));
        e.stageControllerMidi({0x90, 11, 127});
        block(e);
        CHECK(midiOut(e) == std::vector<Out>{{5, 4, {0xF8}}});
        CHECK(controllerOut(e) == Bytes{{0x90, 11, 127}});
        // Nothing staged, and nothing drained in between: the next block starts from empty buffers all the same.
        block(e);
        CHECK(midiOut(e).empty());
        CHECK(controllerOut(e).empty());
    }
    SECTION("without them") {
        Engine e(kRate);
        REQUIRE(e.setGraph(hostIoOnly({}, {})).ok);
        e.stageMidi(4, tagged(5, {0xF8}));
        e.stageControllerMidi({0x90, 11, 127});
        block(e);
        REQUIRE(e.setGraph(io).ok);
        block(e);
        CHECK(midiOut(e).empty());
        CHECK(controllerOut(e).empty());
    }
    SECTION("with controller-in alone, it takes the control surface's and the host MIDI is dropped") {
        Engine e(kRate);
        REQUIRE(e.setGraph(hostIoOnly({docNode("controller-in", "rp.host.controller-in"),
                                       docNode("controller-out", "rp.host.controller-out")},
                                      {docWire("controller-in.midi", "controller-out.midi")}))
                    .ok);
        e.stageMidi(4, tagged(5, {0xF8}));
        e.stageControllerMidi({0x90, 11, 127});
        block(e);
        CHECK(controllerOut(e) == Bytes{{0x90, 11, 127}});
        REQUIRE(e.setGraph(io).ok);
        block(e);
        CHECK(midiOut(e).empty());
        CHECK(controllerOut(e).empty());
    }
    SECTION("with midi-in alone, it takes the host MIDI and the control surface's is dropped") {
        Engine e(kRate);
        REQUIRE(e.setGraph(midiInToOut()).ok);
        e.stageMidi(4, tagged(5, {0xF8}));
        e.stageControllerMidi({0x90, 11, 127});
        block(e);
        CHECK(midiOut(e) == std::vector<Out>{{5, 4, {0xF8}}});
        REQUIRE(e.setGraph(io).ok);
        block(e);
        CHECK(midiOut(e).empty());
        CHECK(controllerOut(e).empty());
    }
    SECTION("a swap to a plan holding midi-in hands it what was staged before the swap, once") {
        Engine e(kRate);
        REQUIRE(e.setGraph(hostIoOnly({}, {})).ok);
        e.stageMidi(4, tagged(5, {0xF8}));
        REQUIRE(e.setGraph(hostIoOnly({docNode("midi-in", "rp.host.midi-in"), docNode("midi-out", "rp.host.midi-out")},
                                      {docWire("midi-in.midi", "midi-out.midi")}))
                    .ok);
        block(e);
        CHECK(midiOut(e) == std::vector<Out>{{5, 4, {0xF8}}});
        block(e);
        CHECK(midiOut(e).empty());
    }
}

TEST_CASE("a block too long to run drops what was staged for it, without allocating", "[engine-graph][host-io]") {
    // A block past the plan's buffers runs nothing, its input nodes included, so what the hosts staged for it goes with
    // it, as a block that ran would leave none of it: a host whose every block is too long would otherwise grow the
    // staging without end, and hand the next block that runs messages at another block's frames.
    Engine e(kRate);
    REQUIRE(e.setGraph(hostIoLoop()).ok);
    constexpr std::uint32_t kLong = Engine::kDefaultMaxBlockFrames + 1;
    std::vector<float> l(kLong), r(kLong);
    float* outs[2] = {l.data(), r.data()};
    e.stageMidi(4, tagged(5, {0xF8}));
    e.stageControllerMidi({0x90, 11, 127});
    rp::test::beginCountingAllocations();
    e.processBlock(kLong, outs, 2);
    const auto counted = rp::test::endCountingAllocations();
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);  // the staging is cleared, keeping its capacity: no message owns its bytes
    block(e);
    CHECK(midiOut(e).empty());
    CHECK(e.controllerOut().empty());
}

TEST_CASE("a document holds one rp.host.midi-in and one rp.host.controller-in at most, as it holds one cart mirror",
          "[engine-graph][host-io]") {
    // Each takes everything staged whether or not its output is wired, so a second would take it from the first
    // whenever the plan ran it first.
    Engine e(kRate);
    for (const std::string type : {"rp.host.midi-in", "rp.host.controller-in"}) {
        INFO(type);
        const auto refused = e.setGraph(hostIoOnly({docNode("in-a", type), docNode("in-b", type)}, {}));
        REQUIRE_FALSE(refused.ok);
        REQUIRE(refused.errors.size() == 1);
        CHECK(refused.errors[0].nodeId == "in-b");
        CHECK(refused.errors[0].code == type.substr(std::string("rp.host.").size()));
        CHECK(e.setGraph(hostIoOnly({docNode("in-a", type)}, {})).ok);
    }
    CHECK(e.setGraph(hostIoOnly({docNode("midi-in", "rp.host.midi-in"), docNode("controller-in", "rp.host.controller-in")}, {})).ok);
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

TEST_CASE("a cart mirror names one of the document's NES systems, its settings read as the node reads them",
          "[engine-graph][host-io][n8]") {
    Engine e(kRate);
    REQUIRE(e.parkCore(bliptoaster(1)));
    REQUIRE(e.parkCore(mgb(2)));
    const auto mirroring = [](std::string settings) {
        const DocExtras mirror{{docNode("cart", "rp.n8.cart-mirror", std::move(settings))}, {}};
        return graphRequest(stereoDocument({{1, kNes}, {2, kGb}}, mirror), {1, 2});
    };
    // An N8 is a NES cart: a Game Boy is refused, as a system outside the slots is.
    const auto gb = e.setGraph(mirroring(R"({"system":2})"));
    REQUIRE_FALSE(gb.ok);
    REQUIRE(gb.errors.size() == 1);
    CHECK(gb.errors[0].nodeId == "cart");
    CHECK(gb.errors[0].code == "cart-mirror");
    // What the node refuses, the engine refuses; what the node reads, the engine reads, a field neither knows included.
    for (const char* refused : {R"({})", R"({"system":0})", R"({"system":"1"})", R"({"system":4294967296})"}) {
        INFO(refused);
        CHECK(e.setGraph(mirroring(refused)).errors.at(0).code == "cart-mirror");
    }
    REQUIRE(e.setGraph(mirroring(R"({"system":1,"note":"the first NES"})")).ok);
    CHECK(e.cartMirrorSystemId() == 1);
}

TEST_CASE("the Game Boy's serialOut carries what its core captured, each byte at frame 0", "[engine-graph][host-io]") {
    // mGB, with MI.OUT capture armed, has a byte clocked out of its serial port every eight frames (it holds the
    // port in external-clock receive). The port carries this block's bytes at frame 0, here into a cart mirror whose
    // sink records them. (buildGraph wires it into the kernel node as a feedback wire: MI.OUT's one block.)
    Engine e(kRate);
    SameBoySystem* const gb = serialOutToCart(e);
    std::vector<std::uint8_t> carried;
    bool eachAtZero = true;
    e.setCoreByteSink([&](std::uint32_t frame, const std::uint8_t* d, std::size_t n, bool) {
        eachAtZero = eachAtZero && frame == 0 && n == 1;
        carried.insert(carried.end(), d, d + n);
    });
    std::size_t captured = 0;
    for (int b = 0; b < 100; ++b) {
        carried.clear();
        block(e);
        const std::vector<std::uint8_t> log = serialLog(*gb);
        INFO("block " << b);
        CHECK(carried == log);
        captured += log.size();
    }
    CHECK(eachAtZero);
    CHECK(captured > 0);
}

TEST_CASE("the Game Boy's serialOut carries the largest block a host runs without a drop", "[engine-graph][host-io]") {
    // The plugin grows its plan to its buffer size, up to the graph's limit. At 65536 frames mGB's port clocks out
    // close to 7000 bytes a block, each its own message.
    Engine e(kRate);
    e.setMaxBlockFrames(rp::graph::kMaxBlockFrames);
    SameBoySystem* const gb = serialOutToCart(e);
    std::vector<std::uint8_t> carried;
    e.setCoreByteSink([&](std::uint32_t, const std::uint8_t* d, std::size_t n, bool) { carried.insert(carried.end(), d, d + n); });
    std::size_t most = 0;
    for (int b = 0; b < 8; ++b) {
        carried.clear();
        block(e, rp::graph::kMaxBlockFrames);
        INFO("block " << b);
        CHECK(carried == serialLog(*gb));
        most = std::max(most, carried.size());
    }
    CHECK(most > 4096);  // past what the port held before it was sized for this block
    CHECK(e.droppedMessages() == 0);
}

TEST_CASE("a full output queue's drops reach the engine and the harness facet", "[engine-graph][host-io]") {
    struct Js {
        JSRuntime* rt  = JS_NewRuntime();
        JSContext* ctx = JS_NewContext(rt);
        ~Js() {
            JS_FreeContext(ctx);
            JS_FreeRuntime(rt);
        }
    } js;  // first, so it outlives the server holding its context

    Engine           e(kRate);
    SystemFactory    factory;
    QueuedInvoker    invoker(e);
    EngineRpcService rpc(e, factory, invoker);
    rpcpp::QuickJSTransport                                   transport(js.ctx, [](JSContext*, JSValue) {});
    rpcpp::TypedRpcServer<rpcpp::Empty, rpcpp::QuickJSCodec> server(transport, rpcpp::QuickJSCodec{js.ctx});
    registerHarnessRpc(server, rpc);
    // droppedMessages as a host calls it: by name, through the harness facet; nullopt when it answers no number.
    const auto dropped = [&]() -> std::optional<std::int64_t> {
        const std::string src = "({jsonrpc: '2.0', id: 1, method: 'droppedMessages', params: []})";
        JSValue message = JS_Eval(js.ctx, src.c_str(), src.size(), "<droppedMessages>", JS_EVAL_TYPE_GLOBAL);
        std::optional<rpcpp::QuickJSOut> reply;
        if (!JS_IsException(message)) reply = server.processMessage(message);
        JS_FreeValue(js.ctx, message);
        if (!reply) return std::nullopt;
        JSValue answer = reply->materialize(js.ctx);
        JSValue result = JS_GetPropertyStr(js.ctx, answer, "result");
        std::optional<std::int64_t> count;
        std::int64_t n = 0;
        if (JS_IsNumber(result) && JS_ToInt64(js.ctx, &n, result) == 0) count = n;
        JS_FreeValue(js.ctx, result);
        JS_FreeValue(js.ctx, answer);
        return count;
    };

    REQUIRE(e.setGraph(midiInToOut()).ok);
    for (int i = 0; i < 3; ++i) e.stageMidi(0, tagged(1, {0xF8}));
    block(e);
    CHECK(e.droppedMessages() == 0);
    CHECK(dropped() == 0);
    // One block's worth more than midi-in's port holds.
    for (std::uint32_t i = 0; i < rp::host::kHostMidiInMessages + 3; ++i) e.stageMidi(0, tagged(1, {0xF8}));
    block(e);
    CHECK(e.midiOut().size() == rp::host::kHostMidiInMessages);
    CHECK(e.droppedMessages() == 3);
    CHECK(dropped() == 3);
}

TEST_CASE("a system node's bytes input reaches the engine's sync trace while it is on, and nothing while it is off",
          "[engine-graph][host-io]") {
    // The engine opens its trace at construction when RETROPLUG_SYNC_TRACE names a file, and its destructor writes
    // what is left. A Game Boy's serialOut feeds a NES's bytes input, so each block's line holds what the Game Boy
    // captured that block, at frame 0, as the NES node recorded it. 256-frame blocks keep that under the trace's
    // 48 bytes a block.
    const std::string path =
        (std::filesystem::temp_directory_path() / ("rp-engine-graph-sync-trace-" + std::to_string(std::random_device{}()) + ".log"))
            .string();
    // Put back as it was however the case ends, so no later engine in this process traces.
    const char* const was = std::getenv("RETROPLUG_SYNC_TRACE");
    struct Restore {
        std::optional<std::string> was;
        ~Restore() { setSyncTraceEnv(was ? was->c_str() : nullptr); }
    } restore{was ? std::optional<std::string>(was) : std::nullopt};
    // The trace part of each block's line, " 0:<hex>" or "", for what the Game Boy captured.
    const auto run = [&](bool traced) {
        setSyncTraceEnv(traced ? path.c_str() : nullptr);
        std::vector<std::string> lines;
        Engine e(kRate);
        auto core = mgb(1);
        core->setSerialOutCapture(true);
        SameBoySystem* const gb = core.get();
        REQUIRE(e.parkCore(std::move(core)));
        REQUIRE(e.parkCore(bliptoaster(2)));
        REQUIRE(e.setGraph(graphRequest(stereoDocument({{1, kGb}, {2, kNes}}, DocExtras{{}, {docWire("sys-1.serialOut", "sys-2.bytes")}}),
                                        {1, 2}))
                    .ok);
        for (int b = 0; b < 200; ++b) {
            block(e, 256);
            std::string hex;
            for (const std::uint8_t byte : serialLog(*gb)) {
                char two[3];
                std::snprintf(two, sizeof two, "%02X", byte);
                hex += two;
            }
            lines.push_back(hex.empty() ? "" : " 0:" + hex);
        }
        return lines;
    };
    // A second traced engine in one process writes <path>.2 and so on (HostSyncTrace).
    const auto traceFile = [&]() -> std::string {
        for (int n = 1; n < 32; ++n) {
            const std::string p = n == 1 ? path : path + "." + std::to_string(n);
            if (std::ifstream(p).good()) return p;
        }
        return "";
    };

    const auto untraced = run(false);
    CHECK(traceFile().empty());
    const auto expected = run(true);
    setSyncTraceEnv(restore.was ? restore.was->c_str() : nullptr);
    CHECK(untraced == expected);  // the trace changes nothing the cores hear
    CHECK(std::count_if(expected.begin(), expected.end(), [](const std::string& l) { return !l.empty(); }) > 0);

    const std::string file = traceFile();
    REQUIRE_FALSE(file.empty());
    std::ifstream in(file);
    std::vector<std::string> traced;
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        const auto bar = line.find('|');
        REQUIRE(bar != std::string::npos);
        traced.push_back(line.substr(bar + 1));
    }
    in.close();
    std::remove(file.c_str());
    CHECK(traced == expected);
}
