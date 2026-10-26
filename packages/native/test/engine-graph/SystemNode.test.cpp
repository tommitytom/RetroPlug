// SystemNode<T> (docs/modular-graph-phase-b1-design.md section 3.1): a core run through its graph node must
// render exactly what an identical core renders driven directly through prepareForBlock /
// stepIfBelowTarget / finishBlock into zeroed lanes, bit for bit, for a lone system and a linked pair,
// across block sizes. Both cores power on in the same state (Deterministic.cpp) and hear the same input.
// The node's inputs (docs/modular-graph-phase-b3a-design.md section 3.1) are held the same way: what reaches a
// node's input must render what the core call it maps to renders, made directly; and the Game Boy's serialOut
// must carry what its core captured.
//
// There is no GBA case: Mesen's GBA core needs a BIOS image, and no fixture here carries one.
//
// Run via `pnpm test:plugin`.

#include <algorithm>
#include <array>
#include <cstdint>
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
#include "system/InputTypes.hpp"
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
    cfg.enableFm = false;  // as the TS side builds it for smsggdj
    // smsggdj reads its options from the battery at boot: an empty SMDJ4 image with the v1 block ('C' 'F', then PAL / SYNC / video /
    // FM off, then their sum) at $3F60 of a 32 KB image, SYNC off so the song free-runs (smsSyncSong.ts).
    cfg.sram.assign(32 * 1024, 0);
    const std::uint8_t header[] = {'S', 'M', 'D', 'J', '4', 1, 32}, options[] = {0x43, 0x46, 0, 0, 0, 0, 0};
    std::memcpy(cfg.sram.data(), header, sizeof header);
    std::memcpy(cfg.sram.data() + 0x3f60, options, sizeof options);
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

// A note-on as raw MIDI bytes on every core's serial port once `seconds` have rendered: how mGB hears MIDI
// (a bare core has no MIDI role, so onMidi would go nowhere). Frame 0 is already due.
std::function<void(std::uint64_t)> sendAt(std::vector<SystemBase*> all, double seconds) {
    return [all = std::move(all), seconds, sent = false](std::uint64_t at) mutable {
        if (sent || at < static_cast<std::uint64_t>(seconds * kRate)) return;
        for (SystemBase* s : all)
            for (std::uint8_t byte : {0x90, 60, 100}) s->pushSerialIn(0, byte);
        sent = true;
    };
}

// parity/scenes.ts smsStart: smsggdj boots a blank song, so once it is up (about 2.5 s) the metronome song (the
// bytes smsSyncSong.ts' buildMetronomeBlock sets that a blank one does not) is poked into work RAM and
// PAUSE is held to start it. Both cores get the same pokes and presses at the same block. The hold is
// 0.6 s because press and release are gated on block starts: an 8192-frame block can carry the clock past
// both thresholds, so a 100 ms hold (parity cuts its blocks at exactly those times) shrinks to one 64-frame
// block, shorter than the video frame the pause line is sampled on.
void pokeMetronome(SystemBase& s) {
    auto ram = s.getMemory(rp::MemoryType::Ram, rp::AccessType::ReadWrite);
    REQUIRE(ram.valid());
    constexpr std::size_t kPhrases = 256, kChains = 3584, kSong = 4864;
    for (std::size_t step = 0; step < 16; step += 4) {
        const std::uint8_t hit[] = {0x0d, 0x00, 0x00, 0x00}, kill[] = {0x00, 0xff, 0x01, 0x00};
        std::memcpy(ram.data() + kPhrases + step * 4, hit, 4);
        std::memcpy(ram.data() + kPhrases + (step + 1) * 4, kill, 4);
    }
    std::memset(ram.data() + kChains, 0, 16 * 2);  // chain 0: phrase 0, sixteen times
    for (std::size_t row = 0; row < 8; ++row) ram.data()[kSong + row * 4] = 0;  // channel 0 plays chain 0
}

