// The engine's side of graphs TS builds (docs/modular-graph-phase-b2-design.md section 2.5): a built core parks
// under its id until a document's system node takes it, a document's system nodes and its slots must name the
// same systems, the tap re-arm rides the swap, a refused document leaves its cores parked, and a core no
// published document placed is freed; and a build spec reaches the core under the field names TS sends. Driven
// on a quiescent Engine, so every plan applies as it is published, except where a test owns the drain itself.
//
// Run via `pnpm test:plugin engine-graph`.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <quickjs.h>

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

std::unique_ptr<SystemBase> mgb(SystemId id) {
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

using rp::host::test::DocSystem;

Engine::GraphRequest request(const std::vector<DocSystem>& systems) {
    std::vector<SystemId> slots;
    for (const DocSystem& s : systems) slots.push_back(s.id);
    return rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), slots);
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

TEST_CASE("a parked core is placed by the document that names it", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(e.parkCore(mgb(1)));
    REQUIRE(e.parkCore(mgb(2)));
    // Four host channels, so the width read back is this document's, not the stereo pair of the first plan.
    const auto r = rp::host::test::graphRequest(rp::host::test::stereoDocument({{2, kGb}, {1, kGb}}, 4), {2, 1});
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

TEST_CASE("a core under an id still live is refused", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(e.parkCore(mgb(1)));
    const std::size_t ram = e.ramSize(1);
    REQUIRE(ram > 0);
    // The live core keeps its snapshot slot: a refused park never touches the registry. A refused NES, its
    // work RAM a quarter of the Game Boy's, would show if its slot answered in place of the live core's.
    CHECK_FALSE(e.parkCore(bliptoaster(1)));  // parked
    CHECK(e.ramSize(1) == ram);
    CHECK_FALSE(e.parkCore(mgb(1)));
    CHECK(e.ramSize(1) == ram);
    REQUIRE(e.setGraph(request({{1, kGb}})).ok);
    CHECK_FALSE(e.parkCore(bliptoaster(1)));  // held by a node
    CHECK(e.ramSize(1) == ram);
    // Once a document drops it and the plan that ran it is reclaimed, the id is free again.
    REQUIRE(e.setGraph(request({})).ok);
    e.reclaim();
    CHECK_FALSE(e.coreLive(1));
    CHECK(e.parkCore(mgb(1)));
}

TEST_CASE("the slots and the system nodes must name the same systems", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(e.parkCore(mgb(1)));
    REQUIRE(e.parkCore(mgb(2)));

    auto missing = request({{1, kGb}, {2, kGb}});
    missing.slots = {1};
    auto result = e.setGraph(missing);
    CHECK_FALSE(result.ok);
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].code == "slots");

    auto twice = request({{1, kGb}});
    twice.slots = {1, 1};
    CHECK(e.setGraph(twice).errors.at(0).code == "slots");

    auto renamed = request({{1, kGb}});
    const auto at = renamed.document.find(R"("sys-1","type")");
    REQUIRE(at != std::string::npos);
    renamed.document.replace(at, 7, R"("gb-1")");
    CHECK(e.setGraph(renamed).errors.at(0).code == "slots");

    auto unbuilt = request({{9, kGb}});
    result = e.setGraph(unbuilt);
    CHECK(result.errors.at(0).code == "unknown-core");
    CHECK(result.errors.at(0).nodeId == "sys-9");

    // Nothing was published, so the cores are still parked and nothing runs.
    CHECK(e.systemCount() == 0);
    CHECK(e.coreLive(1));
}

TEST_CASE("a published document frees every parked core it does not place", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(e.parkCore(mgb(1)));
    REQUIRE(e.parkCore(mgb(2)));
    REQUIRE(e.setGraph(request({{1, kGb}})).ok);
    CHECK(e.coreLive(1));
    CHECK_FALSE(e.coreLive(2));
    // Its snapshot slot went with it, so the id builds again.
    CHECK(e.ramSize(2) == 0);
    CHECK(e.parkCore(mgb(2)));
}

TEST_CASE("a refused document leaves the running plan and the parked cores alone", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(e.parkCore(mgb(1)));
    REQUIRE(e.setGraph(request({{1, kGb}})).ok);
    REQUIRE(e.parkCore(mgb(2)));
    auto bad = request({{1, kGb}, {2, kGb}});
    bad.document.replace(bad.document.find("rp.core.add"), 11, "rp.core.nope");
    CHECK_FALSE(e.setGraph(bad).ok);
    CHECK(e.systemCount() == 1);
    CHECK(e.coreLive(2));
}

