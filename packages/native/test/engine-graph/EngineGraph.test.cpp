// The engine's side of graphs TS builds (docs/modular-graph-phase-b2-design.md section 2.5): a built core parks
// under its id until a document's system node takes it, a document's system nodes and its slots must name the
// same systems, the system table follows the slots, the tap re-arm rides the swap, and a core no published
// document placed is freed. Driven on a quiescent Engine, so every plan applies as it is published.
//
// Run via `pnpm test:plugin engine-graph`.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "NodeRig.hpp"
#include "host/engine/Engine.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

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

// Build a core, claim its snapshot slot and park it, as buildCore does.
bool park(Engine& e, std::unique_ptr<SystemBase> core) {
    if (!e.registry().claim(core->id(), *core)) return false;
    return e.parkCore(std::move(core));
}

struct Sys {
    SystemId    id;
    const char* type;
};

// What buildGraph writes for these systems summed to a stereo pair.
std::string stereoDoc(const std::vector<Sys>& systems) {
    std::string nodes = R"({"id":"kernel","type":"rp.dsp.kernel"})";
    std::string wires;
    std::string left, right;
    for (const Sys& s : systems) {
        const std::string id = std::to_string(s.id);
        nodes += R"(,{"id":"sys-)" + id + R"(","type":")" + s.type + R"(","settings":{"core":)" + id + "}}";
    }
    for (const Sys& s : systems) {
        const std::string id = std::to_string(s.id);
        nodes += R"(,{"id":"split-)" + id + R"(","type":"rp.core.split"})";
        wires += std::string(wires.empty() ? "" : ",") + R"({"from":"sys-)" + id + R"(.mix","to":"split-)" + id + R"(.in"})";
    }
    for (std::size_t c = 0; c < 2 && !systems.empty(); ++c) {
        const std::string half = c ? "out2" : "out1";
        const std::string out = R"("out.out.)" + std::to_string(c + 1) + "\"";
        if (systems.size() == 1) {
            wires += R"(,{"from":"split-)" + std::to_string(systems[0].id) + "." + half + R"(","to":)" + out + "}";
            continue;
        }
        const std::string lane = "lane-" + std::to_string(c);
        nodes += R"(,{"id":")" + lane + R"(","type":"rp.core.add","settings":{"inputs":)" + std::to_string(systems.size()) + "}}";
        for (std::size_t k = 0; k < systems.size(); ++k)
            wires += R"(,{"from":"split-)" + std::to_string(systems[k].id) + "." + half + R"(","to":")" + lane + ".in" +
                     std::to_string(k + 1) + "\"}";
        wires += R"(,{"from":")" + lane + R"(.out","to":)" + out + "}";
    }
    nodes += R"(,{"id":"out","type":"rp.host.audio-out","settings":{"buses":[1,1]}})";
    return R"({"nodes":[)" + nodes + R"(],"wires":[)" + wires + "]}";
}

Engine::GraphRequest request(const std::vector<Sys>& systems) {
    Engine::GraphRequest r;
    r.document = stereoDoc(systems);
    for (const Sys& s : systems) r.slots.push_back(s.id);
    return r;
}

const char* const kGb = "rp.sameboy.gb";
const char* const kNes = "rp.mesen.nes";

}  // namespace

TEST_CASE("a parked core is placed by the document that names it, in slot order", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(park(e, mgb(1)));
    REQUIRE(park(e, mgb(2)));
    auto r = request({{2, kGb}, {1, kGb}});
    const auto result = e.setGraph(r);
    REQUIRE(result.ok);
    CHECK(e.systemCount() == 2);
    CHECK(e.findSystem(1) != nullptr);
    CHECK(e.findSystem(2) != nullptr);
    CHECK(e.coreLive(1));
    // Rendering runs the plan: the table the tail walks is the slots'.
    std::vector<float> l(256), rr(256);
    e.processBlock(256, l.data(), rr.data());
    CHECK(e.hostWidth() == 2);
}

TEST_CASE("a core under an id still live is refused", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(park(e, mgb(1)));
    CHECK_FALSE(e.parkCore(mgb(1)));  // parked
    REQUIRE(e.setGraph(request({{1, kGb}})).ok);
    CHECK_FALSE(e.parkCore(mgb(1)));  // held by a node
    // Once a document drops it and the plan that ran it is reclaimed, the id is free again.
    REQUIRE(e.setGraph(request({})).ok);
    e.reclaim();
    CHECK_FALSE(e.coreLive(1));
    CHECK(park(e, mgb(1)));
}

TEST_CASE("the slots and the system nodes must name the same systems", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(park(e, mgb(1)));
    REQUIRE(park(e, mgb(2)));

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
    REQUIRE(park(e, mgb(1)));
    REQUIRE(park(e, mgb(2)));
    REQUIRE(e.setGraph(request({{1, kGb}})).ok);
    CHECK(e.coreLive(1));
    CHECK_FALSE(e.coreLive(2));
    // Its snapshot slot went with it, so the id builds again.
    CHECK(park(e, mgb(2)));
}

TEST_CASE("a refused document leaves the running plan and the parked cores alone", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(park(e, mgb(1)));
    REQUIRE(e.setGraph(request({{1, kGb}})).ok);
    REQUIRE(park(e, mgb(2)));
    auto bad = request({{1, kGb}, {2, kGb}});
    bad.document.replace(bad.document.find("rp.core.add"), 11, "rp.core.nope");
    CHECK_FALSE(e.setGraph(bad).ok);
    CHECK(e.systemCount() == 1);
    CHECK(e.coreLive(2));
}

TEST_CASE("the tap re-arm rides the swap: restore, then arm", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(park(e, bliptoaster(1)));
    auto* nes = dynamic_cast<MesenNesSystem*>(e.findSystem(1));
    CHECK(nes == nullptr);  // parked, not yet placed

    auto armPins = request({{1, kNes}});
    armPins.rearmTap = true;
    armPins.arm = true;
    armPins.armId = 1;
    armPins.armMode = 1;
    REQUIRE(e.setGraph(armPins).ok);
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
    auto armChannels = request({{1, kNes}});
    armChannels.rearmTap = true;
    armChannels.arm = true;
    armChannels.armId = 1;
    armChannels.armMode = 3;
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
    REQUIRE(park(e, std::move(core)));
    REQUIRE(e.setGraph(request({{1, kGb}})).ok);
    REQUIRE(raw->sampleRate_ == kRate);
    e.setSampleRate(44100.0);
    CHECK(raw->sampleRate_ == 44100.0);
}
