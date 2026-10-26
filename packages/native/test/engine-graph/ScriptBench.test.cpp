// What a script node costs per block (docs/modular-graph-phase-b3a-design.md sections 6 and 7.4), at the block sizes
// a splitting host produces: 1 frame (FL Studio splits down to it), 64 and 512, at 44.1 kHz, where a 1-frame block
// has 22.7 us of real time. B3b's node granularity rests on these numbers: one protocol node per role multiplies the
// script calls a block makes by the node count.
//
// Two tables. The first is the fixed cost of a script node's process call, marshalling included: chains of 1, 4, 16
// and 64 trivial TS nodes (an events pass-through written with indexed loops) between a native source and sink, the
// source sending nothing or one message a block. A node's cost is the chain's less the source wired straight to the
// sink, over the node count. The second times the kernel node in a plan, driven as KernelNode.test.cpp drives it,
// over one structure and input: mGB, an LSDj in MIDI sync and a NES on nes-n8-midi, every host message to all three,
// the transport playing, a steady MIDI stream, and each Game Boy's MI.OUT as its system node carries it, reaching the
// kernel a block after the core clocked it out (a feedback wire, as buildGraph wires it). No core runs: the plan's
// system stand-ins count what reaches their inputs. The plan's native nodes alone (the same document with no kernel)
// are timed too, so the kernel node's own share shows. (While the kernel stage the node replaced still existed,
// 9c3c10f9, this table timed it too, over the same blocks: the node took about 1.4 to 1.65 times as long, with the
// same JS allocations.)
//
// Each figure is a median of kRuns runs; a kernel run times the node, then its plan with no kernel, over the same
// blocks. Allocations are per block over every run: JS from the script host's audio runtime, native on the calling
// thread (CountingAllocator). It asserts that the work was done, never a time: the messages came through, counted per
// system and input (serial in in bytes), and none reached a system in the plan with no kernel.
//
// Hidden from the normal run. Run it on its own: build/bin/retroplug-engine-graph-test "[bench]"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "../graph/CountingAllocator.hpp"
#include "ScriptRig.hpp"
#include "graph/GraphHost.hpp"
#include "graph/HostNodes.hpp"
#include "graph/Queue.hpp"
#include "graph/nodes/CoreNodes.hpp"
#include "host/graph/HostIoNodes.hpp"
#include "host/graph/HostServices.hpp"
#include "host/script/ScriptHost.hpp"

using rp::host::ScriptAllocations;
using rp::host::ScriptHost;
using rp::host::test::ScriptRig;
namespace gt = rp::graph::test;

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr double        kRate = 44100.0;
constexpr double        kBudgetUs = 1e6 / kRate;  // a 1-frame block's real time
constexpr double        kTempo = 120.0;
constexpr int           kRuns = 7;
constexpr int           kWarm = 64;  // blocks run before the first timed one
constexpr std::uint32_t kMaxFrames = 512;
constexpr std::uint32_t kBlockSizes[] = {1, 64, 512};

// The configuration this was built in (cmake/tests.cmake passes $<CONFIG>, empty when none was set), and whether
// assertions are compiled out.
constexpr const char* kBuildType = RP_BUILD_TYPE;
#ifdef NDEBUG
constexpr const char* kAsserts = "NDEBUG";
#else
constexpr const char* kAsserts = "assertions on, numbers not comparable";
#endif

using Clock = std::chrono::steady_clock;
double usSince(Clock::time_point t0) { return std::chrono::duration<double, std::micro>(Clock::now() - t0).count(); }
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
double percentOfBudget(double us) { return 100.0 * us / kBudgetUs; }

// What a stretch of blocks allocated, per block.
struct Allocations {
    double js = 0.0;        // JS allocations and reallocations
    double jsBytes = 0.0;
    double native = 0.0;    // operator new, on the calling thread
};

Allocations scriptDelta(const ScriptAllocations& a, const ScriptAllocations& b, int native, double blocks) {
    return {static_cast<double>(b.allocs + b.reallocs - a.allocs - a.reallocs) / blocks,
            static_cast<double>(b.bytes - a.bytes) / blocks, native / blocks};
}

void printAllocations(const Allocations& a) { std::printf(" | %9.2f %9.1f %9.2f", a.js, a.jsBytes, a.native); }

// ---- trivial nodes in a chain -----------------------------------------------------------------------------------

