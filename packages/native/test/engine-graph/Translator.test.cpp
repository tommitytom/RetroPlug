// The translator (docs/modular-graph-phase-b1-design.md section 4): today's structural calls as documents.
// Each host channel must be fed what today's router adds into it, in today's order, and systems must run
// in runBlock's order. The documents are captured, not planned, so most systems here are stand-ins that
// report a kind; the link-group cases need real Game Boys (the link group is SameBoy config).
//
// Run via `pnpm test:plugin`.

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "NodeRig.hpp"
#include "graph/GraphDocument.hpp"
#include "host/graph/Translator.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

using rp::host::AudioRouting;
using rp::host::HostShape;
using rp::host::Translator;
using rp::host::test::readFile;

namespace {

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

std::unique_ptr<SystemBase> gb(SystemId id) { return std::make_unique<StandIn>(id, SystemKind::SameBoy); }
std::unique_ptr<SystemBase> nes(SystemId id) { return std::make_unique<StandIn>(id, SystemKind::MesenNes); }

std::unique_ptr<SystemBase> linkedMgb(SystemId id, std::uint8_t group) {
    SameBoyConfig cfg;
    cfg.linkGroupId = group;
    return std::make_unique<SameBoySystem>(id, cfg, readFile(RP_MGB_ROM_PATH));
}

struct Rig {
    std::vector<std::string> docs;
    std::vector<SystemId>    released;
    bool                     accept = true;
    Translator               t{[this](const std::string& json, std::shared_ptr<const rp::host::SystemTable>) {
                     docs.push_back(json);
                     return accept;
                 },
                 [this](SystemBase* core) { released.push_back(core->id()); }};

    rp::graph::GraphDocument doc() const { return *rp::graph::parseGraphDocument(docs.back()); }

    std::vector<std::string> nodeOrder() const {
        std::vector<std::string> out;
        for (const auto& n : doc().nodes) out.push_back(n.id);
        return out;
    }

    std::string from(const std::string& to) const {
        for (const auto& w : doc().wires)
            if (w.to == to) return w.from;
        return "";
    }

    // What feeds host channel `c` (0-based), in order: the one endpoint wired straight in, or every input of
    // the rp.core.add chain that sums into it.
    std::vector<std::string> lane(std::uint32_t c) const {
        std::vector<std::string> out;
        std::string src = from("out.out." + std::to_string(c + 1));
        if (src.rfind("lane-", 0) != 0) return src.empty() ? out : std::vector<std::string>{src};
        expand(src.substr(0, src.find('.')), out);
        return out;
    }
    void expand(const std::string& add, std::vector<std::string>& out) const {
        for (int k = 1;; ++k) {
            const std::string in = from(add + ".in" + std::to_string(k));
            if (in.empty()) return;
            if (in.rfind("lane-", 0) == 0) expand(in.substr(0, in.find('.')), out);
            else out.push_back(in);
        }
    }

    std::vector<std::uint32_t> buses() const {
        const auto d = doc();
        for (const auto& n : d.nodes) {
            if (n.id != "out") continue;
            const auto object = n.settings->to_object();
            const auto array = object->get("buses")->to_array();
            std::vector<std::uint32_t> b;
            for (const auto& v : *array) b.push_back(static_cast<std::uint32_t>(*v.to_int()));
            return b;
        }
        return {};
    }
};

using V = std::vector<std::string>;

}  // namespace

TEST_CASE("the empty project is the kernel and the host's outputs", "[engine-graph][translator]") {
    Rig r;
    REQUIRE(r.docs.size() == 1);
    CHECK(r.nodeOrder() == V{"kernel", "out"});
    CHECK(r.buses() == std::vector<std::uint32_t>{1, 1});
}

TEST_CASE("Stereo sums every system into one pair, in slot order", "[engine-graph][translator]") {
    Rig r;
    REQUIRE(r.t.add(gb(1)));
    REQUIRE(r.t.add(nes(2)));
    REQUIRE(r.t.add(gb(3)));
    CHECK(r.nodeOrder() == V{"kernel", "sys-1", "sys-2", "sys-3", "split-1", "split-2", "split-3", "lane-0", "lane-1", "out"});
    CHECK(r.lane(0) == V{"split-1.out1", "split-2.out1", "split-3.out1"});
    CHECK(r.lane(1) == V{"split-1.out2", "split-2.out2", "split-3.out2"});
    REQUIRE(r.t.remove(2));  // a remove shifts the later slots down
    CHECK(r.lane(0) == V{"split-1.out1", "split-3.out1"});
}

TEST_CASE("one system on a lane is wired straight in", "[engine-graph][translator]") {
    Rig r;
    REQUIRE(r.t.add(gb(1)));
    CHECK(r.lane(0) == V{"split-1.out1"});
    CHECK(r.from("out.out.1") == "split-1.out1");
}

