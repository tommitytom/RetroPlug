// SystemNode<T> (docs/modular-graph-phase-b1-design.md section 3.1): a core run through its graph node must
// render exactly what an identical core renders driven directly through prepareForBlock /
// stepIfBelowTarget / finishBlock into zeroed lanes, bit for bit, for a lone system and a linked pair,
// across block sizes. Both cores power on in the same state (Deterministic.cpp) and hear the same input.
// The node's inputs (docs/modular-graph-phase-b3a-design.md section 3.1) are held the same way: what reaches a
// node's input must render what the core call it maps to renders, made directly; and the Game Boy's serialOut
// must carry what its core captured. So are its settings' knobs (docs/modular-graph-phase-c2-design.md sections 3 and
// 4): each must reach the core as the core's own setter, called directly at the start of the block of the plan that
// carries it, or at activation, renders it, and the descriptors must spell the labels the role configs do. Its params
// too: each value must reach the core as its setter, called directly before the core's first block or at the start
// of the block a change lands in, renders it.
//
// The GBA and LSDj cases use sibling fixture ROMs, so each leaves on a run without the resources tree (as CI's) rather
// than failing there.
//
// Run via `pnpm test:plugin`.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "../graph/CountingAllocator.hpp"
#include "EmbeddedRoms.hpp"
#include "GraphDocs.hpp"
#include "NodeRig.hpp"
#include "graph/CanonicalJson.hpp"
#include "system/mesen/MesenGbaSystem.hpp"
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

// These `direct` helpers construct their core classes rather than going through the backend's typed build, on purpose:
// a node's core crosses the backend (ROM gate, seeds, onActivate, the state snapshot) while one built here takes the same
// config straight to the class, so a twin holds both to account, and it is what the twins have always compared against.
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

// smsggdj reads its options from the battery at boot: an empty SMDJ4 image with the v1 block ('C' 'F', then PAL / SYNC /
// video / FM off, then their sum) at $3F60 of a 32 KB image, SYNC off so the song free-runs (smsSyncSong.ts).
std::vector<std::uint8_t> smsBattery() {
    std::vector<std::uint8_t> sram(32 * 1024, 0);
    const std::uint8_t header[] = {'S', 'M', 'D', 'J', '4', 1, 32}, options[] = {0x43, 0x46, 0, 0, 0, 0, 0};
    std::memcpy(sram.data(), header, sizeof header);
    std::memcpy(sram.data() + 0x3f60, options, sizeof options);
    return sram;
}

std::unique_ptr<MesenSmsSystem> sms(SystemId id) {
    MesenSmsConfig cfg;
    cfg.romPath = RP_SMS_ROM_PATH;
    cfg.enableFm = false;  // as the TS side builds it for smsggdj
    cfg.sram = smsBattery();
    auto sys = std::make_unique<MesenSmsSystem>(id, cfg, readFile(RP_SMS_ROM_PATH));
    sys->onActivate(kRate);
    return sys;
}

// The settings a node needs to build what each `direct` helper builds. mgb() is a DMG with no fast boot: the boot chime
// is the signal these cases compare, so a node that must match it names both.
constexpr const char* kGbDmg = R"("model":"dmgB","fastBoot":false)";
// kGbDmg plus further settings members, which must not repeat one it has.
std::string gbDmg(const std::string& extra) { return extra.empty() ? std::string(kGbDmg) : std::string(kGbDmg) + "," + extra; }
// The NES's export mode, as the label its node reads.
const char* nesMode(std::uint32_t mode) {
    switch (mode) {
        case 1: return R"("channelExportMode":"stereoModPins")";
        case 2: return R"("channelExportMode":"pinsPlusRef")";
        case 3: return R"("channelExportMode":"individualMono")";
        default: return "";
    }
}
// What sms() builds: FM off, its SMDJ4 battery as an `sram` seed, and the ROM's stem (the battery a staged Master
// System file takes its name from).
std::string smsNode(NodeRig& rig, SystemId id, const std::string& extra = "") {
    const std::uint32_t sram = rig.put("sram", smsBattery());
    return rig.boot("rp.mesen.sms", id, R"("enableFm":false,"romName":"smsggdj_v0_45","sram":"res:)" +
                                          std::to_string(sram) + "\"" + (extra.empty() ? "" : "," + extra));
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
    // The cores by pointer: a lambda that captured the references themselves would keep the stack slots of this call,
    // not the cores.
    return [a = &a, b = &b, sent = false](std::uint64_t at) mutable {
        if (sent || at < static_cast<std::uint64_t>(1.5 * kRate)) return;
        noteOn(*a, 0, 60);
        noteOn(*b, 0, 60);
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
    // The cores by pointer, as afterBoot keeps them.
    return [a = &a, b = &b, stage = 0](std::uint64_t at) mutable {
        const auto pause = static_cast<std::uint8_t>(SmsButton::Start);
        if (stage == 0 && at >= static_cast<std::uint64_t>(3.0 * kRate)) {
            for (SystemBase* s : {a, b}) {
                pokeMetronome(*s);
                s->pressButton(pause, true);
            }
            stage = 1;
        } else if (stage == 1 && at >= static_cast<std::uint64_t>(3.6 * kRate)) {
            a->pressButton(pause, false);
            b->pressButton(pause, false);
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
            if (std::memcmp(graph.data[c].data(), direct.out.data[c].data(), frames * sizeof(float)) != 0) {
                std::uint32_t at0 = 0;
                while (at0 < frames && graph.data[c][at0] == direct.out.data[c][at0]) ++at0;
                return "block " + std::to_string(b) + " (" + std::to_string(frames) + " frames), channel " + std::to_string(c) +
                       " from frame " + std::to_string(at0) + ": " + std::to_string(graph.data[c][at0]) + " vs " +
                       std::to_string(direct.out.data[c][at0]);
            }
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
        REQUIRE(rig.set(R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb","settings":{)" + rig.boot("rp.sameboy.gb", 1, kGbDmg) +
                        R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                            "wires":[{"from":"gb.mix","to":"out.out.1-2"}]})").ok);
        Direct d({direct.get()}, 2, {{0, 1}});
        CHECK(compare(rig, d, 3.0) == "");
        CHECK(sounds(d));
    }
    SECTION("stems") {
        NodeRig rig(kRate);
        auto direct = mgb(1);
        std::string wires;
        for (int k = 1; k <= 4; ++k)
            wires += std::string(k > 1 ? "," : "") + R"({"from":"gb.ch)" + std::to_string(k) + R"(","to":"out.out.)" +
                     std::to_string(2 * k - 1) + "-" + std::to_string(2 * k) + R"("})";
        REQUIRE(rig.set(R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb","settings":{)" + rig.boot("rp.sameboy.gb", 1, kGbDmg) +
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
    const auto doc = [](const std::string& a, const std::string& b, bool linked) {
        return R"({"nodes":[{"id":"a","type":"rp.sameboy.gb","settings":{)" + a +
               R"(}},{"id":"b","type":"rp.sameboy.gb","settings":{)" + b +
               R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2,2]}}],"wires":[)" +
               (linked ? R"({"from":"a.link","to":"b.link"},)" : "") +
               R"({"from":"a.mix","to":"out.out.1-2"},{"from":"b.mix","to":"out.out.3-4"}]})";
    };
    NodeRig rig(kRate);
    auto d1 = mgb(1), d2 = mgb(2);
    d1->linkPeers_ = {d2.get()};
    d2->linkPeers_ = {d1.get()};
    const std::string a = rig.boot("rp.sameboy.gb", 1, kGbDmg), b = rig.boot("rp.sameboy.gb", 2, kGbDmg);
    REQUIRE(rig.set(doc(a, b, true)).ok);
    auto* const graph1 = dynamic_cast<SameBoySystem*>(rig.builtCore(1));
    auto* const graph2 = dynamic_cast<SameBoySystem*>(rig.builtCore(2));
    REQUIRE(graph1 != nullptr);
    REQUIRE(graph2 != nullptr);
    // The document's link wire filled each graph core's peer list with the other.
    CHECK(graph1->linkPeers_ == std::vector<SameBoySystem*>{graph2});
    CHECK(graph2->linkPeers_ == std::vector<SameBoySystem*>{graph1});
    Direct d({d1.get(), d2.get()}, 4, {{0, 1}, {2, 3}});
    CHECK(compare(rig, d, 4.0, sendAt({d1.get(), d2.get(), graph1, graph2}, 2.0)) == "");
    CHECK(sounds(d));

    // A document without the wire unlinks them again.
    REQUIRE(rig.set(doc(a, b, false)).ok);
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
        REQUIRE(rig.set(R"({"nodes":[{"id":"nes","type":"rp.mesen.nes","settings":{)" + rig.boot("rp.mesen.nes", 1) +
                        R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                            "wires":[{"from":"nes.mix","to":"out.out.1-2"}]})").ok);
        SystemBase* const graphCore = rig.builtCore(1);
        REQUIRE(graphCore != nullptr);
        Direct d({direct.get()}, 2, {{0, 1}});
        CHECK(compare(rig, d, 3.0, afterBoot(*direct, *graphCore)) == "");
        CHECK(sounds(d));
    }
    SECTION("pins") {
        NodeRig rig(kRate);
        auto direct = nes(1, 1);
        REQUIRE(rig.set(R"({"nodes":[{"id":"nes","type":"rp.mesen.nes","settings":{)" + rig.boot("rp.mesen.nes", 1, nesMode(1)) +
                        R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[1,1,1]}}],
                            "wires":[{"from":"nes.pins.pulse","to":"out.out.1"},{"from":"nes.pins.tnd","to":"out.out.2"},
                                     {"from":"nes.pins.expansion","to":"out.out.3"}]})").ok);
        SystemBase* const graphCore = rig.builtCore(1);
        REQUIRE(graphCore != nullptr);
        Direct d({direct.get()}, 3, {{0, 0, 1, 1, 2, 2}});  // a mono stream's two lanes are one buffer
        CHECK(compare(rig, d, 3.0, afterBoot(*direct, *graphCore)) == "");
        CHECK(sounds(d));
    }
}

TEST_CASE("a lone SMS node renders its core's mix bit for bit", "[engine-graph][system-node]") {
    NodeRig rig(kRate);
    auto direct = sms(1);
    REQUIRE(rig.set(R"({"nodes":[{"id":"sms","type":"rp.mesen.sms","settings":{)" + smsNode(rig, 1) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"sms.mix","to":"out.out.1-2"}]})").ok);
    SystemBase* const graphCore = rig.builtCore(1);
    REQUIRE(graphCore != nullptr);
    Direct d({direct.get()}, 2, {{0, 1}});
    CHECK(compare(rig, d, 5.0, smsStart(*direct, *graphCore)) == "");
    CHECK(sounds(d));
}

TEST_CASE("a system node builds its core once, and destroys it when the plan that ran it goes",
          "[engine-graph][system-node]") {
    NodeRig rig(kRate);
    const std::string doc = R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb","settings":{)" +
                            rig.boot("rp.sameboy.gb", 7, kGbDmg) + R"(}}],"wires":[]})";
    REQUIRE(rig.set(doc).ok);
    REQUIRE(rig.builtCore(7) != nullptr);
    CHECK(rig.built.size() == 1);
    REQUIRE(rig.set(doc).ok);  // the same document reuses the instance and its core
    CHECK(rig.released.empty());
    CHECK(rig.built.size() == 1);
    REQUIRE(rig.set(R"({"nodes":[],"wires":[]})").ok);
    rig.host->reclaim();
    CHECK(rig.released == std::vector<SystemId>{7});
    CHECK(rig.built.empty());
}

