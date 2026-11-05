// Exported plans on the desktop (docs/modular-graph-phase-e3-design.md sections 4 and 8): a document planned for the
// RP2350 and written as a blob by exportPlan (graph/PlanExport.hpp), then laid out in an arena by the loader
// (graph/PlanLoader.hpp) over the device-mode builds of its types, as the device will.
//
// - A core and DSP document with a coupled group, a feedback wire, a wired param and params moved during the run,
//   rendered in ragged blocks from the loaded plan, is bit for bit what GraphHost renders from the same document with
//   the desktop's builds, and nothing is allocated from the load to the unload.
// - The blob is byte-stable: exported twice, and by retroplug-export-plan in a process of its own, it is the same
//   bytes, and those are the golden's (golden/plan-blob.hex; RP_PLAN_BLOB_UPDATE=1 records it).
// - Every loader refusal, with its words: the magic, a version, a truncated blob, the CRC, an index out of range, an
//   unknown type, a plan over its arena (its own structures, or a node), and a node that fails part-way, after which
//   nothing it made is alive.
// - The exporter's refusals: a heap and a threads type, a resource named in settings and one bound, a type the device
//   does not build, and a plan over its arena, each with its message; and the tool's.
// - The box's bridge, rp.device.midi-in into rp.core.midi-filter into rp.n8.nes, exported and loaded over a fake cart
//   link (FakeCart.hpp) and a fake MIDI input (FakeMidiIn.hpp): the link is handed every message the filter keeps, in
//   order and at its frame, never one it drops, and the cart's expansion volume at unity; GraphHost's run of the same
//   document with the desktop's builds hands its own fake the same.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "DeviceModeEntries.hpp"
#include "FakeCart.hpp"
#include "FakeMidiIn.hpp"
#include "PlanNodes.hpp"
#include "graph/CountingAllocator.hpp"
#include "graph/Executor.hpp"
#include "graph/GraphHost.hpp"
#include "graph/HostNodes.hpp"
#include "graph/NodeRegistry.hpp"
#include "graph/PlanBlob.hpp"
#include "graph/PlanExport.hpp"
#include "graph/PlanLoader.hpp"
#include "graph/Queue.hpp"
#include "graph/ResourceTable.hpp"
#include "graph/nodes/CoreNodes.hpp"
#include "graph/nodes/DspNodes.hpp"
#include "graph/rp_device.h"
#include "graph/rp_device_midi_in.h"
#include "n8/N8Node.hpp"

namespace {

using namespace rp::graph;

// A type that needs threads, which the Pico refuses (the desktop builds it alone).
struct Threaded {
    static constexpr const char* id = "rp.devtest.threaded";
    static constexpr bool threads = true;
    struct { rp::audio_out<rp::mono> out; } outputs;
    void process(const rp::Block& b) noexcept {
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = 0.0f;
    }
};

const rp_package* planPackageDesktop() {
    static const rp_node_type* const kTypes[] = {rp::node_type<devtest::Source>(), rp::node_type<devtest::Digest>(),
                                                 rp::node_type<devtest::Lockstep>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.devtest.plan","version":"1.0.0"})", 3, kTypes};
    return &kPackage;
}
const rp_package* faultPackageDesktop() {
    static const rp_node_type* const kTypes[] = {rp::node_type<devtest::Fault>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.devtest.fault","version":"1.0.0"})", 1, kTypes};
    return &kPackage;
}
const rp_package* threadedPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Threaded>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.devtest.threaded","version":"1.0.0"})", 1, kTypes};
    return &kPackage;
}

std::unique_ptr<NodeRegistry> registryOf(const std::vector<const rp_package*>& packages) {
    auto registry = std::make_unique<NodeRegistry>();
    for (const rp_package* p : packages) {
        std::string why;
        const bool added = registry->addPackage(p, &why);
        INFO(why);
        REQUIRE(added);
    }
    return registry;
}

// The desktop's builds, which plan and which GraphHost runs; rp.host.audio-out among them, a type no device builds.
std::unique_ptr<NodeRegistry> desktopRegistry() {
    return registryOf({rp_core_package(), rp_dsp_package(), rp_n8_package(), rp_device_io_package(), hostPackage(),
                       planPackageDesktop(), faultPackageDesktop(), threadedPackage()});
}
// The device's builds of the same packages, which the loader creates.
const std::vector<const rp_package*>& deviceTypes() {
    static const std::vector<const rp_package*> types{
        rp_core_package_device_mode(),         rp_dsp_package_device_mode(),
        rp_n8_package_device_mode(),           rp_device_io_package_device_mode(),
        rp_devtest_plan_package_device_mode(), rp_devtest_fault_package_device_mode()};
    return types;
}

// What a device host gives the nodes it loads: a devices interface with no device behind it, and nothing else.
void*                 acquireNoDevice(void*, const char*, const char*) { return nullptr; }
void                  releaseNoDevice(void*, void*) {}
const rp_host_devices kDevices{nullptr, &acquireNoDevice, &releaseNoDevice};
void                  ignoreLog(void*, int, const char*) {}
const void* deviceInterface(const rp_host*, const char* id) {
    return std::strcmp(id, RP_HOST_DEVICES_INTERFACE) == 0 ? &kDevices : nullptr;
}
const rp_host kDeviceHost{RP_ABI_VERSION, nullptr, nullptr, nullptr, &ignoreLog, &deviceInterface};

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

