// The engine's side of graphs TS builds (docs/modular-graph-phase-b2-design.md section 2.5, and c3-design.md sections 3
// and 5 for what a node builds from): a document's system nodes and its slots must name the same systems, the tap
// re-arm rides the swap, a refused document leaves the running plan as it was, and a system node builds its core from
// the ROM resource its settings name and gives it back at reclaim; a system node's settings carry its core's knobs
// beside the system it names (docs/modular-graph-phase-c2-design.md section 4); and Engine::setParam reaches a param of
// the running plan at the next block, a system node's gain and a NES's expansion volume among them, over the RPC as TS
// calls it. Driven on a quiescent Engine, so every plan applies as it is published, except where a test owns the drain
// itself.
//
// Run via `pnpm test:plugin engine-graph`.

#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <quickjs.h>

#include "EmbeddedRoms.hpp"
#include "GraphDocs.hpp"
#include "NodeRig.hpp"
#include "TypedRpcServer.h"
#include "codecs/QuickJSCodec.h"
#include "host/engine/DspCommand.hpp"
#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/rpc/BackendRpcRegistration.hpp"
#include "host/rpc/EngineRpcService.hpp"
#include "system/CoreBackends.hpp"
#include "system/SystemFactory.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "transports/QuickJSTransport.h"

using rp::host::test::readFile;

namespace {

constexpr double kRate = 48000.0;

using rp::host::test::DocSystem;

// `systems` as a stereo document at `channels` host channels, each system's ROM put in `e` first: a node builds its
// core from the resources its settings name.
Engine::GraphRequest request(Engine& e, rp::host::test::Roms& roms, std::vector<DocSystem> systems,
                             std::uint32_t channels = 2) {
    std::vector<SystemId> slots;
    for (const DocSystem& s : systems) slots.push_back(s.id);
    rp::host::test::withRoms(e, systems, roms);
    return rp::host::test::graphRequest(rp::host::test::stereoDocument(systems, channels), slots);
}

// `r`, re-arming the tap to `mode` on `id`.
Engine::GraphRequest arming(Engine::GraphRequest r, SystemId id, std::uint32_t mode) {
    r.rearmTap = true;
    r.arm = true;
    r.armId = id;
    r.armMode = mode;
    return r;
}

const char* const kGb = rp::host::test::kGbType;
const char* const kNes = rp::host::test::kNesType;

}  // namespace

TEST_CASE("a document places its system nodes' cores", "[engine-graph][b2]") {
    Engine e(kRate);
    rp::host::test::Roms roms;
    // Four host channels, so the width read back is this document's, not the stereo pair of the first plan.
    const auto r = request(e, roms, {{2, kGb}, {1, kGb}}, 4);
    const auto result = e.setGraph(r);
    REQUIRE(result.ok);
    CHECK(e.systemCount() == 2);
    CHECK(e.findSystem(1) != nullptr);
    CHECK(e.findSystem(2) != nullptr);
    CHECK(e.coreLive(1));
    CHECK(e.hostWidth() == 4);
    // Rendering runs the plan: the table the tail walks is the slots'.
    std::vector<float> lanes(4 * 256);
    float* outs[4] = {&lanes[0], &lanes[256], &lanes[512], &lanes[768]};
    e.processBlock(256, outs, 4);
}

TEST_CASE("the slots and the system nodes must name the same systems", "[engine-graph][b2]") {
    Engine e(kRate);
    rp::host::test::Roms roms;
    auto systems = std::vector<DocSystem>{{1, kGb}, {2, kGb}};
    withRoms(e, systems, roms);

    auto missing = rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1});
    auto result = e.setGraph(missing);
    CHECK_FALSE(result.ok);
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].code == "slots");

    auto twice = rp::host::test::graphRequest(rp::host::test::stereoDocument({{1, kGb, systems[0].rom}}), {1, 1});
    CHECK(e.setGraph(twice).errors.at(0).code == "slots");

    auto renamed = rp::host::test::graphRequest(rp::host::test::stereoDocument({{1, kGb, systems[0].rom}}), {1});
    const auto at = renamed.document.find(R"("sys-1","type")");
    REQUIRE(at != std::string::npos);
    renamed.document.replace(at, 7, R"("gb-1")");
    CHECK(e.setGraph(renamed).errors.at(0).code == "slots");

    // A system node whose ROM resource the engine does not hold is refused on that node.
    result = e.setGraph(request(e, roms, {{9, kGb, 7}}));
    CHECK(result.errors.at(0).code == "create-failed");
    CHECK(result.errors.at(0).nodeId == "sys-9");
    CHECK(result.errors.at(0).message.find("is not a live resource") != std::string::npos);

    // Nothing was published, so nothing runs and no core was built.
    CHECK(e.systemCount() == 0);
    CHECK_FALSE(e.coreLive(1));
    CHECK(e.liveSystemCount() == 0);
}

TEST_CASE("a refused document leaves the running plan and its cores alone", "[engine-graph][b2]") {
    Engine e(kRate);
    rp::host::test::Roms roms;
    REQUIRE(e.setGraph(request(e, roms, {{1, kGb}})).ok);
    SystemBase* const running = e.findSystem(1);
    REQUIRE(running != nullptr);
    // The refused document reuses sys-1 (same ROM resource) and places two more: sys-2 builds, and sys-3 is handed the
    // NES fixture under a Game Boy node, so its activation refuses the plan after sys-2's core exists. The refusal's
    // unwind is what has to leave the running plan alone.
    const std::uint32_t nesRom = e.putResource("rom", readFile(RP_BLIPTOASTER_ROM_PATH));
    const auto refused = e.setGraph(request(e, roms, {{1, kGb}, {2, kGb}, {3, kGb, nesRom}}));
    CHECK_FALSE(refused.ok);
    REQUIRE(refused.errors.size() == 1);
    CHECK(refused.errors[0].nodeId == "sys-3");
    CHECK(refused.errors[0].code == "activate-failed");
    CHECK(refused.errors[0].message == "the ROM is not a Game Boy ROM");
    CHECK(e.systemCount() == 1);
    CHECK(e.findSystem(1) == running);  // the plan still published is the one that was running
    CHECK(e.coreLive(1));
    CHECK_FALSE(e.coreLive(2));  // built, and destroyed as the refused plan unwound
    CHECK_FALSE(e.coreLive(3));
    CHECK(e.resourcePinCount(nesRom) == 0);
}