namespace {

// A lone NES node with `ports` (output ports of rp.mesen.nes) wired to mono host channels in order, driven
// against an identical core in `mode`; `lanes` is the direct core's host channel per finishBlock lane.
void nesTapCase(std::uint32_t mode, const std::vector<std::string>& ports, std::size_t channels,
                const std::vector<int>& lanes) {
    NodeRig rig(kRate);
    auto direct = nes(1, mode);
    std::string wires, buses;
    for (std::size_t k = 0; k < ports.size(); ++k) {
        wires += std::string(k ? "," : "") + R"({"from":"nes.)" + ports[k] + R"(","to":"out.out.)" + std::to_string(k + 1) + R"("})";
        buses += std::string(k ? "," : "") + "1";
    }
    REQUIRE(rig.set(R"({"nodes":[{"id":"nes","type":"rp.mesen.nes","settings":{)" + rig.boot("rp.mesen.nes", 1, nesMode(mode)) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[)" + buses + R"(]}}],"wires":[)" + wires + "]}")
                .ok);
    SystemBase* const graphCore = rig.builtCore(1);
    REQUIRE(graphCore != nullptr);
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
    REQUIRE(rig.set(R"({"nodes":[{"id":"nes","type":"rp.mesen.nes","settings":{)" + rig.boot("rp.mesen.nes", 1, nesMode(1)) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2,1]}}],
                        "wires":[{"from":"nes.mix","to":"out.out.1-2"},{"from":"nes.pins.pulse","to":"out.out.3"}]})").ok);
    SystemBase* const graphCore = rig.builtCore(1);
    REQUIRE(graphCore != nullptr);
    // Two lanes only, as on the narrow-host path: the connected pin is written as silence.
    Direct d({direct.get()}, 3, {{0, 1}});
    CHECK(compare(rig, d, 3.0, afterBoot(*direct, *graphCore)) == "");
    CHECK(sounds(d));
}

// ---- inputs ----------------------------------------------------------------------------------------------------
// Each is the core call it maps to, made before the core runs, as the kernel stage made it before. The graph core
// hears its input through the node (a feed node's output wired to the input of the same name), the direct core
// through the call itself.

namespace {

using rp::host::test::Feed;
using rp::host::test::Probe;

// A system node "sys" of `type`, its `settings` members (what rig.boot answers), its mix on a stereo host pair, and the
// feed's outputs wired to the system inputs of the same names.
std::string fedDocument(const char* type, const std::string& settings, const std::vector<std::string>& inputs) {
    std::string wires = R"({"from":"sys.mix","to":"out.out.1-2"})";
    for (const auto& in : inputs) wires += R"(,{"from":"feed.)" + in + R"(","to":"sys.)" + in + R"("})";
    return R"({"nodes":[{"id":"feed","type":"rp.test.feed"},{"id":"sys","type":")" + std::string(type) +
           R"(","settings":{)" + settings +
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
        REQUIRE(rig.set(fedDocument("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, kGbDmg), {"serialIn"})).ok);
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
        REQUIRE(rig.set(fedDocument("rp.mesen.nes", rig.boot("rp.mesen.nes", 1), {"midi"})).ok);
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
        REQUIRE(rig.set(fedDocument("rp.mesen.nes", rig.boot("rp.mesen.nes", 1), {"midi"})).ok);
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
        REQUIRE(rig.set(fedDocument("rp.mesen.nes", rig.boot("rp.mesen.nes", 1), {"bytes"})).ok);
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
        REQUIRE(rig.set(fedDocument("rp.mesen.sms", smsNode(rig, 1), {"buttons"})).ok);
        SystemBase* const graphCore = rig.builtCore(1);
        REQUIRE(graphCore != nullptr);
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
        REQUIRE(rig.set(fedDocument("rp.mesen.nes", rig.boot("rp.mesen.nes", 1), {"midi", "bytes"})).ok);
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
        REQUIRE(rig.set(fedDocument("rp.mesen.nes", rig.boot("rp.mesen.nes", 1), {"midi"})).ok);
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
        REQUIRE(rig.set(fedDocument("rp.mesen.nes", rig.boot("rp.mesen.nes", 1), {"bytes"})).ok);
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
        REQUIRE(rig.set(fedDocument("rp.mesen.sms", smsNode(rig, 1), {"buttons"})).ok);
        SystemBase* const graphCore = rig.builtCore(1);
        REQUIRE(graphCore != nullptr);
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
    REQUIRE(rig.set(std::string(R"({"nodes":[{"id":"feed","type":"rp.test.feed"},{"id":"a","type":"rp.sameboy.gb","settings":{)") +
                    rig.boot("rp.sameboy.gb", 1, kGbDmg) + R"(}},{"id":"b","type":"rp.sameboy.gb","settings":{)" +
                    rig.boot("rp.sameboy.gb", 2, kGbDmg) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2,2]}}],"wires":[
                        {"from":"a.link","to":"b.link"},{"from":"a.mix","to":"out.out.1-2"},{"from":"b.mix","to":"out.out.3-4"},
                        {"from":"feed.serialIn","to":"a.serialIn"},{"from":"feed.buttons","to":"a.buttons"}]})")
                .ok);
    auto* const graph1 = dynamic_cast<SameBoySystem*>(rig.builtCore(1));
    REQUIRE(graph1 != nullptr);
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
    REQUIRE(rig.set(R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb","settings":{)" +
                    rig.boot("rp.sameboy.gb", 1, R"("model":"dmgB","fastBoot":false,"serialOutCapture":"drive")") +
                    R"(}},{"id":"probe","type":"rp.test.probe"},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"gb.mix","to":"out.out.1-2"},{"from":"gb.serialOut","to":"probe.bytes"}]})")
                .ok);
    auto* const gb = dynamic_cast<SameBoySystem*>(rig.builtCore(1));
    REQUIRE(gb != nullptr);
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
    REQUIRE(rig.set(R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb","settings":{)" +
                    rig.boot("rp.sameboy.gb", 1, R"("model":"dmgB","fastBoot":false,"serialOutCapture":"listen")") +
                    R"(}},{"id":"probe","type":"rp.test.probe"},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"gb.mix","to":"out.out.1-2"},{"from":"gb.serialOut","to":"probe.bytes"}]})")
                .ok);
    auto* const gb = dynamic_cast<SameBoySystem*>(rig.builtCore(1));
    REQUIRE(gb != nullptr);
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

TEST_CASE("a capture-mode switch drops the Game Boy's half-assembled serial-out byte, and a same-mode call keeps it",
          "[engine-graph][system-node][inputs]") {
    // Drive clocks mGB's port a bit an audio frame, so a block whose length is not a multiple of eight ends with a
    // byte part-assembled. Kept across a switch, those bits would make every later byte straddle two transfers; the
    // store re-sends the mode on every lsdj-sync config edit, which must leave MI.OUT's partial byte alone.
    NodeRig rig(kRate);
    REQUIRE(rig.set(R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb","settings":{)" +
                    rig.boot("rp.sameboy.gb", 1, R"("model":"dmgB","fastBoot":false,"serialOutCapture":"drive")") +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"gb.mix","to":"out.out.1-2"}]})")
                .ok);
    auto* const gb = dynamic_cast<SameBoySystem*>(rig.builtCore(1));
    REQUIRE(gb != nullptr);
    Lanes graph(2);
    const rp_transport t{kRate, 120.0, 0.0, 0};
    bool partial = false;
    for (std::size_t b = 0; b < 160 && !partial; ++b) {
        rig.host->processBlock(kBlocks[b % kBlocks.size()], t, graph.ptrs.data(), 2);
        partial = gb->serialOutBits_ != 0;
    }
    REQUIRE(partial);
    const std::uint8_t byte = gb->serialOutByte_;
    const int bits = gb->serialOutBits_;
    gb->setSerialOutCapture(SerialOutCapture::Drive);
    CHECK(gb->serialOutByte_ == byte);
    CHECK(gb->serialOutBits_ == bits);
    gb->setSerialOutCapture(SerialOutCapture::Listen);
    CHECK(gb->serialOutByte_ == 0);
    CHECK(gb->serialOutBits_ == 0);
}

TEST_CASE("a system node records what its bytes input feeds the core in the engine's sync trace", "[engine-graph][system-node][inputs]") {
    // As the kernel stage recorded its core-bytes sink: the bytes input only, an empty message skipped, and a long
    // MIDI message (which also goes down the core's byte path) not at all.
    NodeRig rig(kRate);
    REQUIRE(rig.set(fedDocument("rp.mesen.nes", rig.boot("rp.mesen.nes", 1), {"midi", "bytes"})).ok);
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

// ---- settings ----------------------------------------------------------------------------------------------------
// A system node's knobs (docs/modular-graph-phase-c2-design.md sections 3 and 4) are its settings beside the `system`
// it names: each reaches the core at the start of the first block of the plan that carries it, as the call the command
// ring made (the core's own value-guarded setter), and everything present at activation, before the core first runs. A
// knob absent leaves the core as it was. Compared, where the knob is audible, against an identical core given the same
// call directly at the same block: bit for bit, so the knob landed in that block and restarted nothing the direct call
// did not.

namespace {

// A system node "sys" of `type`, its `settings` members (what rig.boot answers, the system and ROM included), its param
// values (JSON members, or none), its mix on a stereo host pair; a Game Boy's serialOut on a probe.
std::string knobbed(const char* type, const std::string& settings, const std::string& params = "") {
    const bool gb = std::string(type) == "rp.sameboy.gb";
    return R"({"nodes":[{"id":"sys","type":")" + std::string(type) + R"(","settings":{)" + settings +
           "}" + (params.empty() ? "" : R"(,"params":{)" + params + "}") + "}," +
           (gb ? R"({"id":"probe","type":"rp.test.probe"},)" : "") +
           R"({"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],"wires":[{"from":"sys.mix","to":"out.out.1-2"})" +
           (gb ? R"(,{"from":"sys.serialOut","to":"probe.bytes"})" : "") + "]}";
}

std::string codes(const rp::graph::SetGraphResult& r) {
    std::string s;
    for (const auto& e : r.errors) s += (s.empty() ? "" : ",") + e.code;
    return s;
}

void run(NodeRig& rig, std::uint32_t frames = 512, int blocks = 1) {
    Lanes out(2);
    const rp_transport t{kRate, 120.0, 0.0, 0};
    for (int b = 0; b < blocks; ++b) rig.host->processBlock(frames, t, out.ptrs.data(), 2);
}

// The quoted strings of `export const <name> = [...] as const;` in packages/retroplug/src/settingsEnums.ts.
std::vector<std::string> tsValues(const std::string& name) {
    const auto bytes = readFile(RP_SETTINGS_ENUMS_PATH);
    const std::string ts(bytes.begin(), bytes.end());
    const std::string head = "export const " + name + " = [";
    const auto start = ts.find(head);
    if (start == std::string::npos) return {};
    const auto end = ts.find("] as const;", start);
    std::vector<std::string> values;
    for (auto q = ts.find('"', start); q < end; q = ts.find('"', q + 1)) {
        const auto close = ts.find('"', q + 1);
        values.push_back(ts.substr(q + 1, close - q - 1));
        q = close;
    }
    return values;
}

// The settings schema of a registered type, at its $defs entry: the one that is the settings struct (the rest are the
// resource refs its fields name).
rfl::Generic::Object settingsSchema(NodeRig& rig, const char* type) {
    const auto descriptor = rp::graph::parseJson(rig.registry.find(type)->type->descriptor(rig.registry.find(type)->type));
    REQUIRE(descriptor);
    const auto settings = descriptor->to_object()->get("settings")->to_object();
    REQUIRE(settings);
    const auto defs = settings->get("$defs")->to_object();
    REQUIRE(defs);
    for (const auto& [name, def] : *defs)
        if (const auto entry = def.to_object(); entry && entry->get("properties")) return *entry;
    REQUIRE(false);
    return {};
}

// A property's schema: the non-null branch of the anyOf an optional field is.
rfl::Generic::Object knobSchema(const rfl::Generic::Object& schema, const char* knob) {
    const auto property = schema.get("properties")->to_object()->get(knob)->to_object();
    REQUIRE(property);
    const auto branches = property->get("anyOf")->to_array();
    REQUIRE(branches);
    REQUIRE(branches->size() == 2);
    return *(*branches)[0].to_object();
}

// The numbers a TS field's own entry spells: `name: helper(<numbers>)`, e.g. `lightTemperature: within(-1, 1)` in
// buildGraph.ts's SETTING_FIELDS, or `expansionVolume: clampedInt(0, 200, 100)` in coreRoles.ts's mesen schema. A
// knob's entry has the range's two, a zod clamped field those and its default. A match behind a `//` is skipped, so a
// comment above the entry may spell it too; the first match outside one wins, and no match leaves the case failing on
// an empty list.
std::vector<double> tsNumbers(const char* path, const std::string& entry) {
    const auto bytes = readFile(path);
    const std::string ts(bytes.begin(), bytes.end());
    auto at = ts.find(entry);
    while (at != std::string::npos) {
        const auto lineStart = ts.rfind('\n', at);
        const auto comment = ts.find("//", lineStart == std::string::npos ? 0 : lineStart);
        if (comment == std::string::npos || comment > at) break;
        at = ts.find(entry, at + entry.size());
    }
    if (at == std::string::npos) return {};
    const auto open = ts.find('(', at);
    const auto close = ts.find(')', open);
    if (open == std::string::npos || close == std::string::npos) return {};
    const std::string args = ts.substr(open + 1, close - open - 1);
    std::vector<double> out;
    for (const char* p = args.c_str(); *p;) {
        char* end = nullptr;
        const double value = std::strtod(p, &end);
        if (end == p) break;
        out.push_back(value);
        p = end;
        while (*p == ' ' || *p == ',') ++p;
    }
    return out;
}

// The range a knob's numbers spell: its first two, whether or not a default follows.
std::vector<double> tsRange(const char* path, const std::string& entry) {
    const auto numbers = tsNumbers(path, entry);
    return numbers.size() >= 2 ? std::vector<double>{numbers[0], numbers[1]} : std::vector<double>{};
}

// The number an exported TS constant spells, e.g. systemSettings.ts's `export const GAIN_MIN = -90;`.
double tsConstant(const char* path, const std::string& name) {
    const auto bytes = readFile(path);
    const std::string ts(bytes.begin(), bytes.end());
    const auto at = ts.find("export const " + name + " =");
    if (at == std::string::npos) return 0.0;
    return std::strtod(ts.c_str() + ts.find('=', at) + 1, nullptr);
}

std::vector<std::string> labelsOf(const rfl::Generic::Object& knob) {
    std::vector<std::string> labels;
    const auto values = knob.get("enum")->to_array();
    REQUIRE(values);
    for (const auto& v : *values) labels.push_back(*v.to_string());
    return labels;
}

}  // namespace