// The device's plan nodes with the digest's build altered in one call: a create that fails, or a query_memory that
// asks for an alignment of 16 (as a type with an alignas(16) member would), so the loader's refusals of a device-only
// create and of an alignment past the target's are seen.
const rp_node_type* deviceDigest() {
    const rp_package* p = rp_devtest_plan_package_device_mode();
    for (std::uint32_t i = 0; i < p->node_type_count; ++i)
        if (std::string_view(p->node_types[i]->id) == "rp.devtest.digest") return p->node_types[i];
    return nullptr;
}
rp_node* refuseCreate(const rp_node_type*, const rp_host*, void*, const rp_settings*, rp_error* err) {
    std::snprintf(err->message, sizeof err->message, "%s", "the digest will not be made here");
    return nullptr;
}
bool askSixteen(const rp_node_type*, const rp_settings* s, rp_memory_req* out, rp_error* err) {
    const rp_node_type* real = deviceDigest();
    if (!real->query_memory(real, s, out, err)) return false;
    out->align = 16;
    return true;
}
struct AlteredPlanTypes {
    rp_node_type                     digest;
    std::vector<const rp_node_type*> types;
    rp_package                       package{};
    std::vector<const rp_package*>   all;
    explicit AlteredPlanTypes(void (*alter)(rp_node_type&)) : digest(*deviceDigest()) {
        alter(digest);
        const rp_package* p = rp_devtest_plan_package_device_mode();
        for (std::uint32_t i = 0; i < p->node_type_count; ++i)
            types.push_back(p->node_types[i] == deviceDigest() ? &digest : p->node_types[i]);
        package = rp_package{RP_ABI_VERSION, p->manifest_json, static_cast<std::uint32_t>(types.size()), types.data()};
        all = {rp_core_package_device_mode(), rp_dsp_package_device_mode(), rp_n8_package_device_mode(),
               rp_device_io_package_device_mode(), &package, rp_devtest_fault_package_device_mode()};
    }
};

std::string codes(const ExportResult& r) {
    std::string out;
    for (const auto& e : r.errors) out += (out.empty() ? "" : " ") + e.code;
    return out;
}
std::string described(const ExportResult& r) {
    std::string out;
    for (const auto& e : r.errors) out += e.code + " " + e.nodeId + "." + e.port + ": " + e.message + "\n";
    return out;
}

// ---- the document -------------------------------------------------------------------------------------------------

constexpr std::uint32_t kMaxFrames = 512;
constexpr std::size_t   kFrames = 6000;
constexpr std::size_t   kArenaBytes = 256 * 1024;
const std::vector<std::uint32_t> kSplit = {1, 31, 256, 3, 512, 77, 128};

// A stereo source downmixed into a coupled pair, `a` and `b`: `a` takes the source, `b` takes the biquad's output a
// block late, through a feedback wire; `a` goes through a gain and is summed with `b` into the biquad, whose Q a
// constant drives through a wire; the digest hears the biquad, the source's messages and its control stream.
const char* kDocument = R"({"nodes":[
    {"id":"src","type":"rp.devtest.source","settings":{"seed":3}},
    {"id":"dm","type":"rp.core.downmix"},
    {"id":"a","type":"rp.devtest.lockstep","settings":{"mix":0.5},"params":{"drive":1.25}},
    {"id":"b","type":"rp.devtest.lockstep","settings":{"mix":-0.75}},
    {"id":"g","type":"rp.dsp.gain","settings":{"channels":1},"params":{"gain":0.8}},
    {"id":"mx","type":"rp.core.add"},
    {"id":"k","type":"rp.core.constant","params":{"value":0.7}},
    {"id":"bq","type":"rp.dsp.biquad","settings":{"channels":1},"params":{"frequency":2500}},
    {"id":"dg","type":"rp.devtest.digest"}],
  "wires":[
    {"from":"src.out","to":"dm.in"},
    {"from":"dm.out","to":"a.in"},
    {"from":"a.link","to":"b.link"},
    {"from":"a.out","to":"g.in"},
    {"from":"g.out","to":"mx.in1"},
    {"from":"b.out","to":"mx.in2"},
    {"from":"mx.out","to":"bq.in"},
    {"from":"k.out","to":"bq.q"},
    {"from":"bq.out","to":"b.in","feedback":true},
    {"from":"bq.out","to":"dg.in"},
    {"from":"src.events","to":"dg.events"},
    {"from":"src.control","to":"dg.control"}]})";

// One param change, at frame 0 of the block with this index.
struct Move {
    std::size_t   block;
    const char*   node;
    const char*   param;
    std::uint32_t index;  // the param's position in its node's layout
    double        value;
};
const std::vector<Move> kMoves = {
    {2, "g", "gain", 0, -1.5},      {3, "a", "drive", 0, 1.9},      {5, "bq", "frequency", 0, 700},
    {6, "b", "drive", 0, 0.4},      {8, "k", "value", 0, 0.2},      {11, "g", "gain", 0, 0.3},
};

// The feedback kinds the first document lacks: an events loop (m2's output back into m1, which merges it with the
// source's messages, so every message the source sends goes round again each block, a block late) and a control loop
// (c2's sum back into c1), each heard by the digest.
const char* kLoops = R"({"nodes":[
    {"id":"src","type":"rp.devtest.source","settings":{"seed":5}},
    {"id":"m1","type":"rp.core.merge"},
    {"id":"m2","type":"rp.core.merge"},
    {"id":"c1","type":"rp.core.add","settings":{"kind":"control"}},
    {"id":"c2","type":"rp.core.add","settings":{"kind":"control"}},
    {"id":"k","type":"rp.core.constant","settings":{"min":-1,"max":1},"params":{"value":-0.25}},
    {"id":"dg","type":"rp.devtest.digest"}],
  "wires":[
    {"from":"src.events","to":"m1.in1"},
    {"from":"m2.out","to":"m1.in2","feedback":true},
    {"from":"m1.out","to":"m2.in1"},
    {"from":"src.control","to":"c1.in1"},
    {"from":"c2.out","to":"c1.in2","feedback":true},
    {"from":"c1.out","to":"c2.in1"},
    {"from":"k.out","to":"c2.in2"},
    {"from":"src.out","to":"dg.in"},
    {"from":"m2.out","to":"dg.events"},
    {"from":"c2.out","to":"dg.control"}]})";
const std::vector<Move> kLoopMoves = {{3, "k", "value", 0, 0.5}, {9, "k", "value", 0, -0.75}};

ExportOptions options(std::size_t arena = kArenaBytes) {
    ExportOptions o;
    o.sampleRate = 48000.0;
    o.maxFrames = kMaxFrames;
    o.arenaBytes = arena;
    return o;
}

ExportResult exported(const std::string& document, std::size_t arena = kArenaBytes, bool trialLoad = true,
                      const std::vector<const rp_package*>& types = deviceTypes()) {
    auto registry = desktopRegistry();
    ExportOptions o = options(arena);
    o.trialLoad = trialLoad;
    return exportPlan(document, *registry, types, o);
}