TEST_CASE("the tap re-arm rides the swap: restore, then arm", "[engine-graph][b2]") {
    Engine e(kRate);
    rp::host::test::Roms roms;
    auto systems = std::vector<DocSystem>{{1, kNes}};
    withRoms(e, systems, roms);
    auto nesRequest = [&](std::uint32_t channels = 2) {
        return rp::host::test::graphRequest(rp::host::test::stereoDocument(systems, channels), {1});
    };
    auto* nes = dynamic_cast<MesenNesSystem*>(e.findSystem(1));
    CHECK(nes == nullptr);  // not built until a document places it

    REQUIRE(e.setGraph(arming(nesRequest(), 1, 1)).ok);
    nes = dynamic_cast<MesenNesSystem*>(e.findSystem(1));
    REQUIRE(nes != nullptr);
    CHECK(nes->channelExportMode() == 1);

    // No re-arm: the tap is left as it is.
    REQUIRE(e.setGraph(nesRequest()).ok);
    CHECK(nes->channelExportMode() == 1);

    // A re-arm with nothing to arm restores the mode the core had.
    auto restore = nesRequest();
    restore.rearmTap = true;
    REQUIRE(e.setGraph(restore).ok);
    CHECK(nes->channelExportMode() == 0);

    // Channels, then a re-arm to the same mode, which restores and arms again.
    const auto armChannels = arming(nesRequest(), 1, 3);
    REQUIRE(e.setGraph(armChannels).ok);
    CHECK(nes->channelExportMode() == 3);
    REQUIRE(e.setGraph(armChannels).ok);
    CHECK(nes->channelExportMode() == 3);
    REQUIRE(e.setGraph(restore).ok);
    CHECK(nes->channelExportMode() == 0);
}

TEST_CASE("a sample-rate change re-rates the running plan's cores", "[engine-graph][b2]") {
    Engine e(kRate);
    rp::host::test::Roms roms;
    REQUIRE(e.setGraph(request(e, roms, {{1, kGb}})).ok);
    const auto* raw = dynamic_cast<const SameBoySystem*>(e.findSystem(1));
    REQUIRE(raw != nullptr);
    REQUIRE(raw->sampleRate_ == kRate);
    e.setSampleRate(44100.0);
    CHECK(raw->sampleRate_ == 44100.0);
}

TEST_CASE("bad tap requests are refused", "[engine-graph][b2]") {
    Engine e(kRate);
    rp::host::test::Roms roms;
    auto systems = std::vector<DocSystem>{{1, kNes}};
    withRoms(e, systems, roms);
    const auto nesRequest = [&] { return rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1}); };
    // 257 is mode 1 once narrowed to the command's byte.
    for (const std::uint32_t mode : {0u, 4u, 7u, 257u}) {
        const auto result = e.setGraph(arming(nesRequest(), 1, mode));
        CHECK_FALSE(result.ok);
        REQUIRE(result.errors.size() == 1);
        CHECK(result.errors[0].code == "tap");
    }
    CHECK(e.setGraph(arming(nesRequest(), 2, 1)).errors.at(0).code == "tap");  // not in the slots
    CHECK(e.systemCount() == 0);
    CHECK_FALSE(e.coreLive(1));
}

TEST_CASE("an id above INT32_MAX is read whole, one above UINT32_MAX is refused", "[engine-graph][b2]") {
    Engine e(kRate);
    rp::host::test::Roms roms;
    constexpr SystemId kHigh = 3000000000u;
    REQUIRE(e.setGraph(request(e, roms, {{kHigh, kGb}})).ok);
    CHECK(e.findSystem(kHigh) != nullptr);

    auto over = request(e, roms, {});
    over.document = R"({"nodes":[{"id":"sys-4294967296","type":"rp.sameboy.gb","settings":{"system":4294967296}}],"wires":[]})";
    CHECK(e.setGraph(over).errors.at(0).code == "slots");
}

TEST_CASE("an audio-out with no buses of its own is its default eight channels wide", "[engine-graph][b2]") {
    Engine e(kRate);
    rp::host::test::Roms roms;
    auto systems = std::vector<DocSystem>{{1, kGb}};
    withRoms(e, systems, roms);
    Engine::GraphRequest r;
    r.document = R"({"nodes":[)" + rp::host::test::docs_detail::sys(systems[0]) +
                 R"(,{"id":"out","type":"rp.host.audio-out"}],"wires":[{"from":"sys-1.mix","to":"out.out.1-2"}]})";
    r.slots = {1};
    REQUIRE(e.setGraph(r).ok);
    CHECK(e.hostWidth() == 8);
}

TEST_CASE("setGraph over the RPC layer places a system a document's node builds", "[engine-graph][b2]") {
    Engine           e(kRate);
    rp::host::test::Roms roms;
    QueuedInvoker    invoker(e);
    EngineRpcService rpc(e, invoker);
    auto systems = std::vector<DocSystem>{{5, kGb}};
    withRoms(e, systems, roms);
    RpcSetGraphRequest graph{rp::host::test::stereoDocument(systems), {5}, std::nullopt};
    const auto placed = rpc.setGraph(graph);
    CHECK(placed.ok);
    CHECK(placed.errors.empty());
    CHECK(e.findSystem(5) != nullptr);
    CHECK(e.coreLive(5));
    CHECK(e.ramSize(5) > 0);
}

TEST_CASE("a render into buffers the planned graph does not write is refused", "[engine-graph][b2]") {
    // TS sets the shape a render call needs before it makes the call; the render calls only check it.
    Engine           e(kRate);
    rp::host::test::Roms roms;
    QueuedInvoker    inv(e);
    EngineRpcService svc(e, inv);
    auto systems = std::vector<DocSystem>{{1, kGb}, {2, kGb}};
    withRoms(e, systems, roms);
    REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1, 2})).ok);
    CHECK(e.hostWidth() == 2);
    CHECK_FALSE(svc.renderAudio(10.0).empty());
    CHECK(svc.renderAudioPerSystem(10.0).empty());  // 4 lanes wanted, 2 planned
    RpcRenderBlocksRequest blocks{4, {RpcRenderBlock{256, {}, {}, std::nullopt, std::nullopt, std::nullopt}}};
    CHECK_FALSE(svc.renderBlocks(blocks).error.empty());

    REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::perSystemDocument(systems), {1, 2})).ok);
    CHECK(e.hostWidth() == 4);
    CHECK(svc.renderAudioPerSystem(10.0).size() == 2);
    CHECK(svc.renderAudio(10.0).empty());
    CHECK(svc.renderBlocks(blocks).error.empty());
}

// ---- knobs (docs/modular-graph-phase-c2-design.md section 4) -----------------------------------------------------

namespace {

// A pack of one node, knobs.level, whose param `level` (-1 to 1, 0 by default) it writes to every frame of its mono
// output from the frame each change lands at.
const char* const kLevelPack = R"JS(
(function () {
  var nodes = globalThis.__rp_scriptNodes = globalThis.__rp_scriptNodes || [];
  var ports = [{ id: "out", dir: "out", kind: "audio", channels: { count: 1, layout: "mono" } }];
  var params = [{ id: "level", name: "Level", type: "float", min: -1, max: 1, default: 0 }];
  nodes[nodes.length] = {
    id: "knobs.level",
    descriptor: function () { return { id: "knobs.level", ports: ports, params: params, settings: { type: "object" } }; },
    parseSettings: function (raw) { return raw === undefined ? {} : raw; },
    layout: function () { return { ports: ports, params: params }; },
    create: function () {
      return { process: function (b) {
        var level = b.control("level"), out = b.audio("out")[0], changes = level.changes, v = level.start, k = 0;
        for (var f = 0; f < b.frames; f++) {
          while (k < changes.length && changes[k].frame <= f) v = changes[k++].value;
          out[f] = v;
        }
      } };
    },
    reshapes: false,
    faultOnThrow: false,
  };
})();
)JS";

}  // namespace