constexpr int kChainBlocks = 8192;  // a run
constexpr int kChainLengths[] = {0, 1, 4, 16, 64};

// Events straight through, as cheaply as a script node can be written.
const char* const kPass = R"JS(
rp.register([rp.defineNode({
  id: "rp.bench.pass",
  ports: { in: rp.events.in(), out: rp.events.out() },
  create: () => ({
    process(b) {
      const ms = b.messages("in");
      for (let i = 0; i < ms.length; ++i) b.send("out", ms[i].frame, ms[i].data, ms[i].flags);
    },
  }),
})]);
)JS";

std::string node(const std::string& id, const std::string& type, const std::string& settings = "") {
    return R"({"id":")" + id + R"(","type":")" + type + "\"" + (settings.empty() ? "" : R"(,"settings":)" + settings) + "}";
}
std::string wire(const std::string& from, const std::string& to, bool feedback = false) {
    return R"({"from":")" + from + R"(","to":")" + to + "\"" + (feedback ? R"(,"feedback":true)" : "") + "}";
}
std::string graphJson(const std::vector<std::string>& nodes, const std::vector<std::string>& wires) {
    std::string doc = R"({"nodes":[)";
    for (std::size_t i = 0; i < nodes.size(); ++i) doc += (i ? "," : "") + nodes[i];
    doc += R"(],"wires":[)";
    for (std::size_t i = 0; i < wires.size(); ++i) doc += (i ? "," : "") + wires[i];
    return doc + "]}";
}

// `n` pass nodes between a source and a sink: rp.test.source's events (a note-on a block) for `message`, else
// rp.test.feed's midi output, which sends nothing unless a test queues it.
std::string chain(bool message, int n) {
    std::vector<std::string> nodes{message ? node("src", "rp.test.source") : node("feed", "rp.test.feed"),
                                   node("sink", "rp.test.sink")};
    std::vector<std::string> wires;
    std::string from = message ? "src.events" : "feed.midi";
    for (int k = 1; k <= n; ++k) {
        const std::string id = "p" + std::to_string(k);
        nodes.push_back(node(id, "rp.bench.pass"));
        wires.push_back(wire(from, id + ".in"));
        from = id + ".out";
    }
    wires.push_back(wire(from, "sink.events"));
    return graphJson(nodes, wires);
}

struct ChainRow {
    double      us = 0.0;  // per block
    Allocations allocations;
};

ChainRow timeChain(bool message, int n, std::uint32_t frames) {
    ScriptRig rig(kPass, kMaxFrames, kRate);
    const auto r = rig.set(chain(message, n));
    INFO(ScriptRig::codes(r));
    REQUIRE(r.ok);
    rig.run(frames, kWarm);
    std::vector<double> us;
    us.reserve(kRuns);  // nothing allocates in the counting window but the blocks
    int native = 0;
    const ScriptAllocations before = rig.scripts.audioAllocations();
    for (int run = 0; run < kRuns; ++run) {
        rp::test::beginCountingAllocations();
        const auto t0 = Clock::now();
        rig.run(frames, kChainBlocks);
        us.push_back(usSince(t0) / kChainBlocks);
        native += rp::test::endCountingAllocations().allocations;
        // Every block ran, and the last one's message came through every node.
        const auto& sink = rig.node<gt::Sink>("sink");
        REQUIRE(sink.blocks == static_cast<std::uint32_t>(kWarm + (run + 1) * kChainBlocks));
        REQUIRE(sink.eventCount == (message ? 1u : 0u));
    }
    REQUIRE(rig.scripts.liveInstances() == static_cast<std::uint32_t>(n));
    REQUIRE(rig.scripts.throwCount() == 0);
    return {median(us), scriptDelta(before, rig.scripts.audioAllocations(), native, double{kRuns} * kChainBlocks)};
}

// ---- the kernel ---------------------------------------------------------------------------------------------------

// A run, by block size: a few seconds of audio but at 1 frame, and enough blocks to time there.
int kernelBlocks(std::uint32_t frames) { return frames == 1 ? 32768 : frames == 64 ? 8192 : 2048; }

// kernelProjection's structure for the three, as the kernel node's settings: each pipeline holds only the roles with a
// DSP behaviour, the project MIDI routing sending every message to all of them, and each system says whether it has a
// serialOut.
struct BenchSystem {
    std::uint32_t id;
    const char*   pipeline;
    bool          gameBoy;
};
const BenchSystem kSystems[] = {
    {1, R"([{"kind":"mgb","config":{}}])", true},
    {2, R"([{"kind":"lsdj-sync","config":{"mode":"midiSync","tempoDivisor":1,"autoStart":false}}])", true},
    {3, R"([{"kind":"nes-n8-midi","config":{}}])", false},
};
constexpr std::size_t kSystemCount = std::size(kSystems);

