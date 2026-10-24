// SystemNode<T> (docs/modular-graph-phase-b1-design.md section 3.1): a core run through its graph node must
// render exactly what an identical core renders driven directly through prepareForBlock /
// stepIfBelowTarget / finishBlock into zeroed lanes, bit for bit, for a lone system and a linked pair,
// across block sizes. Both cores power on in the same state (Deterministic.cpp) and hear the same input.
//
// Run via `pnpm test:plugin`.

#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "NodeRig.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/mesen/MesenSmsConfig.hpp"
#include "system/mesen/MesenSmsSystem.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "transport/MidiTypes.hpp"

using rp::host::test::NodeRig;
using rp::host::test::readFile;

namespace {

constexpr double kRate = 48000.0;
// The B0 partitions (blocks-*.test.ts): every size, one after another, and the cycle repeats.
constexpr std::array<std::uint32_t, 5> kBlocks = {64, 199, 512, 1024, 8192};

std::unique_ptr<SameBoySystem> mgb(SystemId id) {
    SameBoyConfig cfg;
    cfg.model = SameBoyModel::DmgB;  // the DMG boot ROM plays its chime: real signal without MIDI
    cfg.fastBoot = false;
    auto sys = std::make_unique<SameBoySystem>(id, cfg, readFile(RP_MGB_ROM_PATH));
    sys->onActivate(kRate);
    return sys;
}

std::unique_ptr<MesenNesSystem> nes(SystemId id, std::uint32_t exportMode) {
    MesenNesConfig cfg;
    cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
    cfg.channelExportMode = exportMode;
    auto sys = std::make_unique<MesenNesSystem>(id, cfg, readFile(RP_BLIPTOASTER_ROM_PATH));
    sys->onActivate(kRate);
    return sys;
}

std::unique_ptr<MesenSmsSystem> sms(SystemId id) {
    MesenSmsConfig cfg;
    cfg.romPath = RP_SMS_ROM_PATH;
    auto sys = std::make_unique<MesenSmsSystem>(id, cfg, readFile(RP_SMS_ROM_PATH));
    sys->onActivate(kRate);
    return sys;
}

// A note-on for the NES (through its N8 FIFO), so the mix and every pin it plays carry signal.
void noteOn(SystemBase& sys, std::uint8_t channel, std::uint8_t key) {
    MidiEvent ev;
    ev.size = 3;
    ev.data[0] = static_cast<std::uint8_t>(0x90 | channel);
    ev.data[1] = key;
    ev.data[2] = 100;
    sys.onMidi(&ev, 1);
}

// BlipToaster ignores MIDI for about a second after power-on (parity/scenes.ts BOOT_MS): send both cores
// the same note once 1.5 s have rendered.
std::function<void(std::uint64_t)> afterBoot(SystemBase& a, SystemBase& b) {
    return [&a, &b, sent = false](std::uint64_t at) mutable {
        if (sent || at < static_cast<std::uint64_t>(1.5 * kRate)) return;
        noteOn(a, 0, 60);
        noteOn(b, 0, 60);
        sent = true;
    };
}

AudioBlockInfo info(std::uint32_t frames) { return AudioBlockInfo{frames, kRate, 120.0, 0.0, false}; }

// `lanes` host channels, each as long as the largest block.
struct Lanes {
    std::vector<std::vector<float>> data;
    std::vector<float*>             ptrs;
    explicit Lanes(std::size_t n) : data(n, std::vector<float>(8192, 0.0f)) {
        for (auto& d : data) ptrs.push_back(d.data());
    }
};

// Drive `direct` the way runUnit drives one unit, into `laneMap` (host channel per lane; a mono stream's
// two lanes share one), and compare with what the graph wrote to the host's channels.
struct Direct {
    std::vector<SystemBase*> members;  // in member order
    Lanes                    out;
    bool                     sounded = false;  // any block, any channel
    std::vector<std::vector<int>> laneMaps;  // per member: host channel of each finishBlock lane
    Direct(std::vector<SystemBase*> m, std::size_t channels, std::vector<std::vector<int>> maps)
        : members(std::move(m)), out(channels), laneMaps(std::move(maps)) {}