TEST_CASE("a system node's settings carry its knobs beside its system, and the engine places it",
          "[engine-graph][settings]") {
    Engine e(kRate);
    rp::host::test::Roms roms;
    auto r = request(e, roms, {{1, kGb, 0, R"("highpass":"off","serialOutCapture":"listen")"},
                         {2, kNes, 0, R"("removeSpriteLimit":true)"}});
    REQUIRE(e.setGraph(r).ok);
    // Given as the nodes took their cores, before a block.
    const auto* gb = dynamic_cast<const SameBoySystem*>(e.findSystem(1));
    const auto* nes = dynamic_cast<const MesenNesSystem*>(e.findSystem(2));
    REQUIRE(gb);
    REQUIRE(nes);
    CHECK(gb->config_.highpass == SameBoyHighpass::Off);
    CHECK(gb->serialOutCapture() == SerialOutCapture::Listen);
    CHECK(nes->config().removeSpriteLimit);

    // Settings a node refuses refuse the document: the running plan keeps its knobs, and the core a new node would have
    // built is never built. The refused document names the two placed systems again (their ROM resources are unchanged,
    // so their nodes are reused) and moves a knob of sys-1's, which must not land.
    const auto refused = e.setGraph(request(e, roms, {{1, kGb, 0, R"("highpass":"removeDcOffset")"},
                                               {2, kNes, 0, R"("removeSpriteLimit":true)"},
                                               {3, kGb, 0, R"("model":"cgb-c")"}}));
    CHECK_FALSE(refused.ok);
    REQUIRE(refused.errors.size() == 1);
    CHECK(refused.errors[0].code == "settings-invalid");
    CHECK(refused.errors[0].nodeId == "sys-3");
    CHECK_FALSE(e.coreLive(3));
    CHECK(e.systemCount() == 2);
    CHECK(e.findSystem(1) == static_cast<const SystemBase*>(gb));   // reused, and the same core
    CHECK(e.findSystem(2) == static_cast<const SystemBase*>(nes));
    // A block runs the plan still published, whose knobs are untouched: the refused change is not among them.
    std::vector<float> left(256), right(256);
    float* outs[2] = {left.data(), right.data()};
    e.processBlock(256, outs, 2);
    CHECK(gb->config_.highpass == SameBoyHighpass::Off);
    CHECK(gb->serialOutCapture() == SerialOutCapture::Listen);
    CHECK(nes->config().removeSpriteLimit);
}

TEST_CASE("a setting lands at the node's next prepare, after a host command sent behind its plan",
          "[engine-graph][settings]") {
    // A caller that must act on the restarted core renders a block first: a model change is only pending when a
    // quiescent writeRam follows it, so the write lands on the core the next block's prepare restarts, and the
    // restarted core never sees it. A knob applied in place leaves the write alone.
    Engine e(kRate);
    rp::host::test::Roms roms;
    auto systems = std::vector<DocSystem>{{1, kGb, 0, R"("model":"dmgB","highpass":"accurate")"}};
    withRoms(e, systems, roms);
    const auto plan = [&](const std::string& knobs) {
        systems[0].extra = knobs;
        return e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1}));
    };
    REQUIRE(plan(R"("model":"dmgB","highpass":"accurate")").ok);
    std::vector<float> l(256), r(256);
    float* outs[2] = {l.data(), r.data()};
    e.processBlock(256, outs, 2);
    const std::vector<std::uint8_t> written = {0xde, 0xad, 0xbe, 0xef, 0x01, 0x23, 0x45, 0x67};
    const auto ram = [&] {
        auto access = e.findSystem(1)->getMemory(rp::MemoryType::Ram, rp::AccessType::ReadWrite);
        REQUIRE(access.valid());
        return std::vector<std::uint8_t>(access.data(), access.data() + written.size());
    };
    QueuedInvoker invoker(e);
    // A highpass change is an in-place knob that restarts nothing: the write is on the core when the block starts,
    // and stays there.
    REQUIRE(plan(R"("model":"dmgB","highpass":"off")").ok);
    invoker.writeRam(1, 0, written);
    REQUIRE(ram() == written);
    e.processBlock(256, outs, 2);
    CHECK(ram() == written);
    const auto* gb = dynamic_cast<const SameBoySystem*>(e.findSystem(1));
    REQUIRE(gb);
    CHECK(gb->config_.highpass == SameBoyHighpass::Off);
    // A model change first, then the write: the write lands on the core the block's prepare replaces.
    REQUIRE(plan(R"("model":"mgb")").ok);
    invoker.writeRam(1, 0, written);
    REQUIRE(ram() == written);
    e.processBlock(256, outs, 2);
    // The restart keeps the instance (its key is the core id); the pointer is re-fetched defensively.
    const auto* restarted = dynamic_cast<const SameBoySystem*>(e.findSystem(1));
    REQUIRE(restarted);
    CHECK(restarted->config_.model == SameBoyModel::Mgb);
    CHECK(ram() != written);
}

TEST_CASE("Engine::setParam reaches a param of the running plan at the next block, clamped", "[engine-graph][settings]") {
    Engine e(kRate);
    rp::host::test::Roms roms;
    REQUIRE(e.loadScriptPack("knobs", kLevelPack).ok);
    auto systems = std::vector<DocSystem>{{1, kGb}};
    rp::host::test::withRoms(e, systems, roms);
    const rp::host::test::DocExtras extras{{rp::host::test::docNode("level", "knobs.level")},
                                           {rp::host::test::docWire("level.out", "out.out.2")}};
    // The Game Boy alone on channel 1, so channel 2 is the toy's.
    auto r = rp::host::test::graphRequest(rp::host::test::stereoDocument(systems, extras), {1});
    const auto split = r.document.find(R"({"from":"split-1.out2","to":"out.out.2"})");
    REQUIRE(split != std::string::npos);
    r.document.erase(split, std::string(R"({"from":"split-1.out2","to":"out.out.2"},)").size());
    REQUIRE(e.setGraph(r).ok);
    std::vector<float> l(256), right(256);
    const auto block = [&] {
        float* outs[2] = {l.data(), right.data()};
        e.processBlock(256, outs, 2);
        return std::pair{right.front(), right.back()};
    };
    CHECK(block() == std::pair{0.0f, 0.0f});

    // Refused: a node or a param the plan does not have, and a NaN.
    CHECK_FALSE(e.setParam("nope", "level", 0.5));
    CHECK_FALSE(e.setParam("level", "nope", 0.5));
    CHECK_FALSE(e.setParam("level", "level", std::numeric_limits<double>::quiet_NaN()));
    CHECK_FALSE(e.setParam("sys-1", "volume", 0.0));
    CHECK(block() == std::pair{0.0f, 0.0f});

    CHECK(e.setParam("level", "level", 0.5));
    CHECK(block() == std::pair{0.5f, 0.5f});  // from the first frame of the next block
    CHECK(e.setParam("level", "level", 5.0));  // clamped to the param's range
    CHECK(block() == std::pair{1.0f, 1.0f});
    CHECK(e.setParam("level", "level", -3.0));
    CHECK(block() == std::pair{-1.0f, -1.0f});
    CHECK(block() == std::pair{-1.0f, -1.0f});  // and held

    // A published document carrying the node again keeps the value it holds.
    REQUIRE(e.setGraph(r).ok);
    CHECK(block() == std::pair{-1.0f, -1.0f});
}