// The blob of a document the exporter takes; with trialLoad false, one the device's builds may refuse at load.
std::vector<std::uint8_t> blobOf(const std::string& document, bool trialLoad = true) {
    const ExportResult r = exported(document, kArenaBytes, trialLoad);
    INFO(described(r));
    REQUIRE(r.ok());
    return r.blob;
}

const rp_transport kTransport{48000.0, 120.0, 0.0, 0};

struct Rendered {
    DigestView    view{};
    std::uint32_t faults = 0;
    std::uint32_t drops = 0;
};

// The desktop's render: the document in a GraphHost with the desktop's builds.
Rendered renderDesktop(const char* document, const std::vector<Move>& moves) {
    auto registry = desktopRegistry();
    ResourceTable resources;
    GraphHost host(*registry, resources, HostConfig{48000.0, kMaxFrames, Target::Desktop});
    const auto result = host.setGraph(document);
    std::string errors;
    for (const auto& e : result.errors) errors += e.code + ": " + e.message + "\n";
    INFO(errors);
    REQUIRE(result.ok);
    std::size_t at = 0, block = 0;
    while (at < kFrames) {
        for (const Move& m : moves)
            if (m.block == block) REQUIRE(host.setParam(m.node, m.param, m.value));
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(kSplit[block % kSplit.size()], kFrames - at));
        host.processBlock(n, kTransport);
        at += n;
        ++block;
    }
    rp_node* digest = host.node("dg");
    REQUIRE(digest);
    const auto* view = static_cast<const DigestView*>(digest->vt->get_interface(digest, kDigestInterface));
    REQUIRE(view);
    return {*view, host.faultCount(), host.droppedMessages()};
}

// An arena of `bytes`, aligned as the loader asks.
struct Arena {
    std::vector<std::max_align_t> storage;
    explicit Arena(std::size_t bytes) : storage(bytes / sizeof(std::max_align_t) + 1) {}
    std::span<std::byte> span(std::size_t bytes) { return {reinterpret_cast<std::byte*>(storage.data()), bytes}; }
};

std::string text(const char* s) { return std::string(s); }

// The loader's refusal of `blob` in an arena of `bytes`, as "<code> <node>: <message>", or "" when it loaded.
std::string refusal(const std::vector<std::uint8_t>& blob, std::size_t bytes = kArenaBytes,
                    const std::vector<const rp_package*>& types = deviceTypes()) {
    Arena arena(bytes);
    PlanLoadError err{};
    LoadedPlan* plan = loadPlan(blob, arena.span(bytes), types, &kDeviceHost, &err);
    if (plan) {
        unloadPlan(plan);
        return "";
    }
    return text(err.code) + " " + text(err.node) + ": " + text(err.message);
}

// The CRC rewritten after an edit, so the edit is what the loader sees and not a bad CRC.
void reseal(std::vector<std::uint8_t>& blob) {
    const std::uint32_t crc = blob::crc32(blob.data(), blob.size() - 4);
    std::memcpy(blob.data() + blob.size() - 4, &crc, 4);
}
blob::Header headerOf(const std::vector<std::uint8_t>& blob) {
    blob::Header h;
    std::memcpy(&h, blob.data(), sizeof h);
    return h;
}

std::string hexOf(const std::vector<std::uint8_t>& bytes) {
    std::string out;
    char digits[3];
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        std::snprintf(digits, sizeof digits, "%02x", bytes[i]);
        out += digits;
        out += (i % 32 == 31 || i + 1 == bytes.size()) ? '\n' : ' ';
    }
    return out;
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The loaded render: the blob loaded over the device's builds in an arena and run as renderDesktop runs the document,
// counting what the loader, the blocks and the unload allocate. The window holds no Catch2 macro (which allocates):
// everything is recorded, and checked after.
struct LoadedRun {
    Rendered      run;
    bool          loaded = false;
    bool          resolved = true;  // every move found its param's stream
    std::uint32_t nodes = 0;
    std::size_t   bytes = 0;
    int           allocations = -1;
    int           frees = -1;
    std::string   error;
};

LoadedRun renderLoaded(const std::vector<std::uint8_t>& blob, const std::vector<Move>& moves,
                       const std::function<void(const LoadedPlan&)>& inspect = {}) {
    LoadedRun r;
    Arena arena(kArenaBytes);
    const std::vector<const rp_package*>& types = deviceTypes();
    std::vector<rp_control_queue*> queues(moves.size(), nullptr);
    PlanLoadError err{};
    std::size_t at = 0, block = 0;
    rp::test::beginCountingAllocations(rp::test::countsCAllocations());
    LoadedPlan* plan = loadPlan(blob, arena.span(kArenaBytes), types, &kDeviceHost, &err);
    if (plan) {
        r.loaded = true;
        r.nodes = static_cast<std::uint32_t>(plan->nodes.size());
        r.bytes = plan->bytes;
        for (std::size_t i = 0; i < moves.size(); ++i) {
            const LoadedNode* n = plan->find(moves[i].node);
            queues[i] = n ? plan->param(static_cast<std::uint32_t>(n - plan->nodes.data()), moves[i].index) : nullptr;
            r.resolved = r.resolved && queues[i];
        }
        while (r.resolved && at < kFrames) {
            for (std::size_t i = 0; i < moves.size(); ++i)
                if (moves[i].block == block) pushControl(*queues[i], 0, moves[i].value);
            const auto n =
                static_cast<std::uint32_t>(std::min<std::size_t>(kSplit[block % kSplit.size()], kFrames - at));
            runBlock(plan->view, n, kTransport);
            at += n;
            ++block;
        }
        if (LoadedNode* d = plan->find("dg"))
            if (const auto* v = static_cast<const DigestView*>(d->node->vt->get_interface(d->node, kDigestInterface)))
                r.run.view = *v;
        r.run.faults = plan->faults.load();
        r.run.drops = plan->drops.load();
    }
    const auto counts = rp::test::endCountingAllocations();
    r.allocations = counts.allocations;
    r.frees = counts.frees;
    if (plan) {
        if (inspect) inspect(*plan);  // after the window: it may use Catch2
        unloadPlan(plan);
    }
    r.error = text(err.code) + " " + text(err.node) + ": " + text(err.message);
    return r;
}

