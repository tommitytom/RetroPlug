// Every document buildGraph's goldens hold must PLAN on the engine. Two files hold them. golden/build-graph.json is the
// matrix of system sets, routings and host shapes the TS builder is held to (packages/retroplug/test/graph/
// build-graph-goldens.test.ts), with no roles; golden/build-graph-roles.json is buildGraph's own record of documents
// with protocol nodes, controllers and every MIDI routing mode (build-graph-role-goldens.test.ts). Those tests only
// compare documents. Here each distinct request is handed to a fresh Engine with the real packages (the script nodes
// among them, from the script bundle every Engine embeds), so a port name, a settings key or a node limit the planner
// refuses fails here for a shape no other suite runs: chained adds past rp.core.add's 16 inputs, a NES's ten channels,
// the Game Boy stems, link cables under every routing, the routing's outputs for thirty-three systems, and each
// protocol node type wired into the system node it feeds, MI.OUT fed back to an LSDj, the N8 cart mirror and a
// controller each way.
//
// The cores are real: a system node takes its core by its concrete class, so a stand-in reporting only its
// kind is refused when the node activates. Each is built as the case's model says (a Game Boy's link group, a
// NES's export mode), and the case's tap arm rides the request, so each NES must end in the mode it armed, or
// the one it was built in. Each plan then runs, and its blocks must fault no node, throw in no script node and drop no
// message: a document can plan and still wedge a linked group or a node's process, and the script host catches a
// script node's throw, counts it and runs the node again next block, rather than faulting. A build-graph.json plan runs
// one quiet block. A build-graph-roles.json plan runs twenty, with the transport playing, host MIDI on four channels
// and a control surface attached and pressed, so its protocol nodes send into the system inputs they feed and its
// controller launches into its cart or out of the host. What reaches a system input the engine does not hand back, so
// only the host's side has a floor: a controller sends its surface something, and one driving a real Game Boy sends
// the host its launch under the system it resolved. The thirty-three systems under perSystem give
// rp.host.audio-out 33 buses, past the 32 it takes: those are refused instead.
//
// Run via `pnpm test:plugin engine-graph`.

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <rfl/json.hpp>

#include "GraphDocs.hpp"
#include "NodeRig.hpp"
#include "graph/CanonicalJson.hpp"
#include "host/engine/Engine.hpp"
#include "system/mesen/MesenGbaConfig.hpp"
#include "system/mesen/MesenGbaSystem.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/mesen/MesenSmsConfig.hpp"
#include "system/mesen/MesenSmsSystem.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

using rp::host::test::readFile;