std::function<void(std::uint64_t)> smsStart(SystemBase& a, SystemBase& b) {
    return [&a, &b, stage = 0](std::uint64_t at) mutable {
        const auto pause = static_cast<std::uint8_t>(SmsButton::Start);
        if (stage == 0 && at >= static_cast<std::uint64_t>(3.0 * kRate)) {
            for (SystemBase* s : {&a, &b}) {
                pokeMetronome(*s);
                s->pressButton(pause, true);
            }
            stage = 1;
        } else if (stage == 1 && at >= static_cast<std::uint64_t>(3.6 * kRate)) {
            a.pressButton(pause, false);
            b.pressButton(pause, false);
            stage = 2;
        }
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

// Drive `direct` the way the engine before B1 drove one unit (prepare every member, step them round-robin
// until none is below, finish each), into `laneMap` (host channel per lane; a mono stream's
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

constexpr std::uint64_t kNever = UINT64_MAX;

// Renders `seconds` through both, block by block, and returns the first mismatch ("" when none). `before`
// runs ahead of each block with its first frame, to give both cores the same input. With a `control`, an
// identical core that hears none of that input, `firstDifference` is the first frame `direct` differs from it
// (kNever if it never does): where the input is audible, so that matching `direct` means the node delivered it.
std::string compare(NodeRig& rig, Direct& direct, double seconds, const std::function<void(std::uint64_t)>& before = {},
                    Direct* control = nullptr, std::uint64_t* firstDifference = nullptr) {
    Lanes graph(direct.out.data.size());
    const rp_transport t{kRate, 120.0, 0.0, 0};
    const auto total = static_cast<std::uint64_t>(seconds * kRate);
    if (firstDifference) *firstDifference = kNever;
    std::uint64_t at = 0;
    for (std::size_t b = 0; at < total; ++b) {
        const std::uint32_t frames = kBlocks[b % kBlocks.size()];
        if (before) before(at);
        rig.host->processBlock(frames, t, graph.ptrs.data(), static_cast<std::uint32_t>(graph.ptrs.size()));
        direct.block(frames);
        if (control) control->block(frames);
        for (std::size_t c = 0; c < graph.data.size(); ++c)
            if (std::memcmp(graph.data[c].data(), direct.out.data[c].data(), frames * sizeof(float)) != 0)
                return "block " + std::to_string(b) + " (" + std::to_string(frames) + " frames), channel " + std::to_string(c);
        if (control && firstDifference && *firstDifference == kNever)
            for (std::size_t c = 0; c < graph.data.size(); ++c)
                for (std::uint32_t i = 0; i < frames; ++i)
                    if (direct.out.data[c][i] != control->out.data[c][i] && at + i < *firstDifference) *firstDifference = at + i;
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

TEST_CASE("a linked pair of Game Boys steps in lockstep, member by member", "[engine-graph][system-node]") {
    // Lockstep itself (members stepping round-robin so a cable byte crosses at its true instant) is only
    // observable with LSDj, which drives the cable: that is the fixture-only lsdj-link parity group. mGB is
    // an external-clock slave and never does, so what is pinned here is the wiring and its one audible
    // effect: a linked mGB's serial port belongs to the cable, so it hears no MIDI note.
    const auto doc = [](std::uint64_t t1, std::uint64_t t2, bool linked) {
        return R"({"nodes":[{"id":"a","type":"rp.sameboy.gb","settings":{"core":)" + std::to_string(t1) +
               R"(}},{"id":"b","type":"rp.sameboy.gb","settings":{"core":)" + std::to_string(t2) +
               R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2,2]}}],"wires":[)" +
               (linked ? R"({"from":"a.link","to":"b.link"},)" : "") +
               R"({"from":"a.mix","to":"out.out.1-2"},{"from":"b.mix","to":"out.out.3-4"}]})";
    };
    NodeRig rig(kRate);
    auto d1 = mgb(1), d2 = mgb(2);
    d1->linkPeers_ = {d2.get()};
    d2->linkPeers_ = {d1.get()};
    auto g1 = mgb(1), g2 = mgb(2);
    SameBoySystem* const graph1 = g1.get();
    SameBoySystem* const graph2 = g2.get();
    const auto t1 = rig.park(std::move(g1)), t2 = rig.park(std::move(g2));
    REQUIRE(rig.set(doc(t1, t2, true)).ok);
    // The document's link wire filled each graph core's peer list with the other.
    CHECK(graph1->linkPeers_ == std::vector<SameBoySystem*>{graph2});
    CHECK(graph2->linkPeers_ == std::vector<SameBoySystem*>{graph1});
    Direct d({d1.get(), d2.get()}, 4, {{0, 1}, {2, 3}});
    CHECK(compare(rig, d, 4.0, sendAt({d1.get(), d2.get(), graph1, graph2}, 2.0)) == "");
    CHECK(sounds(d));

    // A document without the wire unlinks them again.
    REQUIRE(rig.set(doc(t1, t2, false)).ok);
    CHECK(graph1->linkPeers_.empty());
    CHECK(graph2->linkPeers_.empty());

    // The note is the observable difference: a linked pair ignores it, an unlinked core plays it.
    auto x1 = mgb(1), x2 = mgb(2), lone = mgb(3);
    x1->linkPeers_ = {x2.get()};
    x2->linkPeers_ = {x1.get()};
    Direct linkedRun({x1.get(), x2.get()}, 4, {{0, 1}, {2, 3}});
    Direct loneRun({lone.get()}, 2, {{0, 1}});
    const auto send = sendAt({x1.get(), lone.get()}, 2.0);
    bool differed = false;
    for (std::uint64_t at = 0; at < static_cast<std::uint64_t>(4.0 * kRate); at += 512) {
        send(at);
        linkedRun.block(512);
        loneRun.block(512);
        differed = differed || std::memcmp(linkedRun.out.data[0].data(), loneRun.out.data[0].data(), 512 * sizeof(float)) != 0;
    }
    CHECK(differed);
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
    auto viaGraph = sms(1);
    SystemBase* graphCore = viaGraph.get();
    const auto token = rig.park(std::move(viaGraph));
    REQUIRE(rig.set(R"({"nodes":[{"id":"sms","type":"rp.mesen.sms","settings":{"core":)" + std::to_string(token) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"sms.mix","to":"out.out.1-2"}]})").ok);
    Direct d({direct.get()}, 2, {{0, 1}});
    CHECK(compare(rig, d, 5.0, smsStart(*direct, *graphCore)) == "");
    CHECK(sounds(d));
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