// ---- the box's bridge ---------------------------------------------------------------------------------------------

using devtest::FakeMidiIn;

// The box's plan (docs/modular-graph-phase-e3-design.md section 5): the host's MIDI, BlipToaster's filter, the cart.
const char* kBridge = R"({"nodes":[
    {"id":"in","type":"rp.device.midi-in"},
    {"id":"f","type":"rp.core.midi-filter","settings":{"drop":["polyAftertouch","channelAftertouch"]}},
    {"id":"nes","type":"rp.n8.nes"}],
  "wires":[{"from":"in.midi","to":"f.in"},{"from":"f.out","to":"nes.midi"}]})";

// A block's staged MIDI, and whether the filter keeps each message.
struct BridgeMessage {
    std::uint32_t             frame;
    std::vector<std::uint8_t> bytes;
    bool                      kept;
};
const std::vector<std::vector<BridgeMessage>>& bridgeBlocks() {
    static const std::vector<std::vector<BridgeMessage>> blocks = {
        {{0, {0x90, 0x3c, 0x64}, true},
         {3, {0xA0, 0x3c, 0x10}, false},
         {4, {0xD0, 0x20}, false},
         {4, {0xB0, 0x07, 0x64}, true},
         {9, {0xF8}, true},
         {10, {0xF0, 0x7D, 0x50, 0x01, 0xF7}, true},
         {20, {0xE0, 0x00, 0x40}, true}},
        {},
        {{1, {0x80, 0x3c, 0x00}, true}, {2, {0xA1, 0x40, 0x01}, false}, {30, {0xFA}, true}, {30, {0xD3, 0x7f}, false}},
    };
    return blocks;
}

// What a link should have been handed: each kept message at its frame, one line each.
std::string keptLines() {
    std::ostringstream o;
    for (const auto& block : bridgeBlocks())
        for (const BridgeMessage& m : block) {
            if (!m.kept) continue;
            o << m.frame << ":";
            for (const std::uint8_t b : m.bytes) o << " " << std::hex << int(b) << std::dec;
            o << "\n";
        }
    return o.str();
}
std::string pushedLines(const CartRecord& r) {
    std::ostringstream o;
    for (std::uint32_t i = 0; i < r.count && i < CartRecord::kPushes; ++i) {
        o << r.pushes[i].frame << ":";
        for (std::uint32_t k = 0; k < r.pushes[i].size; ++k) o << " " << std::hex << int(r.pushes[i].bytes[k]) << std::dec;
        o << "\n";
    }
    return o.str();
}

// A device host for the bridge: its cart link is `cart` (installed as the N8's, "n8" with rp.n8.cart-link/1) and its
// MIDI input is `midi`.
struct BridgeHost {
    void*           cart = nullptr;
    FakeMidiIn*     midi = nullptr;
    rp_host_devices devices{this, &acquire, &release};
    rp_host         host{RP_ABI_VERSION, this, nullptr, nullptr, &ignoreLog, &interface};
    int             held = 0;

    static void* acquire(void* ctx, const char* name, const char* id) {
        auto* self = static_cast<BridgeHost*>(ctx);
        if (std::strcmp(name, rp::n8::kDeviceName) != 0 || std::strcmp(id, rp::n8::kCartLinkInterface) != 0)
            return nullptr;
        ++self->held;
        return self->cart;
    }
    static void release(void* ctx, void*) { --static_cast<BridgeHost*>(ctx)->held; }
    static const void* lookup(const BridgeHost* self, const char* id) {
        if (std::strcmp(id, RP_HOST_DEVICES_INTERFACE) == 0) return &self->devices;
        if (std::strcmp(id, RP_DEVICE_MIDI_IN_INTERFACE) == 0) return &self->midi->iface;
        return nullptr;
    }
    static const void* interface(const rp_host* h, const char* id) {
        return lookup(static_cast<const BridgeHost*>(h->ctx), id);
    }
    // As GraphHost asks its embedder (HostConfig::interfaces).
    static const void* embedded(void* ctx, const char* id) { return lookup(static_cast<const BridgeHost*>(ctx), id); }
};

void stageBlock(FakeMidiIn& midi, std::size_t block) {
    for (const BridgeMessage& m : bridgeBlocks()[block]) midi.stage(m.frame, m.bytes);
}

}  // namespace