std::string settingsJson() {
    std::string out = R"({"project":[{"kind":"midi-routing","config":{"mode":"sendToAll"}}],"systems":[)";
    for (std::size_t i = 0; i < kSystemCount; ++i)
        out += (i ? "," : "") + std::string(R"({"id":)") + std::to_string(kSystems[i].id) + R"(,"pipeline":)" +
               kSystems[i].pipeline + R"(,"serialOut":)" + (kSystems[i].gameBoy ? "true" : "false") + "}";
    return out + "]}";
}

// The host MIDI: a message every kMidiEvery frames (100 a second), message m at frame m * kMidiEvery from the start, a
// note on and then its note off, on channels 0 to 3 in turn.
constexpr std::uint64_t kMidiEvery = 441;

Bytes hostMessage(std::uint64_t m) {
    const auto channel = static_cast<std::uint8_t>((m / 2) % 4), key = static_cast<std::uint8_t>(48 + (m / 2) % 24);
    if (m % 2 == 0) return {static_cast<std::uint8_t>(0x90 | channel), key, 100};
    return {static_cast<std::uint8_t>(0x80 | channel), key, 0};
}

// One staged host message.
struct Staged {
    std::uint32_t frame = 0;
    Bytes         data;
};

// Block k's staged host MIDI, frame-ordered, as the engine stages it.
void stage(std::uint64_t k, std::uint32_t frames, std::vector<Staged>& out) {
    out.clear();
    const std::uint64_t start = k * frames, end = start + frames;
    for (std::uint64_t m = (start + kMidiEvery - 1) / kMidiEvery; m * kMidiEvery < end; ++m)
        out.push_back({static_cast<std::uint32_t>(m * kMidiEvery - start), hostMessage(m)});
}

double ppqAt(std::uint64_t frame) { return kTempo / 60.0 * static_cast<double>(frame) / kRate; }

// The bytes a Game Boy's serial port clocks out over `frames` from frame `at` while its ROM holds a transfer open (mGB
// always does): one every 8 frames, the most its serialOut carries. Both Game Boys here clock at that rate, and what
// they send is the idle line, which neither role decodes.
std::uint32_t clocked(std::uint64_t at, std::uint32_t frames) { return static_cast<std::uint32_t>((at + frames) / 8 - at / 8); }
constexpr std::uint8_t kIdle[1] = {0xFF};

// What reached a system, by its input.
struct Counts {
    std::uint64_t serialIn = 0, midi = 0, bytes = 0, buttons = 0;
    bool operator==(const Counts&) const = default;
};

// Stands in for a system node: its four inputs, each message counted where the node would hand it to its core.
struct SystemInputs {
    static constexpr const char* id = "rp.bench.system";
    struct {
        rp::bytes_in  serialIn;
        rp::events_in midi;
        rp::bytes_in  bytes;
        rp::events_in buttons;
    } inputs;
    Counts counts;
    void process(const rp::Block&) noexcept {
        // Serial-in in bytes: the kernel node sends a run of same-frame bytes as one message, which the system node
        // applies a byte at a time, so a byte is what a core takes.
        for (const rp_message& m : inputs.serialIn.messages()) counts.serialIn += m.size;
        counts.midi += inputs.midi.messages().size();
        counts.bytes += inputs.bytes.messages().size();
        counts.buttons += inputs.buttons.messages().size();
    }
};

// Stands in for a Game Boy node's serialOut: what its core clocked out in the block, a byte a message at frame 0.
struct SerialOut {
    static constexpr const char* id = "rp.bench.serial-out";
    struct {
        rp::bytes_out serialOut;
    } outputs;
    std::uint64_t at = 0;  // frames run
    void process(const rp::Block& b) noexcept {
        for (std::uint32_t n = clocked(at, b.frames()); n > 0; --n) outputs.serialOut.push(0, kIdle);
        at += b.frames();
    }
};

const rp_package* benchPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<SystemInputs>(), rp::node_type<SerialOut>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.bench"})", static_cast<std::uint32_t>(std::size(kTypes)),
                                     kTypes};
    return &kPackage;
}