TEST_CASE("every system node prepares statelessly and rebuilds only for a new core, so setGraph never waits on one",
          "[engine-graph][system-node][settings]") {
    NodeRig rig(kRate);
    for (const char* type : {"rp.sameboy.gb", "rp.mesen.nes", "rp.mesen.gba", "rp.mesen.sms"}) {
        INFO(type);
        CHECK(rig.registry.find(type)->type->flags == RP_TYPE_STATELESS_PREPARE);
    }
    REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, kGbDmg))).ok);
    const auto* const gb = dynamic_cast<const SameBoySystem*>(rig.builtCore(1));
    REQUIRE(gb != nullptr);
    run(rig);
    rp_node* const before = rig.host->node("sys");
    rig.host->setAudioThreadOwns(true);  // and no block runs until the end, so nothing is adopted
    // Two edits, the second planned while the first is unadopted: neither waits (the adoption timeout is 2 s).
    const auto start = std::chrono::steady_clock::now();
    REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, gbDmg(R"("dmgPalette":"dmg")")))).ok);
    REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, gbDmg(R"("dmgPalette":"gbl","highpass":"off")")))).ok);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500));
    CHECK(rig.host->node("sys") == before);
    run(rig);  // adopts both, in order, and the node applies the last
    CHECK(gb->config_.dmgPalette == SameBoyDmgPalette::Gbl);
    CHECK(gb->config_.highpass == SameBoyHighpass::Off);
    rig.host->setAudioThreadOwns(false);
}

TEST_CASE("a system node's enum knobs take the labels the role configs spell, and its numbers their ranges",
          "[engine-graph][system-node][settings]") {
    NodeRig rig(kRate);
    const auto gb = settingsSchema(rig, "rp.sameboy.gb");
    CHECK(labelsOf(knobSchema(gb, "model")) == tsValues("MODEL_VALUES"));
    CHECK(labelsOf(knobSchema(gb, "highpass")) == tsValues("HIGHPASS_VALUES"));
    CHECK(labelsOf(knobSchema(gb, "colorCorrection")) == tsValues("COLOR_CORRECTION_VALUES"));
    CHECK(labelsOf(knobSchema(gb, "dmgPalette")) == tsValues("DMG_PALETTE_VALUES"));
    CHECK(labelsOf(knobSchema(gb, "serialOutCapture")) == std::vector<std::string>{"off", "listen", "drive"});
    CHECK(*knobSchema(gb, "fastBoot").get("type")->to_string() == "boolean");
    const auto nesSchema = settingsSchema(rig, "rp.mesen.nes");
    CHECK(labelsOf(knobSchema(nesSchema, "region")) == tsValues("REGION_VALUES"));
    CHECK(labelsOf(knobSchema(nesSchema, "s5bNoise")) == tsValues("CARTRIDGE_ACCURACY_VALUES"));
    CHECK(labelsOf(knobSchema(nesSchema, "mmc5PhaseReset")) == tsValues("CARTRIDGE_ACCURACY_VALUES"));
    CHECK(*knobSchema(nesSchema, "removeSpriteLimit").get("type")->to_string() == "boolean");
    CHECK(tsValues("MODEL_VALUES").size() == 14);  // the reader found them
    // A range's bounds, each its own allOf branch.
    const auto bounds = [](const rfl::Generic::Object& knob) {
        std::vector<double> b;
        const auto branches = knob.get("allOf")->to_array();
        REQUIRE(branches);
        for (const auto& branch : *branches)
            for (const char* key : {"minimum", "maximum"})
                if (const auto v = branch.to_object()->get(key)) b.push_back(*v->to_double());
        return b;
    };
    // Each range is TS's, in all three copies: buildGraph's SETTING_FIELDS, which picks a value for the document, the
    // role schema's clamped field, which a stored or edited config comes through, and the node's own declaration.
    CHECK(bounds(knobSchema(gb, "lightTemperature")) == tsRange(RP_BUILD_GRAPH_TS_PATH, "lightTemperature:"));
    CHECK(bounds(knobSchema(gb, "lightTemperature")) == tsRange(RP_CORE_ROLES_TS_PATH, "lightTemperature:"));
    CHECK(bounds(knobSchema(gb, "lightTemperature")) == std::vector<double>{-1.0, 1.0});
    CHECK(bounds(knobSchema(nesSchema, "apuLatencyMs")) == tsRange(RP_BUILD_GRAPH_TS_PATH, "apuLatencyMs:"));
    CHECK(bounds(knobSchema(nesSchema, "apuLatencyMs")) == tsRange(RP_CORE_ROLES_TS_PATH, "apuLatencyMs:"));
    CHECK(bounds(knobSchema(nesSchema, "apuLatencyMs")) == std::vector<double>{0.25, 6.0});
    // The NES's one param whose value comes from its core config, whole percent, the same range its role schema clamps.
    const auto roleRange = tsNumbers(RP_CORE_ROLES_TS_PATH, "expansionVolume:");
    REQUIRE(roleRange.size() == 3);
    const auto nesType = rig.registry.find("rp.mesen.nes")->type;
    const auto descriptor = rp::graph::parseJson(nesType->descriptor(nesType));
    REQUIRE(descriptor);
    const auto params = descriptor->to_object()->get("params")->to_array();
    REQUIRE(params);
    bool found = false;
    for (const auto& p : *params) {
        const auto o = *p.to_object();
        const auto id = o.get("id");
        REQUIRE(id);
        if (*id->to_string() != "expansionVolume") continue;
        found = true;
        CHECK(*o.get("min")->to_double() == roleRange[0]);
        CHECK(*o.get("max")->to_double() == roleRange[1]);
    }
    REQUIRE(found);
    // Every type's gain param holds the range systemSettings.ts spells, the one the store clamps a saved gain to.
    const std::vector<double> gainRange = {tsConstant(RP_SYSTEM_SETTINGS_TS_PATH, "GAIN_MIN"),
                                           tsConstant(RP_SYSTEM_SETTINGS_TS_PATH, "GAIN_MAX")};
    REQUIRE(gainRange == std::vector<double>{-90.0, 12.0});  // the reader found them
    for (const char* type : {"rp.sameboy.gb", "rp.mesen.nes", "rp.mesen.gba", "rp.mesen.sms"}) {
        INFO(type);
        const auto typeDesc =
            rp::graph::parseJson(rig.registry.find(type)->type->descriptor(rig.registry.find(type)->type));
        REQUIRE(typeDesc);
        const auto typeParams = typeDesc->to_object()->get("params")->to_array();
        REQUIRE(typeParams);
        bool gain = false;
        for (const auto& p : *typeParams) {
            const auto o = *p.to_object();
            const auto id = o.get("id");
            REQUIRE(id);
            if (*id->to_string() != "gain") continue;
            gain = true;
            CHECK(*o.get("min")->to_double() == gainRange[0]);
            CHECK(*o.get("max")->to_double() == gainRange[1]);
        }
        REQUIRE(gain);
    }
    // The GBA and the SMS carry the fields every system node does (the system it names, its ROM, its two seeds and the
    // ROM's name) and no knob of their own; the SMS adds its two construct-time fields (the machine and the FM unit).
    const std::pair<const char*, std::size_t> shared[] = {{"rp.mesen.gba", 5}, {"rp.mesen.sms", 7}};
    for (const auto& [type, fields] : shared) {
        INFO(type);
        CHECK(settingsSchema(rig, type).get("properties")->to_object()->size() == fields);
    }

    // Anything else is refused with the document, the core as it was: an ordinal, another spelling, a number out of
    // its range. Each refused document names a system of its own, so no field is spelt twice.
    REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, kGbDmg))).ok);
    run(rig);
    const auto* const core = dynamic_cast<const SameBoySystem*>(rig.builtCore(1));
    REQUIRE(core != nullptr);
    for (const char* refused : {R"("model":9)", R"("model":"CgbC")", R"("model":"cgb-c")", R"("highpass":"removeDCOffset")",
                                R"("lightTemperature":1.5)", R"("serialOutCapture":"loud")", R"("fastBoot":"yes")"}) {
        INFO(refused);
        const auto r = rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 2, refused)));
        CHECK_FALSE(r.ok);
        CHECK(codes(r) == "settings-invalid");
    }
    CHECK(core->config_.model == SameBoyModel::DmgB);
    CHECK(core->config_.highpass == SameBoyHighpass::Accurate);
    for (const char* refused : {R"("apuLatencyMs":0.2)", R"("apuLatencyMs":7)", R"("region":"PAL")", R"("s5bNoise":1)"}) {
        INFO(refused);
        CHECK(codes(rig.set(knobbed("rp.mesen.nes", rig.boot("rp.mesen.nes", 2, refused)))) == "settings-invalid");
    }
}