TEST_CASE("an exported plan, loaded over the device's builds, renders what GraphHost renders, allocating nothing",
          "[device-mode][plan]") {
    const Rendered want = renderDesktop(kDocument, kMoves);
    CHECK(want.view.heard);
    CHECK(want.view.blocks > 10);
    CHECK(want.faults == 0);
    CHECK(want.drops == 0);

    const std::vector<std::uint8_t> blob = blobOf(kDocument);
    // What the blob holds of the document's shape: one coupled pair, one feedback copy, the document's four param
    // values, a mask for every node, a peer list for each of the pair and one wired param (the biquad's Q).
    const blob::Header h = headerOf(blob);
    auto count = [&](blob::Section s) { return h.sections[static_cast<int>(s)].count; };
    std::uint32_t pairs = 0, wired = 0;
    for (std::uint32_t i = 0; i < count(blob::Section::Steps); ++i) {
        blob::StepRecord step;
        std::memcpy(&step, blob.data() + h.sections[static_cast<int>(blob::Section::Steps)].offset + i * sizeof step,
                    sizeof step);
        pairs += step.groupSize == 2 ? 1 : 0;
    }
    for (std::uint32_t i = 0; i < count(blob::Section::ParamViews); ++i) {
        blob::ParamViewRecord v;
        std::memcpy(&v, blob.data() + h.sections[static_cast<int>(blob::Section::ParamViews)].offset + i * sizeof v,
                    sizeof v);
        wired += v.wired;
    }
    CHECK(pairs == 1);
    CHECK(wired == 1);
    CHECK(count(blob::Section::Feedback) == 1);
    CHECK(count(blob::Section::ParamStarts) == 4);
    CHECK(count(blob::Section::Masks) == 9);
    CHECK(count(blob::Section::Peers) == 2);

    // A wired param has no stream a host may write, here as on the desktop (bq's params: frequency, q, gainDb).
    const LoadedRun got = renderLoaded(blob, kMoves, [](const LoadedPlan& plan) {
        const LoadedNode* bq = plan.find("bq");
        REQUIRE(bq);
        const auto n = static_cast<std::uint32_t>(bq - plan.nodes.data());
        CHECK(plan.param(n, 0) != nullptr);
        CHECK(plan.param(n, 1) == nullptr);
        CHECK(plan.param(n, 2) != nullptr);
        CHECK(plan.param(n, 3) == nullptr);
    });
    INFO(got.error);
    REQUIRE(got.loaded);
    REQUIRE(got.resolved);
    CHECK(got.nodes == 9);
    CHECK(got.bytes > 0);
    CHECK(got.bytes <= kArenaBytes);
    CHECK(got.run.view.blocks == want.view.blocks);
    CHECK(got.run.view.heard);
    CHECK(got.run.view.hash == want.view.hash);
    CHECK(got.run.faults == 0);
    CHECK(got.run.drops == 0);
    CHECK(got.allocations == 0);
    CHECK(got.frees == 0);
    CHECK(rp_devtest_plan_live_device_mode() == 0);  // unloadPlan destroyed every one
    {
        auto registry = desktopRegistry();
        ResourceTable resources;
        GraphHost host(*registry, resources, HostConfig{48000.0, kMaxFrames, Target::Desktop});
        REQUIRE(host.setGraph(kDocument).ok);
        CHECK_FALSE(host.setParam("bq", "q", 2.0));  // the desktop's refusal the loader's null matches
    }

    // The loader's own sizing is the exporter's: one computation.
    const ExportResult r = exported(kDocument);
    REQUIRE(r.ok());
    CHECK(r.size.bytes == got.bytes);
    CHECK(r.size.nodes == 9);
}

TEST_CASE("events and control feedback load and render as GraphHost renders them", "[device-mode][plan]") {
    const Rendered want = renderDesktop(kLoops, kLoopMoves);
    CHECK(want.view.heard);
    CHECK(want.faults == 0);
    CHECK(want.drops == 0);

    const std::vector<std::uint8_t> blob = blobOf(kLoops);
    const blob::Header h = headerOf(blob);
    CHECK(h.sections[static_cast<int>(blob::Section::Feedback)].count == 2);
    const LoadedRun got = renderLoaded(blob, kLoopMoves);
    INFO(got.error);
    REQUIRE(got.loaded);
    REQUIRE(got.resolved);
    CHECK(got.run.view.blocks == want.view.blocks);
    CHECK(got.run.view.hash == want.view.hash);
    CHECK(got.run.faults == 0);
    CHECK(got.run.drops == 0);
    CHECK(got.run.view.heard);
    CHECK(got.allocations == 0);
    CHECK(got.frees == 0);
}

TEST_CASE("an exported blob is byte-stable: twice, in another process, and against its golden", "[device-mode][plan]") {
    const std::vector<std::uint8_t> first = blobOf(kDocument);
    const std::vector<std::uint8_t> second = blobOf(kDocument);
    CHECK(first == second);

    // The golden: its bytes, so a change to the format or to a type's settings is a reviewed diff.
    const std::filesystem::path golden = std::filesystem::path(RP_DEVICE_MODE_GOLDEN_DIR) / "plan-blob.hex";
    if (const char* update = std::getenv("RP_PLAN_BLOB_UPDATE"); update && std::string(update) == "1") {
        std::ofstream(golden, std::ios::binary) << hexOf(first);
        WARN("recorded " << golden.string());
    }
    CHECK(readFile(golden) == hexOf(first));

    // The tool, in a process of its own, over a document of the packages it carries (core, dsp, n8): the same bytes as
    // this process exports, its header form holding them, and a plan over its arena refused with the loader's words.
    const auto dir =
        std::filesystem::temp_directory_path() / ("rp-export-plan-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(dir);
    const std::string doc = R"({"nodes":[
        {"id":"c","type":"rp.core.constant","params":{"value":0.25}},
        {"id":"d","type":"rp.core.delay","settings":{"maxSamples":2000,"channels":1},"params":{"time":120}},
        {"id":"g","type":"rp.dsp.gain","settings":{"channels":1},"params":{"gain":-0.5}},
        {"id":"bq","type":"rp.dsp.biquad","settings":{"type":"peak","channels":1}}],
      "wires":[{"from":"d.out","to":"g.in"},{"from":"g.out","to":"bq.in"},{"from":"c.out","to":"bq.gainDb"}]})";
    std::ofstream(dir / "plan.json") << doc;
    const std::string tool = RP_EXPORT_PLAN_TOOL;
    auto run = [&](const std::string& args) {
        std::string command = "\"" + tool + "\" \"" + (dir / "plan.json").string() + "\" " + args + " > \"" +
                              (dir / "stdout.txt").string() + "\" 2> \"" + (dir / "stderr.txt").string() + "\"";
#ifdef _WIN32
        command = "\"" + command + "\"";  // cmd /c drops a command line's outer quotes when it starts with one
#endif
        return std::system(command.c_str());
    };
    REQUIRE(run("\"" + (dir / "plan.bin").string() + "\" --arena 65536 --max-frames 512") == 0);
    auto registry = registryOf({rp_core_package(), rp_dsp_package(), rp_n8_package()});
    const ExportResult here = exportPlan(doc, *registry, deviceTypes(), options(65536));
    INFO(described(here));
    REQUIRE(here.ok());
    const std::string bytes = readFile(dir / "plan.bin");
    CHECK(std::vector<std::uint8_t>(bytes.begin(), bytes.end()) == here.blob);
    CHECK(contains(readFile(dir / "stdout.txt"), "4 nodes"));

    REQUIRE(run("\"" + (dir / "plan.hpp").string() + "\" --arena 65536 --max-frames 512 --header kPlan") == 0);
    const std::string header = readFile(dir / "plan.hpp");
    CHECK(contains(header, "alignas(4) inline constexpr std::uint8_t kPlan[] = {"));
    std::ostringstream firstBytes;
    for (int i = 0; i < 4; ++i) firstBytes << ' ' << static_cast<unsigned>(here.blob[i]) << ',';
    CHECK(contains(header, firstBytes.str()));  // "RPPL"

    // An arena with room for the plan's own structures and the constant, and not for the delay's line.
    const std::size_t small = here.size.bytes - here.size.instanceBytes + 1000;
    CHECK(run("\"" + (dir / "small.bin").string() + "\" --arena " + std::to_string(small) + " --max-frames 512") != 0);
    CHECK(contains(readFile(dir / "stderr.txt"), "arena d: \"d\" (rp.core.delay) does not fit"));
    CHECK_FALSE(std::filesystem::exists(dir / "small.bin"));
    CHECK(run("\"" + (dir / "x.bin").string() + "\" --arena") != 0);  // a usage error
    CHECK(run("\"" + (dir / "x.bin").string() + "\" --sample-rate inf") != 0);
    // A refusal at a port names it.
    std::ofstream(dir / "plan.json", std::ios::trunc)
        << R"({"nodes":[{"id":"pl","type":"rp.core.buffer-player","resources":{"sample":"res:1"}}],"wires":[]})";
    CHECK(run("\"" + (dir / "x.bin").string() + "\"") != 0);
    CHECK(contains(readFile(dir / "stderr.txt"), "resource pl.sample: resources are not exported to rp2350 yet"));
    std::filesystem::remove_all(dir);
}