namespace {

// A lone NES node with `ports` (output ports of rp.mesen.nes) wired to mono host channels in order, driven
// against an identical core in `mode`; `lanes` is the direct core's host channel per finishBlock lane.
void nesTapCase(std::uint32_t mode, const std::vector<std::string>& ports, std::size_t channels,
                const std::vector<int>& lanes) {
    NodeRig rig(kRate);
    auto direct = nes(1, mode);
    auto viaGraph = nes(1, mode);
    SystemBase* graphCore = viaGraph.get();
    const auto token = rig.park(std::move(viaGraph));
    std::string wires, buses;
    for (std::size_t k = 0; k < ports.size(); ++k) {
        wires += std::string(k ? "," : "") + R"({"from":"nes.)" + ports[k] + R"(","to":"out.out.)" + std::to_string(k + 1) + R"("})";
        buses += std::string(k ? "," : "") + "1";
    }
    REQUIRE(rig.set(R"({"nodes":[{"id":"nes","type":"rp.mesen.nes","settings":{"core":)" + std::to_string(token) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[)" + buses + R"(]}}],"wires":[)" + wires + "]}")
                .ok);
    Direct d({direct.get()}, channels, {lanes});
    CHECK(compare(rig, d, 3.0, afterBoot(*direct, *graphCore)) == "");
    CHECK(sounds(d));
}

}  // namespace

TEST_CASE("a lone NES node renders channel-export mode 2 (four pin streams, 8 lanes) bit for bit", "[engine-graph][system-node]") {
    nesTapCase(2, {"pins.pulse", "pins.tnd", "pins.expansion", "pins.mixref"}, 4, {0, 0, 1, 1, 2, 2, 3, 3});
}

TEST_CASE("a lone NES node renders channel-export mode 3 (five channel streams, 10 lanes) bit for bit", "[engine-graph][system-node]") {
    nesTapCase(3, {"channels.sq1", "channels.sq2", "channels.tri", "channels.noise", "channels.dmc"}, 5,
               {0, 0, 1, 1, 2, 2, 3, 3, 4, 4});
}