// The kernel node in its plan, or that plan with no kernel.
struct KernelBench {
    std::uint32_t frames;
    // Before the graph, so it is destroyed after it: the kernel's shim hands its instance back to it.
    ScriptHost               scripts;
    rp::graph::NodeRegistry  registry;
    rp::graph::ResourceTable resources;
    // The engine's host I/O, standing in: what is staged for the block, and the host outputs counted.
    const std::vector<Staged>* staged = nullptr;
    std::uint64_t              midiOut = 0, controllerOut = 0;
    rp::host::EngineHostIo hostIo{this, &takeMidi, &takeNothing, &disconnected, &countMidiOut, &countControllerOut, &noCart};
    std::unique_ptr<rp::graph::GraphHost> host;

    // `kernel`: false for the plan's native nodes alone.
    KernelBench(std::uint32_t blockFrames, bool kernel) : frames(blockFrames) {
        const std::vector<ScriptHost::Chunk> chunks{ScriptHost::embeddedBundle()};
        std::string error;
        const bool loaded = scripts.load(chunks, &error);
        INFO(error);
        REQUIRE(loaded);
        for (const rp_package* p : {rp::graph::corePackage(), rp::graph::hostPackage(), rp::host::hostIoPackage(), benchPackage(),
                                    scripts.package()})
            REQUIRE(registry.addPackage(p, &error));
        rp::graph::HostConfig config;
        config.sampleRate = kRate;
        config.maxFrames = kMaxFrames;
        config.interfaces = [](void* ctx, const char* id) -> const void* {
            return std::string(id) == RP_ENGINE_HOST_IO_INTERFACE ? &static_cast<KernelBench*>(ctx)->hostIo : nullptr;
        };
        config.interfacesCtx = this;
        host = std::make_unique<rp::graph::GraphHost>(registry, resources, config);
        const auto r = host->setGraph(document(kernel));
        for (const auto& e : r.errors) UNSCOPED_INFO(e.code << " at " << e.nodeId << "." << e.port << ": " << e.message);
        REQUIRE(r.ok);
    }
    ~KernelBench() { host.reset(); }

    // As buildGraph wires it: the host I/O nodes into the kernel, each Game Boy's serialOut into it by a feedback wire,
    // its outputs to each system's inputs, and on to the host outputs.
    static std::string document(bool kernel) {
        std::vector<std::string> nodes{node("midi-in", "rp.host.midi-in"), node("controller-in", "rp.host.controller-in"),
                                       node("midi-out", "rp.host.midi-out"), node("controller-out", "rp.host.controller-out")};
        std::vector<std::string> wires;
        if (kernel) {
            nodes.push_back(node("kernel", "rp.dsp.kernel", settingsJson()));
            wires = {wire("midi-in.midi", "kernel.midiIn"), wire("controller-in.midi", "kernel.controllerIn"),
                     wire("controller-in.connected", "kernel.controllerConnected"), wire("kernel.midiOut", "midi-out.midi"),
                     wire("kernel.controllerOut", "controller-out.midi")};
        }
        for (const BenchSystem& s : kSystems) {
            const std::string id = "sys-" + std::to_string(s.id), port = "kernel." + id;
            nodes.push_back(node(id, "rp.bench.system"));
            if (s.gameBoy) nodes.push_back(node(id + "-serial", "rp.bench.serial-out"));
            if (!kernel) continue;
            if (s.gameBoy) wires.push_back(wire(id + "-serial.serialOut", port + ".serialOut", true));
            for (const char* in : {"serialIn", "midi", "bytes", "buttons"}) wires.push_back(wire(port + "." + in, id + "." + in));
        }
        return graphJson(nodes, wires);
    }

    // ---- the plan, as the engine runs a block ------------------------------------------------------------------
    void block(std::uint64_t k, const std::vector<Staged>& midi) {
        staged = &midi;
        scripts.beginBlock();
        host->processBlock(frames, rp_transport{kRate, kTempo, ppqAt(k * frames), RP_TRANSPORT_PLAYING});
        scripts.endBlock();
    }
    Counts nodeCounts(std::size_t i) const {
        return rp::peer_as<SystemInputs>(host->node("sys-" + std::to_string(kSystems[i].id)))->counts;
    }