TEST_CASE("Engine::setParam sets a system node's gain and a NES's expansion volume at the next block, clamped",
          "[engine-graph][settings][params]") {
    Engine e(kRate);
    rp::host::test::Roms roms;
    auto systems = std::vector<DocSystem>{{1, kGb}, {2, kNes}};
    withRoms(e, systems, roms);
    REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1, 2})).ok);
    const auto* gb = dynamic_cast<const SameBoySystem*>(e.findSystem(1));
    const auto* nes = dynamic_cast<const MesenNesSystem*>(e.findSystem(2));
    REQUIRE(gb);
    REQUIRE(nes);
    std::vector<float> l(256), r(256);
    const auto block = [&] {
        float* outs[2] = {l.data(), r.data()};
        e.processBlock(256, outs, 2);
    };
    block();
    CHECK(gb->config_.gainDb == 0.0f);
    CHECK(nes->config().expansionVolume == 100);
    CHECK(e.setParam("sys-1", "gain", -6.0));
    CHECK(e.setParam("sys-2", "expansionVolume", 40.0));
    CHECK(e.setParam("sys-2", "gain", 30.0));  // clamped to +12
    CHECK(gb->config_.gainDb == 0.0f);         // at the next block, not before
    block();
    CHECK(gb->config_.gainDb == -6.0f);
    CHECK(nes->config().expansionVolume == 40);
    CHECK(nes->config().gainDb == 12.0f);
    CHECK_FALSE(e.setParam("sys-1", "expansionVolume", 40.0));  // a NES's alone
    // A document carrying a value moves it; one dropping the param keeps the live value.
    systems[0].params = R"("gain":-20)";
    REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1, 2})).ok);
    block();
    CHECK(gb->config_.gainDb == -20.0f);
    systems[0].params.clear();
    REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1, 2})).ok);
    block();
    CHECK(gb->config_.gainDb == -20.0f);
    CHECK(nes->config().expansionVolume == 40);
}

TEST_CASE("setParam over the RPC layer takes the node, the param and the value as realBackend sends them", "[engine-graph][params]") {
    struct Js {
        JSRuntime* rt  = JS_NewRuntime();
        JSContext* ctx = JS_NewContext(rt);
        ~Js() {
            JS_FreeContext(ctx);
            JS_FreeRuntime(rt);
        }
    } js;  // first, so it outlives the server holding its context

    Engine           e(kRate);
    rp::host::test::Roms roms;
    QueuedInvoker    invoker(e);
    EngineRpcService rpc(e, invoker);
    rpcpp::QuickJSTransport                                   transport(js.ctx, [](JSContext*, JSValue) {});
    rpcpp::TypedRpcServer<rpcpp::Empty, rpcpp::QuickJSCodec> server(transport, rpcpp::QuickJSCodec{js.ctx});
    registerEmulatorRpc(server, rpc);
    REQUIRE(e.setGraph(request(e, roms, {{1, kGb}, {2, kNes}})).ok);

    // The call createEmulatorClient makes: true when the engine took it.
    const auto setParam = [&](const std::string& params) {
        const std::string src = "({jsonrpc: '2.0', id: 1, method: 'setParam', params: [" + params + "]})";
        JSValue message = JS_Eval(js.ctx, src.c_str(), src.size(), "<setParam>", JS_EVAL_TYPE_GLOBAL);
        std::optional<rpcpp::QuickJSOut> reply;
        if (!JS_IsException(message)) reply = server.processMessage(message);
        JS_FreeValue(js.ctx, message);
        if (!reply) return false;
        JSValue answer = reply->materialize(js.ctx);
        JSValue result = JS_GetPropertyStr(js.ctx, answer, "result");
        const bool accepted = JS_IsBool(result) && JS_ToBool(js.ctx, result);
        JS_FreeValue(js.ctx, result);
        JS_FreeValue(js.ctx, answer);
        return accepted;
    };
    CHECK(setParam("'sys-1', 'gain', -6.5"));
    // An integer: a whole-percent param, decoded to the double the engine takes rather than refused.
    CHECK(setParam("'sys-2', 'expansionVolume', 40"));
    CHECK_FALSE(setParam("'sys-1', 'nope', 0"));
    CHECK_FALSE(setParam("'sys-9', 'gain', 0"));
    std::vector<float> l(256), r(256);
    float* outs[2] = {l.data(), r.data()};
    e.processBlock(256, outs, 2);
    const auto* gb = dynamic_cast<const SameBoySystem*>(e.findSystem(1));
    const auto* nes = dynamic_cast<const MesenNesSystem*>(e.findSystem(2));
    REQUIRE(gb);
    REQUIRE(nes);
    CHECK(gb->config_.gainDb == -6.5f);
    CHECK(nes->config().expansionVolume == 40);
}