TEST_CASE("a NES node with its mix connected renders the mix even when the tap is armed", "[engine-graph][system-node]") {
    NodeRig rig(kRate);
    auto direct = nes(1, 1);
    auto viaGraph = nes(1, 1);
    SystemBase* graphCore = viaGraph.get();
    const auto token = rig.park(std::move(viaGraph));
    REQUIRE(rig.set(R"({"nodes":[{"id":"nes","type":"rp.mesen.nes","settings":{"core":)" + std::to_string(token) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2,1]}}],
                        "wires":[{"from":"nes.mix","to":"out.out.1-2"},{"from":"nes.pins.pulse","to":"out.out.3"}]})").ok);
    // Two lanes only, as on the narrow-host path: the connected pin is written as silence.
    Direct d({direct.get()}, 3, {{0, 1}});
    CHECK(compare(rig, d, 3.0, afterBoot(*direct, *graphCore)) == "");
    CHECK(sounds(d));
}

TEST_CASE("a core of the wrong kind under a node's token is refused and released", "[engine-graph][system-node]") {
    NodeRig rig(kRate);
    const auto wrong = rig.park(nes(5, 0));
    CHECK_FALSE(rig.set(R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb","settings":{"core":)" + std::to_string(wrong) +
                       R"(}}],"wires":[]})").ok);
    CHECK(rig.parked.empty());
    CHECK(rig.released == std::vector<SystemId>{5});

    // A core stays with its node id: the same token under another id finds nothing parked.
    const auto token = rig.park(mgb(6));
    const auto gb = [&](const char* id) {
        return std::string(R"({"nodes":[{"id":")") + id + R"(","type":"rp.sameboy.gb","settings":{"core":)" + std::to_string(token) +
               R"(}}],"wires":[]})";
    };
    REQUIRE(rig.set(gb("one")).ok);
    CHECK_FALSE(rig.set(gb("two")).ok);
    CHECK(rig.set(gb("one")).ok);
}

// ---- inputs ----------------------------------------------------------------------------------------------------
// Each is the core call it maps to, made before the core runs, as the kernel stage made it before. The graph core
// hears its input through the node (a feed node's output wired to the input of the same name), the direct core
// through the call itself.

namespace {

using rp::host::test::Feed;
using rp::host::test::Probe;

// A system node "sys" of `type` on its core's token, its mix on a stereo host pair, and the feed's outputs wired to
// the system inputs of the same names.
std::string fedDocument(const char* type, std::uint64_t token, const std::vector<std::string>& inputs) {
    std::string wires = R"({"from":"sys.mix","to":"out.out.1-2"})";
    for (const auto& in : inputs) wires += R"(,{"from":"feed.)" + in + R"(","to":"sys.)" + in + R"("})";
    return R"({"nodes":[{"id":"feed","type":"rp.test.feed"},{"id":"sys","type":")" + std::string(type) +
           R"(","settings":{"core":)" + std::to_string(token) +
           R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],"wires":[)" + wires + "]}";
}

// `send` once, ahead of the first block at or past `seconds`.
std::function<void(std::uint64_t)> once(double seconds, std::function<void()> send) {
    return [seconds, send = std::move(send), sent = false](std::uint64_t at) mutable {
        if (sent || at < static_cast<std::uint64_t>(seconds * kRate)) return;
        send();
        sent = true;
    };
}

::MidiEvent midiEvent(std::uint32_t frame, const std::vector<std::uint8_t>& bytes) {
    ::MidiEvent ev{};
    ev.frame = frame;
    ev.size = static_cast<std::uint32_t>(bytes.size());
    for (std::size_t j = 0; j < bytes.size() && j < ::MidiEvent::kDataSize; ++j) ev.data[j] = bytes[j];
    return ev;
}

const std::vector<std::uint8_t> kNoteOn60{0x90, 60, 100};
const std::vector<std::uint8_t> kNoteOn64{0x90, 64, 100};
const std::vector<std::uint8_t> kNoteOff72{0x80, 72, 0};  // a note nothing plays, so nothing of its own to hear

}  // namespace