TEST_CASE("the tap re-arm rides the swap: restore, then arm", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(e.parkCore(bliptoaster(1)));
    auto* nes = dynamic_cast<MesenNesSystem*>(e.findSystem(1));
    CHECK(nes == nullptr);  // parked, not yet placed

    REQUIRE(e.setGraph(arming(request({{1, kNes}}), 1, 1)).ok);
    nes = dynamic_cast<MesenNesSystem*>(e.findSystem(1));
    REQUIRE(nes != nullptr);
    CHECK(nes->channelExportMode() == 1);

    // No re-arm: the tap is left as it is.
    REQUIRE(e.setGraph(request({{1, kNes}})).ok);
    CHECK(nes->channelExportMode() == 1);

    // A re-arm with nothing to arm restores the mode the core had.
    auto restore = request({{1, kNes}});
    restore.rearmTap = true;
    REQUIRE(e.setGraph(restore).ok);
    CHECK(nes->channelExportMode() == 0);

    // Channels, then a re-arm to the same mode, which restores and arms again.
    const auto armChannels = arming(request({{1, kNes}}), 1, 3);
    REQUIRE(e.setGraph(armChannels).ok);
    CHECK(nes->channelExportMode() == 3);
    REQUIRE(e.setGraph(armChannels).ok);
    CHECK(nes->channelExportMode() == 3);
    REQUIRE(e.setGraph(restore).ok);
    CHECK(nes->channelExportMode() == 0);
}

TEST_CASE("a sample-rate change re-rates the running plan's cores", "[engine-graph][b2]") {
    Engine e(kRate);
    auto core = mgb(1);
    auto* raw = static_cast<SameBoySystem*>(core.get());
    REQUIRE(e.parkCore(std::move(core)));
    REQUIRE(e.setGraph(request({{1, kGb}})).ok);
    REQUIRE(raw->sampleRate_ == kRate);
    e.setSampleRate(44100.0);
    CHECK(raw->sampleRate_ == 44100.0);
}

TEST_CASE("a parked core follows a sample-rate change", "[engine-graph][b2]") {
    Engine e(kRate);
    auto core = mgb(1);
    auto* raw = static_cast<SameBoySystem*>(core.get());
    REQUIRE(e.parkCore(std::move(core)));
    e.setSampleRate(44100.0);
    CHECK(raw->sampleRate_ == 44100.0);
}

TEST_CASE("a document refused after its nodes took their cores leaves them parked", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(e.parkCore(mgb(1)));
    // The test thread is the audio thread from here: a push only queues, so the ring fills, and the plan's
    // swap finds no room after its node took core 1.
    e.setAudioThreadOwns(true);
    DspCommand bpm;
    bpm.kind = DspCommand::Kind::SetBpm;
    bpm.setBpm = {120.0};
    while (e.pushCommand(bpm)) {
    }
    const auto refused = e.setGraph(request({{1, kGb}}));
    CHECK_FALSE(refused.ok);
    REQUIRE(refused.errors.size() == 1);
    CHECK(refused.errors[0].code == "busy");
    CHECK(e.coreLive(1));
    CHECK(e.ramSize(1) > 0);

    // With the ring drained, the same document places it.
    e.drainCommands();
    REQUIRE(e.setGraph(request({{1, kGb}})).ok);
    e.drainCommands();
    CHECK(e.findSystem(1) != nullptr);
    e.setAudioThreadOwns(false);
}

TEST_CASE("a system node of another type than its core is refused before anything is built", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(e.parkCore(mgb(1)));
    const auto result = e.setGraph(request({{1, kNes}}));
    CHECK_FALSE(result.ok);
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].code == "core-type");
    CHECK(result.errors[0].nodeId == "sys-1");
    CHECK(e.coreLive(1));
    REQUIRE(e.setGraph(request({{1, kGb}})).ok);
    CHECK(e.findSystem(1) != nullptr);
}

TEST_CASE("bad tap requests are refused", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(e.parkCore(bliptoaster(1)));
    // 257 is mode 1 once narrowed to the command's byte.
    for (const std::uint32_t mode : {0u, 4u, 7u, 257u}) {
        const auto result = e.setGraph(arming(request({{1, kNes}}), 1, mode));
        CHECK_FALSE(result.ok);
        REQUIRE(result.errors.size() == 1);
        CHECK(result.errors[0].code == "tap");
    }
    CHECK(e.setGraph(arming(request({{1, kNes}}), 2, 1)).errors.at(0).code == "tap");  // not in the slots
    CHECK(e.systemCount() == 0);
    CHECK(e.coreLive(1));
}