TEST_CASE("putResource and releaseResource over the RPC layer", "[engine-graph][resource]") {
    // The two calls createEmulatorClient makes for a resource (docs/modular-graph-phase-c3-design.md section 4): the
    // answer realBackend turns into {id} or {error}, and the drop. Driven through the QuickJS codec, because the
    // optional fields are the wire shape: `id` a number when a resource was created, `error` a string when none was.
    struct Js {
        JSRuntime* rt  = JS_NewRuntime();
        JSContext* ctx = JS_NewContext(rt);
        ~Js() {
            JS_FreeContext(ctx);
            JS_FreeRuntime(rt);
        }
    } js;  // first, so it outlives the server holding its context

    Engine           e(kRate);
    QueuedInvoker    invoker(e);
    EngineRpcService rpc(e, invoker);
    rpcpp::QuickJSTransport                                   transport(js.ctx, [](JSContext*, JSValue) {});
    rpcpp::TypedRpcServer<rpcpp::Empty, rpcpp::QuickJSCodec> server(transport, rpcpp::QuickJSCodec{js.ctx});
    registerEmulatorRpc(server, rpc);

    // One call, answering its `result` (owned by the caller).
    const auto call = [&](const char* method, const std::string& params) -> JSValue {
        const std::string src =
            std::string("({jsonrpc: '2.0', id: 1, method: '") + method + "', params: [" + params + "]})";
        JSValue message = JS_Eval(js.ctx, src.c_str(), src.size(), "<rpc>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(message)) {
            JS_FreeValue(js.ctx, message);
            return JS_UNDEFINED;
        }
        std::optional<rpcpp::QuickJSOut> reply = server.processMessage(message);
        JS_FreeValue(js.ctx, message);
        if (!reply) return JS_UNDEFINED;
        JSValue answer = reply->materialize(js.ctx);
        JSValue result = JS_GetPropertyStr(js.ctx, answer, "result");
        JS_FreeValue(js.ctx, answer);
        return result;
    };
    struct Answer {
        std::uint32_t id = 0;  // 0: absent
        std::string   error;   // empty: absent
    };
    const auto put = [&](const std::string& params) {
        Answer a;
        JSValue result = call("putResource", params);
        JSValue id = JS_GetPropertyStr(js.ctx, result, "id");
        if (JS_IsNumber(id)) {
            double value = 0.0;
            JS_ToFloat64(js.ctx, &value, id);
            a.id = static_cast<std::uint32_t>(value);
        }
        JS_FreeValue(js.ctx, id);
        JSValue error = JS_GetPropertyStr(js.ctx, result, "error");
        if (JS_IsString(error)) {
            if (const char* text = JS_ToCString(js.ctx, error)) {
                a.error = text;
                JS_FreeCString(js.ctx, text);
            }
        }
        JS_FreeValue(js.ctx, error);
        JS_FreeValue(js.ctx, result);
        return a;
    };

    // The bytes themselves: an immutable resource of the type the spec gave, owned and pinned by nobody yet.
    const Answer byBytes = put("{type: 'rom', bytes: new Uint8Array([1, 2, 3, 4])}");
    REQUIRE(byBytes.error.empty());
    CHECK(byBytes.id != 0);
    const rp_resource* bytesRom = e.resource(byBytes.id);
    REQUIRE(bytesRom);
    CHECK(std::string(bytesRom->type) == "rom");
    const auto* payload = static_cast<const rp_bytes*>(bytesRom->payload);
    REQUIRE(payload);
    CHECK(payload->size == 4);
    CHECK(payload->data[2] == 3);
    CHECK(e.resourcePinCount(byBytes.id) == 0);

    // A path: native reads the file, so a battery or a 32 MB ROM never crosses the bridge. The SMS tracker's `.sav` is
    // a real battery the tree already carries.
    const Answer byPath = put(std::string("{type: 'sram', path: '") + RP_SMS_SAV_PATH + "'}");
    REQUIRE(byPath.error.empty());
    const rp_resource* sram = e.resource(byPath.id);
    REQUIRE(sram);
    CHECK(std::string(sram->type) == "sram");
    CHECK(static_cast<const rp_bytes*>(sram->payload)->size == std::filesystem::file_size(RP_SMS_SAV_PATH));

    // An embedded ROM, by its marker.
    const Answer byMarker = put("{type: 'rom', embedded: 'mgb'}");
    REQUIRE(byMarker.error.empty());
    REQUIRE(e.resource(byMarker.id));
    CHECK(static_cast<const rp_bytes*>(e.resource(byMarker.id)->payload)->size == rp::embeddedMgbRom().size());

    // Every refusal answers `error` and creates nothing: a type no node reads, a file that is not there, a marker the
    // binary does not carry, and a spec with no source or two.
    for (const auto& refused : {
             put("{type: 'kit', bytes: new Uint8Array([1])}"),
             put("{type: 'sram', path: '/nonexistent/rp-resource.sav'}"),
             put("{type: 'rom', embedded: 'nope'}"),
             put("{type: 'rom'}"),
             put("{type: 'rom', bytes: new Uint8Array([1]), embedded: 'mgb'}"),
             put("{type: 'rom', bytes: new Uint8Array(0)}"),
         }) {
        CHECK(refused.id == 0);
        CHECK_FALSE(refused.error.empty());
    }
    // An empty byte block is a resource for a seed (a blank battery), but never for a rom. Its payload carries a valid
    // data pointer at size 0: a node forms its vector from (data, data + size), which is undefined on a null pointer.
    const Answer blank = put("{type: 'sram', bytes: new Uint8Array(0)}");
    REQUIRE(blank.error.empty());
    const auto* blankPayload = static_cast<const rp_bytes*>(e.resource(blank.id)->payload);
    REQUIRE(blankPayload);
    CHECK(blankPayload->size == 0);
    CHECK(blankPayload->data != nullptr);
    CHECK(put("{type: 'kit', bytes: new Uint8Array([1])}").error.find("\"rom\", \"sram\" or \"state\"") !=
          std::string::npos);
    CHECK(e.resource(byBytes.id) != nullptr);  // the refusals left what was there alone

    // The owner's drop: idempotent, answered as a boolean, and the id stays dead.
    const auto release = [&](std::uint32_t id) {
        JSValue result = call("releaseResource", std::to_string(id));
        const bool dropped = JS_IsBool(result) && JS_ToBool(js.ctx, result);
        JS_FreeValue(js.ctx, result);
        return dropped;
    };
    CHECK(release(byBytes.id));
    CHECK_FALSE(release(byBytes.id));
    CHECK(e.resource(byBytes.id) == nullptr);
    CHECK_FALSE(release(9999));
    // Something the running plan pins would stay alive here instead; nothing pins these, so a drop frees them.
    CHECK(release(byMarker.id));
    const Answer later = put("{type: 'rom', bytes: new Uint8Array([9])}");
    REQUIRE(later.error.empty());
    CHECK(later.id > byMarker.id);  // ids are never reused
}

// ---- a system node builds its core from its settings (docs/modular-graph-phase-c3-design.md sections 3 and 5) -------

namespace {

// A stereo document placing one system node that builds its own core: sys-<id> names `system` and its ROM resource,
// with whatever construct-time settings or params the case adds.
std::string builtDoc(SystemId id, const char* type, std::uint32_t rom, const std::string& extra = "",
                     const std::string& params = "") {
    const std::string node = "sys-" + std::to_string(id);
    const std::string settings = R"({"system":)" + std::to_string(id) + R"(,"rom":"res:)" + std::to_string(rom) + "\"" +
                                 (extra.empty() ? "" : "," + extra) + "}";
    return R"({"nodes":[{"id":")" + node + R"(","type":")" + type + R"(","settings":)" + settings +
           (params.empty() ? "" : R"(,"params":)" + params) +
           R"(},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                "wires":[{"from":")" + node + R"(.mix","to":"out.out.1-2"}]})";
}