TEST_CASE("a system node's inputs reach its core as the calls they map to, bit for bit", "[engine-graph][system-node][inputs]") {
    // `heard`: where the direct core first differed from an identical one that heard nothing, so the input was
    // audible, and the graph core matching the direct one means the node delivered it.
    std::uint64_t heard = kNever;

    SECTION("serialIn: one pushSerialIn per byte, at the message's frame") {
        NodeRig rig(kRate);
        auto direct = mgb(1), control = mgb(1);
        REQUIRE(rig.set(fedDocument("rp.sameboy.gb", rig.park(mgb(1)), {"serialIn"})).ok);
        Feed& feed = rig.node<Feed>("feed");
        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
        CHECK(compare(rig, d, 4.0, once(2.0, [&] {
                  feed.next[Feed::SerialIn].push_back({10, kNoteOn60});
                  for (std::uint8_t b : kNoteOn60) direct->pushSerialIn(10, b);
              }), &c, &heard) == "");
        CHECK(heard != kNever);
    }
    SECTION("midi: a channel message through onMidi") {
        NodeRig rig(kRate);
        auto direct = nes(1, 0), control = nes(1, 0);
        REQUIRE(rig.set(fedDocument("rp.mesen.nes", rig.park(nes(1, 0)), {"midi"})).ok);
        Feed& feed = rig.node<Feed>("feed");
        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
        CHECK(compare(rig, d, 3.0, once(1.5, [&] {
                  feed.next[Feed::Midi].push_back({20, kNoteOn60});
                  const ::MidiEvent ev = midiEvent(20, kNoteOn60);
                  direct->onMidi(&ev, 1);
              }), &c, &heard) == "");
        CHECK(heard != kNever);
    }
    SECTION("midi: a message longer than a MidiEvent holds goes down the core's byte path, unframed") {
        NodeRig rig(kRate);
        auto direct = nes(1, 0), control = nes(1, 0);
        REQUIRE(rig.set(fedDocument("rp.mesen.nes", rig.park(nes(1, 0)), {"midi"})).ok);
        Feed& feed = rig.node<Feed>("feed");
        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
        const std::vector<std::uint8_t> run{0x90, 60, 100, 0x90, 64, 100};  // two notes as one run
        CHECK(compare(rig, d, 3.0, once(1.5, [&] {
                  feed.next[Feed::Midi].push_back({0, run});
                  direct->pushCoreBytes(0, run.data(), run.size(), false);
              }), &c, &heard) == "");
        CHECK(heard != kNever);
    }
    SECTION("bytes: pushCoreBytes, the flush flag riding RP_MESSAGE_FLUSH") {
        NodeRig rig(kRate);
        auto direct = nes(1, 0), control = nes(1, 0);
        REQUIRE(rig.set(fedDocument("rp.mesen.nes", rig.park(nes(1, 0)), {"bytes"})).ok);
        Feed& feed = rig.node<Feed>("feed");
        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
        // A note, then a flushed message that drops it before it plays; a second, unflushed note plays.
        auto first = once(1.5, [&] {
            feed.next[Feed::Bytes].push_back({0, kNoteOn60});
            feed.next[Feed::Bytes].push_back({10, kNoteOff72, RP_MESSAGE_FLUSH});
            direct->pushCoreBytes(0, kNoteOn60.data(), kNoteOn60.size(), false);
            direct->pushCoreBytes(10, kNoteOff72.data(), kNoteOff72.size(), true);
        });
        auto second = once(2.5, [&] {
            feed.next[Feed::Bytes].push_back({0, kNoteOn64});
            direct->pushCoreBytes(0, kNoteOn64.data(), kNoteOn64.size(), false);
        });
        CHECK(compare(rig, d, 3.5, [&](std::uint64_t at) { first(at); second(at); }, &c, &heard) == "");
        CHECK(heard >= static_cast<std::uint64_t>(2.5 * kRate));
        CHECK(heard != kNever);
    }
    SECTION("buttons: [button, down] through pressButton") {
        NodeRig rig(kRate);
        auto direct = sms(1), control = sms(1);
        auto viaGraph = sms(1);
        SystemBase* graphCore = viaGraph.get();
        REQUIRE(rig.set(fedDocument("rp.mesen.sms", rig.park(std::move(viaGraph)), {"buttons"})).ok);
        Feed& feed = rig.node<Feed>("feed");
        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
        // smsStart's song in every core, its PAUSE held (through the input, for the graph core) only where it plays.
        const auto pause = static_cast<std::uint8_t>(SmsButton::Start);
        auto start = once(3.0, [&] {
            for (SystemBase* s : {static_cast<SystemBase*>(direct.get()), graphCore, static_cast<SystemBase*>(control.get())})
                pokeMetronome(*s);
            feed.next[Feed::Buttons].push_back({0, {pause, 1}});
            direct->pressButton(pause, true);
        });
        auto release = once(3.6, [&] {
            feed.next[Feed::Buttons].push_back({0, {pause, 0}});
            direct->pressButton(pause, false);
        });
        CHECK(compare(rig, d, 5.0, [&](std::uint64_t at) { start(at); release(at); }, &c, &heard) == "");
        CHECK(heard != kNever);
    }
    SECTION("in fan-out order: midi before bytes, so a flushed message drops the MIDI queued ahead of it") {
        // The one order a core can show: on the NES both land in the N8 FIFO. serialIn goes first and buttons last,
        // as the kernel stage fanned them out, but no core takes two of those in one queue.
        NodeRig rig(kRate);
        auto direct = nes(1, 0), control = nes(1, 0);
        REQUIRE(rig.set(fedDocument("rp.mesen.nes", rig.park(nes(1, 0)), {"midi", "bytes"})).ok);
        Feed& feed = rig.node<Feed>("feed");
        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
        auto first = once(1.5, [&] {
            feed.next[Feed::Midi].push_back({0, kNoteOn60});
            feed.next[Feed::Bytes].push_back({0, kNoteOff72, RP_MESSAGE_FLUSH});
            const ::MidiEvent ev = midiEvent(0, kNoteOn60);
            direct->onMidi(&ev, 1);
            direct->pushCoreBytes(0, kNoteOff72.data(), kNoteOff72.size(), true);
        });
        auto second = once(2.5, [&] {
            feed.next[Feed::Midi].push_back({0, kNoteOn64});
            const ::MidiEvent ev = midiEvent(0, kNoteOn64);
            direct->onMidi(&ev, 1);
        });
        CHECK(compare(rig, d, 3.5, [&](std::uint64_t at) { first(at); second(at); }, &c, &heard) == "");
        CHECK(heard >= static_cast<std::uint64_t>(2.5 * kRate));
        CHECK(heard != kNever);
    }
}