TEST_CASE("OnePerInstance puts L then R on one lane, and wraps past the outputs", "[engine-graph][translator]") {
    Rig r;
    for (SystemId id = 1; id <= 3; ++id) REQUIRE(r.t.add(gb(id)));
    REQUIRE(r.t.setRouting(AudioRouting::OnePerInstance));
    // Flat, never (L0 + R0) + (L2 + R2): float addition is not associative.
    CHECK(r.lane(0) == V{"split-1.out1", "split-1.out2", "split-3.out1", "split-3.out2"});
    CHECK(r.lane(1) == V{"split-2.out1", "split-2.out2"});
}

TEST_CASE("TwoPerInstance gives slot i the pair 2i mod N", "[engine-graph][translator]") {
    Rig r;
    for (SystemId id = 1; id <= 5; ++id) REQUIRE(r.t.add(gb(id)));
    REQUIRE(r.t.setRouting(AudioRouting::TwoPerInstance));
    REQUIRE(r.t.setShape({HostShape::Kind::Multi, 4}));
    CHECK(r.lane(0) == V{"split-1.out1", "split-3.out1", "split-5.out1"});
    CHECK(r.lane(1) == V{"split-1.out2", "split-3.out2", "split-5.out2"});
    CHECK(r.lane(2) == V{"split-2.out1", "split-4.out1"});
    CHECK(r.lane(3) == V{"split-2.out2", "split-4.out2"});
}

TEST_CASE("a lane fed by more than 16 contributions chains its adds", "[engine-graph][translator]") {
    Rig r;
    for (SystemId id = 1; id <= 20; ++id) REQUIRE(r.t.add(gb(id)));
    V want;
    for (SystemId id = 1; id <= 20; ++id) want.push_back("split-" + std::to_string(id) + ".out1");
    CHECK(r.lane(0) == want);
    CHECK(r.from("lane-0-1.in1") == "lane-0.out");
}

TEST_CASE("linked Game Boys run after the unlinked ones, each group in slot order", "[engine-graph][translator]") {
    Rig r;
    REQUIRE(r.t.add(linkedMgb(1, 1)));
    REQUIRE(r.t.add(linkedMgb(2, 0)));
    REQUIRE(r.t.add(linkedMgb(3, 1)));
    CHECK(r.nodeOrder() == V{"kernel", "sys-2", "sys-1", "sys-3", "split-2", "split-1", "split-3", "lane-0", "lane-1", "out"});
    CHECK(r.from("sys-3.link") == "sys-1.link");
    CHECK(r.lane(0) == V{"split-2.out1", "split-1.out1", "split-3.out1"});
    REQUIRE(r.t.setLinkGroup(3, 0));  // unlinking dissolves a group of two
    CHECK(r.nodeOrder() == V{"kernel", "sys-1", "sys-2", "sys-3", "split-1", "split-2", "split-3", "lane-0", "lane-1", "out"});
    CHECK(r.from("sys-3.link").empty());
}

TEST_CASE("ChannelSplit fans a Game Boy's stems, or falls back to Stereo on a narrow host", "[engine-graph][translator]") {
    Rig r;
    REQUIRE(r.t.add(gb(1)));
    REQUIRE(r.t.setRouting(AudioRouting::ChannelSplit));
    REQUIRE(r.t.setShape({HostShape::Kind::Multi, 8}));
    for (std::uint32_t k = 0; k < 4; ++k) {
        const std::string stem = "stem-1-" + std::to_string(k);
        CHECK(r.from(stem + ".in") == "sys-1.ch" + std::to_string(k + 1));
        CHECK(r.lane(2 * k) == V{stem + ".out1"});
        CHECK(r.lane(2 * k + 1) == V{stem + ".out2"});
    }
    REQUIRE(r.t.setShape({HostShape::Kind::Multi, 4}));
    CHECK(r.lane(0) == V{"split-1.out1"});
    CHECK(r.lane(2).empty());
}