Engine::GraphRequest builtRequest(std::string document, std::vector<SystemId> slots) {
    return rp::host::test::graphRequest(std::move(document), std::move(slots));
}

// The one reason a refusal gives, as "node: code: message".
std::string refusal(const Engine::GraphResult& r) {
    if (r.ok || r.errors.size() != 1) return r.ok ? "ok" : std::to_string(r.errors.size()) + " errors";
    const auto& e = r.errors.front();
    return e.nodeId + ": " + e.code + ": " + e.message;
}

// A core that publishes nothing: claiming a snapshot slot for one costs no boot, so 64 of them can fill the pool.
class EmptyCore final : public SystemBase {
public:
    explicit EmptyCore(SystemId id) : SystemBase(id) {}
    SystemKind kind() const override { return SystemKind::SameBoy; }
    void onActivate(double) override {}
    void onSampleRateChanged(double) override {}
};

}  // namespace

TEST_CASE("a system node builds its core from its system and ROM resource, and gives it all back at reclaim",
          "[engine-graph][b2][systems]") {
    Engine e(kRate);
    const std::uint32_t rom = e.putResource("rom", readFile(RP_MGB_ROM_PATH));
    REQUIRE(rom != 0);
    CHECK(e.setGraph(builtRequest(builtDoc(1, rp::host::test::kGbType, rom), {1})).ok);
    CHECK(e.findSystem(1) != nullptr);
    CHECK(e.findSystem(1)->kind() == SystemKind::SameBoy);
    CHECK(e.coreLive(1));
    CHECK(e.liveSystemCount() == 1);
    CHECK(e.registry().liveCount() == 1);
    CHECK(e.resourcePinCount(rom) == 1);  // the node's instance
    CHECK(e.ramSize(1) > 0);              // its snapshot slot is claimed and seeded by the build

    std::vector<float> lanes(2 * 256, 0.0f);
    float* outs[2] = {&lanes[0], &lanes[256]};
    e.processBlock(256, outs, 2);

    // The empty document takes it out: the plan it ran in is reclaimed, and with it the core, its slot and its pins.
    REQUIRE(e.setGraph(builtRequest(rp::host::test::stereoDocument({}), {})).ok);
    e.reclaim();
    CHECK_FALSE(e.coreLive(1));
    CHECK(e.liveSystemCount() == 0);
    CHECK(e.registry().liveCount() == 0);
    CHECK(e.resourcePinCount(rom) == 0);
}

TEST_CASE("a build that fails refuses the document and leaves the running plan, the pins and the slots as they were",
          "[engine-graph][b2][systems]") {
    Engine e(kRate);
    const std::uint32_t mgbRom = e.putResource("rom", readFile(RP_MGB_ROM_PATH));
    const std::uint32_t nesRom = e.putResource("rom", readFile(RP_BLIPTOASTER_ROM_PATH));
    REQUIRE(mgbRom != 0);
    REQUIRE(nesRom != 0);
    REQUIRE(e.setGraph(builtRequest(builtDoc(1, rp::host::test::kGbType, mgbRom), {1})).ok);
    const std::size_t cores = e.liveSystemCount(), slots = e.registry().liveCount();

    SECTION("a ROM the console's gate refuses") {
        const auto r = e.setGraph(builtRequest(builtDoc(2, rp::host::test::kGbType, nesRom), {2}));
        CHECK_FALSE(r.ok);
        CHECK(refusal(r) == "sys-2: activate-failed: the ROM is not a Game Boy ROM");
        CHECK_FALSE(e.coreLive(2));
        CHECK(e.resourcePinCount(nesRom) == 0);  // pinned at create, dropped as the refused plan unwinds
    }
    SECTION("an id a live core still holds") {
        // A NES under the Game Boy's id is a different node, so it is a fresh instance that builds: the id the older
        // instance still holds refuses the build rather than sharing the core. The refused core is a NES, a quarter of
        // the Game Boy's work RAM, so a slot answered by the wrong core changes the size the registry publishes.
        const std::uint32_t otherRom = e.putResource("rom", readFile(RP_BLIPTOASTER_ROM_PATH));
        const std::uint32_t ram = e.ramSize(1);
        REQUIRE(ram > 0);
        const auto r = e.setGraph(builtRequest(builtDoc(1, rp::host::test::kNesType, otherRom), {1}));
        CHECK_FALSE(r.ok);
        CHECK(refusal(r) == "sys-1: activate-failed: system 1 is live");
        CHECK(e.ramSize(1) == ram);  // the live core's slot, not one a released second claim freed for a NES
        CHECK(e.resourcePinCount(otherRom) == 0);
    }
    SECTION("two nodes naming one system") {
        // Both would have to be sys-2, so the node ids repeat; naming one system twice is what the document is refused
        // for, before the planner reaches the repeated id.
        const std::string settings = R"({"system":2,"rom":"res:)" + std::to_string(mgbRom) + R"("})";
        const std::string document =
            R"({"nodes":[{"id":"sys-2","type":"rp.sameboy.gb","settings":)" + settings + "}," +
            R"({"id":"sys-2","type":"rp.sameboy.gb","settings":)" + settings + R"(}],"wires":[]})";
        const auto r = e.setGraph(builtRequest(document, {2}));
        CHECK_FALSE(r.ok);
        CHECK(refusal(r) == ": slots: two system nodes name system 2");
    }
    SECTION("a node naming no system, and a system that is not an id") {
        const auto neither = e.setGraph(builtRequest(
            R"({"nodes":[{"id":"sys-2","type":"rp.sameboy.gb","settings":{}}],"wires":[]})", {2}));
        CHECK(refusal(neither) ==
              "sys-2: settings-invalid: a system node names the system it builds ({\"system\": <id>, \"rom\": \"res:N\"})");
        // A `system` field that is not a system id is the document's mistake, said by the slots check.
        const auto outOfRange = e.setGraph(builtRequest(
            R"({"nodes":[{"id":"sys-2","type":"rp.sameboy.gb","settings":{"system":99999999999,"rom":"res:)" +
                std::to_string(mgbRom) + R"("}}],"wires":[]})", {2}));
        CHECK(refusal(outOfRange) == "sys-2: slots: a system node's system is a system id, 1 to 4294967295");
    }

    // Every section: the running plan, its core, its slot and the pins are exactly what they were.
    CHECK(e.coreLive(1));
    CHECK(e.findSystem(1) != nullptr);
    CHECK(e.liveSystemCount() == cores);
    CHECK(e.registry().liveCount() == slots);
    CHECK(e.resourcePinCount(mgbRom) == 1);
}