TEST_CASE("a system node skips an empty midi or bytes message and a short buttons payload, and takes what follows",
          "[engine-graph][system-node][inputs]") {
    // As the kernel stage skipped them: nothing reaches the core for the message skipped, and the input's later
    // messages still do. `heard` as above.
    std::uint64_t heard = kNever;

    SECTION("midi: an empty message, then a note") {
        NodeRig rig(kRate);
        auto direct = nes(1, 0), control = nes(1, 0);
        REQUIRE(rig.set(fedDocument("rp.mesen.nes", rig.park(nes(1, 0)), {"midi"})).ok);
        Feed& feed = rig.node<Feed>("feed");
        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
        CHECK(compare(rig, d, 3.0, once(1.5, [&] {
                  feed.next[Feed::Midi] = {{0, {}}, {20, kNoteOn60}};
                  const ::MidiEvent ev = midiEvent(20, kNoteOn60);
                  direct->onMidi(&ev, 1);
              }), &c, &heard) == "");
        CHECK(heard != kNever);
    }
    SECTION("bytes: an empty flushed message, then a note") {
        NodeRig rig(kRate);
        auto direct = nes(1, 0), control = nes(1, 0);
        REQUIRE(rig.set(fedDocument("rp.mesen.nes", rig.park(nes(1, 0)), {"bytes"})).ok);
        Feed& feed = rig.node<Feed>("feed");
        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
        CHECK(compare(rig, d, 3.0, once(1.5, [&] {
                  feed.next[Feed::Bytes] = {{0, {}, RP_MESSAGE_FLUSH}, {0, kNoteOn60}};
                  direct->pushCoreBytes(0, kNoteOn60.data(), kNoteOn60.size(), false);
              }), &c, &heard) == "");
        CHECK(heard != kNever);
        CHECK(rig.traced.size() == 1);  // the note's bytes, and nothing for the empty message
    }
    SECTION("buttons: a payload shorter than [button, down] presses nothing") {
        // Two one-byte payloads, PAUSE's code then a 1: read past its end, the first would be [PAUSE, 1], a press
        // that starts smsStart's song. Every core gets the song, and none a press, so all three render alike.
        NodeRig rig(kRate);
        auto direct = sms(1), control = sms(1);
        auto viaGraph = sms(1);
        SystemBase* graphCore = viaGraph.get();
        REQUIRE(rig.set(fedDocument("rp.mesen.sms", rig.park(std::move(viaGraph)), {"buttons"})).ok);
        Feed& feed = rig.node<Feed>("feed");
        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
        const auto pause = static_cast<std::uint8_t>(SmsButton::Start);
        CHECK(compare(rig, d, 5.0, once(3.0, [&] {
                  for (SystemBase* s : {static_cast<SystemBase*>(direct.get()), graphCore, static_cast<SystemBase*>(control.get())})
                      pokeMetronome(*s);
                  feed.next[Feed::Buttons] = {{0, {pause}}, {0, {1}}};
              }), &c, &heard) == "");
        CHECK(heard == kNever);
    }
}