TEST_CASE("an id above INT32_MAX is read whole, one above UINT32_MAX is refused", "[engine-graph][b2]") {
    Engine e(kRate);
    constexpr SystemId kHigh = 3000000000u;
    REQUIRE(e.parkCore(mgb(kHigh)));
    REQUIRE(e.setGraph(request({{kHigh, kGb}})).ok);
    CHECK(e.findSystem(kHigh) != nullptr);

    auto over = request({});
    over.document = R"({"nodes":[{"id":"sys-4294967296","type":"rp.sameboy.gb","settings":{"core":4294967296}}],"wires":[]})";
    CHECK(e.setGraph(over).errors.at(0).code == "slots");
}

TEST_CASE("an audio-out with no buses of its own is its default eight channels wide", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(e.parkCore(mgb(1)));
    auto r = request({{1, kGb}});
    r.document = R"({"nodes":[{"id":"sys-1","type":"rp.sameboy.gb","settings":{"core":1}},)"
                 R"({"id":"out","type":"rp.host.audio-out"}],"wires":[{"from":"sys-1.mix","to":"out.out.1-2"}]})";
    REQUIRE(e.setGraph(r).ok);
    CHECK(e.hostWidth() == 8);
}

TEST_CASE("buildCore and setGraph over the RPC layer", "[engine-graph][b2]") {
    Engine           e(kRate);
    SystemFactory    factory;
    registerCoreBackends(factory);
    QueuedInvoker    invoker(e);
    EngineRpcService rpc(e, factory, invoker);

    BackendConstructSpec spec{};
    spec.id = 5;
    spec.romPath = "";
    spec.embeddedRom = "mgb";
    spec.platform = "gb";
    spec.core = "sameboy";
    spec.settings = R"({"linkGroupId":3})";
    const auto built = rpc.buildCore(spec);
    REQUIRE(built.has_value());
    CHECK(built->type == "rp.sameboy.gb");
    CHECK(built->exportMode == 0);
    CHECK(built->linkGroup == 3);
    CHECK(e.coreLive(5));
    CHECK(e.ramSize(5) > 0);
    CHECK_FALSE(rpc.buildCore(spec).has_value());  // still live
    CHECK(e.ramSize(5) > 0);

    // 0 is the snapshot registry's free-slot id.
    spec.id = 0;
    CHECK_FALSE(rpc.buildCore(spec).has_value());
    CHECK_FALSE(e.coreLive(0));

    RpcSetGraphRequest graph{rp::host::test::stereoDocument({{5, kGb}}), {5}, std::nullopt};
    const auto placed = rpc.setGraph(graph);
    CHECK(placed.ok);
    CHECK(placed.errors.empty());
    CHECK(e.findSystem(5) != nullptr);
}