TEST_CASE("each knob reaches its core at the start of the next block of the plan that carries it",
          "[engine-graph][system-node][settings]") {
    SECTION("Game Boy") {
        NodeRig rig(kRate);
        REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, kGbDmg))).ok);
        const auto* const gb = dynamic_cast<const SameBoySystem*>(rig.builtCore(1));
        REQUIRE(gb != nullptr);
        run(rig);
        // The model's restart has its own case below; every other knob here, each off what mgb() built.
        REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot(
                                              "rp.sameboy.gb", 1,
                                              R"("model":"dmgB","fastBoot":true,"highpass":"removeDcOffset","colorCorrection":"modernBalanced",)"
                                              R"("dmgPalette":"mgb","lightTemperature":-0.5,"serialOutCapture":"listen")")))
                    .ok);
        // Adopted as it was published (the rig is quiescent), but applied as the next block starts.
        CHECK(gb->config_.highpass == SameBoyHighpass::Accurate);
        CHECK(gb->serialOutCapture() == SerialOutCapture::Off);
        run(rig);
        CHECK(gb->config_.model == SameBoyModel::DmgB);
        CHECK(gb->config_.fastBoot == true);
        CHECK(gb->config_.highpass == SameBoyHighpass::RemoveDcOffset);
        CHECK(gb->config_.colorCorrection == SameBoyColorCorrection::ModernBalanced);
        CHECK(gb->config_.dmgPalette == SameBoyDmgPalette::Mgb);
        CHECK(gb->config_.lightTemperature == -0.5);
        CHECK(gb->serialOutCapture() == SerialOutCapture::Listen);
        REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, gbDmg(R"("serialOutCapture":"drive")")))).ok);
        run(rig);
        CHECK(gb->serialOutCapture() == SerialOutCapture::Drive);
    }
    SECTION("NES") {
        NodeRig rig(kRate);
        REQUIRE(rig.set(knobbed("rp.mesen.nes", rig.boot("rp.mesen.nes", 1))).ok);
        const auto* const core = dynamic_cast<const MesenNesSystem*>(rig.builtCore(1));
        REQUIRE(core != nullptr);
        run(rig);
        const std::uint32_t window = core->apuFlushCycleLength();
        REQUIRE(core->config().s5bNoise == 1);  // MesenNesConfig's defaults, "n8"
        REQUIRE(core->config().mmc5PhaseReset == 1);
        REQUIRE(rig.set(knobbed("rp.mesen.nes", rig.boot(
                                                    "rp.mesen.nes", 1,
                                                    R"("region":"pal","removeSpriteLimit":true,"apuLatencyMs":3.5,"s5bNoise":"chip","mmc5PhaseReset":"chip")")))
                    .ok);
        CHECK(core->config().region == 0);
        run(rig);
        CHECK(core->config().region == static_cast<std::uint32_t>(NesRegion::Pal));
        CHECK(core->config().removeSpriteLimit);
        CHECK(core->config().apuLatencyMs == 3.5);
        CHECK(core->apuFlushCycleLength() != window);  // the live window, re-thresholded
        CHECK(core->config().s5bNoise == static_cast<std::uint32_t>(CartridgeAccuracy::Chip));
        CHECK(core->config().mmc5PhaseReset == static_cast<std::uint32_t>(CartridgeAccuracy::Chip));
    }
}

TEST_CASE("a knob restarts or resets its core only where the core's own setter does, in the block the plan lands",
          "[engine-graph][system-node][settings]") {
    // The graph core against a direct one given the same setter calls at the same block, bit for bit, and against a
    // control that hears none: where the knob is audible, the direct core differs from the control from that block on.
    std::uint64_t heard = kNever;

    SECTION("Game Boy: highpass and the display group apply in place") {
        NodeRig rig(kRate);
        auto direct = mgb(1), control = mgb(1);
        REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, kGbDmg))).ok);
        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
        // During the boot chime, so the filter is heard; a restart would replay the chime and break the match.
        CHECK(compare(rig, d, 3.0, once(0.3, [&] {
                  REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot(
                                                        "rp.sameboy.gb", 1,
                                                        R"("model":"dmgB","fastBoot":false,"highpass":"off","dmgPalette":"gbl",)"
                                                        R"("colorCorrection":"lowContrast","lightTemperature":0.25)")))
                              .ok);
                  direct->setHighpass(SameBoyHighpass::Off);
                  direct->setDmgPalette(SameBoyDmgPalette::Gbl);
                  direct->setColorCorrection(SameBoyColorCorrection::LowContrast);
                  direct->setLightTemperature(0.25);
              }), &c, &heard) == "");
        CHECK(heard >= static_cast<std::uint64_t>(0.3 * kRate));
        CHECK(heard != kNever);
    }
    SECTION("Game Boy: a palette alone changes no sample") {
        NodeRig rig(kRate);
        auto control = mgb(1);
        REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, kGbDmg))).ok);
        Direct c({control.get()}, 2, {{0, 1}});
        CHECK(compare(rig, c, 3.0, once(0.3, [&] {
                  REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, gbDmg(R"("dmgPalette":"dmg")")))).ok);
              })) == "");
        CHECK(sounds(c));
    }
    SECTION("Game Boy: a model change restarts the core") {
        NodeRig rig(kRate);
        auto direct = mgb(1), control = mgb(1);
        REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, kGbDmg))).ok);
        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
        CHECK(compare(rig, d, 4.0, once(2.0, [&] {
                  REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, R"("model":"mgb","fastBoot":false)"))).ok);
                  direct->setModel(SameBoyModel::Mgb);
              }), &c, &heard) == "");
        CHECK(heard >= static_cast<std::uint64_t>(2.0 * kRate));  // the chime again, from the block it landed in
        CHECK(heard != kNever);
    }
    SECTION("Game Boy: a fast boot edit and a model edit landing in one block restart it with the new fast boot") {
        // Two documents published before a block commit together; the restart reads the fast boot, so it applies
        // first, as the two edits made one block apart would. The other order is audible: off, the boot ROM chimes.
        const auto build = [] {
            SameBoyConfig cfg;
            cfg.model = SameBoyModel::DmgB;
            cfg.fastBoot = true;
            auto sys = std::make_unique<SameBoySystem>(1, cfg, readFile(RP_MGB_ROM_PATH));
            sys->onActivate(kRate);
            return sys;
        };
        NodeRig rig(kRate);
        auto direct = build(), control = build();
        REQUIRE(rig.set(knobbed("rp.sameboy.gb",
                                rig.boot("rp.sameboy.gb", 1, R"("model":"dmgB","fastBoot":true)"))).ok);
        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
        CHECK(compare(rig, d, 3.0, once(0.5, [&] {
                  REQUIRE(rig.set(knobbed("rp.sameboy.gb",
                                          rig.boot("rp.sameboy.gb", 1, R"("model":"dmgB","fastBoot":false)"))).ok);
                  REQUIRE(rig.set(knobbed("rp.sameboy.gb",
                                          rig.boot("rp.sameboy.gb", 1, R"("model":"mgb","fastBoot":false)"))).ok);
                  direct->setFastBoot(false);
                  direct->setModel(SameBoyModel::Mgb);
                  control->setModel(SameBoyModel::Mgb);
                  control->setFastBoot(false);
              }), &c, &heard) == "");
        CHECK(heard >= static_cast<std::uint64_t>(0.5 * kRate));
        CHECK(heard != kNever);
    }
    SECTION("NES: the sprite limit and accuracy switches apply in place, a region change resets it") {
        NodeRig rig(kRate);
        auto direct = nes(1, 0), control = nes(1, 0);
        REQUIRE(rig.set(knobbed("rp.mesen.nes", rig.boot("rp.mesen.nes", 1))).ok);
        SystemBase* const graphCore = rig.builtCore(1);
        REQUIRE(graphCore != nullptr);
        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
        auto note = [&, sent = false](std::uint64_t at) mutable {  // as afterBoot, to all three
            if (sent || at < static_cast<std::uint64_t>(1.5 * kRate)) return;
            for (SystemBase* s : {graphCore, static_cast<SystemBase*>(direct.get()), static_cast<SystemBase*>(control.get())})
                noteOn(*s, 0, 60);
            sent = true;
        };
        auto inPlace = once(2.0, [&] {
            REQUIRE(rig.set(knobbed("rp.mesen.nes", rig.boot(
                                                      "rp.mesen.nes", 1,
                                                      R"("removeSpriteLimit":true,"s5bNoise":"chip","mmc5PhaseReset":"chip")")))
                        .ok);
            direct->setRemoveSpriteLimit(true);
            direct->setS5bNoise(0);
            direct->setMmc5PhaseReset(0);
        });
        auto reset = once(3.0, [&] {
            REQUIRE(rig.set(knobbed("rp.mesen.nes", rig.boot(
                                                      "rp.mesen.nes", 1,
                                                      R"("removeSpriteLimit":true,"s5bNoise":"chip","mmc5PhaseReset":"chip","region":"pal")")))
                        .ok);
            direct->setRegion(static_cast<std::uint32_t>(NesRegion::Pal));
        });
        CHECK(compare(rig, d, 4.0, [&](std::uint64_t at) { note(at); inPlace(at); reset(at); }, &c, &heard) == "");
        // Nothing of the switches is heard; the reset is, from the block it landed in.
        CHECK(heard >= static_cast<std::uint64_t>(3.0 * kRate));
        CHECK(heard != kNever);
    }
}

TEST_CASE("a document that re-carries an unchanged model beside a highpass that moves applies only the highpass",
          "[engine-graph][system-node][settings]") {
    // The node's own guard (moved) is the one that decides, which the core's setter cannot stand in for once another
    // path has moved the core: a document still carrying the model the node applied last leaves that model alone.
    NodeRig rig(kRate);
    auto direct = mgb(1);
    REQUIRE(rig.set(knobbed("rp.sameboy.gb",
                            rig.boot("rp.sameboy.gb", 1, R"("model":"mgb","fastBoot":false,"highpass":"accurate")")))
                    .ok);
    auto* const gb = dynamic_cast<SameBoySystem*>(rig.builtCore(1));
    REQUIRE(gb != nullptr);
    direct->setModel(SameBoyModel::Mgb);
    direct->setHighpass(SameBoyHighpass::Accurate);
    Direct d({direct.get()}, 2, {{0, 1}});
    // Both cores are moved to another model by a path of their own, so a re-applied mgb would restart the node's.
    auto elsewhere = once(0.5, [&] {
        gb->setModel(SameBoyModel::DmgB);
        direct->setModel(SameBoyModel::DmgB);
    });
    // The next document re-carries mgb, unchanged since the one before, beside a highpass that moves.
    auto moved = once(1.0, [&] {
        REQUIRE(rig.set(knobbed("rp.sameboy.gb",
                                rig.boot("rp.sameboy.gb", 1, R"("model":"mgb","fastBoot":false,"highpass":"off")")))
                        .ok);
        direct->setHighpass(SameBoyHighpass::Off);
    });
    CHECK(compare(rig, d, 2.0, [&](std::uint64_t at) { elsewhere(at); moved(at); }) == "");
    CHECK(gb->config_.model == SameBoyModel::DmgB);  // the document's mgb was not applied again
    CHECK(gb->config_.highpass == SameBoyHighpass::Off);
}

TEST_CASE("a region a document carries that the core already has resets it not at all",
          "[engine-graph][system-node][settings]") {
    // The core setter's own guard, reached mid-run: the document's value differs from the one the node applied last,
    // so the node applies it, and the core, already holding it through another path, must not reset.
    const auto built = [](SystemId id, std::uint32_t region) {
        MesenNesConfig cfg;
        cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
        cfg.region = region;
        auto sys = std::make_unique<MesenNesSystem>(id, cfg, readFile(RP_BLIPTOASTER_ROM_PATH));
        sys->onActivate(kRate);
        return sys;
    };
    NodeRig rig(kRate);
    auto direct = built(1, static_cast<std::uint32_t>(NesRegion::Pal));
    REQUIRE(rig.set(knobbed("rp.mesen.nes", rig.boot("rp.mesen.nes", 1, R"("region":"pal")"))).ok);
    auto* const core = dynamic_cast<MesenNesSystem*>(rig.builtCore(1));
    REQUIRE(core != nullptr);
    // The direct core was built as Pal too: the document's value is the build's, so neither applies it at activation.
    Direct d({direct.get()}, 2, {{0, 1}});
    auto note = afterBoot(*direct, *core);
    // Both cores are put on Ntsc by a path of their own, which the node's settings do not know about.
    auto elsewhere = once(0.5, [&] {
        core->setRegion(static_cast<std::uint32_t>(NesRegion::Ntsc));
        direct->setRegion(static_cast<std::uint32_t>(NesRegion::Ntsc));
    });
    // The next document carries ntsc, now a different value from the one it applied last, beside a knob that moves:
    // the node applies the region, and setRegion's guard is what keeps the core from resetting.
    auto moved = once(1.0, [&] {
        REQUIRE(rig.set(knobbed("rp.mesen.nes", rig.boot("rp.mesen.nes", 1, R"("region":"ntsc","removeSpriteLimit":true)"))).ok);
        direct->setRemoveSpriteLimit(true);
    });
    CHECK(compare(rig, d, 3.0, [&](std::uint64_t at) { note(at); elsewhere(at); moved(at); }) == "");
    CHECK(core->config().region == static_cast<std::uint32_t>(NesRegion::Ntsc));
    CHECK(core->config().removeSpriteLimit);
}