TEST_CASE("a linked Game Boy takes its inputs in prepare, before its group steps", "[engine-graph][system-node][inputs]") {
    // A coupled member is never processed, only prepared, stepped and finished, so its inputs are applied at the top
    // of prepare. Seen in the core's queues right after the block: a linked core's serial port belongs to the cable,
    // so the byte is still queued; the press was taken while the group stepped, so it was queued before the first step.
    NodeRig rig(kRate);
    auto d1 = mgb(1), d2 = mgb(2);
    d1->linkPeers_ = {d2.get()};
    d2->linkPeers_ = {d1.get()};
    auto g1 = mgb(1);
    SameBoySystem* const graph1 = g1.get();
    const auto t1 = rig.park(std::move(g1)), t2 = rig.park(mgb(2));
    REQUIRE(rig.set(R"({"nodes":[{"id":"feed","type":"rp.test.feed"},{"id":"a","type":"rp.sameboy.gb","settings":{"core":)" +
                    std::to_string(t1) + R"(}},{"id":"b","type":"rp.sameboy.gb","settings":{"core":)" + std::to_string(t2) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2,2]}}],"wires":[
                        {"from":"a.link","to":"b.link"},{"from":"a.mix","to":"out.out.1-2"},{"from":"b.mix","to":"out.out.3-4"},
                        {"from":"feed.serialIn","to":"a.serialIn"},{"from":"feed.buttons","to":"a.buttons"}]})")
                .ok);
    Feed& feed = rig.node<Feed>("feed");
    Direct d({d1.get(), d2.get()}, 4, {{0, 1}, {2, 3}});
    Lanes graph(4);
    const rp_transport t{kRate, 120.0, 0.0, 0};
    const auto start = static_cast<std::uint8_t>(GameboyButton::Start);
    for (int b = 0; b < 48; ++b) {
        const bool send = b == 40;
        if (send) {
            feed.next[Feed::SerialIn].push_back({0, {0x90}});
            feed.next[Feed::Buttons].push_back({0, {start, 1}});
            d1->pushSerialIn(0, 0x90);
            d1->pressButton(start, true);
        }
        rig.host->processBlock(1024, t, graph.ptrs.data(), 4);
        d.block(1024);
        for (std::size_t c = 0; c < 4; ++c) REQUIRE(std::memcmp(graph.data[c].data(), d.out.data[c].data(), 1024 * sizeof(float)) == 0);
        if (!send) continue;
        REQUIRE(graph1->serialIn_.size() == 1);
        REQUIRE(d1->serialIn_.size() == 1);
        CHECK(graph1->serialIn_.front().byte == 0x90);
        CHECK(graph1->serialIn_.front().offset == d1->serialIn_.front().offset);
        CHECK(graph1->pendingButtons_.empty());
        CHECK(d1->pendingButtons_.empty());
    }
}