TEST_CASE("the loader refuses a blob it cannot run, in its own words, creating nothing", "[device-mode][plan]") {
    const std::vector<std::uint8_t> good = blobOf(kDocument);
    REQUIRE(refusal(good).empty());

    SECTION("not a blob") {
        std::vector<std::uint8_t> b = good;
        b[0] = 'X';
        CHECK(refusal(b) == "plan-format : not a plan blob: it does not start with \"RPPL\"");
        CHECK(refusal({}) == "plan-format : not a plan blob: it does not start with \"RPPL\"");
    }
    SECTION("another version") {
        std::vector<std::uint8_t> b = good;
        const std::uint32_t two = 2;
        std::memcpy(b.data() + 4, &two, 4);
        CHECK(refusal(b) == "plan-version : plan format 2, this loader reads 1");
    }
    SECTION("a truncated blob") {
        std::vector<std::uint8_t> b(good.begin(), good.end() - 8);
        CHECK(refusal(b) == "plan-truncated : the blob is " + std::to_string(b.size()) + " bytes, its header says " +
                                std::to_string(good.size()));
        CHECK(contains(refusal(std::vector<std::uint8_t>(good.begin(), good.begin() + 40)),
                       "plan-truncated : the blob is 40 bytes, shorter than its header"));
    }
    SECTION("a bad CRC") {
        std::vector<std::uint8_t> b = good;
        b[headerOf(b).sections[static_cast<int>(blob::Section::Settings)].offset] ^= 0x01;
        CHECK(contains(refusal(b), "plan-crc : the blob's CRC is 0x"));
        CHECK(contains(refusal(b), ", its bytes give 0x"));
    }
    SECTION("an index out of range") {
        std::vector<std::uint8_t> b = good;
        const blob::Header h = headerOf(b);
        const auto& views = h.sections[static_cast<int>(blob::Section::Views)];
        const auto& queues = h.sections[static_cast<int>(blob::Section::Queues)];
        // The first events view: its queue made one past the pool.
        bool edited = false;
        for (std::uint32_t i = 0; i < views.count && !edited; ++i) {
            blob::ViewRecord v;
            std::memcpy(&v, b.data() + views.offset + i * sizeof v, sizeof v);
            if (v.kind != RP_PORT_EVENTS) continue;
            v.index = queues.count;
            std::memcpy(b.data() + views.offset + i * sizeof v, &v, sizeof v);
            reseal(b);
            CHECK(refusal(b) == "plan-malformed : views record " + std::to_string(i) + " names queue " +
                                    std::to_string(queues.count) + ", past the plan's " +
                                    std::to_string(queues.count) + " queues");
            edited = true;
        }
        CHECK(edited);
        // A section that runs past the blob.
        b = good;
        blob::Header bad = headerOf(b);
        bad.sections[static_cast<int>(blob::Section::Steps)].count = 0x10000000;
        std::memcpy(b.data(), &bad, sizeof bad);
        reseal(b);
        CHECK(contains(refusal(b), "plan-malformed : the steps section ["));
        CHECK(contains(refusal(b), "is not within the blob's"));
    }
    SECTION("an unknown type") {
        CHECK(refusal(good, kArenaBytes, {rp_core_package_device_mode(), rp_dsp_package_device_mode()}) ==
              "unknown-type src: no node type \"rp.devtest.source\" is registered");
    }
    SECTION("over its arena") {
        CHECK(contains(refusal(good, 4096), "arena : the plan's buffers, queues and steps take "));
        CHECK(contains(refusal(good, 4096), "-byte arena by "));
        // A node that tips it over: rp.core.delay's one-second default, which the Pico's arena cannot hold.
        const auto exportedBig = exported(R"({"nodes":[{"id":"src","type":"rp.devtest.source"},
            {"id":"d","type":"rp.core.delay"},{"id":"dg","type":"rp.devtest.digest"}],
            "wires":[{"from":"src.out","to":"d.in"},{"from":"d.out","to":"dg.in"}]})", 0);
        REQUIRE(exportedBig.ok());
        const std::vector<std::uint8_t>& big = exportedBig.blob;
        PlanSize size;
        PlanLoadError err{};
        REQUIRE(sizePlan(big, deviceTypes(), 0, &size, &err));
        const std::string why = refusal(big, kArenaBytes);
        CHECK(why == "arena d: \"d\" (rp.core.delay) does not fit: the plan needs " + std::to_string(size.bytes) +
                         " bytes, " + std::to_string(size.bytes - kArenaBytes) + " past the " +
                         std::to_string(kArenaBytes) + "-byte arena");
        CHECK(size.instanceBytes > 384000);
        CHECK(refusal(big, size.bytes).empty());  // exactly its size is enough
        CHECK(contains(refusal(big, size.bytes - 1), "1 past the"));
    }
    SECTION("a node whose activate fails part-way unwinds what was made before it") {
        // Not trial-loaded: the exporter would refuse it (the exporter's cases).
        const auto b = blobOf(R"({"nodes":[{"id":"src","type":"rp.devtest.source"},
            {"id":"a","type":"rp.devtest.lockstep"},{"id":"z","type":"rp.devtest.lockstep"},
            {"id":"f","type":"rp.devtest.fault","settings":{"at":"activate"}},{"id":"dg","type":"rp.devtest.digest"}],
            "wires":[{"from":"a.link","to":"z.link"},{"from":"f.out","to":"dg.in"}]})", false);
        REQUIRE(rp_devtest_plan_live_device_mode() == 0);
        CHECK(refusal(b) == "activate-failed f: activate failed");
        CHECK(rp_devtest_plan_live_device_mode() == 0);  // f, z, a and src destroyed; dg never made
    }
    SECTION("a node whose create fails part-way unwinds what was made before it") {
        const AlteredPlanTypes failing([](rp_node_type& t) { t.create = &refuseCreate; });
        REQUIRE(rp_devtest_plan_live_device_mode() == 0);
        // dg is the document's last node: the eight before it were created and activated, then unwound.
        CHECK(refusal(good, kArenaBytes, failing.all) == "create-failed dg: the digest will not be made here");
        CHECK(rp_devtest_plan_live_device_mode() == 0);
    }
    SECTION("a node that asks more alignment than an rp2350 arena gives") {
        const AlteredPlanTypes wide([](rp_node_type& t) { t.query_memory = &askSixteen; });
        CHECK(refusal(good, kArenaBytes, wide.all) ==
              "node-memory dg: node asked for an alignment of 16, where an rp2350 arena gives a power of two up to 8");
    }
    SECTION("settings a node's own query_memory refuses") {
        std::vector<std::uint8_t> b = good;
        const blob::Header h = headerOf(b);
        blob::NodeRecord a;  // node 2, the lockstep "a"
        std::memcpy(&a, b.data() + h.sections[static_cast<int>(blob::Section::Nodes)].offset + 2 * sizeof a,
                    sizeof a);
        b[h.sections[static_cast<int>(blob::Section::Settings)].offset + a.settings.offset] = 0xc1;  // never used
        reseal(b);
        CHECK(contains(refusal(b), "settings-invalid a: "));
    }
    SECTION("another target, another section count, another record size") {
        auto edited = [&](auto edit) {
            std::vector<std::uint8_t> b = good;
            blob::Header h = headerOf(b);
            edit(h);
            std::memcpy(b.data(), &h, sizeof h);
            reseal(b);
            return refusal(b);
        };
        CHECK(edited([](blob::Header& h) { h.target = 2; }) ==
              "plan-target : the plan is for target 2, this loader runs rp2350 plans");
        CHECK(edited([](blob::Header& h) { h.sectionCount = 16; }) ==
              "plan-malformed : the plan has 16 sections, not 17");
        CHECK(edited([](blob::Header& h) { h.sections[static_cast<int>(blob::Section::Nodes)].recordBytes = 36; }) ==
              "plan-malformed : the nodes section's records are 36 bytes, not 32");
        CHECK(edited([](blob::Header& h) {
                  h.sections[static_cast<int>(blob::Section::ParamViews)].recordBytes = 4;
              }) == "plan-malformed : the param views section's records are 4 bytes, not 8");
        // A wired flag that is neither 0 nor 1.
        {
            std::vector<std::uint8_t> b = good;
            const blob::Header h = headerOf(b);
            const std::uint32_t seven = 7;
            const auto at = h.sections[static_cast<int>(blob::Section::ParamViews)].offset + 3 * 8 + 4;
            std::memcpy(b.data() + at, &seven, 4);
            reseal(b);
            CHECK(refusal(b) == "plan-malformed : param views record 3 has a wired flag of 7, not 0 or 1");
        }
        // Sizes past what any arena holds: a sum that would wrap is refused, not wrapped.
        CHECK(edited([](blob::Header& h) {
                  h.buffers = 0xFFFFFFFFu;
                  h.maxFrames = 0x40000000u;
              }) == "arena : the plan's sizes overflow what any arena holds");
    }
}