TEST_CASE("a system node gives its core every knob its settings carry as it builds it, before the core first runs",
          "[engine-graph][system-node][settings]") {
    SECTION("Game Boy, its capture included") {
        NodeRig rig(kRate);
        auto direct = mgb(1);
        REQUIRE(rig.set(knobbed("rp.sameboy.gb",
                                rig.boot("rp.sameboy.gb", 1,
                                         R"("model":"mgb","fastBoot":false,"highpass":"off","serialOutCapture":"drive")")))
                        .ok);
        auto* const gb = dynamic_cast<SameBoySystem*>(rig.builtCore(1));
        REQUIRE(gb != nullptr);
        // Activated as it was planned: no block has run.
        CHECK(gb->config_.model == SameBoyModel::Mgb);
        CHECK(gb->config_.highpass == SameBoyHighpass::Off);
        CHECK(gb->serialOutCapture() == SerialOutCapture::Drive);
        direct->setModel(SameBoyModel::Mgb);
        direct->setHighpass(SameBoyHighpass::Off);
        direct->setSerialOutCapture(SerialOutCapture::Drive);
        // From the first block on, the graph core renders and captures what the direct one, given the same calls
        // before its first block, does: mGB opens its port once the boot ROM has run, about 2.5 s in.
        Direct d({direct.get()}, 2, {{0, 1}});
        Lanes graph(2);
        const rp_transport t{kRate, 120.0, 0.0, 0};
        std::size_t captured = 0;
        for (int b = 0; b < 160; ++b) {
            rig.host->processBlock(1024, t, graph.ptrs.data(), 2);
            d.block(1024);
            INFO("block " << b);
            REQUIRE(std::memcmp(graph.data[0].data(), d.out.data[0].data(), 1024 * sizeof(float)) == 0);
            std::vector<std::uint8_t> carried, logged;
            for (const auto& m : rig.node<Probe>("probe").bytes) carried.insert(carried.end(), m.data.begin(), m.data.end());
            for (const auto& entry : direct->serialOutLog_) logged.push_back(entry.second);
            REQUIRE(carried == logged);
            captured += carried.size();
        }
        CHECK(captured > 0);
        CHECK(sounds(d));
    }
    SECTION("NES") {
        NodeRig rig(kRate);
        REQUIRE(rig.set(knobbed("rp.mesen.nes",
                                rig.boot("rp.mesen.nes", 1, R"("region":"dendy","apuLatencyMs":2,"s5bNoise":"chip")")))
                        .ok);
        const auto* const core = dynamic_cast<const MesenNesSystem*>(rig.builtCore(1));
        REQUIRE(core != nullptr);
        CHECK(core->config().region == static_cast<std::uint32_t>(NesRegion::Dendy));
        CHECK(core->config().apuLatencyMs == 2.0);
        CHECK(core->config().s5bNoise == 0);
        CHECK(core->config().mmc5PhaseReset == 1);  // absent: as built
    }
}

TEST_CASE("a knob absent from a system node's settings leaves its core as it was built, or as the last document left it",
          "[engine-graph][system-node][settings]") {
    SECTION("Game Boy") {
        // Every knob built away from SameBoyConfig's defaults, and the capture armed on the core directly.
        const auto build = [] {
            SameBoyConfig cfg;
            cfg.model = SameBoyModel::Mgb;
            cfg.fastBoot = false;
            cfg.highpass = SameBoyHighpass::RemoveDcOffset;
            cfg.colorCorrection = SameBoyColorCorrection::ReduceContrast;
            cfg.dmgPalette = SameBoyDmgPalette::Gbl;
            cfg.lightTemperature = 0.75;
            auto sys = std::make_unique<SameBoySystem>(1, cfg, readFile(RP_MGB_ROM_PATH));
            sys->onActivate(kRate);
            sys->setSerialOutCapture(SerialOutCapture::Drive);
            return sys;
        };
        NodeRig rig(kRate);
        auto direct = build();
        REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot(
                                              "rp.sameboy.gb", 1,
                                              R"("model":"mgb","fastBoot":false,"highpass":"removeDcOffset",)"
                                              R"("colorCorrection":"reduceContrast","dmgPalette":"gbl","lightTemperature":0.75,)"
                                              R"("serialOutCapture":"drive")")))
                        .ok);
        auto* const gb = dynamic_cast<SameBoySystem*>(rig.builtCore(1));
        REQUIRE(gb != nullptr);
        Direct d({direct.get()}, 2, {{0, 1}});
        CHECK(compare(rig, d, 2.0) == "");  // nothing restarted, nothing re-filtered
        CHECK(gb->config_.model == SameBoyModel::Mgb);
        CHECK(gb->config_.fastBoot == false);
        CHECK(gb->config_.highpass == SameBoyHighpass::RemoveDcOffset);
        CHECK(gb->config_.colorCorrection == SameBoyColorCorrection::ReduceContrast);
        CHECK(gb->config_.dmgPalette == SameBoyDmgPalette::Gbl);
        CHECK(gb->config_.lightTemperature == 0.75);
        CHECK(gb->serialOutCapture() == SerialOutCapture::Drive);

        // Present, then dropped: the core keeps what the last document carrying it gave it.
        REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot(
                                              "rp.sameboy.gb", 1,
                                              R"("model":"mgb","fastBoot":false,"highpass":"off","serialOutCapture":"listen")")))
                        .ok);
        run(rig);
        REQUIRE(gb->config_.highpass == SameBoyHighpass::Off);
        REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, R"("model":"mgb","fastBoot":false)"))).ok);
        run(rig, 512, 4);
        CHECK(gb->config_.highpass == SameBoyHighpass::Off);
        CHECK(gb->serialOutCapture() == SerialOutCapture::Listen);
    }
    SECTION("NES") {
        NodeRig rig(kRate);
        // Built as Pal; a document that carries no region leaves it as it was built.
        REQUIRE(rig.set(knobbed("rp.mesen.nes", rig.boot(
                                                    "rp.mesen.nes", 1,
                                                    R"("region":"pal","removeSpriteLimit":true,"apuLatencyMs":3,)"
                                                    R"("s5bNoise":"chip","mmc5PhaseReset":"chip")")))
                        .ok);
        const auto* const core = dynamic_cast<const MesenNesSystem*>(rig.builtCore(1));
        REQUIRE(core != nullptr);
        REQUIRE(rig.set(knobbed("rp.mesen.nes", rig.boot("rp.mesen.nes", 1))).ok);
        run(rig, 512, 4);
        CHECK(core->config().region == static_cast<std::uint32_t>(NesRegion::Pal));
        CHECK(core->config().removeSpriteLimit);
        CHECK(core->config().apuLatencyMs == 3.0);
        CHECK(core->config().s5bNoise == 0);
        CHECK(core->config().mmc5PhaseReset == 0);
    }
}

TEST_CASE("a Game Boy node's settings commit, and every knob it applies in place, allocate nothing on the block thread",
          "[engine-graph][system-node][settings]") {
    // Only a model's restart allocates, as it did through the command ring. (A NES's blocks allocate on their own, in
    // Mesen, and its region reset allocates; its other setters allocate nothing: the NES cases below hold both.)
    NodeRig rig(kRate);
    REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, kGbDmg))).ok);
    const auto* const gb = dynamic_cast<const SameBoySystem*>(rig.builtCore(1));
    REQUIRE(gb != nullptr);
    Lanes out(2);
    const rp_transport t{kRate, 120.0, 0.0, 0};
    for (int b = 0; b < 8; ++b) rig.host->processBlock(512, t, out.ptrs.data(), 2);
    rig.host->setAudioThreadOwns(true);  // so the commit happens in a block, as it does under an audio thread
    REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot(
                                              "rp.sameboy.gb", 1,
                                              R"("model":"dmgB","fastBoot":true,"highpass":"off","colorCorrection":"modernAccurate",)"
                                              R"("dmgPalette":"gbl","lightTemperature":0.5,"serialOutCapture":"listen")")))
                .ok);
    rp::test::beginCountingAllocations();
    for (int b = 0; b < 2; ++b) rig.host->processBlock(512, t, out.ptrs.data(), 2);
    const auto counted = rp::test::endCountingAllocations();
    rig.host->setAudioThreadOwns(false);
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
    CHECK(gb->config_.highpass == SameBoyHighpass::Off);
    CHECK(gb->config_.dmgPalette == SameBoyDmgPalette::Gbl);
    CHECK(gb->serialOutCapture() == SerialOutCapture::Listen);
}

TEST_CASE("a NES node's settings allocate no more on the block thread than the same window with no edit",
          "[engine-graph][system-node][settings]") {
    // Mesen allocates inside its own blocks, so the claim is relative. A second core, warmed through the same blocks,
    // is the no-edit window at the same emulated time. The block an edit lands in is skipped: the changed emulation can
    // make Mesen allocate a little differently there (the setters themselves are held separately below).
    NodeRig rig(kRate), control(kRate);
    REQUIRE(rig.set(knobbed("rp.mesen.nes", rig.boot("rp.mesen.nes", 1))).ok);
    REQUIRE(control.set(knobbed("rp.mesen.nes", control.boot("rp.mesen.nes", 1))).ok);
    const auto* const core = dynamic_cast<const MesenNesSystem*>(rig.builtCore(1));
    REQUIRE(core != nullptr);
    Lanes out(2), controlOut(2);
    const rp_transport t{kRate, 120.0, 0.0, 0};
    for (int b = 0; b < 8; ++b) {
        rig.host->processBlock(512, t, out.ptrs.data(), 2);
        control.host->processBlock(512, t, controlOut.ptrs.data(), 2);
    }
    rig.host->setAudioThreadOwns(true);      // so the commit happens in a block, as it does under an audio thread
    control.host->setAudioThreadOwns(true);
    const auto window = [&](NodeRig& r, Lanes& lanes) {
        rp::test::beginCountingAllocations();
        for (int b = 0; b < 2; ++b) r.host->processBlock(512, t, lanes.ptrs.data(), 2);
        return rp::test::endCountingAllocations();
    };
    window(control, controlOut);  // the control's edit block, at the same emulated time
    const auto idle = window(control, controlOut);
    REQUIRE(rig.set(knobbed("rp.mesen.nes", rig.boot(
                                              "rp.mesen.nes", 1,
                                              R"("removeSpriteLimit":true,"apuLatencyMs":2,"s5bNoise":"chip","mmc5PhaseReset":"chip")")))
                .ok);
    window(rig, out);  // the block the settings land in
    const auto knobs = window(rig, out);
    rig.host->setAudioThreadOwns(false);
    control.host->setAudioThreadOwns(false);
    INFO("idle " << idle.allocations << " allocations / " << idle.frees << " frees, knobs " << knobs.allocations
                 << " / " << knobs.frees);
    CHECK(knobs.allocations == idle.allocations);
    CHECK(knobs.frees == idle.frees);
    CHECK(core->config().removeSpriteLimit);
    CHECK(core->config().apuLatencyMs == 2.0);
    CHECK(core->config().s5bNoise == 0);
    CHECK(core->config().mmc5PhaseReset == 0);
}

TEST_CASE("a NES's setters that do not reset allocate nothing, and its region reset does",
          "[engine-graph][system-node][settings]") {
    auto core = nes(1, 0);
    const auto count = [&](auto fn) {
        rp::test::beginCountingAllocations();
        fn();
        return rp::test::endCountingAllocations();
    };
    const auto remove = count([&] { core->setRemoveSpriteLimit(true); });
    const auto latency = count([&] { core->setApuLatencyMs(2.0); });
    const auto s5b = count([&] { core->setS5bNoise(0); });
    const auto mmc5 = count([&] { core->setMmc5PhaseReset(0); });
    const auto gain = count([&] { core->setGainDb(-6.0f); });
    const auto volume = count([&] { core->setExpansionVolume(40); });
    const auto region = count([&] { core->setRegion(static_cast<std::uint32_t>(NesRegion::Dendy)); });
    INFO("remove " << remove.allocations << ", latency " << latency.allocations << ", s5b " << s5b.allocations
                   << ", mmc5 " << mmc5.allocations << ", gain " << gain.allocations << ", volume "
                   << volume.allocations << ", region " << region.allocations);
    CHECK(remove.allocations == 0);
    CHECK(latency.allocations == 0);
    CHECK(s5b.allocations == 0);
    CHECK(mmc5.allocations == 0);
    CHECK(gain.allocations == 0);
    CHECK(volume.allocations == 0);
    CHECK(region.allocations > 0);  // UpdateRegion + Reset
    CHECK(core->config().region == static_cast<std::uint32_t>(NesRegion::Dendy));
}