    static KernelBench& of(void* ctx) { return *static_cast<KernelBench*>(ctx); }
    // As the engine's: the staging, frame-ordered, onto the queue.
    static void takeMidi(void* ctx, rp_queue* out) {
        for (const auto& m : *of(ctx).staged) rp::graph::pushMessage(*out, m.frame, m.data);
    }
    static void takeNothing(void*, rp_queue*) {}
    static bool disconnected(void*) { return false; }
    static void countMidiOut(void* ctx, std::uint32_t, std::uint32_t, const std::uint8_t*, std::size_t) { ++of(ctx).midiOut; }
    static void countControllerOut(void* ctx, const std::uint8_t*, std::size_t) { ++of(ctx).controllerOut; }
    static void noCart(void*, std::uint32_t, const std::uint8_t*, std::size_t, bool) {}
};

struct KernelRow {
    double      us = 0.0;  // per block
    Allocations allocations;
};

struct KernelRows {
    KernelRow     node, plan;  // plan: the node's plan with no kernel in it
    Counts        delivered;          // to every system, by input, over every block run
    std::uint64_t blocks = 0;         // run, warm-up included
};

KernelRows timeKernel(std::uint32_t frames) {
    const int blocks = kernelBlocks(frames);
    KernelRows rows;
    KernelBench bench(frames, true), plan(frames, false);
    // Block k's staging, for the run's blocks, made before the run.
    std::vector<std::vector<Staged>> staged(static_cast<std::size_t>(std::max(blocks, kWarm)));
    const auto prepare = [&](std::uint64_t first, int count) {
        for (int i = 0; i < count; ++i) stage(first + static_cast<std::uint64_t>(i), frames, staged[static_cast<std::size_t>(i)]);
    };
    // One side over `count` blocks from block `first`: its time per block, its native allocations added to `native`.
    const auto runSide = [&](auto&& block, std::uint64_t first, int count, int& native) {
        rp::test::beginCountingAllocations();
        const auto t0 = Clock::now();
        for (int i = 0; i < count; ++i) block(first + static_cast<std::uint64_t>(i), staged[static_cast<std::size_t>(i)]);
        const double us = usSince(t0) / count;
        native += rp::test::endCountingAllocations().allocations;
        return us;
    };
    const auto nodeBlock = [&](std::uint64_t k, const auto& midi) { bench.block(k, midi); };
    const auto planBlock = [&](std::uint64_t k, const auto& midi) { plan.block(k, midi); };

    int ignored = 0;
    prepare(0, kWarm);
    runSide(nodeBlock, 0, kWarm, ignored);
    runSide(planBlock, 0, kWarm, ignored);

    std::vector<double> nodeUs, planUs;
    int nativeNode = 0, nativePlan = 0;
    const ScriptAllocations nodeBefore = bench.scripts.audioAllocations(), planBefore = plan.scripts.audioAllocations();
    std::uint64_t k = kWarm;
    for (int run = 0; run < kRuns; ++run, k += static_cast<std::uint64_t>(blocks)) {
        prepare(k, blocks);
        nodeUs.push_back(runSide(nodeBlock, k, blocks, nativeNode));
        planUs.push_back(runSide(planBlock, k, blocks, nativePlan));
    }
    const double measured = double{kRuns} * blocks;
    rows.node = {median(nodeUs), scriptDelta(nodeBefore, bench.scripts.audioAllocations(), nativeNode, measured)};
    rows.plan = {median(planUs), scriptDelta(planBefore, plan.scripts.audioAllocations(), nativePlan, measured)};

    // The kernel handed every system its messages, input by input (serial in in bytes), and with no kernel nothing
    // reached any. Nothing here sends core bytes or presses a button, so those two are 0.
    for (std::size_t i = 0; i < kSystemCount; ++i) {
        INFO("system " << kSystems[i].id);
        const Counts got = bench.nodeCounts(i);
        rows.delivered.serialIn += got.serialIn;
        rows.delivered.midi += got.midi;
        rows.delivered.bytes += got.bytes;
        rows.delivered.buttons += got.buttons;
        CHECK(plan.nodeCounts(i) == Counts{});
    }
    CHECK(bench.nodeCounts(0).serialIn > 0);  // mGB: the host MIDI, in bytes
    CHECK(bench.nodeCounts(1).serialIn > 0);  // the LSDj: its clocks
    CHECK(bench.nodeCounts(2).midi > 0);      // the NES: the host MIDI
    CHECK(rows.delivered.bytes == 0);         // nothing here sends core bytes
    CHECK(rows.delivered.buttons == 0);       // or presses a button
    CHECK(bench.midiOut == 0);                // or sends host MIDI out
    CHECK(bench.host->droppedMessages() == 0);
    CHECK(bench.scripts.throwCount() == 0);
    CHECK(bench.scripts.liveInstances() == 1);
    rows.blocks = k;
    return rows;
}