TEST_CASE("the exporter refuses what a device cannot run, and says why", "[device-mode][plan]") {
    SECTION("a heap type and a threads type, as the planner refuses them on the Pico") {
        const auto heap = exported(R"({"nodes":[{"id":"p","type":"rp.dsp.dpcm","settings":{"buffer":"res:1"}}],
                                       "wires":[]})");
        CHECK(contains(codes(heap), "capability"));
        CHECK(contains(described(heap),
                       "node type \"rp.dsp.dpcm\" needs a heap, which the Pico target does not provide"));
        const auto threads = exported(R"({"nodes":[{"id":"t","type":"rp.devtest.threaded"}],"wires":[]})");
        CHECK(codes(threads) == "capability");
        CHECK(contains(described(threads), "needs threads, which the Pico target does not provide"));
    }
    SECTION("a resource, named in settings or bound") {
        const auto named = exported(R"({"nodes":[{"id":"f","type":"rp.devtest.fault","settings":{"rom":"res:3"}}],
                                        "wires":[]})");
        REQUIRE(named.errors.size() == 1);
        CHECK(named.errors[0].code == "resource");
        CHECK(named.errors[0].nodeId == "f");
        CHECK(named.errors[0].message == "rom: \"res:3\" is named, and resources are not exported to rp2350 yet");
        const auto bound = exported(R"({"nodes":[{"id":"pl","type":"rp.core.buffer-player",
                                                  "resources":{"sample":"res:1"}}],"wires":[]})");
        REQUIRE(bound.errors.size() == 1);
        CHECK(bound.errors[0].code == "resource");
        CHECK(bound.errors[0].nodeId == "pl");
        CHECK(bound.errors[0].port == "sample");
        CHECK(bound.errors[0].message == "resources are not exported to rp2350 yet");
    }
    SECTION("a type the device does not build") {
        const auto r = exported(R"({"nodes":[{"id":"src","type":"rp.devtest.source"},
            {"id":"out","type":"rp.host.audio-out"}],"wires":[]})");
        REQUIRE(r.errors.size() == 1);
        CHECK(r.errors[0].code == "unknown-type");
        CHECK(r.errors[0].nodeId == "out");
        CHECK(r.errors[0].message == "no node type \"rp.host.audio-out\" is registered");
    }
    SECTION("what only the device's build refuses, found by the trial load") {
        const auto r = exported(R"({"nodes":[{"id":"src","type":"rp.devtest.source"},
            {"id":"f","type":"rp.devtest.fault","settings":{"at":"activate"}}],"wires":[]})");
        REQUIRE(r.errors.size() == 1);
        CHECK(r.errors[0].code == "activate-failed");
        CHECK(r.errors[0].nodeId == "f");
        CHECK(r.errors[0].message == "activate failed");
        CHECK(r.blob.empty());
        CHECK(rp_devtest_plan_live_device_mode() == 0);  // the trial load unwound src
        CHECK(exported(R"({"nodes":[{"id":"f","type":"rp.devtest.fault","settings":{"at":"process"}}],
                           "wires":[]})").ok());  // a fault at the block is the box's to report, not the export's
    }
    SECTION("a type that asks more alignment than an rp2350 arena gives") {
        const AlteredPlanTypes wide([](rp_node_type& t) { t.query_memory = &askSixteen; });
        const auto r = exported(kDocument, kArenaBytes, true, wide.all);
        REQUIRE(r.errors.size() == 1);
        CHECK(r.errors[0].code == "node-memory");
        CHECK(r.errors[0].nodeId == "dg");
        CHECK(contains(r.errors[0].message, "up to 8"));
    }
    SECTION("a non-finite sample rate") {
        auto registry = desktopRegistry();
        ExportOptions o = options();
        o.sampleRate = std::numeric_limits<double>::infinity();
        CHECK(codes(exportPlan(kDocument, *registry, deviceTypes(), o)) == "export-options");
    }
    SECTION("a plan over its arena") {
        const auto r = exported(R"({"nodes":[{"id":"d","type":"rp.core.delay"}],"wires":[]})");
        REQUIRE(r.errors.size() == 1);
        CHECK(r.errors[0].code == "arena");
        CHECK(r.errors[0].nodeId == "d");
        CHECK(contains(r.errors[0].message, "\"d\" (rp.core.delay) does not fit: the plan needs "));
        CHECK(contains(r.errors[0].message, " past the 262144-byte arena"));
        CHECK(r.blob.empty());
        // With no arena the same plan is sized and not refused.
        const auto unbounded = exported(R"({"nodes":[{"id":"d","type":"rp.core.delay"}],"wires":[]})", 0);
        CHECK(unbounded.ok());
        CHECK(unbounded.size.bytes > kArenaBytes);
    }
    SECTION("what the planner refuses on the desktop too") {
        CHECK(codes(exported(R"({"nodes":[{"id":"a","type":"rp.devtest.lockstep"}],
                                  "wires":[{"from":"a.out","to":"a.in"}]})")) == "cycle");
        CHECK(codes(exported("not json")) == "document");
    }
}

