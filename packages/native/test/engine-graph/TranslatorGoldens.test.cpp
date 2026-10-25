// The translator's documents as goldens for B2's TS builder (docs/modular-graph-phase-b2-design.md section
// 7.1): every model in a matrix of system sets, routings and host shapes, with the document the translator
// builds for it, its slot order, and the NES tap the engine's SyncTap would arm. The TS test
// (packages/retroplug/test/graph/build-graph-goldens.test.ts) holds buildGraph to the same file, so the two
// builders agree on every case before the translator is deleted.
//
// This test fails when the translator's output drifts from the committed file; RP_GOLDENS_UPDATE=1 rewrites
// it instead. Run via `pnpm test:plugin engine-graph`.

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "NodeRig.hpp"
#include "graph/CanonicalJson.hpp"
#include "host/graph/Translator.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

using rp::host::AudioRouting;
using rp::host::HostShape;
using rp::host::Translator;
using rp::host::test::readFile;

namespace {

// A core that reports a kind and nothing else: the translator reads a Game Boy's link group and a NES's build
// mode from the real cores, so those cases build real ones, and every other system can be one of these.
class StandIn final : public SystemBase {
public:
    StandIn(SystemId id, SystemKind k) : SystemBase(id), kind_(k) {}
    SystemKind kind() const override { return kind_; }
    void onActivate(double) override {}
    void onSampleRateChanged(double) override {}
    std::vector<ChannelStream> channelLayout() const override { return {{"Mix", true}}; }
    void finishBlock(const AudioBlockInfo&, float* const*, std::size_t) override {}

private:
    SystemKind kind_;
};

struct Sys {
    const char*   type;           // the node type buildGraph places it as
    std::uint8_t  linkGroup = 0;  // Game Boys only
    std::uint32_t exportMode = 0; // NES only
    SystemId      id = 0;         // 0: the next id in slot order
};

std::unique_ptr<SystemBase> build(const Sys& s, SystemId id) {
    const std::string type = s.type;
    if (type == "rp.sameboy.gb") {
        if (s.linkGroup == 0) return std::make_unique<StandIn>(id, SystemKind::SameBoy);
        SameBoyConfig cfg;
        cfg.linkGroupId = s.linkGroup;
        return std::make_unique<SameBoySystem>(id, cfg, readFile(RP_MGB_ROM_PATH));
    }
    if (type == "rp.mesen.nes") {
        if (s.exportMode == 0) return std::make_unique<StandIn>(id, SystemKind::MesenNes);
        MesenNesConfig cfg;
        cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
        cfg.channelExportMode = s.exportMode;
        return std::make_unique<MesenNesSystem>(id, cfg, readFile(RP_BLIPTOASTER_ROM_PATH));
    }
    if (type == "rp.mesen.gba") return std::make_unique<StandIn>(id, SystemKind::MesenGba);
    return std::make_unique<StandIn>(id, SystemKind::MesenSms);
}

struct Set {
    const char*      name;
    std::vector<Sys> systems;
};

const char* const kGb = "rp.sameboy.gb";
const char* const kNes = "rp.mesen.nes";
const char* const kGba = "rp.mesen.gba";
const char* const kSms = "rp.mesen.sms";

std::vector<Set> sets() {
    std::vector<Sys> seventeen(17, Sys{kGb});
    return {
        {"empty", {}},
        {"gb", {{kGb}}},
        {"nes", {{kNes}}},
        {"nes-built-pins", {{kNes, 0, 1}}},
        {"nes-built-pins-ref", {{kNes, 0, 2}}},
        {"nes-built-channels", {{kNes, 0, 3}}},
        {"gba", {{kGba}}},
        {"sms", {{kSms}}},
        {"gb-nes", {{kGb}, {kNes}}},
        {"linked-around-unlinked", {{kGb, 1}, {kGb}, {kGb, 1}}},
        {"two-link-groups", {{kGb, 1}, {kGb, 2}, {kGb, 1}, {kGb, 2}, {kNes}}},
        {"group-of-one", {{kGb, 3}, {kNes}}},
        {"nine-mixed", {{kGb}, {kNes}, {kGba}, {kSms}, {kGb, 1}, {kGb, 1}, {kGb}, {kNes, 0, 3}, {kGb}}},
        {"seventeen", seventeen},
        {"sparse-ids", {{kGb, 0, 0, 3}, {kNes, 0, 0, 7}, {kGb, 0, 0, 12}}},
    };
}

struct Routing {
    AudioRouting mode;
    const char*  name;  // the TS value (settingsEnums.ts AUDIO_ROUTING_VALUES)
};
const Routing kRoutings[] = {{AudioRouting::Stereo, "stereo"},
                             {AudioRouting::TwoPerInstance, "twoPerInstance"},
                             {AudioRouting::OnePerInstance, "onePerInstance"},
                             {AudioRouting::ChannelSplit, "channelSplit"},
                             {AudioRouting::PinSplit, "pinSplit"},
                             {AudioRouting::StereoPinSplit, "stereoPinSplit"}};

struct Shape {
    HostShape   shape;
    const char* json;   // the TS HostShape
    const char* label;  // for the case name
};
const Shape kShapes[] = {{{HostShape::Kind::Multi, 2}, R"({"channels":2,"kind":"multi"})", "multi2"},
                         {{HostShape::Kind::Multi, 4}, R"({"channels":4,"kind":"multi"})", "multi4"},
                         {{HostShape::Kind::Multi, 6}, R"({"channels":6,"kind":"multi"})", "multi6"},
                         {{HostShape::Kind::Multi, 8}, R"({"channels":8,"kind":"multi"})", "multi8"},
                         {{HostShape::Kind::PerSystem, 0}, R"({"kind":"perSystem"})", "perSystem"},
                         {{HostShape::Kind::PerChannel, 0}, R"({"kind":"perChannel"})", "perChannel"}};

// What Engine::syncTap arms for this routing and table: the pins (1) or the channels (3) of a lone NES.
std::string tapArm(AudioRouting routing, const rp::host::SystemTable& table) {
    const bool pins = routing == AudioRouting::PinSplit || routing == AudioRouting::StereoPinSplit;
    const bool split = pins || routing == AudioRouting::ChannelSplit;
    if (!split || table.slots.size() != 1 || table.slots[0].core->kind() != SystemKind::MesenNes) return "null";
    return R"({"id":)" + std::to_string(table.slots[0].id) + R"(,"mode":)" + (pins ? "1" : "3") + "}";
}

// The document with each core token replaced by its system's id, the form buildGraph writes. Tokens are
// handed out in slot order from 1, and each appears exactly once, as {"core":<token>}.
std::string normalized(std::string doc, const std::vector<SystemId>& ids) {
    for (std::size_t k = ids.size(); k-- > 0;) {
        const std::string from = R"({"core":)" + std::to_string(k + 1) + "}";
        const std::string to = R"({"core":)" + std::to_string(ids[k]) + "}";
        const auto at = doc.find(from);
        REQUIRE(at != std::string::npos);
        doc.replace(at, from.size(), to);
    }
    auto canonical = rp::graph::canonicalizeJson(doc);
    REQUIRE(canonical);
    return *canonical;
}

std::string generate() {
    std::vector<std::string> documents;
    std::vector<std::string> cases;
    for (const Set& set : sets()) {
        Translator t([](const std::string&, std::shared_ptr<const rp::host::SystemTable>, bool) { return true; },
                     [](SystemBase*) {});
        std::vector<SystemId> ids;
        std::string systems = "[";
        SystemId next = 1;
        for (const Sys& s : set.systems) {
            const SystemId id = s.id ? s.id : next;
            next = id + 1;
            ids.push_back(id);
            REQUIRE(t.add(build(s, id)));
            if (systems.size() > 1) systems += ",";
            systems += R"({"exportMode":)" + std::to_string(s.exportMode) + R"(,"id":)" + std::to_string(id) +
                       R"(,"linkGroup":)" + std::to_string(s.linkGroup) + R"(,"type":")" + s.type + R"("})";
        }
        systems += "]";
        for (const Shape& shape : kShapes) {
            REQUIRE(t.setShape(shape.shape));
            for (const Routing& routing : kRoutings) {
                REQUIRE(t.setRouting(routing.mode));
                const std::string doc = normalized(t.documentJson(), ids);
                std::size_t index = 0;
                while (index < documents.size() && documents[index] != doc) ++index;
                if (index == documents.size()) documents.push_back(doc);
                const rp::host::SystemTable table = t.table();
                std::string slots = "[";
                for (std::size_t i = 0; i < table.slots.size(); ++i)
                    slots += (i ? "," : "") + std::to_string(table.slots[i].id);
                slots += "]";
                cases.push_back(R"({"document":)" + std::to_string(index) + R"(,"model":{"routing":")" + routing.name +
                                R"(","shape":)" + shape.json + R"(,"systems":)" + systems + R"(},"name":")" + set.name +
                                "/" + routing.name + "/" + shape.label + R"(","slots":)" + slots +
                                R"(,"tapArm":)" + tapArm(routing.mode, table) + "}");
            }
        }
    }
    std::string out = "{\"documents\":[\n";
    for (std::size_t i = 0; i < documents.size(); ++i) out += documents[i] + (i + 1 < documents.size() ? ",\n" : "\n");
    out += "],\"cases\":[\n";
    for (std::size_t i = 0; i < cases.size(); ++i) out += cases[i] + (i + 1 < cases.size() ? ",\n" : "\n");
    return out + "]}\n";
}

}  // namespace

TEST_CASE("the translator's documents match the goldens buildGraph is held to", "[engine-graph][translator][goldens]") {
    const std::string text = generate();
    if (const char* u = std::getenv("RP_GOLDENS_UPDATE"); u && std::string(u) == "1") {
        std::FILE* f = std::fopen(RP_TRANSLATOR_GOLDENS_PATH, "wb");
        REQUIRE(f);
        REQUIRE(std::fwrite(text.data(), 1, text.size(), f) == text.size());
        std::fclose(f);
        SUCCEED("rewrote " RP_TRANSLATOR_GOLDENS_PATH);
        return;
    }
    const auto bytes = readFile(RP_TRANSLATOR_GOLDENS_PATH);
    REQUIRE(std::string(bytes.begin(), bytes.end()) == text);
}