TEST_CASE("a Game Boy node takes knob edits published while an audio thread runs its blocks", "[engine-graph][system-node][settings]") {
    // The race check for the settings path under ThreadSanitizer (build-tsan/): each edit is planned on this thread,
    // never waiting, while the audio thread commits the last one and applies it, and the plans it hands back are freed
    // here with the settings they held.
    NodeRig rig(kRate);
    REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, kGbDmg))).ok);
    const auto* const gb = dynamic_cast<const SameBoySystem*>(rig.builtCore(1));
    REQUIRE(gb != nullptr);
    const char* const palettes[] = {"grey", "dmg", "mgb", "gbl"};
    const char* const captures[] = {"off", "listen", "drive"};
    const auto edit = [&](int i) {
        return knobbed("rp.sameboy.gb",
                       rig.boot("rp.sameboy.gb", 1,
                                gbDmg(std::string(R"("dmgPalette":")") + palettes[i % 4] + R"(","serialOutCapture":")" +
                                      captures[i % 3] + R"(","highpass":")" + (i % 2 ? "off" : "accurate") +
                                      R"(","lightTemperature":)" + std::to_string((i % 5) * 0.25 - 0.5))));
    };
    std::atomic<bool> running{true};
    std::atomic<int>  blocks{0};
    rig.host->setAudioThreadOwns(true);
    std::thread audio([&] {
        Lanes out(2);
        const rp_transport t{kRate, 120.0, 0.0, 0};
        while (running.load(std::memory_order_acquire)) {
            rig.host->processBlock(256, t, out.ptrs.data(), 2);
            blocks.fetch_add(1, std::memory_order_release);
        }
    });
    int accepted = 0;
    constexpr int kEdits = 60;
    for (int i = 0; i < kEdits; ++i) {
        accepted += rig.set(edit(i)).ok ? 1 : 0;
        rig.host->reclaim();
        const int seen = blocks.load(std::memory_order_acquire);
        while (blocks.load(std::memory_order_acquire) < seen + 2) std::this_thread::yield();  // let a block or two run
    }
    running.store(false, std::memory_order_release);
    audio.join();
    rig.host->setAudioThreadOwns(false);
    CHECK(accepted == kEdits);
    run(rig);  // whatever the audio thread left unapplied
    CHECK(gb->config_.dmgPalette == SameBoyDmgPalette::Gbl);  // the last edit's, 59
    CHECK(gb->serialOutCapture() == SerialOutCapture::Drive);
    CHECK(gb->config_.highpass == SameBoyHighpass::Off);
    CHECK(gb->config_.lightTemperature == 0.5);
}

// ---- params -----------------------------------------------------------------------------------------------------
//
// A system node's params (docs/modular-graph-phase-c2-design.md sections 3 and 4): every type's gain, and a NES's
// expansion volume. The document's value reaches the core before its first block, and each change (setParam, or a
// document carrying another value) at the start of the block it lands in, as the core's own setter called directly
// there renders it, bit for bit; a value the core already has calls nothing.

namespace {

// Each param a registered type declares, as "<id> <type> <min>..<max> <default> <unit>".
std::vector<std::string> declaredParams(NodeRig& rig, const char* type) {
    const auto descriptor = rp::graph::parseJson(rig.registry.find(type)->type->descriptor(rig.registry.find(type)->type));
    REQUIRE(descriptor);
    const auto params = descriptor->to_object()->get("params")->to_array();
    REQUIRE(params);
    std::vector<std::string> out;
    for (const auto& p : *params) {
        const auto o = *p.to_object();
        const auto num = [&](const char* key) { return rp::graph::jsNumber(*o.get(key)->to_double()); };
        out.push_back(*o.get("id")->to_string() + " " + *o.get("type")->to_string() + " " + num("min") + ".." + num("max") +
                      " " + num("default") + " " + *o.get("unit")->to_string());
    }
    return out;
}

}  // namespace

TEST_CASE("every system node takes its gain as a param, and a NES its expansion volume", "[engine-graph][system-node][params]") {
    NodeRig rig(kRate);
    for (const char* type : {"rp.sameboy.gb", "rp.mesen.gba", "rp.mesen.sms"}) {
        INFO(type);
        CHECK(declaredParams(rig, type) == std::vector<std::string>{"gain float -90..12 0 dB"});
    }
    CHECK(declaredParams(rig, "rp.mesen.nes") == std::vector<std::string>{"gain float -90..12 0 dB", "expansionVolume int 0..200 100 %"});
}

TEST_CASE("a system node gives its core the document's params before the core's first block, and each change at the "
          "start of the block it lands in",
          "[engine-graph][system-node][params]") {
    std::uint64_t heard = kNever;

    SECTION("Game Boy") {
        NodeRig rig(kRate);
        auto direct = mgb(1), control = mgb(1);
        REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, kGbDmg), R"("gain":-12)")).ok);
        const auto* const gb = dynamic_cast<const SameBoySystem*>(rig.builtCore(1));
        REQUIRE(gb != nullptr);
        // Planned and activated, but a node reads its params in a block: the core has the value as that block starts.
        CHECK(gb->config_.gainDb == 0.0f);
        direct->setGainDb(-12.0f, true);  // the node snaps its first value in
        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
        CHECK(compare(rig, d, 3.0, once(1.0, [&] {
                  REQUIRE(rig.host->setParam("sys", "gain", -3.0));
                  direct->setGainDb(-3.0f);
              }), &c, &heard) == "");
        CHECK(heard < static_cast<std::uint64_t>(1.0 * kRate));  // the document's gain, on the boot chime
        CHECK(gb->config_.gainDb == -3.0f);
        CHECK(sounds(d));
    }
    SECTION("NES, its expansion volume in whole percent") {
        NodeRig rig(kRate);
        auto direct = nes(1, 0);
        REQUIRE(rig.set(knobbed("rp.mesen.nes", rig.boot("rp.mesen.nes", 1), R"("gain":-6,"expansionVolume":40)")).ok);
        auto* const core = dynamic_cast<MesenNesSystem*>(rig.builtCore(1));
        REQUIRE(core != nullptr);
        direct->setGainDb(-6.0f, true);  // the node snaps its first value in
        direct->setExpansionVolume(40);
        Direct d({direct.get()}, 2, {{0, 1}});
        auto edit = once(2.0, [&] {
            REQUIRE(rig.host->setParam("sys", "expansionVolume", 150.4));  // rounded, as an int param's value is
            REQUIRE(rig.host->setParam("sys", "gain", 3.0));
            direct->setExpansionVolume(150);
            direct->setGainDb(3.0f);
        });
        auto note = afterBoot(*direct, *core);
        CHECK(compare(rig, d, 3.0, [&](std::uint64_t at) { note(at); edit(at); }) == "");
        CHECK(core->config().gainDb == 3.0f);
        CHECK(core->config().expansionVolume == 150);
        CHECK(sounds(d));
    }
    SECTION("SMS") {
        NodeRig rig(kRate);
        auto direct = sms(1);
        REQUIRE(rig.set(knobbed("rp.mesen.sms", smsNode(rig, 1), R"("gain":-9)")).ok);
        SystemBase* const graphCore = rig.builtCore(1);
        REQUIRE(graphCore != nullptr);
        direct->setGainDb(-9.0f, true);  // the node snaps its first value in
        Direct d({direct.get()}, 2, {{0, 1}});
        CHECK(compare(rig, d, 5.0, smsStart(*direct, *graphCore)) == "");
        CHECK(sounds(d));
    }
}

TEST_CASE("a published param value the core already has changes nothing, and another value lands at the start of its "
          "plan's first block",
          "[engine-graph][system-node][params]") {
    NodeRig rig(kRate);
    auto direct = mgb(1);
    REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, kGbDmg))).ok);
    Direct d({direct.get()}, 2, {{0, 1}});
    auto live = once(0.3, [&] {
        REQUIRE(rig.host->setParam("sys", "gain", -12.0));
        direct->setGainDb(-12.0f);
    });
    // The store's next publish, for whatever reason, carries the value it set live: nothing moves.
    auto same = once(0.6, [&] {
        REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, gbDmg(R"("dmgPalette":"dmg")")), R"("gain":-12)"))
                    .ok);
    });
    // A document dropping the param leaves the stream, and the core, where they were.
    auto dropped = once(0.9, [&] {
        REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, gbDmg(R"("dmgPalette":"dmg")")))).ok);
    });
    auto other = once(1.2, [&] {
        REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, kGbDmg), R"("gain":-1.5)")).ok);
        direct->setGainDb(-1.5f);
    });
    CHECK(compare(rig, d, 2.0, [&](std::uint64_t at) { live(at); same(at); dropped(at); other(at); }) == "");
    CHECK(sounds(d));
}

TEST_CASE("a param edit, live or published, allocates nothing on the block thread", "[engine-graph][system-node][params]") {
    NodeRig rig(kRate);
    REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, kGbDmg))).ok);
    const auto* const gb = dynamic_cast<const SameBoySystem*>(rig.builtCore(1));
    REQUIRE(gb != nullptr);
    Lanes out(2);
    const rp_transport t{kRate, 120.0, 0.0, 0};
    for (int b = 0; b < 8; ++b) rig.host->processBlock(512, t, out.ptrs.data(), 2);
    rig.host->setAudioThreadOwns(true);  // so each edit lands in a block, as it does under an audio thread
    REQUIRE(rig.host->setParam("sys", "gain", -6.0));
    rp::test::beginCountingAllocations();
    for (int b = 0; b < 2; ++b) rig.host->processBlock(512, t, out.ptrs.data(), 2);
    auto counted = rp::test::endCountingAllocations();
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
    CHECK(gb->config_.gainDb == -6.0f);
    REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot("rp.sameboy.gb", 1, kGbDmg), R"("gain":-3)")).ok);
    rp::test::beginCountingAllocations();
    for (int b = 0; b < 2; ++b) rig.host->processBlock(512, t, out.ptrs.data(), 2);
    counted = rp::test::endCountingAllocations();
    rig.host->setAudioThreadOwns(false);
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
    CHECK(gb->config_.gainDb == -3.0f);
}

TEST_CASE("a NES node's param edit allocates no more on the block thread than the same window with no edit",
          "[engine-graph][system-node][params]") {
    NodeRig rig(kRate), control(kRate);
    REQUIRE(rig.set(knobbed("rp.mesen.nes", rig.boot("rp.mesen.nes", 1))).ok);
    REQUIRE(control.set(knobbed("rp.mesen.nes", control.boot("rp.mesen.nes", 1))).ok);
    const auto* const core = dynamic_cast<const MesenNesSystem*>(rig.builtCore(1));
    REQUIRE(core != nullptr);
    Lanes out(2), controlOut(2);
    const rp_transport t{kRate, 120.0, 0.0, 0};
    for (int b = 0; b < 8; ++b) {
        rig.host->processBlock(512, t, out.ptrs.data(), 2);
        control.host->processBlock(512, t, controlOut.ptrs.data(), 2);
    }
    rig.host->setAudioThreadOwns(true);
    control.host->setAudioThreadOwns(true);
    const auto window = [&](NodeRig& r, Lanes& lanes) {
        rp::test::beginCountingAllocations();
        for (int b = 0; b < 2; ++b) r.host->processBlock(512, t, lanes.ptrs.data(), 2);
        return rp::test::endCountingAllocations();
    };
    window(control, controlOut);  // the control's edit block, at the same emulated time
    const auto idle = window(control, controlOut);
    REQUIRE(rig.host->setParam("sys", "gain", -6.0));
    REQUIRE(rig.host->setParam("sys", "expansionVolume", 40));
    window(rig, out);  // the block the params land in
    const auto live = window(rig, out);
    rig.host->setAudioThreadOwns(false);
    control.host->setAudioThreadOwns(false);
    INFO("idle " << idle.allocations << " allocations / " << idle.frees << " frees, live " << live.allocations << " / "
                 << live.frees);
    CHECK(live.allocations == idle.allocations);
    CHECK(live.frees == idle.frees);
    CHECK(core->config().gainDb == -6.0f);
    CHECK(core->config().expansionVolume == 40);
}