// A time per block, with the share it takes of a 1-frame block's real time and of its own block's.
void printTime(double us, std::uint32_t frames) {
    std::printf(" | %9.3f %8.1f%% %8.2f%%", us, percentOfBudget(us), percentOfBudget(us) / frames);
}

void chainTable() {
    std::printf("\nchains of n pass-through TS nodes (rp.bench.pass) between a native source and sink, %d blocks a run\n",
                kChainBlocks);
    std::printf("input: rp.test.feed (nothing) or rp.test.source (one note-on a block); a node = (the chain - the source "
                "wired straight to the sink) / n\n");
    std::printf("%-7s %6s %5s | %9s %9s %9s | %9s %9s | %9s %9s %9s\n", "input", "frames", "nodes", "us/block", "%1-frame",
                "%own", "us/node", "%1-frame", "JS allocs", "JS bytes", "native");
    for (const bool message : {false, true}) {
        for (const std::uint32_t frames : kBlockSizes) {
            double baseline = 0.0;
            for (const int n : kChainLengths) {
                const ChainRow row = timeChain(message, n, frames);
                if (n == 0) baseline = row.us;
                std::printf("%-7s %6u %5d", message ? "1 msg" : "nothing", frames, n);
                printTime(row.us, frames);
                const double perNode = n == 0 ? 0.0 : (row.us - baseline) / n;
                if (n == 0) std::printf(" | %9s %9s", "-", "-");
                else std::printf(" | %9.3f %8.1f%%", perNode, percentOfBudget(perNode));
                printAllocations(row.allocations);
                std::printf("\n");
            }
        }
    }
}

void kernelTable() {
    std::printf("\nthe kernel node: mGB, an LSDj in MIDI sync and a NES on nes-n8-midi, routing sendToAll; playing at %.0f "
                "bpm; a host message every %llu frames; each Game Boy's MI.OUT a byte every 8 frames\n",
                kTempo, static_cast<unsigned long long>(kMidiEvery));
    std::printf("node: its plan (the host I/O nodes, the kernel node, each Game Boy's serialOut, a stand-in for each "
                "system's inputs); plan: that plan with no kernel; no core runs\n");
    std::printf("%6s  %-20s %6s | %9s %9s %9s | %9s %9s %9s\n", "frames", "path", "blocks", "us/block", "%1-frame", "%own",
                "JS allocs", "JS bytes", "native");
    for (const std::uint32_t frames : kBlockSizes) {
        const KernelRows rows = timeKernel(frames);
        const int blocks = kernelBlocks(frames);
        for (const auto& [path, row] : {std::pair{"kernel node, plan", rows.node}, std::pair{"plan, no kernel", rows.plan}}) {
            std::printf("%6u  %-20s %6d", frames, path, blocks);
            printTime(row.us, frames);
            printAllocations(row.allocations);
            std::printf("\n");
        }
        const auto count = [](std::uint64_t n) { return static_cast<unsigned long long>(n); };
        std::printf("%6s  the kernel node alone (node - plan) %.3f us; delivered in %llu blocks: %llu serial-in bytes, %llu MIDI "
                    "messages, %llu core-byte messages, %llu button presses\n",
                    "", rows.node.us - rows.plan.us, count(rows.blocks), count(rows.delivered.serialIn), count(rows.delivered.midi),
                    count(rows.delivered.bytes), count(rows.delivered.buttons));
    }
}

}  // namespace

TEST_CASE("a script node's cost per block, at 1, 64 and 512-frame blocks", "[.][bench]") {
    const auto start = Clock::now();
    std::printf("\nscript-node cost per block at %.1f kHz, %s build (%s), medians of %d runs\n", kRate / 1000.0,
                kBuildType[0] != '\0' ? kBuildType : "no CMAKE_BUILD_TYPE", kAsserts, kRuns);
    std::printf("%%1-frame: the share of a 1-frame block's real time (%.1f us); %%own: of the row's own block's; allocations "
                "per block, native on the block thread\n",
                kBudgetUs);
    chainTable();
    kernelTable();
    std::printf("total %.2f s\n", usSince(start) / 1e6);
}