TEST_CASE("the Game Boy's serialOut carries what its core captured this block, each byte at the frame its core "
          "completed it, and leaves the log",
          "[engine-graph][system-node][inputs]") {
    // mGB holds its serial port in external-clock receive, so once MI.OUT capture is armed the engine clocks a byte
    // out of it every eight frames. Each byte goes out a byte a message at the frame the core completed it, a byte
    // stamped at or past the block's end (completed in its last sample, or past it) at the block's last frame. The
    // node copies the core's log rather than draining it: the core clears it at its next prepareForBlock.
    NodeRig rig(kRate);
    auto core = mgb(1);
    core->setSerialOutCapture(SerialOutCapture::Drive);
    SameBoySystem* const gb = core.get();
    REQUIRE(rig.set(R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb","settings":{"core":)" + std::to_string(rig.park(std::move(core))) +
                    R"(}},{"id":"probe","type":"rp.test.probe"},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"gb.mix","to":"out.out.1-2"},{"from":"gb.serialOut","to":"probe.bytes"}]})")
                .ok);
    Probe& probe = rig.node<Probe>("probe");
    Lanes graph(2);
    const rp_transport t{kRate, 120.0, 0.0, 0};
    std::size_t captured = 0, atOrPastEnd = 0;
    for (std::size_t b = 0; b < 160; ++b) {
        const std::uint32_t frames = kBlocks[b % kBlocks.size()];
        rig.host->processBlock(frames, t, graph.ptrs.data(), 2);
        std::vector<std::pair<std::uint32_t, std::uint8_t>> log, carried;
        for (const auto& [frame, byte] : gb->serialOutLog_) {
            log.emplace_back(std::min(frame, frames - 1), byte);
            atOrPastEnd += frame >= frames;
        }
        bool eachAlone = true, inside = true;
        for (const auto& m : probe.bytes) {
            eachAlone = eachAlone && m.data.size() == 1;
            inside = inside && m.frame < frames;
            if (!m.data.empty()) carried.emplace_back(m.frame, m.data[0]);
        }
        INFO("block " << b);
        CHECK(eachAlone);
        CHECK(inside);  // every message lies inside the block that carries it
        CHECK(carried == log);
        captured += log.size();
    }
    CHECK(captured > 0);
    CHECK(atOrPastEnd > 0);  // the core stamped a byte at or past a block's end, so the clamp ran
}

TEST_CASE("a Game Boy whose capture listens is never clocked: mGB, waiting on an external clock, sends nothing",
          "[engine-graph][system-node][inputs]") {
    // Listen captures only the bytes a Game Boy clocks out itself (master sync's LSDj). mGB holds its port in
    // external-clock receive, which Drive clocks a bit an audio frame (the case above); listening, nothing clocks it.
    NodeRig rig(kRate);
    auto core = mgb(1);
    core->setSerialOutCapture(SerialOutCapture::Listen);
    SameBoySystem* const gb = core.get();
    REQUIRE(rig.set(R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb","settings":{"core":)" + std::to_string(rig.park(std::move(core))) +
                    R"(}},{"id":"probe","type":"rp.test.probe"},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"gb.mix","to":"out.out.1-2"},{"from":"gb.serialOut","to":"probe.bytes"}]})")
                .ok);
    Probe& probe = rig.node<Probe>("probe");
    Lanes graph(2);
    const rp_transport t{kRate, 120.0, 0.0, 0};
    for (std::size_t b = 0; b < 160; ++b) {
        rig.host->processBlock(kBlocks[b % kBlocks.size()], t, graph.ptrs.data(), 2);
        INFO("block " << b);
        CHECK(gb->serialOutLog_.empty());
        CHECK(probe.bytes.empty());
    }
}

TEST_CASE("a system node records what its bytes input feeds the core in the engine's sync trace", "[engine-graph][system-node][inputs]") {
    // As the kernel stage recorded its core-bytes sink: the bytes input only, an empty message skipped, and a long
    // MIDI message (which also goes down the core's byte path) not at all.
    NodeRig rig(kRate);
    REQUIRE(rig.set(fedDocument("rp.mesen.nes", rig.park(nes(1, 0)), {"midi", "bytes"})).ok);
    Feed& feed = rig.node<Feed>("feed");
    feed.next[Feed::Bytes] = {{5, {1, 2, 3}}, {7, {}}};
    feed.next[Feed::Midi] = {{0, {0x90, 60, 100, 0x90, 64, 100}}};
    Lanes graph(2);
    const rp_transport t{kRate, 120.0, 0.0, 0};
    rig.host->processBlock(64, t, graph.ptrs.data(), 2);
    REQUIRE(rig.traced.size() == 1);
    CHECK(rig.traced[0].frame == 5);
    CHECK(rig.traced[0].data == std::vector<std::uint8_t>{1, 2, 3});
}