    void block(std::uint32_t frames) {
        for (auto& d : out.data) std::fill_n(d.begin(), frames, 0.0f);
        for (SystemBase* s : members) s->prepareForBlock(info(frames));
        for (bool below = true; below;) {
            below = false;
            for (SystemBase* s : members)
                if (s->stepIfBelowTarget(frames)) below = true;
        }
        for (std::size_t m = 0; m < members.size(); ++m) {
            std::vector<float*> lanes;
            for (int ch : laneMaps[m]) lanes.push_back(out.ptrs[static_cast<std::size_t>(ch)]);
            members[m]->finishBlock(info(frames), lanes.data(), lanes.size());
        }
        for (const auto& ch : out.data)
            for (std::uint32_t i = 0; i < frames; ++i) sounded = sounded || ch[i] != 0.0f;
    }
};

// Renders `seconds` through both, block by block, and returns the first mismatch ("" when none). `before`
// runs ahead of each block with its first frame, to give both cores the same input.
std::string compare(NodeRig& rig, Direct& direct, double seconds, const std::function<void(std::uint64_t)>& before = {}) {
    Lanes graph(direct.out.data.size());
    const rp_transport t{kRate, 120.0, 0.0, 0};
    const auto total = static_cast<std::uint64_t>(seconds * kRate);
    std::uint64_t at = 0;
    for (std::size_t b = 0; at < total; ++b) {
        const std::uint32_t frames = kBlocks[b % kBlocks.size()];
        if (before) before(at);
        rig.host->processBlock(frames, t, graph.ptrs.data(), static_cast<std::uint32_t>(graph.ptrs.size()));
        direct.block(frames);
        for (std::size_t c = 0; c < graph.data.size(); ++c)
            if (std::memcmp(graph.data[c].data(), direct.out.data[c].data(), frames * sizeof(float)) != 0)
                return "block " + std::to_string(b) + " (" + std::to_string(frames) + " frames), channel " + std::to_string(c);
        at += frames;
    }
    return "";
}

bool sounds(const Direct& d) { return d.sounded; }

}  // namespace

TEST_CASE("a lone Game Boy node renders its core's mix and stems bit for bit", "[engine-graph][system-node]") {
    SECTION("mix") {
        NodeRig rig(kRate);
        auto direct = mgb(1);
        const auto token = rig.park(mgb(1));
        REQUIRE(rig.set(R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb","settings":{"core":)" + std::to_string(token) +
                        R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                            "wires":[{"from":"gb.mix","to":"out.out.1-2"}]})").ok);
        Direct d({direct.get()}, 2, {{0, 1}});
        CHECK(compare(rig, d, 3.0) == "");
        CHECK(sounds(d));
    }
    SECTION("stems") {
        NodeRig rig(kRate);
        auto direct = mgb(1);
        const auto token = rig.park(mgb(1));
        std::string wires;
        for (int k = 1; k <= 4; ++k)
            wires += std::string(k > 1 ? "," : "") + R"({"from":"gb.ch)" + std::to_string(k) + R"(","to":"out.out.)" +
                     std::to_string(2 * k - 1) + "-" + std::to_string(2 * k) + R"("})";
        REQUIRE(rig.set(R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb","settings":{"core":)" + std::to_string(token) +
                        R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2,2,2,2]}}],"wires":[)" + wires + "]}")
                    .ok);
        Direct d({direct.get()}, 8, {{0, 1, 2, 3, 4, 5, 6, 7}});
        CHECK(compare(rig, d, 3.0) == "");
        CHECK(sounds(d));
    }
}