TEST_CASE("a full snapshot pool refuses the build, and the core it built is dropped", "[engine-graph][b2][systems]") {
    Engine e(kRate);
    const std::uint32_t rom = e.putResource("rom", readFile(RP_MGB_ROM_PATH));
    std::vector<std::unique_ptr<EmptyCore>> stubs;
    for (std::uint32_t i = 0; i < SnapshotRegistry::kSlotCount; ++i) {
        stubs.push_back(std::make_unique<EmptyCore>(1000 + i));
        REQUIRE(e.registry().claim(1000 + i, *stubs.back()));
    }
    const auto r = e.setGraph(builtRequest(builtDoc(3, rp::host::test::kGbType, rom), {3}));
    CHECK(refusal(r) == "sys-3: activate-failed: the snapshot pool is full");
    CHECK_FALSE(e.coreLive(3));
    CHECK(e.liveSystemCount() == 0);
    CHECK(e.registry().liveCount() == SnapshotRegistry::kSlotCount);
    CHECK(e.resourcePinCount(rom) == 0);
    for (std::uint32_t i = 0; i < SnapshotRegistry::kSlotCount; ++i) e.registry().release(1000 + i);
    stubs.clear();
    CHECK(e.registry().liveCount() == 0);
    // The pool has room again: the same document builds.
    CHECK(e.setGraph(builtRequest(builtDoc(3, rp::host::test::kGbType, rom), {3})).ok);
    CHECK(e.coreLive(3));
}

TEST_CASE("a construct-time field is applied by building a new core, which an id an older one holds refuses",
          "[engine-graph][b2][systems]") {
    Engine e(kRate);
    const std::uint32_t rom = e.putResource("rom", readFile(RP_BLIPTOASTER_ROM_PATH));
    REQUIRE(e.setGraph(builtRequest(builtDoc(1, rp::host::test::kNesType, rom, R"("channelExportMode":"mix")"), {1})).ok);
    auto* first = dynamic_cast<MesenNesSystem*>(e.findSystem(1));
    REQUIRE(first != nullptr);
    CHECK(first->config().channelExportMode == 0);
    const std::size_t cores = e.liveSystemCount(), slots = e.registry().liveCount();

    // The same id: the older instance still holds it, so the build refuses rather than sharing the core.
    const auto sameId = e.setGraph(
        builtRequest(builtDoc(1, rp::host::test::kNesType, rom, R"("channelExportMode":"individualMono")"), {1}));
    CHECK(refusal(sameId) == "sys-1: activate-failed: system 1 is live");
    CHECK(e.findSystem(1) == first);  // the running plan still runs the same core
    CHECK(first->config().channelExportMode == 0);

    // A new id, as TS rebuilds: the new core is built with the new field.
    REQUIRE(e.setGraph(
                builtRequest(builtDoc(2, rp::host::test::kNesType, rom, R"("channelExportMode":"individualMono")"), {2}))
                .ok);
    auto* second = dynamic_cast<MesenNesSystem*>(e.findSystem(2));
    REQUIRE(second != nullptr);
    CHECK(second->config().channelExportMode == 3);
    // Quiescent, so publishing the new plan freed the old one with it: one core and one slot, as before the rebuild,
    // and the ROM pinned once (the new plan's node, and only it).
    CHECK(e.liveSystemCount() == cores);
    CHECK(e.registry().liveCount() == slots);
    CHECK(e.resourcePinCount(rom) == 1);
    CHECK_FALSE(e.coreLive(1));
}

TEST_CASE("a refused plan destroys the core its earlier node built, down to the last slot and pin",
          "[engine-graph][b2][systems]") {
    Engine e(kRate);
    const std::uint32_t mgbRom = e.putResource("rom", readFile(RP_MGB_ROM_PATH));
    const std::uint32_t nesRom = e.putResource("rom", readFile(RP_BLIPTOASTER_ROM_PATH));
    REQUIRE(mgbRom != 0);
    REQUIRE(nesRom != 0);
    const std::size_t cores = e.liveSystemCount(), slots = e.registry().liveCount();

    // sys-1 builds a real core first, claimed and in the plan's table; sys-2 then finds NES bytes under a Game Boy node,
    // so the plan is refused while sys-1's core is built. The unwind destroys it (the design's section 5 cleanup), which
    // is the only place destroySystem runs before a published plan owns the core.
    const std::string document =
        R"({"nodes":[{"id":"sys-1","type":"rp.sameboy.gb","settings":{"system":1,"rom":"res:)" +
        std::to_string(mgbRom) + R"("}},{"id":"sys-2","type":"rp.sameboy.gb","settings":{"system":2,"rom":"res:)" +
        std::to_string(nesRom) + R"("}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
            "wires":[{"from":"sys-1.mix","to":"out.out.1-2"}]})";
    const auto refused = e.setGraph(builtRequest(document, {1, 2}));
    CHECK(refusal(refused) == "sys-2: activate-failed: the ROM is not a Game Boy ROM");
    CHECK_FALSE(e.coreLive(1));
    CHECK(e.findSystem(1) == nullptr);
    CHECK(e.liveSystemCount() == cores);
    CHECK(e.registry().liveCount() == slots);
    CHECK(e.resourcePinCount(mgbRom) == 0);
    CHECK(e.resourcePinCount(nesRom) == 0);

    // The id and its slot went back: the same document without its bad node builds system 1 again. A slot left claimed
    // by the unwound core would refuse this with "the snapshot pool is full", and a live-core map entry it left would
    // refuse every later document naming system 1.
    REQUIRE(e.setGraph(builtRequest(builtDoc(1, rp::host::test::kGbType, mgbRom), {1})).ok);
    CHECK(e.coreLive(1));
}

TEST_CASE("a plan refused for a busy ring destroys the core its node built", "[engine-graph][b2][systems]") {
    Engine e(kRate);
    const std::uint32_t rom = e.putResource("rom", readFile(RP_MGB_ROM_PATH));
    REQUIRE(rom != 0);
    const std::size_t cores = e.liveSystemCount(), slots = e.registry().liveCount();
    // The test thread is the audio thread from here: a push only queues, so the ring fills and the plan's swap finds
    // no room after its node built and claimed its core.
    e.setAudioThreadOwns(true);
    DspCommand bpm;
    bpm.kind = DspCommand::Kind::SetBpm;
    bpm.setBpm = {120.0};
    while (e.pushCommand(bpm)) {
    }
    const auto refused = e.setGraph(builtRequest(builtDoc(1, rp::host::test::kGbType, rom), {1}));
    CHECK_FALSE(refused.ok);
    REQUIRE(refused.errors.size() == 1);
    CHECK(refused.errors[0].code == "busy");
    CHECK_FALSE(e.coreLive(1));
    CHECK(e.liveSystemCount() == cores);
    CHECK(e.registry().liveCount() == slots);
    CHECK(e.resourcePinCount(rom) == 0);

    // With the ring drained, the same document builds and keeps its core.
    e.drainCommands();
    REQUIRE(e.setGraph(builtRequest(builtDoc(1, rp::host::test::kGbType, rom), {1})).ok);
    e.drainCommands();
    CHECK(e.coreLive(1));
    e.setAudioThreadOwns(false);
}