TEST_CASE("a single NES splits into channels, pins, or pins on pairs", "[engine-graph][translator]") {
    Rig r;
    REQUIRE(r.t.add(nes(1)));
    REQUIRE(r.t.setShape({HostShape::Kind::Multi, 8}));
    REQUIRE(r.t.setRouting(AudioRouting::ChannelSplit));
    CHECK(r.lane(0) == V{"sys-1.channels.sq1"});
    CHECK(r.lane(4) == V{"sys-1.channels.dmc"});
    CHECK(r.lane(5).empty());
    REQUIRE(r.t.setRouting(AudioRouting::PinSplit));
    CHECK(r.lane(0) == V{"sys-1.pins.pulse"});
    CHECK(r.lane(2) == V{"sys-1.pins.expansion"});
    REQUIRE(r.t.setRouting(AudioRouting::StereoPinSplit));
    CHECK(r.lane(2) == V{"sys-1.pins.tnd"});  // each pin mirrored onto both lanes of its pair
    CHECK(r.lane(3) == V{"sys-1.pins.tnd"});
    // Too narrow for its lanes: the mix feeds the host as in Stereo (the tap is the engine's SyncTap).
    REQUIRE(r.t.setShape({HostShape::Kind::Multi, 4}));
    CHECK(r.lane(0) == V{"split-1.out1"});
    REQUIRE(r.t.add(gb(2)));  // two systems: every split renders as Stereo
    REQUIRE(r.t.setShape({HostShape::Kind::Multi, 8}));
    CHECK(r.lane(0) == V{"split-1.out1", "split-2.out1"});
}

TEST_CASE("PerSystem and PerChannel lay the shapes the render calls want", "[engine-graph][translator]") {
    Rig r;
    REQUIRE(r.t.add(gb(1)));
    REQUIRE(r.t.add(nes(2)));
    REQUIRE(r.t.setShape({HostShape::Kind::PerSystem, 0}));
    CHECK(r.from("out.out.1-2") == "sys-1.mix");
    CHECK(r.from("out.out.3-4") == "sys-2.mix");
    CHECK(r.buses() == std::vector<std::uint32_t>{2, 2});
    REQUIRE(r.t.remove(1));
    REQUIRE(r.t.setRouting(AudioRouting::ChannelSplit));  // arms the NES's five channels
    REQUIRE(r.t.setShape({HostShape::Kind::PerChannel, 0}));
    CHECK(r.from("out.out.1") == "sys-2.channels.sq1");
    CHECK(r.from("out.out.9") == "sys-2.channels.dmc");
    CHECK(r.buses() == std::vector<std::uint32_t>(10, 1));
}

TEST_CASE("replace keeps the slot; a refused plan leaves the model as it was", "[engine-graph][translator]") {
    Rig r;
    REQUIRE(r.t.add(gb(1)));
    REQUIRE(r.t.add(gb(2)));
    REQUIRE(r.t.replace(1, gb(5)));
    CHECK(r.lane(0) == V{"split-5.out1", "split-2.out1"});
    CHECK_FALSE(r.t.replace(9, gb(6)));  // no such system: the core is freed, never adopted
    CHECK(r.released == std::vector<SystemId>{6});
    r.accept = false;
    CHECK_FALSE(r.t.add(gb(7)));
    CHECK(r.released == std::vector<SystemId>{6, 7});
    CHECK(r.t.systemCount() == 2);
}

TEST_CASE("every host lane of a lone NES is fed as today's split routers feed it", "[engine-graph][translator]") {
    // The tables are the rules the split routers held before the flip, not the translator's own: stream k lands on
    // lane (stride * k) mod N with stride 1 for mono streams (2 under StereoPinSplit, which mirrors the mono
    // stem onto both lanes of its pair), the core's streams in channelLayout order, and unused lanes stay empty.
    Rig r;
    REQUIRE(r.t.add(nes(1)));
    REQUIRE(r.t.setShape({HostShape::Kind::Multi, 8}));

    REQUIRE(r.t.setRouting(AudioRouting::ChannelSplit));
    const V channels[8] = {V{"sys-1.channels.sq1"},   V{"sys-1.channels.sq2"}, V{"sys-1.channels.tri"}, V{"sys-1.channels.noise"},
                           V{"sys-1.channels.dmc"},   V{},                     V{},                     V{}};
    for (std::uint32_t c = 0; c < 8; ++c) CHECK(r.lane(c) == channels[c]);

    REQUIRE(r.t.setRouting(AudioRouting::PinSplit));
    const V pins[8] = {V{"sys-1.pins.pulse"}, V{"sys-1.pins.tnd"}, V{"sys-1.pins.expansion"}, V{}, V{}, V{}, V{}, V{}};
    for (std::uint32_t c = 0; c < 8; ++c) CHECK(r.lane(c) == pins[c]);

    REQUIRE(r.t.setRouting(AudioRouting::StereoPinSplit));
    const V pairs[8] = {V{"sys-1.pins.pulse"},     V{"sys-1.pins.pulse"}, V{"sys-1.pins.tnd"}, V{"sys-1.pins.tnd"},
                        V{"sys-1.pins.expansion"}, V{"sys-1.pins.expansion"}, V{},              V{}};
    for (std::uint32_t c = 0; c < 8; ++c) CHECK(r.lane(c) == pairs[c]);
}