TEST_CASE("a wire's value reaches a system node's params clamped to their ranges", "[engine-graph][system-node][params]") {
    const auto driven = [](bool infinite, double level) {
        NodeRig rig(kRate);
        const std::string value = R"({"level":)" + rp::graph::jsNumber(level) +
                                  (infinite ? R"(,"infinite":true)" : "") + "}";
        const std::string doc = R"({"nodes":[{"id":"gain","type":"rp.test.level","settings":)" + value +
                                R"(},{"id":"exp","type":"rp.test.level","settings":)" + value +
                                R"(},{"id":"sys","type":"rp.mesen.nes","settings":{)" + rig.boot("rp.mesen.nes", 1) +
                                R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],)" +
                                R"("wires":[{"from":"gain.out","to":"sys.gain"},{"from":"exp.out","to":"sys.expansionVolume"},)" +
                                R"({"from":"sys.mix","to":"out.out.1-2"}]})";
        REQUIRE(rig.set(doc).ok);
        const auto* const core = dynamic_cast<const MesenNesSystem*>(rig.builtCore(1));
        REQUIRE(core != nullptr);
        Lanes out(2);
        const rp_transport t{kRate, 120.0, 0.0, 0};
        for (int b = 0; b < 3; ++b) rig.host->processBlock(256, t, out.ptrs.data(), 2);
        return std::pair{core->config().gainDb, core->config().expansionVolume};
    };
    // A wire carries any double: a value past the range lands at the range's end, and an infinity too.
    CHECK(driven(false, 1e9) == std::pair{12.0f, 200u});
    CHECK(driven(false, -1e9) == std::pair{-90.0f, 0u});
    CHECK(driven(true, 1.0) == std::pair{12.0f, 200u});
    CHECK(driven(true, -1.0) == std::pair{-90.0f, 0u});
}

TEST_CASE("a system node takes param edits made while an audio thread runs its blocks", "[engine-graph][system-node][params]") {
    // The race check for the params path under ThreadSanitizer (build-tsan/): live edits and documents carrying a value
    // from this thread, while the audio thread drains them and the node applies the latest.
    NodeRig rig(kRate);
    REQUIRE(rig.set(knobbed("rp.mesen.nes", rig.boot("rp.mesen.nes", 1))).ok);
    const auto* const core = dynamic_cast<const MesenNesSystem*>(rig.builtCore(1));
    REQUIRE(core != nullptr);
    std::atomic<bool> running{true};
    std::atomic<int>  blocks{0};
    rig.host->setAudioThreadOwns(true);
    std::thread audio([&] {
        Lanes out(2);
        const rp_transport t{kRate, 120.0, 0.0, 0};
        while (running.load(std::memory_order_acquire)) {
            rig.host->processBlock(256, t, out.ptrs.data(), 2);
            blocks.fetch_add(1, std::memory_order_release);
        }
    });
    int accepted = 0;
    constexpr int kEdits = 60;
    for (int i = 0; i < kEdits; ++i) {
        const double gain = -(i % 30), percent = 100 + i;
        const std::string values = R"("gain":)" + rp::graph::jsNumber(gain) + R"(,"expansionVolume":)" + rp::graph::jsNumber(percent);
        const bool ok = i % 10 == 9 ? rig.set(knobbed("rp.mesen.nes", rig.boot("rp.mesen.nes", 1), values)).ok
                                    : rig.host->setParam("sys", "gain", gain) && rig.host->setParam("sys", "expansionVolume", percent);
        accepted += ok ? 1 : 0;
        rig.host->reclaim();
        const int seen = blocks.load(std::memory_order_acquire);
        while (blocks.load(std::memory_order_acquire) < seen + 2) std::this_thread::yield();
    }
    running.store(false, std::memory_order_release);
    audio.join();
    rig.host->setAudioThreadOwns(false);
    CHECK(accepted == kEdits);
    run(rig);
    CHECK(core->config().gainDb == -29.0f);  // the last edit's, 59
    CHECK(core->config().expansionVolume == 159);
}

// ---- a node that builds its own core (docs/modular-graph-phase-c3-design.md sections 3 and 5) ---------------------

namespace {

// The resource id a byte block goes in as.
std::uint32_t resource(NodeRig& rig, const char* type, std::vector<std::uint8_t> bytes) {
    return rig.put(type, std::move(bytes));
}

// The bytes of a fixture the resources tree holds, or empty when it is not there (as CI runs): the LSDj and GBA ROMs
// are not in the repository.
std::vector<std::uint8_t> resourceFixture(const char* relative) {
    const char* root = std::getenv("RETROPLUG_RESOURCES_DIR");
    if (!root) return {};
    return readFile((std::string(root) + relative).c_str());
}

// Run `core` directly for `blocks` blocks of 512 frames, as the engine's triad would, and return the savestate it then
// has: the seed a later core restores, as a duplicate's or a saved state's.
std::vector<std::uint8_t> stateAfter(SystemBase& core, int blocks) {
    Direct d({&core}, 2, {{0, 1}});
    for (int b = 0; b < blocks; ++b) d.block(512);
    return core.saveStateBytes();
}

}  // namespace

TEST_CASE("a Game Boy node built from its ROM resource renders what a core built from the same bytes renders",
          "[engine-graph][system-node][resources]") {
    const auto rom = readFile(RP_MGB_ROM_PATH);
    SECTION("no seed") {
        NodeRig rig(kRate);
        auto direct = mgb(1);
        const auto romId = resource(rig, "rom", rom);
        REQUIRE(rig.set(R"({"nodes":[{"id":"sys-1","type":"rp.sameboy.gb","settings":{)" +
                        rig.boot("rp.sameboy.gb", 1, R"("model":"dmgB","fastBoot":false)", romId) +
                        R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                            "wires":[{"from":"sys-1.mix","to":"out.out.1-2"}]})").ok);
        Direct d({direct.get()}, 2, {{0, 1}});
        CHECK(compare(rig, d, 2.0) == "");
        CHECK(sounds(d));
    }
    SECTION("an sram seed") {
        NodeRig rig(kRate);
        SameBoyConfig cfg;
        cfg.model = SameBoyModel::DmgB;
        cfg.fastBoot = false;
        cfg.sram.assign(8 * 1024, 0x5a);
        auto direct = std::make_unique<SameBoySystem>(1, cfg, rom);
        direct->onActivate(kRate);
        const auto romId = resource(rig, "rom", rom);
        const auto sramId = resource(rig, "sram", cfg.sram);
        REQUIRE(rig.set(R"({"nodes":[{"id":"sys-1","type":"rp.sameboy.gb","settings":{)" +
                        rig.boot("rp.sameboy.gb", 1,
                                 R"("model":"dmgB","fastBoot":false,"sram":"res:)" + std::to_string(sramId) + "\"",
                                 romId) +
                        R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                            "wires":[{"from":"sys-1.mix","to":"out.out.1-2"}]})").ok);
        Direct d({direct.get()}, 2, {{0, 1}});
        CHECK(compare(rig, d, 2.0) == "");
        CHECK(sounds(d));
    }
    SECTION("a state seed") {
        NodeRig rig(kRate);
        auto source = mgb(9);
        const auto state = stateAfter(*source, 24);
        REQUIRE(!state.empty());
        SameBoyConfig cfg;
        cfg.model = SameBoyModel::DmgB;
        cfg.fastBoot = false;
        cfg.savestate = state;
        auto direct = std::make_unique<SameBoySystem>(1, cfg, rom);
        direct->onActivate(kRate);
        const auto romId = resource(rig, "rom", rom);
        const auto stateId = resource(rig, "state", state);
        REQUIRE(rig.set(R"({"nodes":[{"id":"sys-1","type":"rp.sameboy.gb","settings":{)" +
                        rig.boot("rp.sameboy.gb", 1,
                                 R"("model":"dmgB","fastBoot":false,"state":"res:)" + std::to_string(stateId) + "\"",
                                 romId) +
                        R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                            "wires":[{"from":"sys-1.mix","to":"out.out.1-2"}]})").ok);
        Direct d({direct.get()}, 2, {{0, 1}});
        CHECK(compare(rig, d, 2.0) == "");
    }
}

TEST_CASE("a system node placed at a gain plays its first block at it, from the first sample",
          "[engine-graph][system-node][params]") {
    // The core's gain smoother starts at unity, so applyGain snaps a node's first value: a node placed at -12 dB never
    // plays a block at 0 dB. A node always builds its core fresh, so the block its gain first lands in is normally
    // silent; a `state` seed taken while the DMG boot chime sounds makes it audible, and a ramp down from unity would
    // show in it.
    const auto rom = readFile(RP_MGB_ROM_PATH);
    SameBoyConfig sourceCfg;
    sourceCfg.model = SameBoyModel::DmgB;
    sourceCfg.fastBoot = false;
    auto source = std::make_unique<SameBoySystem>(9, sourceCfg, rom);
    source->onActivate(kRate);
    Direct run({source.get()}, 2, {{0, 1}});
    // The seed is taken at a block boundary whose next 64 frames sound: the chime is a sequence of notes, so a
    // boundary inside a note is what makes the node's first block audible.
    std::vector<std::uint8_t> before = source->saveStateBytes(), state;
    for (int b = 0; b < 4000 && state.empty(); ++b) {
        run.sounded = false;
        run.block(64);
        if (run.sounded) state = before;
        else before = source->saveStateBytes();
    }
    REQUIRE(!state.empty());

    NodeRig rig(kRate);
    SameBoyConfig cfg = sourceCfg;
    cfg.savestate = state;
    auto direct = std::make_unique<SameBoySystem>(1, cfg, rom);
    direct->onActivate(kRate);
    direct->setGainDb(-12.0f, true);  // the snap the node makes at its first block
    const auto romId = resource(rig, "rom", rom);
    const auto stateId = resource(rig, "state", state);
    REQUIRE(rig.set(R"({"nodes":[{"id":"sys-1","type":"rp.sameboy.gb","settings":{)" +
                    rig.boot("rp.sameboy.gb", 1,
                             R"("model":"dmgB","fastBoot":false,"state":"res:)" + std::to_string(stateId) + "\"",
                             romId) +
                    R"(},"params":{"gain":-12}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"sys-1.mix","to":"out.out.1-2"}]})").ok);
    auto* const built = dynamic_cast<SameBoySystem*>(rig.builtCore(1));
    REQUIRE(built != nullptr);
    CHECK(built->config_.gainDb == 0.0f);  // the param lands at the first block, not at the build
    // The seed's first block carries signal, which is what makes the snap observable there.
    auto probe = std::make_unique<SameBoySystem>(1, cfg, rom);
    probe->onActivate(kRate);
    probe->setGainDb(-12.0f, true);
    Direct first({probe.get()}, 2, {{0, 1}});
    first.block(64);
    REQUIRE(sounds(first));
    // Bit for bit against a core snapped the same way: without the snap the node's first block sits at unity and ramps
    // down, which the comparison covers inside its first 20 ms.
    Direct d({direct.get()}, 2, {{0, 1}});
    CHECK(compare(rig, d, 0.05) == "");
    CHECK(sounds(d));
    CHECK(built->config_.gainDb == -12.0f);
}

TEST_CASE("a non-default model reaches a Game Boy core before it boots, so a savestate seed is not lost to a restart",
          "[engine-graph][system-node][resources]") {
    // The state is taken from a core running as the model the document names: the node built at the default model and
    // restarted into CGB would lose exactly this. SameBoy's own restart discards the savestate it just restored.
    const auto rom = readFile(RP_MGB_ROM_PATH);
    SameBoyConfig sourceCfg;
    sourceCfg.model = SameBoyModel::Mgb;
    sourceCfg.fastBoot = true;
    auto source = std::make_unique<SameBoySystem>(9, sourceCfg, rom);
    source->onActivate(kRate);
    const auto state = stateAfter(*source, 400);  // past the boot chime, so the state carries signal
    REQUIRE(!state.empty());

    NodeRig rig(kRate);
    SameBoyConfig cfg = sourceCfg;
    cfg.savestate = state;
    auto direct = std::make_unique<SameBoySystem>(1, cfg, rom);
    direct->onActivate(kRate);
    const auto romId = resource(rig, "rom", rom);
    const auto stateId = resource(rig, "state", state);
    REQUIRE(rig.set(R"({"nodes":[{"id":"sys-1","type":"rp.sameboy.gb","settings":{)" +
                    rig.boot("rp.sameboy.gb", 1,
                             R"("model":"mgb","state":"res:)" + std::to_string(stateId) + "\"", romId) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"sys-1.mix","to":"out.out.1-2"}]})").ok);
    // The core the node built is the model the document carried; it never restarted into anything.
    auto* built = dynamic_cast<SameBoySystem*>(rig.builtCore(1));
    REQUIRE(built != nullptr);
    CHECK(built->config_.model == SameBoyModel::Mgb);
    Direct d({direct.get()}, 2, {{0, 1}});
    // mGB itself is silent as an Mgb (its chime is a DMG boot ROM's), so the comparison is the observation: a core
    // restarted into another model would not follow this state bit for bit.
    CHECK(compare(rig, d, 2.0) == "");
}