TEST_CASE("the box's bridge, exported and loaded, hands the cart link what the filter keeps, in order, and never what "
          "it drops",
          "[device-mode][plan][device-io]") {
    const ExportResult r = exported(kBridge);
    INFO(described(r));
    REQUIRE(r.ok());
    constexpr std::uint32_t kBlock = 64;

    // The loaded plan, over the device's builds and the device-mode fake link.
    CartRecord device{};
    FakeMidiIn deviceMidi;
    BridgeHost deviceHost;
    deviceHost.cart = rp_devtest_cart_device_mode(&device);
    deviceHost.midi = &deviceMidi;
    Arena         arena(kArenaBytes);
    PlanLoadError err{};
    LoadedPlan*   plan = loadPlan(r.blob, arena.span(kArenaBytes), deviceTypes(), &deviceHost.host, &err);
    INFO(text(err.code) << " " << text(err.node) << ": " << text(err.message));
    REQUIRE(plan);
    CHECK(plan->nodes.size() == 3);
    CHECK(deviceHost.held == 1);
    for (std::size_t b = 0; b < bridgeBlocks().size(); ++b) {
        stageBlock(deviceMidi, b);
        runBlock(plan->view, kBlock, kTransport);
    }
    CHECK(plan->faults.load() == 0);
    CHECK(plan->drops.load() == 0);
    unloadPlan(plan);
    CHECK(deviceHost.held == 0);

    // GraphHost's run of the same document, over the desktop's builds and the desktop's fake link.
    CartRecord             desktop{};
    rp::graph::devtest::FakeCart desktopCart;
    desktopCart.record = &desktop;
    FakeMidiIn desktopMidi;
    BridgeHost desktopHost;
    desktopHost.cart = static_cast<rp::n8::CartLink*>(&desktopCart);
    desktopHost.midi = &desktopMidi;
    auto          registry = desktopRegistry();
    ResourceTable resources;
    HostConfig    config{48000.0, kMaxFrames, Target::Pico};
    config.interfaces = &BridgeHost::embedded;
    config.interfacesCtx = &desktopHost;
    {
        GraphHost host(*registry, resources, config);
        REQUIRE(host.setGraph(kBridge).ok);
        for (std::size_t b = 0; b < bridgeBlocks().size(); ++b) {
            stageBlock(desktopMidi, b);
            host.processBlock(kBlock, kTransport);
        }
        CHECK(host.faultCount() == 0);
        CHECK(host.droppedMessages() == 0);
    }

    for (const CartRecord* rec : {&device, &desktop}) {
        INFO((rec == &device ? "loaded" : "GraphHost"));
        CHECK(pushedLines(*rec) == keptLines());
        CHECK(rec->refused == 0);
        CHECK(rec->volume == rp::n8::expansionVolumeRegister(rp::n8::kExpansionVolumeDefault));  // unity: 128
        CHECK(rec->volume == 128);
    }
    // The dropped kinds never reached a push, and what reached one is every message of the others.
    std::size_t staged = 0, kept = 0;
    for (const auto& block : bridgeBlocks())
        for (const BridgeMessage& m : block) {
            ++staged;
            kept += m.kept;
        }
    CHECK(device.count == kept);
    CHECK(kept < staged);
}