TEST_CASE("systems are placed by slot, not by their place in the sum order, past a link group", "[engine-graph][translator]") {
    // Slots 0 and 2 are linked, slot 1 is not: the sum order is slot 1, 0, 2, so a placement by sum position
    // would put the unlinked system on the first pair.
    Rig r;
    REQUIRE(r.t.add(linkedMgb(1, 1)));
    REQUIRE(r.t.add(linkedMgb(2, 0)));
    REQUIRE(r.t.add(linkedMgb(3, 1)));
    REQUIRE(r.nodeOrder()[1] == "sys-2");
    REQUIRE(r.t.setShape({HostShape::Kind::Multi, 8}));

    REQUIRE(r.t.setRouting(AudioRouting::TwoPerInstance));
    CHECK(r.lane(0) == V{"split-1.out1"});
    CHECK(r.lane(1) == V{"split-1.out2"});
    CHECK(r.lane(2) == V{"split-2.out1"});
    CHECK(r.lane(3) == V{"split-2.out2"});
    CHECK(r.lane(4) == V{"split-3.out1"});
    CHECK(r.lane(5) == V{"split-3.out2"});

    REQUIRE(r.t.setRouting(AudioRouting::OnePerInstance));
    CHECK(r.lane(0) == V{"split-1.out1", "split-1.out2"});
    CHECK(r.lane(1) == V{"split-2.out1", "split-2.out2"});
    CHECK(r.lane(2) == V{"split-3.out1", "split-3.out2"});

    REQUIRE(r.t.setShape({HostShape::Kind::PerSystem, 0}));
    CHECK(r.from("out.out.1-2") == "sys-1.mix");
    CHECK(r.from("out.out.3-4") == "sys-2.mix");
    CHECK(r.from("out.out.5-6") == "sys-3.mix");
}

TEST_CASE("a refused plan leaves the model as it was, for every structural call", "[engine-graph][translator]") {
    // After a refused call the document the translator would publish next must be the one from before it, and
    // a real call afterwards must build on that, not on the refused change.
    auto rigged = [](Rig& r) {
        REQUIRE(r.t.add(linkedMgb(1, 1)));
        REQUIRE(r.t.add(linkedMgb(2, 0)));
        REQUIRE(r.t.add(linkedMgb(3, 1)));
        REQUIRE(r.t.setShape({HostShape::Kind::Multi, 4}));
        REQUIRE(r.t.setRouting(AudioRouting::TwoPerInstance));
    };
    auto settled = [](Rig& r, const std::string& before, std::size_t released) {
        CHECK(r.t.documentJson() == before);
        CHECK(r.t.systemCount() == 3);
        CHECK(r.released.size() == released);
        r.accept = true;
        REQUIRE(r.t.setShape({HostShape::Kind::Multi, 4}));  // a no-op: nothing is republished
        REQUIRE(r.t.add(gb(9)));  // a real call builds on the restored model, then undoes cleanly
        REQUIRE(r.t.remove(9));
        CHECK(r.t.documentJson() == before);
    };

    SECTION("replace") {
        Rig r;
        rigged(r);
        const std::string before = r.t.documentJson();
        r.accept = false;
        CHECK_FALSE(r.t.replace(2, linkedMgb(5, 0)));
        CHECK(r.t.find(2) != nullptr);
        CHECK(r.t.find(5) == nullptr);
        CHECK(r.released == std::vector<SystemId>{5});
        settled(r, before, 1);
    }
    SECTION("remove") {
        Rig r;
        rigged(r);
        const std::string before = r.t.documentJson();
        r.accept = false;
        CHECK_FALSE(r.t.remove(2));
        CHECK(r.t.find(2) != nullptr);
        settled(r, before, 0);
    }
    SECTION("setRouting") {
        Rig r;
        rigged(r);
        const std::string before = r.t.documentJson();
        r.accept = false;
        CHECK_FALSE(r.t.setRouting(AudioRouting::OnePerInstance));
        CHECK(r.t.routing() == AudioRouting::TwoPerInstance);
        settled(r, before, 0);
    }
    SECTION("setLinkGroup") {
        Rig r;
        rigged(r);
        const std::string before = r.t.documentJson();
        r.accept = false;
        CHECK_FALSE(r.t.setLinkGroup(3, 0));  // would dissolve the group
        CHECK_FALSE(r.t.setLinkGroup(2, 1));  // would join it
        settled(r, before, 0);
        CHECK(r.from("sys-3.link") == "sys-1.link");
        CHECK(r.from("sys-2.link").empty());
    }
    SECTION("setShape") {
        Rig r;
        rigged(r);
        const std::string before = r.t.documentJson();
        r.accept = false;
        CHECK_FALSE(r.t.setShape({HostShape::Kind::PerSystem, 0}));
        CHECK(r.t.shape() == HostShape{HostShape::Kind::Multi, 4});
        settled(r, before, 0);
    }
}