TEST_CASE("a NES node built from its ROM resource takes its region, export mode and expansion volume",
          "[engine-graph][system-node][resources]") {
    const auto rom = readFile(RP_BLIPTOASTER_ROM_PATH);
    NodeRig rig(kRate);
    MesenNesConfig cfg;
    cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
    cfg.region = static_cast<std::uint32_t>(NesRegion::Ntsc);
    cfg.channelExportMode = 1;  // stereoModPins: three mono streams
    cfg.expansionVolume = 50;   // the blob path builds the core AT the volume; the node gives its param at its first block
    auto direct = std::make_unique<MesenNesSystem>(1, cfg, rom);
    direct->onActivate(kRate);
    const auto romId = resource(rig, "rom", rom);
    REQUIRE(rig.set(R"({"nodes":[{"id":"sys-1","type":"rp.mesen.nes","settings":{)" +
                    rig.boot("rp.mesen.nes", 1,
                             R"("region":"ntsc","channelExportMode":"stereoModPins")", romId) +
                    R"(},"params":{"expansionVolume":50}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[1,1,1]}}],
                        "wires":[{"from":"sys-1.pins.pulse","to":"out.out.1"},{"from":"sys-1.pins.tnd","to":"out.out.2"},
                                 {"from":"sys-1.pins.expansion","to":"out.out.3"}]})").ok);
    auto* built = rig.builtCore(1);
    REQUIRE(built != nullptr);
    CHECK(dynamic_cast<MesenNesSystem*>(built)->config().region == static_cast<std::uint32_t>(NesRegion::Ntsc));
    Direct d({direct.get()}, 3, {{0, 0, 1, 1, 2, 2}});  // a mono stream's two lanes are one buffer
    CHECK(compare(rig, d, 3.0, afterBoot(*direct, *built)) == "");
    CHECK(sounds(d));
}

TEST_CASE("a Master System node built from its ROM resource boots with FM off", "[engine-graph][system-node][resources]") {
    NodeRig rig(kRate);
    auto direct = sms(1);
    const auto romId = resource(rig, "rom", readFile(RP_SMS_ROM_PATH));
    // smsggdj reads its options from the battery: the same image, and the same seed sms() builds the core with.
    std::vector<std::uint8_t> sram(32 * 1024, 0);
    const std::uint8_t header[] = {'S', 'M', 'D', 'J', '4', 1, 32}, options[] = {0x43, 0x46, 0, 0, 0, 0, 0};
    std::memcpy(sram.data(), header, sizeof header);
    std::memcpy(sram.data() + 0x3f60, options, sizeof options);
    const auto sramId = resource(rig, "sram", sram);
    REQUIRE(rig.set(R"({"nodes":[{"id":"sys-1","type":"rp.mesen.sms","settings":{)" +
                    rig.boot("rp.mesen.sms", 1, R"("sram":"res:)" + std::to_string(sramId) +
                             R"(","enableFm":false,"romName":"smsggdj_v0_45")", romId) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"sys-1.mix","to":"out.out.1-2"}]})").ok);
    auto* built = rig.builtCore(1);
    REQUIRE(built != nullptr);
    CHECK_FALSE(dynamic_cast<MesenSmsSystem*>(built)->config().enableFm);
    Direct d({direct.get()}, 2, {{0, 1}});
    CHECK(compare(rig, d, 5.0, smsStart(*direct, *built)) == "");
    CHECK(sounds(d));
}

TEST_CASE("a GBA node built from its ROM resource renders what a core built from the same bytes renders",
          "[engine-graph][system-node][resources]") {
    // The GBA is the one console whose typed path no other resource case reaches, and its ROM is a sibling fixture, so
    // the case leaves on a run without one (as CI's) rather than failing there.
    const auto rom = resourceFixture("/roms/nanoloop287d.gba");
    if (rom.empty()) return;
    NodeRig rig(kRate);
    auto direct = std::make_unique<MesenGbaSystem>(1, MesenGbaConfig{}, rom);
    direct->onActivate(kRate);
    const auto romId = resource(rig, "rom", rom);
    REQUIRE(rig.set(R"({"nodes":[{"id":"sys-1","type":"rp.mesen.gba","settings":{)" +
                    rig.boot("rp.mesen.gba", 1, "", romId) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"sys-1.mix","to":"out.out.1-2"}]})").ok);
    auto* built = rig.builtCore(1);
    REQUIRE(built != nullptr);
    CHECK(dynamic_cast<MesenGbaSystem*>(built) != nullptr);
    Direct d({direct.get()}, 2, {{0, 1}});
    // Nanoloop plays nothing until it is told to: both cores get the same Start.
    const auto start = [a = direct.get(), b = built, pressed = false](std::uint64_t at) mutable {
        if (pressed || at < static_cast<std::uint64_t>(1.0 * kRate)) return;
        for (SystemBase* s : {static_cast<SystemBase*>(a), b})
            s->pressButton(static_cast<std::uint8_t>(GbaButton::Start), true);
        pressed = true;
    };
    CHECK(compare(rig, d, 4.0, start) == "");
    CHECK(d.sounded);
}

TEST_CASE("an LSDj node built from its ROM resource renders what a core built from the same bytes renders",
          "[engine-graph][system-node][resources]") {
    // A 1 MiB Game Boy ROM whose banking exercises more of the typed SameBoy path than the embedded mGB image does. The
    // ROM is a sibling fixture, so the case leaves on a run without one (as CI's).
    const auto rom = resourceFixture("/roms/lsdj/lsdj9_4_1-develop.gb");
    if (rom.empty()) return;
    NodeRig rig(kRate);
    auto direct = std::make_unique<SameBoySystem>(1, SameBoyConfig{}, rom);
    direct->onActivate(kRate);
    const auto romId = resource(rig, "rom", rom);
    REQUIRE(rig.set(R"({"nodes":[{"id":"sys-1","type":"rp.sameboy.gb","settings":{)" +
                    rig.boot("rp.sameboy.gb", 1, "", romId) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"sys-1.mix","to":"out.out.1-2"}]})").ok);
    auto* built = rig.builtCore(1);
    REQUIRE(built != nullptr);
    CHECK(dynamic_cast<SameBoySystem*>(built) != nullptr);
    Direct d({direct.get()}, 2, {{0, 1}});
    CHECK(compare(rig, d, 2.0) == "");
    CHECK(d.sounded);
}

TEST_CASE("a Master System node without a romName stages each system under a stem of its own",
          "[engine-graph][system-node][resources]") {
    NodeRig rig(kRate);
    const auto romId = resource(rig, "rom", readFile(RP_SMS_ROM_PATH));
    const std::string document =
        R"({"nodes":[{"id":"sys-1","type":"rp.mesen.sms","settings":{)" +
        rig.boot("rp.mesen.sms", 1, R"("enableFm":false)", romId) +
        R"(}},{"id":"sys-2","type":"rp.mesen.sms","settings":{)" +
        rig.boot("rp.mesen.sms", 2, R"("enableFm":false,"romName":"smsggdj_v0_45")", romId) +
        R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2,2]}}],
            "wires":[{"from":"sys-1.mix","to":"out.out.1-2"},{"from":"sys-2.mix","to":"out.out.3-4"}]})";
    REQUIRE(rig.set(document).ok);
    auto* unnamed = dynamic_cast<MesenSmsSystem*>(rig.builtCore(1));
    auto* named = dynamic_cast<MesenSmsSystem*>(rig.builtCore(2));
    REQUIRE(unnamed != nullptr);
    REQUIRE(named != nullptr);
    // Mesen reads the battery's stem out of the staged file's name, so two systems staged under one fallback name share
    // one battery. A node with no romName gets its system id in the stem; a node with one stages under that instead.
    const std::filesystem::path staged(unnamed->stagedRomPath()), explicitName(named->stagedRomPath());
    CHECK(staged.stem().string() == "system-1");
    CHECK(explicitName.stem().string() == "smsggdj_v0_45");
    CHECK(staged != explicitName);
}

// ---- what a boot costs (docs/modular-graph-phase-c3-design.md section 9) --------------------------------------------

TEST_CASE("a boot's cost per console, through the build a system node makes", "[.][bench]") {
    using Clock = std::chrono::steady_clock;
    auto micros = [](Clock::time_point t0) { return std::chrono::duration<double, std::micro>(Clock::now() - t0).count(); };
    constexpr int kRuns = 5;

    NodeRig rig(kRate);
    const auto embedded = rp::embeddedMgbRom();
    struct Boot {
        const char*               name;
        SystemConfig              config;
        std::vector<std::uint8_t> rom;
    };
    std::vector<Boot> boots;
    boots.push_back({"mGB (embedded)", SameBoyConfig{}, {embedded.begin(), embedded.end()}});
    if (auto lsdj = resourceFixture("/roms/lsdj/lsdj9_4_1-develop.gb"); !lsdj.empty())
        boots.push_back({"LSDj", SameBoyConfig{}, std::move(lsdj)});
    boots.push_back({"NES", MesenNesConfig{}, readFile(RP_BLIPTOASTER_ROM_PATH)});
    boots.push_back({"Master System", MesenSmsConfig{}, readFile(RP_SMS_ROM_PATH)});
    if (auto gba = resourceFixture("/roms/nanoloop287d.gba"); !gba.empty())
        boots.push_back({"GBA", MesenGbaConfig{}, std::move(gba)});

    std::printf("\nboot through rp.host.systems/1's build at %.1f kHz: construct, onActivate (ROM, battery, savestate), the "
                "state snapshot, and the same build through setGraph (slot claim and one plan with it); median of %d runs\n",
                kRate / 1000.0, kRuns);
    for (auto& boot : boots) {
        std::vector<double> built, planned;
        for (int run = 0; run < kRuns; ++run) {
            SystemBuildRequest request;
            request.rom = boot.rom;
            request.config = boot.config;
            std::string why;
            const auto t0 = Clock::now();
            std::unique_ptr<SystemBase> core = rig.factory.build(1, request, kRate, &why);
            built.push_back(micros(t0));
            REQUIRE(core);
        }
        // The same boot where it happens in the engine: a plan with one system node, published on a quiescent engine.
        Engine engine(kRate);
        const std::string type = std::holds_alternative<SameBoyConfig>(boot.config)   ? "rp.sameboy.gb"
                                 : std::holds_alternative<MesenNesConfig>(boot.config) ? "rp.mesen.nes"
                                 : std::holds_alternative<MesenSmsConfig>(boot.config) ? "rp.mesen.sms"
                                                                                       : "rp.mesen.gba";
        for (int run = 0; run < kRuns; ++run) {
            const std::uint32_t romId = engine.putResource("rom", boot.rom);
            REQUIRE(romId != 0);
            const auto t0 = Clock::now();
            const auto placed = engine.setGraph(rp::host::test::graphRequest(
                R"({"nodes":[{"id":"sys-1","type":")" + type + R"(","settings":{"system":1,"rom":"res:)" +
                    std::to_string(romId) + R"("}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"sys-1.mix","to":"out.out.1-2"}]})",
                {1}));
            planned.push_back(micros(t0));
            REQUIRE(placed.ok);
            REQUIRE(engine.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument({}), {})).ok);
            engine.reclaim();
        }
        std::sort(built.begin(), built.end());
        std::sort(planned.begin(), planned.end());
        std::printf("  %-16s boot %7.1f ms   through setGraph %7.1f ms   (%zu KiB of ROM)\n", boot.name,
                    built[built.size() / 2] / 1000.0, planned[planned.size() / 2] / 1000.0, boot.rom.size() / 1024);
    }
}