TEST_CASE("a linked pair of Game Boys steps in runUnit's lockstep", "[engine-graph][system-node]") {
    NodeRig rig(kRate);
    auto d1 = mgb(1), d2 = mgb(2);
    d1->linkPeers_ = {d2.get()};
    d2->linkPeers_ = {d1.get()};
    const auto t1 = rig.park(mgb(1)), t2 = rig.park(mgb(2));
    REQUIRE(rig.set(R"({"nodes":[{"id":"a","type":"rp.sameboy.gb","settings":{"core":)" + std::to_string(t1) +
                    R"(}},{"id":"b","type":"rp.sameboy.gb","settings":{"core":)" + std::to_string(t2) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2,2]}}],
                        "wires":[{"from":"a.link","to":"b.link"},{"from":"a.mix","to":"out.out.1-2"},{"from":"b.mix","to":"out.out.3-4"}]})")
                .ok);
    Direct d({d1.get(), d2.get()}, 4, {{0, 1}, {2, 3}});
    CHECK(compare(rig, d, 3.0) == "");
    CHECK(sounds(d));
}

TEST_CASE("a lone NES node renders its mix, and its pins when tapped, bit for bit", "[engine-graph][system-node]") {
    SECTION("mix") {
        NodeRig rig(kRate);
        auto direct = nes(1, 0);
        auto viaGraph = nes(1, 0);
        SystemBase* graphCore = viaGraph.get();
        const auto token = rig.park(std::move(viaGraph));
        REQUIRE(rig.set(R"({"nodes":[{"id":"nes","type":"rp.mesen.nes","settings":{"core":)" + std::to_string(token) +
                        R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                            "wires":[{"from":"nes.mix","to":"out.out.1-2"}]})").ok);
        Direct d({direct.get()}, 2, {{0, 1}});
        CHECK(compare(rig, d, 3.0, afterBoot(*direct, *graphCore)) == "");
        CHECK(sounds(d));
    }
    SECTION("pins") {
        NodeRig rig(kRate);
        auto direct = nes(1, 1);
        auto viaGraph = nes(1, 1);
        SystemBase* graphCore = viaGraph.get();
        const auto token = rig.park(std::move(viaGraph));
        REQUIRE(rig.set(R"({"nodes":[{"id":"nes","type":"rp.mesen.nes","settings":{"core":)" + std::to_string(token) +
                        R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[1,1,1]}}],
                            "wires":[{"from":"nes.pins.pulse","to":"out.out.1"},{"from":"nes.pins.tnd","to":"out.out.2"},
                                     {"from":"nes.pins.expansion","to":"out.out.3"}]})").ok);
        Direct d({direct.get()}, 3, {{0, 0, 1, 1, 2, 2}});  // a mono stream's two lanes are one buffer
        CHECK(compare(rig, d, 3.0, afterBoot(*direct, *graphCore)) == "");
        CHECK(sounds(d));
    }
}

TEST_CASE("a lone SMS node renders its core's mix bit for bit", "[engine-graph][system-node]") {
    NodeRig rig(kRate);
    auto direct = sms(1);
    const auto token = rig.park(sms(1));
    REQUIRE(rig.set(R"({"nodes":[{"id":"sms","type":"rp.mesen.sms","settings":{"core":)" + std::to_string(token) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"sms.mix","to":"out.out.1-2"}]})").ok);
    Direct d({direct.get()}, 2, {{0, 1}});
    CHECK(compare(rig, d, 2.0) == "");
}

TEST_CASE("a system node takes its core once, and releases it when destroyed", "[engine-graph][system-node]") {
    NodeRig rig(kRate);
    const auto token = rig.park(mgb(7));
    const std::string doc = R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb","settings":{"core":)" + std::to_string(token) + "}}]," +
                            R"("wires":[]})";
    REQUIRE(rig.set(doc).ok);
    CHECK(rig.parked.empty());
    REQUIRE(rig.set(doc).ok);  // the same document reuses the instance and its core
    CHECK(rig.released.empty());
    REQUIRE(rig.set(R"({"nodes":[],"wires":[]})").ok);
    rig.host->reclaim();
    CHECK(rig.released == std::vector<SystemId>{7});
    // A token nothing is parked under does not plan.
    CHECK_FALSE(rig.set(R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb","settings":{"core":999}}],"wires":[]})").ok);
}