namespace {

constexpr double        kRate = 48000.0;
constexpr std::uint32_t kBlockFrames = 256;
// A role golden's blocks: at 120 bpm, about five clock ticks at 24 PPQN.
constexpr int kDrivenBlocks = 20;

// A golden case, as build-graph-goldens.test.ts reads it; the model's routing is what built the document, and
// nothing here needs it.
struct GoldenSystem {
    std::uint32_t exportMode = 0;
    SystemId      id = 0;
    std::uint32_t linkGroup = 0;
    std::string   type;
};
struct GoldenShape {
    std::string kind;
};
// A role golden's controller: where its launches go, and the system it was set for (0 is the first).
struct GoldenController {
    std::string target;
    SystemId    systemId = 0;
};
struct GoldenModel {
    std::optional<GoldenController> controller;
    GoldenShape                     shape;
    std::vector<GoldenSystem>       systems;
};
struct GoldenTap {
    SystemId      id = 0;
    std::uint32_t mode = 0;
};
struct GoldenCase {
    std::string              name;
    GoldenModel              model;
    std::size_t              document = 0;
    std::vector<SystemId>    slots;
    std::optional<GoldenTap> tapArm;
};

struct Goldens {
    std::vector<std::string> documents;
    std::vector<GoldenCase>  cases;
};

// Through parseJson, then each document re-spelled as the text setGraph is sent; the cases read typed, so
// their integers are range-checked.
Goldens readGoldens(const char* path) {
    INFO(path);
    const auto bytes = readFile(path);
    REQUIRE_FALSE(bytes.empty());
    const auto root = rp::graph::parseJson(std::string(bytes.begin(), bytes.end()));
    REQUIRE(root);
    const auto object = root->to_object();
    REQUIRE(object);
    const auto documents = object->get("documents").and_then([](const rfl::Generic& g) { return g.to_array(); });
    const auto cases = object->get("cases");
    REQUIRE(documents);
    REQUIRE(cases);
    Goldens out;
    for (const auto& d : *documents) out.documents.push_back(rp::graph::canonicalJson(d));
    auto parsed = rfl::json::read<std::vector<GoldenCase>>(rp::graph::canonicalJson(*cases));
    REQUIRE(parsed);
    out.cases = std::move(*parsed);
    return out;
}

// The core buildCore would build for `s`, activated at the engine's rate as the factory activates one.
std::unique_ptr<SystemBase> build(const GoldenSystem& s) {
    std::unique_ptr<SystemBase> core;
    if (s.type == rp::host::test::kGbType) {
        SameBoyConfig cfg;
        cfg.linkGroupId = static_cast<std::uint8_t>(s.linkGroup);
        core = std::make_unique<SameBoySystem>(s.id, cfg, readFile(RP_MGB_ROM_PATH));
    } else if (s.type == rp::host::test::kNesType) {
        MesenNesConfig cfg;
        cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
        cfg.channelExportMode = s.exportMode;
        core = std::make_unique<MesenNesSystem>(s.id, cfg, readFile(RP_BLIPTOASTER_ROM_PATH));
    } else if (s.type == "rp.mesen.gba") {
        // The smallest ROM GbaConsole::LoadRom takes (GbaAudio.test.cpp); with no BIOS it boots HLE.
        core = std::make_unique<MesenGbaSystem>(s.id, MesenGbaConfig{}, std::vector<std::uint8_t>(1024, 0));
    } else if (s.type == "rp.mesen.sms") {
        MesenSmsConfig cfg;
        cfg.romPath = RP_SMS_ROM_PATH;
        core = std::make_unique<MesenSmsSystem>(s.id, cfg, readFile(RP_SMS_ROM_PATH));
    }
    if (core) core->onActivate(kRate);
    return core;
}

// rp.host.audio-out takes 32 buses, and perSystem gives each system one.
bool expectRefused(const GoldenModel& model) { return model.shape.kind == "perSystem" && model.systems.size() > 32; }

// What the engine is handed, and how its cores were built: two cases that agree on it are one run.
std::string requestKey(const GoldenCase& c) {
    return std::to_string(c.document) + rfl::json::write(c.slots) + rfl::json::write(c.tapArm) +
           rfl::json::write(c.model.systems);
}

// A press of the top-left pad, as a Launchpad in Programmer mode sends it (padIndex in src/launchpad/profile.ts).
const std::vector<std::uint8_t> kPadPress{0x90, 81, 100};

// Plan `c` on a fresh Engine and run it, quiet for one block or `driven` for kDrivenBlocks; what went wrong, or
// nothing.
std::optional<std::string> plan(const GoldenCase& c, const std::string& document, bool driven) {
    Engine e(kRate);
    std::map<SystemId, const GoldenSystem*> byId;
    for (const GoldenSystem& s : c.model.systems) byId[s.id] = &s;
    for (const SystemId id : c.slots) {
        const auto it = byId.find(id);
        if (it == byId.end()) return "slot " + std::to_string(id) + " is no system of the model";
        auto core = build(*it->second);
        if (!core) return "no core is built as " + it->second->type;
        if (!e.parkCore(std::move(core))) return "the engine would not park core " + std::to_string(id);
    }
    auto request = rp::host::test::graphRequest(document, c.slots);
    if (c.tapArm) request = rp::host::test::rearming(std::move(request), c.tapArm->id, c.tapArm->mode);
    const Engine::GraphResult result = e.setGraph(request);

    auto firstError = [&] {
        if (result.errors.empty()) return std::string("no error given");
        const auto& err = result.errors[0];
        return err.code + " at " + err.nodeId + (err.port.empty() ? "" : "." + err.port) + ": " + err.message;
    };
    if (expectRefused(c.model)) {
        if (result.ok) return std::string("planned, though its audio-out has more buses than the 32 it takes");
        if (result.errors.empty() || result.errors[0].nodeId != "out" || result.errors[0].code != "settings-invalid")
            return "refused for another reason: " + firstError();
        if (e.systemCount() != 0) return std::string("refused, yet a plan with its systems runs");
        return std::nullopt;
    }
    if (!result.ok) return "refused: " + firstError();
    if (e.systemCount() != c.slots.size()) return std::string("planned, but not every slot is placed");
    // Each NES is in the mode the arm asked for, or the one it was built in.
    for (const SystemId id : c.slots) {
        const auto* nes = dynamic_cast<const MesenNesSystem*>(e.findSystem(id));
        if (!nes) continue;
        const std::uint32_t want = c.tapArm && c.tapArm->id == id ? c.tapArm->mode : byId.at(id)->exportMode;
        if (nes->channelExportMode() != want)
            return "NES " + std::to_string(id) + " is in mode " + std::to_string(nes->channelExportMode()) +
                   ", not " + std::to_string(want);
    }
    // Into as many channels as the plan writes. Driven, each block stages a note and its release on each of four
    // channels (under oneChannelPerInstance, channel k reaches the (k + 1)th system in slot order), and the surface's
    // press lands in the second.
    std::vector<std::vector<float>> lanes(e.hostWidth(), std::vector<float>(kBlockFrames, 0.0f));
    std::vector<float*>             outs;
    for (auto& lane : lanes) outs.push_back(lane.data());
    if (driven) {
        e.setBpm(120.0);
        e.setTransport(true);
        e.setControllerConnected(true);
    }
    // A floor under the traffic, from what the engine hands its host: a controller takes its surface, so sends it
    // something, and one driving a real Game Boy launches the press out of the host under the system it resolved ("0 is
    // the first system"; with none, 4294967295).
    const std::optional<GoldenController>& controller = c.model.controller;
    const SystemId controllerSystem = !controller            ? 0
                                      : controller->systemId ? controller->systemId
                                      : c.slots.empty()      ? 0xffffffffu
                                                             : c.slots.front();
    std::size_t surfaceOut = 0;
    bool        launchedOut = false;
    for (int b = 0; b < (driven ? kDrivenBlocks : 1); ++b) {
        if (driven) {
            for (std::uint8_t ch = 0; ch < 4; ++ch) {
                const auto key = static_cast<std::uint8_t>(48 + 12 * ch + b % 12);
                e.stageMidi(16u * ch, std::vector<std::uint8_t>{static_cast<std::uint8_t>(0x90 | ch), key, 100});
                e.stageMidi(128u + 16u * ch, std::vector<std::uint8_t>{static_cast<std::uint8_t>(0x80 | ch), key, 0});
            }
            if (b == 1) e.stageControllerMidi(kPadPress);
        }
        e.processBlock(kBlockFrames, outs.data(), outs.size());
        surfaceOut += e.controllerOut().size();
        for (const EngineMidiOut& m : e.midiOut()) launchedOut = launchedOut || m.system == controllerSystem;
        const std::string when = driven ? " by block " + std::to_string(b + 1) : " in its first block";
        if (e.faultCount() != 0) return std::to_string(e.faultCount()) + " node(s) faulted" + when;
        // The script host catches a script node's throw, counts it and runs the node again next block (design section
        // 4.5), so a fault cannot show one.
        if (e.scriptThrows() != 0) return std::to_string(e.scriptThrows()) + " script node throw(s)" + when;
        if (e.droppedMessages() != 0) return std::to_string(e.droppedMessages()) + " message(s) dropped" + when;
    }
    if (driven && controller) {
        if (surfaceOut == 0) return std::string("its controller sent its surface nothing");
        if (controller->target == "midiOut" && !launchedOut)
            return "its controller sent no host MIDI out under system " + std::to_string(controllerSystem);
    }
    return std::nullopt;
}

// Every case of the file at `path`, each distinct request once; the case names that went wrong, with why, and how many
// were refused as they must be.
struct FileResult {
    std::vector<std::string> wrong;
    std::size_t              refused = 0;
};
FileResult planAll(const char* path, bool driven) {
    const Goldens goldens = readGoldens(path);
    REQUIRE_FALSE(goldens.cases.empty());
    std::set<std::string> seen;
    std::set<std::size_t> documents;
    FileResult            out;
    for (const GoldenCase& c : goldens.cases) {
        if (!seen.insert(requestKey(c)).second) continue;
        REQUIRE(c.document < goldens.documents.size());
        documents.insert(c.document);
        if (expectRefused(c.model)) ++out.refused;
        if (const auto why = plan(c, goldens.documents[c.document], driven)) out.wrong.push_back(c.name + ": " + *why);
    }
    // Every document was handed over.
    CHECK(documents.size() == goldens.documents.size());
    return out;
}

}  // namespace

TEST_CASE("every document buildGraph's goldens hold plans on the engine", "[engine-graph][goldens]") {
    const FileResult goldens = planAll(RP_BUILD_GRAPH_GOLDENS_PATH, false);
    for (const std::string& w : goldens.wrong) UNSCOPED_INFO(w);
    CHECK(goldens.wrong.empty());
    // The refusal is one the goldens still hold.
    CHECK(goldens.refused > 0);
}

TEST_CASE("every document buildGraph's role goldens hold plans on the engine and runs driven",
          "[engine-graph][goldens]") {
    const FileResult roles = planAll(RP_BUILD_GRAPH_ROLE_GOLDENS_PATH, true);
    for (const std::string& w : roles.wrong) UNSCOPED_INFO(w);
    CHECK(roles.wrong.empty());
    CHECK(roles.refused == 0);
}