TEST_CASE("a build spec's serialOutCapture crosses the wire under the name TS sends", "[engine-graph][b2]") {
    // A loaded LSDj in a MIDI-out mode captures serial out only through this field, and reflect-cpp skips a
    // field it does not know, so a name that drifted between realBackend.ts's specParams and
    // BackendConstructSpec would drop it with nothing failing. Each mode arrives as itself, and a name that is no
    // mode refuses the build rather than building a core that captures nothing. The spec is decoded as every host decodes it: a
    // live JS object, through rpcpp's QuickJS codec, into the registered buildCore.
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
    registerCoreBackends(factory);
    QueuedInvoker    invoker(e);
    EngineRpcService rpc(e, factory, invoker);
    rpcpp::QuickJSTransport                                   transport(js.ctx, [](JSContext*, JSValue) {});
    rpcpp::TypedRpcServer<rpcpp::Empty, rpcpp::QuickJSCodec> server(transport, rpcpp::QuickJSCodec{js.ctx});
    registerEmulatorRpc(server, rpc);

    // The call specParams makes for an embedded mGB, `extra` appended to the spec. True when it built.
    const auto buildCore = [&](SystemId id, const std::string& extra) {
        const std::string src = "({jsonrpc: '2.0', id: 1, method: 'buildCore', params: [{id: " + std::to_string(id) +
                                ", romPath: '', platform: 'gb', core: 'sameboy', embeddedRom: 'mgb'" + extra + "}]})";
        JSValue message = JS_Eval(js.ctx, src.c_str(), src.size(), "<buildCore>", JS_EVAL_TYPE_GLOBAL);
        std::optional<rpcpp::QuickJSOut> reply;
        if (!JS_IsException(message)) reply = server.processMessage(message);
        JS_FreeValue(js.ctx, message);
        if (!reply) return false;
        JSValue answer = reply->materialize(js.ctx);
        JSValue result = JS_GetPropertyStr(js.ctx, answer, "result");
        const bool built = JS_IsObject(result);
        JS_FreeValue(js.ctx, result);
        JS_FreeValue(js.ctx, answer);
        return built;
    };
    REQUIRE(buildCore(1, ", serialOutCapture: 'drive'"));
    REQUIRE(buildCore(2, ", serialOutCapture: 'listen'"));
    REQUIRE(buildCore(3, ""));
    CHECK_FALSE(buildCore(4, ", serialOutCapture: 'loud'"));
    REQUIRE(e.setGraph(request({{1, kGb}, {2, kGb}, {3, kGb}})).ok);
    const auto mode = [&](SystemId id) {
        const auto* sb = dynamic_cast<const SameBoySystem*>(e.findSystem(id));
        REQUIRE(sb != nullptr);
        return sb->serialOutCapture();
    };
    CHECK(mode(1) == SerialOutCapture::Drive);
    CHECK(mode(2) == SerialOutCapture::Listen);
    CHECK(mode(3) == SerialOutCapture::Off);
    CHECK_FALSE(e.coreLive(4));

    // The live call takes the same names, and null for off; a name that is no mode is refused and changes nothing.
    const auto setCapture = [&](SystemId id, const std::string& name) {
        const std::string src = "({jsonrpc: '2.0', id: 2, method: 'setSerialOutCapture', params: [" + std::to_string(id) + ", " +
                                name + "]})";
        JSValue message = JS_Eval(js.ctx, src.c_str(), src.size(), "<setSerialOutCapture>", JS_EVAL_TYPE_GLOBAL);
        std::optional<rpcpp::QuickJSOut> reply;
        if (!JS_IsException(message)) reply = server.processMessage(message);
        JS_FreeValue(js.ctx, message);
        if (!reply) return false;
        JSValue answer = reply->materialize(js.ctx);
        JSValue result = JS_GetPropertyStr(js.ctx, answer, "result");
        const bool accepted = JS_IsBool(result) && JS_ToBool(js.ctx, result);
        JS_FreeValue(js.ctx, result);
        JS_FreeValue(js.ctx, answer);
        invoker.drainInto(e);
        return accepted;
    };
    CHECK(setCapture(3, "'listen'"));
    CHECK(mode(3) == SerialOutCapture::Listen);
    CHECK_FALSE(setCapture(3, "'loud'"));
    CHECK(mode(3) == SerialOutCapture::Listen);
    CHECK(setCapture(1, "null"));
    CHECK(mode(1) == SerialOutCapture::Off);
}

TEST_CASE("a render into buffers the planned graph does not write is refused", "[engine-graph][b2]") {
    // TS sets the shape a render call needs before it makes the call; the render calls only check it.
    Engine           e(kRate);
    SystemFactory    factory;
    QueuedInvoker    inv(e);
    EngineRpcService svc(e, factory, inv);
    REQUIRE(e.parkCore(mgb(1)));
    REQUIRE(e.parkCore(mgb(2)));
    REQUIRE(e.setGraph(request({{1, kGb}, {2, kGb}})).ok);
    CHECK(e.hostWidth() == 2);
    CHECK_FALSE(svc.renderAudio(10.0).empty());
    CHECK(svc.renderAudioPerSystem(10.0).empty());  // 4 lanes wanted, 2 planned
    RpcRenderBlocksRequest blocks{4, {RpcRenderBlock{256, {}, {}, std::nullopt, std::nullopt, std::nullopt}}};
    CHECK_FALSE(svc.renderBlocks(blocks).error.empty());

    REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::perSystemDocument({{1, kGb}, {2, kGb}}), {1, 2})).ok);
    CHECK(e.hostWidth() == 4);
    CHECK(svc.renderAudioPerSystem(10.0).size() == 2);
    CHECK(svc.renderAudio(10.0).empty());
    CHECK(svc.renderBlocks(blocks).error.empty());
}