TEST_CASE("a document changing only a param or a live knob keeps a system node's core",
          "[engine-graph][b2][systems]") {
    Engine e(kRate);
    const std::uint32_t rom = e.putResource("rom", readFile(RP_MGB_ROM_PATH));
    REQUIRE(rom != 0);
    const char* gb = rp::host::test::kGbType;
    REQUIRE(e.setGraph(builtRequest(builtDoc(1, gb, rom), {1})).ok);
    auto* core = e.findSystem(1);
    REQUIRE(core != nullptr);
    CHECK(e.liveSystemCount() == 1);

    // Only the params: the store republishes the document for every edit, and the node's rebuild_key is unchanged, so the
    // instance is reused and its core never rebuilds (a rebuild under the same id would refuse "system 1 is live").
    CHECK(e.setGraph(builtRequest(builtDoc(1, gb, rom, "", R"({"gain":-6})"), {1})).ok);
    CHECK(e.findSystem(1) == core);
    CHECK(e.liveSystemCount() == 1);

    // Only a live knob: applied to the same core at its next block.
    CHECK(e.setGraph(builtRequest(builtDoc(1, gb, rom, R"("highpass":"off")"), {1})).ok);
    CHECK(e.findSystem(1) == core);
    CHECK(e.liveSystemCount() == 1);

    // The identical document again: nothing changed, nothing built.
    CHECK(e.setGraph(builtRequest(builtDoc(1, gb, rom, R"("highpass":"off")"), {1})).ok);
    CHECK(e.findSystem(1) == core);
    CHECK(e.liveSystemCount() == 1);
    CHECK(e.resourcePinCount(rom) == 1);

    // A second system building from the same ROM resource: its own node, its own core, the ROM pinned by both.
    const std::string two =
        R"({"nodes":[{"id":"sys-1","type":"rp.sameboy.gb","settings":{"system":1,"rom":"res:)" +
        std::to_string(rom) + R"("}},{"id":"sys-2","type":"rp.sameboy.gb","settings":{"system":2,"rom":"res:)" +
        std::to_string(rom) + R"("}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2,2]}}],
            "wires":[{"from":"sys-1.mix","to":"out.out.1-2"},{"from":"sys-2.mix","to":"out.out.3-4"}]})";
    const auto twoSystems = e.setGraph(builtRequest(two, {1, 2}));
    CHECK(twoSystems.ok);
    CHECK(e.findSystem(1) == core);
    CHECK(e.findSystem(2) != nullptr);
    CHECK(e.liveSystemCount() == 2);
    CHECK(e.registry().liveCount() == 2);
    CHECK(e.resourcePinCount(rom) == 2);
}

TEST_CASE("a reused system node's core is in the new plan's table without its node building again",
          "[engine-graph][b2][systems]") {
    // A node whose rebuild_key is unchanged is reused across a republish: it returns from activate without calling
    // buildSystem, so its core reaches the new plan's table only from the engine's live cores (the pre-fill
    // publishGraph makes from them). The slot is read straight off the table, and a block run: a null entry would be
    // dereferenced by that block.
    Engine e(kRate);
    const std::uint32_t rom = e.putResource("rom", readFile(RP_MGB_ROM_PATH));
    REQUIRE(rom != 0);
    const char* gb = rp::host::test::kGbType;
    REQUIRE(e.setGraph(builtRequest(builtDoc(1, gb, rom), {1})).ok);
    SystemBase* core = e.findSystem(1);
    REQUIRE(core != nullptr);
    REQUIRE(e.setGraph(builtRequest(builtDoc(1, gb, rom), {1})).ok);
    CHECK(e.findSystem(1) == core);  // before a block, the reused node has built nothing, so this is the pre-fill
    std::vector<float> left(512), right(512);
    float* outs[2] = {left.data(), right.data()};
    e.processBlock(512, outs, 2);
    CHECK(e.findSystem(1) == core);
    CHECK(e.coreLive(1));
}

TEST_CASE("a build outside the setGraph that activates it is refused rather than joining the live cores",
          "[engine-graph][b2][systems]") {
    Engine e(kRate);
    const std::uint32_t rom = e.putResource("rom", readFile(RP_MGB_ROM_PATH));
    REQUIRE(rom != 0);
    SystemBuildRequest request;
    request.rom = readFile(RP_MGB_ROM_PATH);
    request.config = SameBoyConfig{};
    std::string why;
    CHECK(e.buildSystem(1, request, &why) == nullptr);
    CHECK(why.find("setGraph") != std::string::npos);
    CHECK_FALSE(e.coreLive(1));
    CHECK(e.liveSystemCount() == 0);
    CHECK(e.registry().liveCount() == 0);
    CHECK(e.resourcePinCount(rom) == 0);
}

TEST_CASE("a system built from resources joins and leaves while an audio thread runs its blocks",
          "[engine-graph][b2][systems][sanitizer]") {
    Engine e(kRate);
    const std::uint32_t rom = e.putResource("rom", readFile(RP_MGB_ROM_PATH));
    REQUIRE(rom != 0);
    e.setAudioThreadOwns(true);
    std::atomic<bool> running{true};
    std::atomic<int>  blocks{0};
    std::thread       audio([&] {
        std::vector<std::vector<float>> lanes(2, std::vector<float>(512, 0.0f));
        float* outs[2] = {lanes[0].data(), lanes[1].data()};
        while (running.load(std::memory_order_acquire)) {
            e.drainCommands();
            e.processBlock(512, outs, 2);
            blocks.fetch_add(1, std::memory_order_release);
        }
    });
    auto waitBlocks = [&](int n) {
        const int seen = blocks.load(std::memory_order_acquire);
        while (blocks.load(std::memory_order_acquire) < seen + n) std::this_thread::yield();
    };
    for (SystemId id = 1; id <= 3; ++id) {
        REQUIRE(e.setGraph(builtRequest(builtDoc(id, rp::host::test::kGbType, rom), {id})).ok);
        // The plan the audio thread adopts is the block thread's view, so the control thread watches the live cores
        // instead: the build put this one there before the plan was published.
        CHECK(e.coreLive(id));
        waitBlocks(2);
        REQUIRE(e.setGraph(builtRequest(rp::host::test::stereoDocument({}), {})).ok);
        // The audio thread hands the old plan back on its next block; the core leaves with it.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (e.coreLive(id) && std::chrono::steady_clock::now() < deadline) {
            e.reclaim();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK_FALSE(e.coreLive(id));
        waitBlocks(2);
    }
    running.store(false, std::memory_order_release);
    audio.join();
    e.setAudioThreadOwns(false);
    e.reclaim();
    CHECK(e.liveSystemCount() == 0);
    CHECK(e.registry().liveCount() == 0);
    CHECK(e.resourcePinCount(rom) == 0);
}
