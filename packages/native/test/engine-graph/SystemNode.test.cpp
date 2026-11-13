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
// The GBA cases use a sibling fixture ROM, so each SKIPs, counted, on a run without the resources tree (as CI's); the
// LSDj cases leave early there rather than failing.
//
// The last cases hold the block thread's allocation promise (docs/modular-graph-phase-c4-design.md section 4): a
// warmed core's block allocates and frees nothing, both for the audio path on its own (a fixed audio ring, and
// scratches, tap buffers and stream blips sized at activation) and for a whole block with emulation in it (Mesen's
// per-frame video path and SameBoy's own), and arming the tap allocates nothing on the thread that arms it.
//
// Run via `pnpm test:plugin`.

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "graph/CountingAllocator.hpp"
#include "graph/Packages.hpp"
#include "GraphDocs.hpp"
#include "NodeRig.hpp"
#include "SyntheticRom.hpp"  // rp::blastem::test's images (packages/blastem/native/test)
#include "graph/CanonicalJson.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/graph/HostIoNodes.hpp"
#include "system/SystemBuild.hpp"
#include "system/blastem/BlastemMdConfig.hpp"
#include "system/blastem/BlastemMdSystem.hpp"
#include "system/mesen/MesenAudioDevice.hpp"
#include "system/mesen/MesenBackend.hpp"
#include "system/mesen/MesenGbaSystem.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/mesen/MesenSmsConfig.hpp"
#include "system/mesen/MesenSmsSystem.hpp"
#include "system/InputCapacity.hpp"
#include "system/mesen/InputTypes.hpp"  // NesButton, GbaButton, SmsButton
#include "system/sameboy/InputTypes.hpp"  // GameboyButton
#include "system/sameboy/SameBoyBackend.hpp"
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

// The same core with BlipToaster's reliable link on. The in-repo image predates the capability byte the core reads at
// activate (its SIG says none), so this copy sets the bit: what the case measures is the host's side of the link (the
// frame window, the message buffer, the sync), which runs inside a block whether or not the ROM answers, not the ROM's
// receiver. Nothing else in the image moves.
std::unique_ptr<MesenNesSystem> linkedNes(SystemId id) {
    MesenNesConfig cfg;
    cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
    auto rom = readFile(RP_BLIPTOASTER_ROM_PATH);
    REQUIRE(rom.size() > 0x10 + 19);
    rom[0x10 + 18] = 0x01;  // NesN8FifoRole::romAdvertisesLink: the SIG's capability byte, bit 0
    auto sys = std::make_unique<MesenNesSystem>(id, cfg, rom);
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

// What mdNode() builds: the synthetic cart romFor hands the node (SyntheticRom.hpp's fmPsgRom, whose PSG sounds from
// power-on) and the region the settings label names, so the node's core and this twin boot the same machine.
std::unique_ptr<rp::blastem::BlastemMdSystem> md(SystemId id) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;  // ntscUs: the same region the node's settings below carry
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(id, cfg, rp::blastem::test::fmPsgRom());
    sys->onActivate(kRate);
    return sys;
}

// The node's settings, plus any extra member a case needs (the port-2 claim, which TS writes from the roles the
// system's pipeline places).
std::string mdNode(NodeRig& rig, SystemId id, const std::string& extra = "") {
    return rig.boot("rp.blastem.md", id,
                    std::string(R"("region":"ntscUs")") + (extra.empty() ? "" : "," + extra));
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

// `lanes` host channels, each as long as the largest block (`frames`, the default being the engine's 8192).
struct Lanes {
    std::vector<std::vector<float>> data;
    std::vector<float*>             ptrs;
    explicit Lanes(std::size_t n, std::size_t frames = 8192) : data(n, std::vector<float>(frames, 0.0f)) {
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
        for (std::size_t m = 0; m < members.size(); ++m) {
            members[m]->setBlockLanes(laneMaps[m].size());  // as the node's prepare says: which ring this block drains
            members[m]->prepareForBlock(info(frames));
        }
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

TEST_CASE("a lone Mega Drive node builds its core from the ROM resource", "[engine-graph][system-node][blastem-md]") {
    NodeRig rig(kRate);
    REQUIRE(rig.set(R"({"nodes":[{"id":"md","type":"rp.blastem.md","settings":{)" + mdNode(rig, 1) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"md.mix","to":"out.out.1-2"}]})").ok);
    SystemBase* const graphCore = rig.builtCore(1);
    REQUIRE(graphCore != nullptr);
    // Not a const read: getMemory refills this instance's own view buffer before it hands the view out.
    auto* core = static_cast<rp::blastem::BlastemMdSystem*>(graphCore);
    CHECK(core->activated());
    CHECK(core->masterClock() == 53693175u);  // ntscUs, from the settings' label
    CHECK(core->getMemory(rp::MemoryType::Ram, rp::AccessType::Read).valid());
}

TEST_CASE("a lone Mega Drive node renders its core's mix bit for bit",
          "[engine-graph][system-node][blastem-md]") {
    NodeRig rig(kRate);
    auto direct = md(1);
    REQUIRE(rig.set(R"({"nodes":[{"id":"md","type":"rp.blastem.md","settings":{)" + mdNode(rig, 1) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"md.mix","to":"out.out.1-2"}]})").ok);
    SystemBase* const graphCore = rig.builtCore(1);
    REQUIRE(graphCore != nullptr);
    Direct d({direct.get()}, 2, {{0, 1}});  // stereo: the node's two mix lanes are the host's two
    CHECK(compare(rig, d, 1.0) == "");      // one second, in the suite's ragged block pattern
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
        std::uint8_t front = 0;
        REQUIRE(graph1->peekSerialByte(front));
        CHECK(front == 0x90);
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

TEST_CASE("a Mega Drive node feeds controller port 2 only for the system whose role claimed it",
          "[engine-graph][system-node][inputs][blastem-md]") {
    // Port 2 is player 2 on every Mega Drive cart, so a level reaches its pins only for the system whose node says a
    // role claimed the port (`coreBytesIn`: TS writes the claim of the protocol node that system's pipeline places).
    // Unclaimed, both doors into the core's byte path stay shut: the MIDI message too long for the framed door, which
    // applyInputs sends down pushCoreBytes as it does for every core with a raw ingress, and the bytes input the sync
    // node's levels arrive on. The pins then read what the cart's own poll has always read: the pull-ups a pad leaves.
    const std::vector<std::uint8_t> sysex{0xF0, 0x7D, 0x42, 0x03, 0x00, 0xF7};  // longer than a MidiEvent holds
    for (const bool claimed : {false, true}) {
        const char* const which = claimed ? "claimed" : "unclaimed";
        INFO(which);
        NodeRig rig(kRate);
        REQUIRE(rig.set(fedDocument("rp.blastem.md",
                                    mdNode(rig, 1, claimed ? R"("coreBytesIn":true)" : R"("coreBytesIn":false)"),
                                    {"midi", "bytes"}))
                    .ok);
        auto* const core = static_cast<rp::blastem::BlastemMdSystem*>(rig.builtCore(1));
        REQUIRE(core != nullptr);
        CHECK(core->hasCoreBytesIn() == claimed);
        Feed& feed = rig.node<Feed>("feed");
        feed.next[Feed::Midi]  = {{0, sysex}};
        feed.next[Feed::Bytes] = {{4, {0x20}}, {8, {0x00}}};
        Lanes   graph(2);
        const rp_transport t{kRate, 120.0, 0.0, 0};
        rig.host->processBlock(64, t, graph.ptrs.data(), 2);
        // Claimed: one level per byte of the two messages, the last of them what the grid left on the pins. Unclaimed:
        // nothing taken at all, and the held level still the idle one the port booted on.
        CHECK(core->levelsAccepted() == (claimed ? sysex.size() + 2 : 0u));
        CHECK(core->portDevice().lastApplied() == (claimed ? 0x00 : 0x60));
        CHECK(core->clampedBlocks() == 0);
    }
}

TEST_CASE("a message staged at a Mega Drive node is refused while port 2 stands unclaimed",
          "[engine-graph][system-node][inputs][blastem-md][stage]") {
    // The other door into the same byte path: the UI stages a settings SysEx at the node whose menu sent it
    // (rp.stage/1), and the node asks hasCoreBytesIn before handing it over. On a cart that claimed nothing the answer
    // is no, so the message stays the caller's to route elsewhere rather than becoming levels on player 2's pins; on
    // the cart that claimed the port it is the raw ingress the sync node's levels use too.
    const std::vector<std::uint8_t> sysex{0xF0, 0x7D, 0x42, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0xF7};
    for (const bool claimed : {false, true}) {
        const char* const which = claimed ? "claimed" : "unclaimed";
        INFO(which);
        Engine e(kRate);
        rp::host::test::Roms roms;
        std::vector<rp::host::test::DocSystem> systems{
            {1, "rp.blastem.md", 0,
             claimed ? R"("region":"ntscUs","coreBytesIn":true)" : R"("region":"ntscUs")"}};
        rp::host::test::withRoms(e, systems, roms);
        REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1})).ok);
        QueuedInvoker invoker(e);
        auto* const core = static_cast<rp::blastem::BlastemMdSystem*>(e.findSystem(1));
        REQUIRE(core != nullptr);
        CHECK(core->hasCoreBytesIn() == claimed);
        invoker.stageNodeMidi("sys-1", sysex);
        invoker.drainInto(e);
        Lanes lanes(2);
        e.processBlock(512, lanes.ptrs.data(), 2);
        CHECK(core->levelsAccepted() == (claimed ? sysex.size() : 0u));
        // Claimed, the message is a run of levels and the last of them is what the grid left held; unclaimed, the pins
        // never moved off the pull-ups the port booted with.
        CHECK(core->portDevice().lastApplied() == (claimed ? sysex.back() : 0x60));
    }
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

// A listed source's text.
std::string readText(const std::string& path) {
    const auto bytes = readFile(path.c_str());
    return std::string(bytes.begin(), bytes.end());
}

// The TS sources the configure lists for this oracle (packages/native/cmake/tests.cmake): every package's own TS, with
// the app's settingsEnums.ts, systemSettings.ts and graph/buildGraph.ts beside them. The list is a file the configure
// writes, one path a line, which the oracle reads at run time: a package's new TS file rewrites that list and leaves
// this binary alone.
const std::vector<std::string>& settingsTsSources() {
    static const std::vector<std::string> sources = [] {
        std::vector<std::string> out;
        std::istringstream lines(readText(RP_SETTINGS_TS_LIST));
        for (std::string path; std::getline(lines, path);) {
            if (!path.empty() && path.back() == '\r') path.pop_back();  // a list written on Windows
            if (!path.empty()) out.push_back(path);
        }
        return out;
    }();
    return sources;
}

// The one listed source whose path contains `where` - how a case names a package's or the app's file without the
// configure having to name each one. Two matches would read the first of them in some run and the other in the next.
std::string settingsTsSource(const std::string& where) {
    std::vector<std::string> hits;
    for (const auto& path : settingsTsSources())
        if (path.find(where) != std::string::npos) hits.push_back(path);
    INFO("settings TS source named by \"" << where << "\"");
    REQUIRE(hits.size() == 1);
    return hits.front();
}

// The console descriptor that declares `type`: the one listed source whose text names it, which is the package's
// consoles.ts (packages/*/ts/*.ts is the configure's list). A node type two sources declare fails, as an enum two do.
std::string descriptorSource(const std::string& type) {
    const std::string line = "nodeType: \"" + type + "\"";
    std::vector<std::string> hits;
    for (const auto& path : settingsTsSources())
        if (readText(path).find(line) != std::string::npos) hits.push_back(path);
    std::string where;
    for (const auto& path : hits) where += (where.empty() ? ": " : " and ") + path;
    INFO("the console descriptor of " << type << " is declared by " << hits.size() << " source(s)" << where);
    REQUIRE(hits.size() == 1);
    return hits.front();
}

// The quoted strings of `export const <name> = [...] as const;`, read from whichever source declares it: an enum lives
// with the package that owns the knob reading it (SameBoy's MODEL_VALUES is @retroplug/sameboy's, the app's own enums
// are settingsEnums.ts's). A name no source spells leaves the case failing on an empty list; one two sources spell
// fails too, because the stale copy of a moved enum is exactly the thing that would otherwise be silently compared.
std::vector<std::string> tsValues(const std::string& name) {
    const std::string head = "export const " + name + " = [";
    std::vector<std::string> declared;
    for (const auto& path : settingsTsSources()) {
        const std::string ts = readText(path);
        if (ts.find(head) == std::string::npos) continue;
        declared.push_back(path);
        if (declared.size() > 1) break;  // the refusal names both, and reading further says nothing more
    }
    std::string where;
    for (const auto& path : declared) where += (where.empty() ? ": " : " and ") + path;
    INFO("enum " << name << " is declared by " << declared.size() << " source(s)" << where);
    REQUIRE(declared.size() == 1);
    const std::string ts = readText(declared[0]);
    const auto start = ts.find(head);
    const auto end = ts.find("] as const;", start);
    std::vector<std::string> values;
    for (auto q = ts.find('"', start); q < end; q = ts.find('"', q + 1)) {
        const auto close = ts.find('"', q + 1);
        values.push_back(ts.substr(q + 1, close - q - 1));
        q = close;
    }
    return values;
}

// The settings schema of a registered type, at its $defs entry: the one entry with properties, the settings struct (a
// resource ref's schema is a string inline, node.hpp).
rfl::Generic::Object settingsSchema(NodeRig& rig, const char* type) {
    const auto descriptor = rp::graph::parseJson(rig.registry.find(type)->type->descriptor(rig.registry.find(type)->type));
    REQUIRE(descriptor);
    const auto settings = descriptor->to_object()->get("settings")->to_object();
    REQUIRE(settings);
    const auto defs = settings->get("$defs")->to_object();
    REQUIRE(defs);
    std::optional<rfl::Generic::Object> settingsDef;
    for (const auto& [name, def] : *defs)
        if (const auto entry = def.to_object(); entry && entry->get("properties")) {
            REQUIRE_FALSE(settingsDef);  // one entry has properties: the settings struct
            settingsDef = *entry;
        }
    REQUIRE(settingsDef);
    return *settingsDef;
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
// buildGraph.ts's SETTING_FIELDS, or `expansionVolume: clampedInt(0, 200, 100)` in @retroplug/mesen's role schema. A
// knob's entry has the range's two, a zod clamped field those and its default. A match behind a `//` is skipped, so a
// comment above the entry may spell it too; the first match outside one wins, and no match leaves the case failing on
// an empty list.
std::vector<double> tsNumbers(const std::string& where, const std::string& entry) {
    const std::string ts = readText(settingsTsSource(where));
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
std::vector<double> tsRange(const std::string& where, const std::string& entry) {
    const auto numbers = tsNumbers(where, entry);
    return numbers.size() >= 2 ? std::vector<double>{numbers[0], numbers[1]} : std::vector<double>{};
}

// The number an exported TS constant spells, e.g. systemSettings.ts's `export const GAIN_MIN = -90;`.
double tsConstant(const std::string& where, const std::string& name) {
    const std::string ts = readText(settingsTsSource(where));
    const auto at = ts.find("export const " + name + " =");
    if (at == std::string::npos) return 0.0;
    return std::strtod(ts.c_str() + ts.find('=', at) + 1, nullptr);
}

// A construct-time field of a node type's console descriptor, with the fallback a document names when the stored config
// holds none the node takes, e.g. `n8LinkRttUs: { takes: intIn(0, 100'000), fallback: 2200 },`. Both are read out of the
// descriptor's constructFields, so a field added there is one the case must cover: the fallback is the JSON token after
// `fallback:`, so a label comes back with its quotes and a number or a flag as written.
struct TsConstructField {
    std::string name;
    std::string fallback;
};

std::vector<TsConstructField> tsConstructFields(const std::string& type) {
    const std::string ts = readText(descriptorSource(type));
    // One source may declare several descriptors (a package's consoles.ts holds its console's three), so the block is
    // the one after the node type's own line and before the next descriptor's: `constructFields` is optional, so a
    // descriptor that omits it must not read the next one's table.
    const auto nodeType = ts.find("nodeType: \"" + type + "\"");
    if (nodeType == std::string::npos) return {};
    const auto at = ts.find("constructFields: {", nodeType);
    if (at == std::string::npos) return {};
    const auto next = ts.find("nodeType: \"", nodeType + 1);
    if (next != std::string::npos && at > next) return {};
    const auto open = ts.find('{', at);
    if (open == std::string::npos) return {};
    std::size_t depth = 0;
    std::size_t close = open;
    for (; close < ts.size(); ++close) {
        if (ts[close] == '{') ++depth;
        else if (ts[close] == '}' && --depth == 0) break;
    }
    if (close >= ts.size()) return {};
    const std::string entry = ts.substr(open + 1, close - open - 1);
    std::vector<TsConstructField> fields;
    for (auto takes = entry.find(": { takes:"); takes != std::string::npos;
         takes = entry.find(": { takes:", takes + 1)) {
        auto name = takes;
        while (name > 0 && (std::isalnum(static_cast<unsigned char>(entry[name - 1])) || entry[name - 1] == '_'))
            --name;
        const auto fallback = entry.find("fallback:", takes);
        if (fallback == std::string::npos) continue;
        const auto start = entry.find_first_not_of(' ', fallback + std::strlen("fallback:"));
        const auto end = entry.find_first_of(",}\n", start);
        if (start == std::string::npos || end == std::string::npos) continue;
        std::string value = entry.substr(start, end - start);
        while (!value.empty() && value.back() == ' ') value.pop_back();
        fields.push_back({entry.substr(name, takes - name), value});
    }
    return fields;
}

// The construct-time fields as a settings body: `"field":value` for each, in the table's order.
std::string constructJson(const std::vector<TsConstructField>& fields) {
    std::string out;
    for (const auto& field : fields) {
        if (!out.empty()) out += ",";
        out += "\"" + field.name + "\":" + field.fallback;
    }
    return out;
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
    // A range's bounds, each its own allOf branch. (The numbers a role schema clamps are read from the console package
    // whose role declares the field.)
    const auto bounds = [](const rfl::Generic::Object& knob) {
        std::vector<double> b;
        const auto branches = knob.get("allOf")->to_array();
        REQUIRE(branches);
        for (const auto& branch : *branches)
            for (const char* key : {"minimum", "maximum"})
                if (const auto v = branch.to_object()->get(key)) b.push_back(*v->to_double());
        return b;
    };
    // Each range is TS's, in all three copies: the console descriptor's setting field, which picks a value for the
    // document, the role schema's clamped field, which a stored or edited config comes through, and the node's own
    // declaration.
    CHECK(bounds(knobSchema(gb, "lightTemperature")) == tsRange(descriptorSource("rp.sameboy.gb"), "lightTemperature:"));
    CHECK(bounds(knobSchema(gb, "lightTemperature")) == tsRange("packages/sameboy/ts/app.ts", "lightTemperature:"));
    CHECK(bounds(knobSchema(gb, "lightTemperature")) == std::vector<double>{-1.0, 1.0});
    CHECK(bounds(knobSchema(nesSchema, "apuLatencyMs")) == tsRange(descriptorSource("rp.mesen.nes"), "apuLatencyMs:"));
    CHECK(bounds(knobSchema(nesSchema, "apuLatencyMs")) == tsRange("packages/mesen/ts/app.ts", "apuLatencyMs:"));
    CHECK(bounds(knobSchema(nesSchema, "apuLatencyMs")) == std::vector<double>{0.25, 6.0});
    // The NES's one param whose value comes from its core config, whole percent, the same range its role schema clamps.
    const auto roleRange = tsNumbers("packages/mesen/ts/app.ts", "expansionVolume:");
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
    const std::vector<double> gainRange = {tsConstant("src/systemSettings.ts", "GAIN_MIN"),
                                           tsConstant("src/systemSettings.ts", "GAIN_MAX")};
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
    // A knob a refused document leaked would land at the next block, so one runs before the core is read again: what
    // the refusals left unapplied is what they must not have applied.
    run(rig);
    CHECK(core->config_.model == SameBoyModel::DmgB);
    CHECK(core->config_.highpass == SameBoyHighpass::Accurate);
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
                                              R"("model":"dmgB","fastBoot":true,"highpass":"removeDcOffset",)"
                                              R"("colorCorrection":"modernBalanced",)"
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
                                                    R"("region":"pal","removeSpriteLimit":true,"apuLatencyMs":3.5,)"
                                                    R"("s5bNoise":"chip","mmc5PhaseReset":"chip")")))
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
                                                      R"("removeSpriteLimit":true,"s5bNoise":"chip",)"
                                                      R"("mmc5PhaseReset":"chip","region":"pal")")))
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

TEST_CASE("a node's build carries every knob of its type, checked field by field on the core it built",
          "[engine-graph][system-node][settings]") {
    // A field a type's config() forgets is seeded as applied and never reaches the core, which no twin at the defaults
    // catches: every knob here is away from its default, and each is read back off the built core. The Game Boy's
    // highpass is audible, so its section also compares the core bit for bit against one built from the same config.
    SECTION("Game Boy") {
        const auto build = [] {
            SameBoyConfig cfg;
            cfg.model = SameBoyModel::DmgB;
            cfg.fastBoot = false;
            cfg.highpass = SameBoyHighpass::RemoveDcOffset;
            cfg.colorCorrection = SameBoyColorCorrection::ReduceContrast;
            cfg.dmgPalette = SameBoyDmgPalette::Gbl;
            cfg.lightTemperature = 0.75;
            auto sys = std::make_unique<SameBoySystem>(1, cfg, readFile(RP_MGB_ROM_PATH));
            sys->onActivate(kRate);
            return sys;
        };
        NodeRig rig(kRate);
        auto direct = build();
        REQUIRE(rig.set(knobbed("rp.sameboy.gb", rig.boot(
                                              "rp.sameboy.gb", 1,
                                              R"("model":"dmgB","fastBoot":false,"highpass":"removeDcOffset",)"
                                              R"("colorCorrection":"reduceContrast","dmgPalette":"gbl",)"
                                              R"("lightTemperature":0.75)")))
                        .ok);
        auto* const gb = dynamic_cast<SameBoySystem*>(rig.builtCore(1));
        REQUIRE(gb != nullptr);
        CHECK(gb->config_.model == SameBoyModel::DmgB);
        CHECK(gb->config_.fastBoot == false);
        CHECK(gb->config_.highpass == SameBoyHighpass::RemoveDcOffset);
        CHECK(gb->config_.colorCorrection == SameBoyColorCorrection::ReduceContrast);
        CHECK(gb->config_.dmgPalette == SameBoyDmgPalette::Gbl);
        CHECK(gb->config_.lightTemperature == 0.75);
        // The highpass twin: the built core filters what a core built from the same config filters, bit for bit.
        Direct d({direct.get()}, 2, {{0, 1}});
        CHECK(compare(rig, d, 2.0) == "");
        CHECK(sounds(d));
    }
    SECTION("NES") {
        // A card root this case alone names, in the `/` spelling a JSON string can hold (a Windows temp path is
        // backslashed). Two cores given one sdRoot share one emulated card, which is what the setting is for and not
        // what a knob check wants; the FIFO only stores the path it is given, so a case that names a root and runs no
        // block leaves nothing on disk.
        const std::string sdRoot =
            (std::filesystem::temp_directory_path() /
             ("rp-engine-graph-nes-sd-" + std::to_string(std::random_device{}()))).generic_string();
        NodeRig rig(kRate);
        REQUIRE(rig.set(knobbed("rp.mesen.nes", rig.boot(
                                                    "rp.mesen.nes", 1,
                                                    R"("region":"dendy","removeSpriteLimit":true,"apuLatencyMs":2,)"
                                                    R"("s5bNoise":"chip","mmc5PhaseReset":"chip",)"
                                                    R"("sdRoot":")" + sdRoot + R"(","fifo":"hardware",)"
                                                    R"("fifoDepth":4096,"fifoBytesPerSecond":150000,"n8Link":"off",)"
                                                    R"("n8LinkRttUs":3000,"channelExportMode":"individualMono")")))
                        .ok);
        const auto* const core = dynamic_cast<const MesenNesSystem*>(rig.builtCore(1));
        REQUIRE(core != nullptr);
        const MesenNesConfig& cfg = core->config();
        CHECK(cfg.region == static_cast<std::uint32_t>(NesRegion::Dendy));
        CHECK(cfg.removeSpriteLimit);
        CHECK(cfg.apuLatencyMs == 2.0);
        CHECK(cfg.s5bNoise == static_cast<std::uint32_t>(CartridgeAccuracy::Chip));
        CHECK(cfg.mmc5PhaseReset == static_cast<std::uint32_t>(CartridgeAccuracy::Chip));
        CHECK(cfg.sdRoot == sdRoot);
        CHECK(cfg.fifo == 1);
        CHECK(cfg.fifoDepth == 4096);
        CHECK(cfg.fifoBytesPerSecond == 150000);
        CHECK(cfg.n8Link == 1u);  // "off"
        CHECK(cfg.n8LinkRttUs == 3000);
        CHECK(cfg.channelExportMode == 3);
    }
    SECTION("Master System") {
        NodeRig rig(kRate);
        REQUIRE(rig.set(knobbed("rp.mesen.sms", rig.boot("rp.mesen.sms", 1, R"("enableFm":false,"gameGear":true)"))).ok);
        const auto* const core = dynamic_cast<const MesenSmsSystem*>(rig.builtCore(1));
        REQUIRE(core != nullptr);
        CHECK_FALSE(core->config().enableFm);
        CHECK(core->config().gameGear);
    }
}

TEST_CASE("a document naming a construct-time field's fallback builds the core a document naming none builds",
          "[engine-graph][system-node][settings]") {
    // A fresh role config names every construct-time field at the value buildGraph.ts writes as its fallback
    // (CONSTRUCT_FIELDS), read here out of the TS the way the knob ranges above are. A node builds its core at the
    // config's own default when a field is absent, so a document naming the fallbacks must build the same core: a
    // later native default the fallback does not follow would make a sparse stored config name another value than a
    // fresh one, and an old project would rebuild - losing its state - on its first edit.
    SECTION("NES") {
        NodeRig rig(kRate);
        REQUIRE(rig.set(knobbed("rp.mesen.nes", rig.boot("rp.mesen.nes", 1))).ok);
        const auto* const absent = dynamic_cast<const MesenNesSystem*>(rig.builtCore(1));
        REQUIRE(absent != nullptr);
        const MesenNesConfig atDefaults = absent->config();
        // Every construct-time field, as the document buildGraph writes for a fresh config names it. The expected
        // list is the check, not the source: a field added to CONSTRUCT_FIELDS fails here until its core comparison
        // is added below.
        const auto fields = tsConstructFields("rp.mesen.nes");
        REQUIRE(constructJson(fields) ==
                R"("sdRoot":"","fifo":"instant","fifoDepth":0,"fifoBytesPerSecond":0,"n8Link":"auto",)"
                R"("n8LinkRttUs":2200,"channelExportMode":"mix")");
        REQUIRE(rig.set(knobbed("rp.mesen.nes", rig.boot("rp.mesen.nes", 2, constructJson(fields)))).ok);
        const auto* const core = dynamic_cast<const MesenNesSystem*>(rig.builtCore(2));
        REQUIRE(core != nullptr);
        const MesenNesConfig& cfg = core->config();
        CHECK(cfg.sdRoot == atDefaults.sdRoot);
        CHECK(cfg.fifo == atDefaults.fifo);
        CHECK(cfg.fifoDepth == atDefaults.fifoDepth);
        CHECK(cfg.fifoBytesPerSecond == atDefaults.fifoBytesPerSecond);
        CHECK(cfg.n8Link == atDefaults.n8Link);
        CHECK(cfg.n8LinkRttUs == atDefaults.n8LinkRttUs);
        CHECK(cfg.channelExportMode == atDefaults.channelExportMode);
    }
    SECTION("Master System") {
        NodeRig rig(kRate);
        REQUIRE(rig.set(knobbed("rp.mesen.sms", rig.boot("rp.mesen.sms", 1))).ok);
        const auto* const absent = dynamic_cast<const MesenSmsSystem*>(rig.builtCore(1));
        REQUIRE(absent != nullptr);
        const MesenSmsConfig atDefaults = absent->config();
        const auto fields = tsConstructFields("rp.mesen.sms");
        REQUIRE(constructJson(fields) == R"("enableFm":true)");
        REQUIRE(rig.set(knobbed("rp.mesen.sms", rig.boot("rp.mesen.sms", 2, constructJson(fields)))).ok);
        const auto* const core = dynamic_cast<const MesenSmsSystem*>(rig.builtCore(2));
        REQUIRE(core != nullptr);
        CHECK(core->config().enableFm == atDefaults.enableFm);
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
                                              R"("model":"dmgB","fastBoot":true,"highpass":"off",)"
                                              R"("colorCorrection":"modernAccurate","dmgPalette":"gbl",)"
                                              R"("lightTemperature":0.5,"serialOutCapture":"listen")")))
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
                                              R"("removeSpriteLimit":true,"apuLatencyMs":2,"s5bNoise":"chip",)"
                                              R"("mmc5PhaseReset":"chip")")))
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
        CHECK(sounds(d));  // the seed's window is inside the boot chime
    }
}

TEST_CASE("a battery seed reaches a battery-backed cart, and the built core is what saves it back",
          "[engine-graph][system-node][resources]") {
    // The Game Boy twins' mGB cart has no battery (GB_save_battery_size is 0), so both cores ignore an sram seed there
    // and nothing pins SameBoyBackend's copy. LSDj's cart has one: the seed a node names is what the built core saves.
    // The ROM is a sibling fixture, so the case leaves on a run without one (as CI's).
    const auto rom = resourceFixture("/roms/lsdj/lsdj9_4_1-develop.gb");
    if (rom.empty()) return;
    std::vector<std::uint8_t> sram(128 * 1024);  // LSDj 9.4.1's cart: a pattern no fresh battery holds
    for (std::size_t i = 0; i < sram.size(); ++i) sram[i] = static_cast<std::uint8_t>(i * 7 + 3);
    NodeRig rig(kRate);
    const auto romId = resource(rig, "rom", rom);
    const auto sramId = resource(rig, "sram", sram);
    REQUIRE(rig.set(R"({"nodes":[{"id":"sys-1","type":"rp.sameboy.gb","settings":{)" +
                    rig.boot("rp.sameboy.gb", 1, R"("sram":"res:)" + std::to_string(sramId) + "\"", romId) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"sys-1.mix","to":"out.out.1-2"}]})").ok);
    auto* const built = dynamic_cast<SameBoySystem*>(rig.builtCore(1));
    REQUIRE(built != nullptr);
    CHECK(built->saveSramBytes() == sram);
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
    // restarted into CGB would lose exactly this. SameBoy's own restart discards the savestate it just restored. The
    // model is dmgB (SameBoyConfig's default is cgbC) and the state is taken inside the boot chime, so the seed's next
    // block sounds and a restart into another model is heard as well as compared.
    const auto rom = readFile(RP_MGB_ROM_PATH);
    SameBoyConfig sourceCfg;
    sourceCfg.model = SameBoyModel::DmgB;
    sourceCfg.fastBoot = false;
    auto source = std::make_unique<SameBoySystem>(9, sourceCfg, rom);
    source->onActivate(kRate);
    const auto state = stateAfter(*source, 100);  // about 1.07 s in: the boot chime is still playing
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
                             R"("model":"dmgB","fastBoot":false,"state":"res:)" + std::to_string(stateId) + "\"",
                             romId) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"sys-1.mix","to":"out.out.1-2"}]})").ok);
    // The core the node built is the model the document carried; it never restarted into anything.
    auto* built = dynamic_cast<SameBoySystem*>(rig.builtCore(1));
    REQUIRE(built != nullptr);
    CHECK(built->config_.model == SameBoyModel::DmgB);
    Direct d({direct.get()}, 2, {{0, 1}});
    CHECK(compare(rig, d, 2.0) == "");
    CHECK(sounds(d));
}

TEST_CASE("a NES node built from its ROM resource restores a savestate seed, bit for bit",
          "[engine-graph][system-node][resources]") {
    // A savestate is a build seed, and a Mesen core's restore is a private virtual reached only through
    // seedSramAndState, so this pins the dispatch: the node's core and a directly built one restore the same bytes
    // and render the same block, the N8 flush a restore makes included. The seed is taken while the ROM is playing a
    // note, so a core that skips the restore starts silent and the comparison sees it. The ROM is in the repository,
    // so CI runs it.
    NodeRig rig(kRate);
    auto source = nes(9, 0);
    Direct run({source.get()}, 2, {{0, 1}});
    std::vector<std::uint8_t> state;
    for (int b = 0, at = 0; b < 400 && state.empty(); ++b, at += 512) {
        if (at < static_cast<int>(1.5 * kRate) && at + 512 >= static_cast<int>(1.5 * kRate)) noteOn(*source, 0, 60);
        run.sounded = false;
        run.block(512);
        if (run.sounded) state = source->saveStateBytes();  // the note is sounding across the next boundary
    }
    REQUIRE(!state.empty());
    MesenNesConfig cfg;
    cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
    cfg.savestate = state;
    auto direct = std::make_unique<MesenNesSystem>(1, cfg, readFile(RP_BLIPTOASTER_ROM_PATH));
    direct->onActivate(kRate);
    // The seed's first block carries signal, which is what makes a core that boots fresh instead of restoring audible.
    auto probe = std::make_unique<MesenNesSystem>(1, cfg, readFile(RP_BLIPTOASTER_ROM_PATH));
    probe->onActivate(kRate);
    Direct first({probe.get()}, 2, {{0, 1}});
    first.block(512);
    REQUIRE(sounds(first));
    const auto romId = resource(rig, "rom", readFile(RP_BLIPTOASTER_ROM_PATH));
    const auto stateId = resource(rig, "state", state);
    REQUIRE(rig.set(R"({"nodes":[{"id":"sys-1","type":"rp.mesen.nes","settings":{)" +
                    rig.boot("rp.mesen.nes", 1, R"("state":"res:)" + std::to_string(stateId) + "\"", romId) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"sys-1.mix","to":"out.out.1-2"}]})").ok);
    auto* const built = dynamic_cast<MesenNesSystem*>(rig.builtCore(1));
    REQUIRE(built != nullptr);
    CHECK(built->config().savestate == state);  // the seed reached the built core's config, not only its audio
    Direct d({direct.get()}, 2, {{0, 1}});
    CHECK(compare(rig, d, 1.0) == "");
    CHECK(sounds(d));
}

TEST_CASE("a NES node built from its ROM resource takes its region, export mode and expansion volume",
          "[engine-graph][system-node][resources]") {
    const auto rom = readFile(RP_BLIPTOASTER_ROM_PATH);
    NodeRig rig(kRate);
    MesenNesConfig cfg;
    cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
    cfg.region = static_cast<std::uint32_t>(NesRegion::Ntsc);
    cfg.channelExportMode = 1;  // stereoModPins: three mono streams
    cfg.expansionVolume = 50;   // a direct core built at the volume; the node gives its param at its first block
    auto direct = std::make_unique<MesenNesSystem>(1, cfg, rom);
    direct->onActivate(kRate);
    const auto romId = resource(rig, "rom", rom);
    REQUIRE(rig.set(R"({"nodes":[{"id":"sys-1","type":"rp.mesen.nes","settings":{)" +
                    rig.boot("rp.mesen.nes", 1,
                             R"("region":"ntsc","channelExportMode":"stereoModPins")", romId) +
                    R"(},"params":{"expansionVolume":50}},{"id":"out","type":"rp.host.audio-out",)"
                    R"("settings":{"buses":[1,1,1]}}],"wires":[{"from":"sys-1.pins.pulse","to":"out.out.1"},)"
                    R"({"from":"sys-1.pins.tnd","to":"out.out.2"},{"from":"sys-1.pins.expansion","to":"out.out.3"}]})")
                    .ok);
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
    // the case SKIPs, counted, on a run without one (as CI's) rather than passing as if it had run.
    const auto rom = resourceFixture("/roms/nanoloop287d.gba");
    if (rom.empty()) SKIP("the GBA fixture ROM is absent from the resources tree");
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
    const auto embedded = rp::graph::embeddedResource("mgb");
    // A console's boot as its node makes one: the package's build function, and a config of its own type alive for the
    // call (docs/modular-graph-phase-f2-design.md section 3). The request is the caller's, its ROM copied before the
    // clock starts: what is timed is the console's build, not the copy of its ROM into the request.
    struct Boot {
        const char*                                          name;
        const char*                                          type;
        std::vector<std::uint8_t>                            rom;
        std::function<std::unique_ptr<SystemBase>(SystemBuildRequest)> build;
    };
    std::vector<Boot> boots;
    const auto gameBoy = [](SystemBuildRequest request) {
        SameBoyConfig cfg;
        request.build = rp::sameboy::buildCore;
        request.config = &cfg;
        return request.build(1, std::move(request), kRate, nullptr);
    };
    const auto nes = [](SystemBuildRequest request) {
        MesenNesConfig cfg;
        request.build = rp::mesen::buildNesCore;
        request.config = &cfg;
        return request.build(1, std::move(request), kRate, nullptr);
    };
    const auto sms = [](SystemBuildRequest request) {
        MesenSmsConfig cfg;
        request.build = rp::mesen::buildSmsCore;
        request.config = &cfg;
        return request.build(1, std::move(request), kRate, nullptr);
    };
    const auto gba = [](SystemBuildRequest request) {
        MesenGbaConfig cfg;
        request.build = rp::mesen::buildGbaCore;
        request.config = &cfg;
        return request.build(1, std::move(request), kRate, nullptr);
    };
    boots.push_back({"mGB (embedded)", "rp.sameboy.gb", {embedded.begin(), embedded.end()}, gameBoy});
    if (auto lsdj = resourceFixture("/roms/lsdj/lsdj9_4_1-develop.gb"); !lsdj.empty())
        boots.push_back({"LSDj", "rp.sameboy.gb", std::move(lsdj), gameBoy});
    boots.push_back({"NES", "rp.mesen.nes", readFile(RP_BLIPTOASTER_ROM_PATH), nes});
    boots.push_back({"Master System", "rp.mesen.sms", readFile(RP_SMS_ROM_PATH), sms});
    if (auto gbaRom = resourceFixture("/roms/nanoloop287d.gba"); !gbaRom.empty())
        boots.push_back({"GBA", "rp.mesen.gba", std::move(gbaRom), gba});

    std::printf("\nboot through rp.host.systems/2's build at %.1f kHz: construct, onActivate (ROM, battery, savestate), the "
                "state snapshot, and the same build through setGraph (slot claim and one plan with it); median of %d runs\n",
                kRate / 1000.0, kRuns);
    for (auto& boot : boots) {
        std::vector<double> built, planned;
        for (int run = 0; run < kRuns; ++run) {
            SystemBuildRequest request;
            request.rom = boot.rom;
            const auto t0 = Clock::now();
            std::unique_ptr<SystemBase> core = boot.build(std::move(request));
            built.push_back(micros(t0));
            REQUIRE(core);
        }
        // The same boot where it happens in the engine: a plan with one system node, published on a quiescent engine.
        Engine engine(kRate);
        const std::string type = boot.type;
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

// The block thread's allocation promise (docs/modular-graph-phase-c4-design.md section 4): a warmed core's block
// allocates and frees nothing, emulation included. Two kinds of case hold it.
//
// The AUDIO PATH's own case drains a ring a previous block filled, so no emulated frame runs inside its window: the
// ring and the scratch work one block does, at ragged sizes, with the ring already holding what the block drains (the
// ring's fill is the block clock, so a full ring means nothing steps). A Mesen core collects its audio in a fixed ring
// (MesenAudioDevice), and sizes its per-block scratches, the tap's rings, its resample scratch and each stream's blip
// at activation, so that path allocates nothing at any block size a host splits to, and arming the tap allocates
// nothing either (it re-initialises a stream's blip in place; see blip_reset).
//
// The WHOLE-BLOCK case runs the node's whole triad inside the window - prepare, step, finish - so it also holds what a
// real block carries: Mesen's per-frame video path (BaseControlManager::GetPortStates, the controller states and the
// RenderedFrame input snapshot through VideoDecoder, the notification dispatch) and SameBoy's own per-frame work.
// Both console paths are Mesen's frame plumbing and ours; neither allocates once the core is warm.

namespace {

// Host block sizes this file's other cases never run a core at, as parity/blocks-tiny.ts records the FL shapes: 1-frame
// blocks between odd sizes, a small block, and the plan's maximum, the widest swing a block's buffers have to survive.
constexpr std::array<std::uint32_t, 8> kRaggedBlocks = {33, 1, 222, 1, 7, 4096, 2, 8192};

// One block as the node runs it: zero the lanes the system sums into, prepare, and - when `fill` - run the step phase
// between the prepare and the drain. That order is the node's own triad, so the per-block counters the SMS and the NES
// reset in prepare are reset BEFORE the step and not after it. `frames` is a ragged size from kRaggedBlocks, or 512 for
// the long tapped run, so the scratches and the tap's rings see every shape. kRaggedBlocks drains ~12.5 k frames: well
// under SystemBase's 0.5 s snapshot interval (24 k frames at 48 kHz), so no savestate capture - which allocates, in
// Mesen's serializer - can land in a counted window. A counted stretch longer than that has to account for the publish
// (chargeSnapshotInterval below).
//
// The step stays OUTSIDE this window: the case holds the drain's own promise (the ring, the scratches, the tap), and
// the whole-block case below runs the step inside its window too.
rp::test::AllocationCount drainBlock(SystemBase& core, std::uint32_t frames, float* const* lanes, std::size_t laneCount,
                                     bool count, bool fill) {
    for (std::size_t l = 0; l < laneCount; ++l) std::fill_n(lanes[l], frames, 0.0f);
    core.setBlockLanes(laneCount);  // what a node's prepare tells a core: which ring this block drains
    core.prepareForBlock(info(frames));
    if (fill) {
        while (core.stepIfBelowTarget(frames)) {
        }
    }
    if (count) rp::test::beginCountingAllocations(true);
    core.finishBlock(info(frames), lanes, laneCount);
    return count ? rp::test::endCountingAllocations() : rp::test::AllocationCount{};
}

// Every ragged size on `core`, drained through `lanes`. The block drained at each size is the FIRST one at that size,
// filled by real emulation just before it (a window that found an empty ring would measure the zero-fill instead of
// the drain): a buffer that still grew lazily at a new high-water mark would allocate here. One 1-frame block is run
// first, so none of the counted drains is a core's first block: a first block's storage filling, and the boot capture
// a backend-built core arms, is the first-block case below; this one holds the drain on an already-running core.
void checkRaggedDrainsAreFree(SystemBase& core, const std::vector<float*>& lanes) {
    drainBlock(core, 1, lanes.data(), lanes.size(), false, true);
    for (const std::uint32_t frames : kRaggedBlocks) {
        const auto counted = drainBlock(core, frames, lanes.data(), lanes.size(), true, true);
        INFO(frames << " frames: " << counted.allocations << " allocations, " << counted.frees << " frees");
        REQUIRE(counted.allocations == 0);
        REQUIRE(counted.frees == 0);
    }
}

// The samples the step phase captured for the block in progress, before finishBlock drains them. Every backend stops
// stepping at the block's frame count, so "at least `frames`" is what proves the block was filled by emulation: a core
// whose step loop stopped, or a plan that refused the block, leaves the ring short, finishBlock blanks what is missing,
// and the window would have been vacuously free.
std::uint32_t capturedFrames(SystemBase& core) {
    if (auto* nes = dynamic_cast<MesenNesSystem*>(&core)) return nes->availableFrames();
    if (auto* sms = dynamic_cast<MesenSmsSystem*>(&core)) return sms->availableFrames();
    if (auto* gba = dynamic_cast<MesenGbaSystem*>(&core)) return gba->availableFrames();
    if (auto* gb = dynamic_cast<SameBoySystem*>(&core)) return gb->audioFrameCount_;
    return 0;
}

// Host input a counted block carries: the calls a system node's applyInputs makes at the top of its prepare, before
// the core's prepareForBlock, at the frames the node's input queues tag each message with. A core's ingress queues are
// sized at activation from what a block's input can deliver, so staging a block's input grows nothing.
struct NoInput {
    void operator()(SystemBase&, std::uint32_t) const {}
};

// A NES's block of host input: MIDI at its frames (notes and clocks, which the N8 FIFO's role queues for the sample
// offset each message carries and, under BlipToaster's reliable link, frames as it releases them), a raw run on the
// node's `bytes` input, a message longer than a MidiEvent holds down that same raw door, and a burst of presses.
struct NesInput {
    void operator()(SystemBase& core, std::uint32_t frames) const {
        const auto note = [&core](std::uint32_t frame, std::uint8_t status, std::uint8_t key) {
            ::MidiEvent ev{};
            ev.frame = frame;
            ev.size  = 3;
            ev.data[0] = status;
            ev.data[1] = key;
            ev.data[2] = 100;
            core.onMidi(&ev, 1);
        };
        note(frames / 8, 0x90, 60);
        note(frames / 2, 0x80, 60);
        ::MidiEvent clock{};
        clock.frame   = frames / 4;
        clock.size    = 1;
        clock.data[0] = 0xF8;
        core.onMidi(&clock, 1);
        clock.frame = frames / 3;
        core.onMidi(&clock, 1);
        const std::uint8_t raw[] = {0x90, 67, 100, 0xF8, 0x80, 67, 0x00};
        core.pushCoreBytes(frames / 8, raw, sizeof raw, false);
        // A settings upload's shape: a SysEx far longer than a MidiEvent holds, down the raw door a node hands one to
        // (pushCoreBytes), which is also what a protocol node's bytes input reaches.
        std::array<std::uint8_t, 256> sysex{};
        sysex.front() = 0xF0;
        sysex.back()  = 0xF7;
        core.pushCoreBytes(frames / 3, sysex.data(), sysex.size(), false);
        for (int k = 0; k < 12; ++k)
            core.pressButton(static_cast<std::uint8_t>(k % 8), (k % 2) == 0);
    }
};

// A Master System's block of host input: the transport's level byte at its sample offset, and a burst of presses.
struct SmsInput {
    void operator()(SystemBase& core, std::uint32_t frames) const {
        const std::uint8_t levels[] = {0xFF, 0xF7, 0x7F, 0x77};
        core.pushCoreBytes(frames / 2, levels, sizeof levels, false);
        for (int k = 0; k < 12; ++k)
            core.pressButton(static_cast<std::uint8_t>(k % 8), (k % 2) == 0);
    }
};

// A Game Boy's block of host input: the MIDI mGB hears, on its serial port, a message's bytes at its message's frame,
// and a burst of presses.
struct GameBoyInput {
    void operator()(SystemBase& core, std::uint32_t frames) const {
        // A run of notes and clocks, each message's bytes at its own frame: a library deque allocates its first node
        // on the first push, so a queue that grew on the block thread would show even though it need not grow every
        // block. The 64 presses a block outrun what their 10 ms spacing consumes, so the press queue stays deep
        // (the stems half takes a fresh core rather than filling it).
        for (int m = 0; m < 24; ++m) {
            const std::uint8_t status = (m % 4 == 3) ? 0xF8 : (m % 2 == 0 ? 0x90 : 0x80);
            const std::uint8_t bytes[] = {status, static_cast<std::uint8_t>(48 + m), 100};
            for (const std::uint8_t byte : bytes) core.pushSerialIn(frames * std::uint32_t(m + 1) / 32, byte);
        }
        for (int k = 0; k < 64; ++k)
            core.pressButton(static_cast<std::uint8_t>(k % 8), (k % 2) == 0);
    }
};

// A GBA's block of host input: presses, the one door its core offers.
struct GbaInput {
    void operator()(SystemBase& core, std::uint32_t) const {
        for (int k = 0; k < 12; ++k)
            core.pressButton(static_cast<std::uint8_t>(k % 8), (k % 2) == 0);
    }
};

// One whole block as the node runs it, with the step phase inside the counted window: the block's host input, prepare,
// step until the core has filled the block, finish. This is the promise's real shape - a block with emulated frames,
// video frames, controller polls, host input and notifications in it - so it counts everything the block does, not just
// the drain, and the block it counts is proven filled above.
template <class Stage>
rp::test::AllocationCount wholeBlock(SystemBase& core, std::uint32_t frames, const std::vector<float*>& lanes,
                                     bool count, Stage stage) {
    for (float* const lane : lanes) std::fill_n(lane, frames, 0.0f);
    if (count) rp::test::beginCountingAllocations(true);
    stage(core, frames);
    core.setBlockLanes(lanes.size());  // what a node's prepare tells a core: which ring this block drains
    core.prepareForBlock(info(frames));
    while (core.stepIfBelowTarget(frames)) {
    }
    const std::uint32_t captured = count ? capturedFrames(core) : 0;  // read, not asserted, inside the window
    core.finishBlock(info(frames), lanes.data(), lanes.size());
    const auto counted = count ? rp::test::endCountingAllocations() : rp::test::AllocationCount{};
    // After the window: under -s or a reporter that records every assertion, a REQUIRE builds assertion stats, which
    // allocate, and every counted window would fail spuriously.
    if (count) REQUIRE(captured >= frames);
    return counted;
}

// A hash of a core's whole work RAM, to hold that a counted window really emulated: a plan that refused the block
// (Engine::processBlock's early return), a node that stopped stepping, or a core that stepped without running the
// console leaves the memory where it was. Read outside the counted window, on both sides of it.
std::uint64_t ramHash(SystemBase& core) {
    auto ram = core.getMemory(rp::MemoryType::Ram, rp::AccessType::Read);
    REQUIRE(ram.valid());
    std::uint64_t hash = 1469598103934665603ull;
    for (std::size_t i = 0; i < ram.size(); ++i) {
        hash ^= ram[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

// Warm `core` through real blocks, then run every ragged size as a counted WHOLE block, the first block at each size
// being the counted one. The warm run is deliberately below the ragged set's maximum, so the 8192-frame window is still
// the first block at that size: a buffer that grew lazily at a new high-water mark would allocate there. ~4 k frames of
// warm-up is several emulated frames' worth of Mesen's frame path and of SameBoy's own per-frame work, so what remains
// is steady state, and it is long enough that a control manager which registers a port of its own has done so. A
// device's state capacity is its constructor's (`ClearState` keeps it), so no poll can be what sizes it.
template <class Stage>
void checkWholeBlocksAreFree(SystemBase& core, const std::vector<float*>& lanes, Stage stage) {
    core.pressButton(0, true);
    wholeBlock(core, 1024, lanes, false, stage);
    core.pressButton(0, false);
    for (int b = 0; b < 3; ++b) wholeBlock(core, 1024, lanes, false, stage);
    for (const std::uint32_t frames : kRaggedBlocks) {
        // A window holds what it staged landed, not only that it was free: nothing it delivered was refused. A
        // queue that grew instead of dropping would show in the allocation count above, and one sized 0 or an arena
        // bound off by one would show here.
        const std::uint64_t droppedBefore = core.inputDropped();
        const auto counted = wholeBlock(core, frames, lanes, true, stage);
        INFO(frames << " frames: " << counted.allocations << " allocations, " << counted.frees << " frees, "
                    << (core.inputDropped() - droppedBefore) << " dropped");
        REQUIRE(counted.allocations == 0);
        REQUIRE(counted.frees == 0);
        REQUIRE(core.inputDropped() == droppedBefore);
    }
}

// A core the Engine built publishes a whole savestate every 0.5 s of block frames (SystemBase's accumulator), and that
// capture allocates, in Mesen's streaming serializer. Charge a whole interval uncounted, through the publish call
// itself, so the capture happens HERE and not inside a counted window: every counted stretch below then stays under
// the interval (24 k frames at this file's rate) and cannot reach it.
void chargeSnapshotInterval(SystemBase& core) {
    core.publishStateSnapshot(static_cast<std::uint32_t>(kRate * 0.5), kRate);
}

}  // namespace

TEST_CASE("a Mesen audio ring keeps a block in fixed storage, draining, wrapping and growing without allocating",
          "[engine-graph][mesen][audio-ring][alloc]") {
    // One PlayBuffer can never carry more than Mesen's own output buffer holds, and a core built outside a plan
    // defaults to the engine's plan size (a node hands its own in at activation).
    STATIC_REQUIRE(MesenAudioDevice::kMaxPlayBufferFrames == 0x8000);
    STATIC_REQUIRE(MesenAudioDevice::kDefaultBlockFrames == Engine::kDefaultMaxBlockFrames);
    STATIC_REQUIRE(SystemBase::kDefaultBlockFrames == Engine::kDefaultMaxBlockFrames);

    MesenAudioDevice ring;
    CHECK(ring.capacityFrames() == MesenAudioDevice::kDefaultBlockFrames + MesenAudioDevice::kMaxPlayBufferFrames);
    CHECK(ring.availableFrames() == 0);

    // A frame's samples are (n, -n) mod 30000, so a drained frame says where it was written.
    const auto put = [](std::uint32_t first, std::uint32_t count, std::vector<int16_t>& into) {
        into.resize(std::size_t(count) * 2);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto v = static_cast<std::int16_t>(static_cast<std::int32_t>((first + i) % 30000) - 15000);
            into[i * 2] = v;
            into[i * 2 + 1] = static_cast<std::int16_t>(-v);
        }
    };
    // What `count` drained frames from written index `first` must be, bit for bit (the ring normalises with the same
    // 1/32768). The first frame that is not, or "" when they all are.
    const auto expect = [](std::uint32_t first, std::uint32_t count, const float* got) -> std::string {
        constexpr float kScale = 1.0f / 32768.0f;
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto v = static_cast<std::int32_t>((first + i) % 30000) - 15000;
            if (got[i * 2] != static_cast<float>(v) * kScale || got[i * 2 + 1] != static_cast<float>(-v) * kScale)
                return "frame " + std::to_string(first + i) + ": " + std::to_string(got[i * 2]) + " / " +
                       std::to_string(got[i * 2 + 1]);
        }
        return "";
    };

    std::vector<int16_t> in;
    std::vector<float>   out;

    SECTION("fill, drain, and the frame count the block clock reads") {
        put(0, 5, in);
        ring.PlayBuffer(in.data(), 5, 48000, true);
        CHECK(ring.availableFrames() == 5);
        out.resize(10);
        CHECK(ring.drain(out.data(), 3) == 3);
        CHECK(ring.availableFrames() == 2);
        CHECK(expect(0, 3, out.data()) == "");
        CHECK(ring.drain(out.data(), 8) == 2);
        CHECK(ring.availableFrames() == 0);
        CHECK(expect(3, 2, out.data()) == "");
        CHECK(ring.drain(out.data(), 8) == 0);
    }

    SECTION("wrap: a tail that runs off the end continues at the front") {
        const std::uint32_t capacity = ring.capacityFrames();
        put(0, capacity - 100, in);
        ring.PlayBuffer(in.data(), capacity - 100, 48000, true);
        out.resize(std::size_t(capacity) * 2);
        CHECK(ring.drain(out.data(), 500) == 500);
        CHECK(expect(0, 500, out.data()) == "");
        // The tail now sits 100 frames short of the end, so this write crosses it: 100 frames at the end, 500 at the
        // front, over the space the drain just freed.
        put(capacity - 100, 600, in);
        ring.PlayBuffer(in.data(), 600, 48000, true);
        CHECK(ring.availableFrames() == capacity);
        CHECK(ring.drain(out.data(), capacity - 100) == capacity - 100);
        CHECK(expect(500, capacity - 600, out.data()) == "");
        CHECK(expect(capacity - 100, 100, out.data() + std::size_t(capacity - 600) * 2) == "");
        // The drain crosses the end 400 frames in: those 400 values sit at the front of the ring, where the write's
        // second half put them, and are read by the drain's second range.
        CHECK(expect(capacity, 400, out.data() + std::size_t(capacity - 500) * 2) == "");
        CHECK(ring.availableFrames() == 100);
        // And the rest of the write, which the drain above did not reach.
        CHECK(ring.drain(out.data(), 100) == 100);
        CHECK(expect(capacity + 400, 100, out.data()) == "");
        CHECK(ring.availableFrames() == 0);
    }

    SECTION("a resize on re-activation keeps what the ring holds") {
        put(0, 7, in);
        ring.PlayBuffer(in.data(), 7, 48000, true);
        ring.reserveAudio(65536);  // a host grew its block: the node re-activates and the ring learns the new maximum
        CHECK(ring.capacityFrames() == 65536 + MesenAudioDevice::kMaxPlayBufferFrames);
        CHECK(ring.availableFrames() == 7);
        out.resize(14);
        CHECK(ring.drain(out.data(), 7) == 7);
        CHECK(expect(0, 7, out.data()) == "");
        // Growth only, and a smaller request is not a shrink.
        ring.reserveAudio(1);
        CHECK(ring.capacityFrames() == 65536 + MesenAudioDevice::kMaxPlayBufferFrames);
    }

    SECTION("a resize while the queued frames wrap keeps them, in order") {
        // The re-activation copy has two ranges to move (the tail before the end and the head after it). The section
        // above grows a queue whose head is 0, so only the first runs; this one wraps first, then grows.
        const std::uint32_t capacity = ring.capacityFrames();
        put(0, capacity - 100, in);
        ring.PlayBuffer(in.data(), capacity - 100, 48000, true);
        out.resize(std::size_t(capacity) * 2);
        CHECK(ring.drain(out.data(), 500) == 500);  // head moves 500 frames into the buffer
        put(capacity - 100, 600, in);
        ring.PlayBuffer(in.data(), 600, 48000, true);  // wraps: the queue now starts 500 in and continues at the front
        CHECK(ring.availableFrames() == capacity);
        ring.reserveAudio(65536);  // larger than the current capacity, so the frames are copied out of the ring
        CHECK(ring.capacityFrames() == 65536 + MesenAudioDevice::kMaxPlayBufferFrames);
        CHECK(ring.availableFrames() == capacity);
        std::vector<float> all(std::size_t(capacity) * 2);
        CHECK(ring.drain(all.data(), capacity) == capacity);
        // What was queued before the wrap, then the write that wrapped, still in arrival order and bit for bit.
        CHECK(expect(500, capacity - 600, all.data()) == "");
        CHECK(expect(capacity - 100, 600, all.data() + std::size_t(capacity - 600) * 2) == "");
        CHECK(ring.availableFrames() == 0);
    }

    SECTION("a write past capacity drops the newest and counts them") {
        // The drop policy is unreachable in production, so the ring asserts when it happens (a debug build): it is only
        // holdable where that assert is compiled out, which is every configured build here but a debug one.
#ifndef NDEBUG
        SKIP("a debug build asserts on the overflow this provokes");
#endif
        const std::uint32_t capacity = ring.capacityFrames();
        put(0, capacity, in);
        ring.PlayBuffer(in.data(), capacity, 48000, true);
        put(capacity, 5, in);
        ring.PlayBuffer(in.data(), 5, 48000, true);
        CHECK(ring.overflowFrames() == 5);
        CHECK(ring.availableFrames() == capacity);
        out.resize(std::size_t(capacity) * 2);
        CHECK(ring.drain(out.data(), capacity) == capacity);
        // What was already queued survived in order; the five frames that did not fit are gone, not wrapped over it.
        CHECK(expect(0, capacity, out.data()) == "");
    }

    SECTION("a warm ring allocates nothing to write or drain") {
        std::array<int16_t, 1024 * 2>  chunk{};
        std::array<float, 8192 * 2>    dest{};
        for (int i = 0; i < int(chunk.size()); ++i) chunk[std::size_t(i)] = static_cast<std::int16_t>(i);
        rp::test::beginCountingAllocations();
        for (int b = 0; b < 64; ++b) {
            ring.PlayBuffer(chunk.data(), 1024, 48000, true);
            ring.drain(dest.data(), 1024);
            (void)ring.availableFrames();
        }
        const auto counted = rp::test::endCountingAllocations();
        INFO("ring: " << counted.allocations << " allocations, " << counted.frees << " frees");
        CHECK(counted.allocations == 0);
        CHECK(counted.frees == 0);
    }
}

TEST_CASE("a NES whose tap is armed over the mix keeps its mix ring to one block, however long it runs",
          "[engine-graph][system-node][mesen][tap][alloc]") {
    // The narrow-host path: TS wires a single NES's mix on a host too narrow for its split, and the engine still arms
    // the tap (docs/modular-graph-phase-b0-design.md section 2.5). The step gates on ONE of the core's rings and the
    // mix path drains the other, so the mix ring used to keep each block's flush overshoot: at 512-frame blocks about
    // 25 frames a block, which filled C4's fixed ring after about 17 s and then dropped audio every block. The core
    // gates on the ring the block drains (SystemNode::prepare tells it the lane count), so the mix ring keeps at most
    // the overshoot of one flush, as the plain mix path's does, and nothing is dropped.
    const auto oneFlush = [](MesenNesSystem& core) {
        // The most one gate's overshoot can be: the APU flush window (apuLatencyMs) in host frames.
        return static_cast<std::uint32_t>(std::ceil(core.config().apuLatencyMs / 1000.0 * kRate)) + 1;
    };
    const auto narrowHost = [](Engine& e, rp::host::test::Roms& roms) {
        auto systems = std::vector<rp::host::test::DocSystem>{{1, rp::host::test::kNesType}};
        rp::host::test::withRoms(e, systems, roms);
        // What buildGraph sends for a split routing on a 2-lane host: the mix wired, the tap armed anyway.
        REQUIRE(e.setGraph(rp::host::test::rearming(
                            rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1}), 1, 1))
                    .ok);
        auto* const core = dynamic_cast<MesenNesSystem*>(e.findSystem(1));
        REQUIRE(core != nullptr);
        REQUIRE(core->channelExportMode() == 1);
        return core;
    };

    SECTION("past the capacity the drift filled, in one-frame blocks") {
        // A one-frame block needs one flush to satisfy the gate and drains one frame, so the old gate left a whole
        // flush in the mix ring every block: 40960 frames of ring in about 620 blocks, then a drop every block. 2000
        // blocks run well past that, and the fill stays within one block plus one flush the whole way.
        Engine e(kRate);
        rp::host::test::Roms roms;
        auto* const core = narrowHost(e, roms);
        const std::uint32_t bound = 1 + oneFlush(*core);
        Lanes lanes(2);
        for (int b = 0; b < 2000; ++b) {
            e.processBlock(1, lanes.ptrs.data(), 2);
            INFO("block " << b << ": fill " << core->audioRingFill() << " of " << core->audioRingCapacity());
            CHECK(core->audioRingFill() <= bound);
        }
        CHECK(core->audioRingOverflow() == 0);
        REQUIRE(core->channelExportMode() == 1);  // still armed: the case ran the defect's path, not a plain mix
        CHECK(core->tapOverflow() == 0);

        // And the drain of a real block, with emulated frames in it, allocates nothing (the snapshot interval is
        // charged first: 2000 one-frame blocks plus this stretch stay under it).
        chargeSnapshotInterval(*core);
        for (int b = 0; b < 16; ++b) {
            for (float* const lane : lanes.ptrs) std::fill_n(lane, 512, 0.0f);
            rp::test::beginCountingAllocations(true);
            e.processBlock(512, lanes.ptrs.data(), 2);
            const auto counted = rp::test::endCountingAllocations();
            INFO("block " << b << ": " << counted.allocations << " allocations, " << counted.frees << " frees");
            REQUIRE(counted.allocations == 0);
            REQUIRE(counted.frees == 0);
            CHECK(core->audioRingFill() <= 512 + oneFlush(*core));
        }
        CHECK(core->audioRingOverflow() == 0);
    }

    SECTION("a driver that does not say which ring it drains keeps the tap's gate") {
        // SystemBase::setBlockLanes is how a driver says what its block drains; 0 (the default) is "not told", and a
        // core then keeps its own rule: the tap's rings whenever they are armed. The engine's node always tells, so
        // this is a test or tool's path, and it is held here rather than left as an accident: driven untold, the mix
        // ring keeps each block's flush overshoot (the tap ring is discarded and starts empty), so its fill grows past
        // one flush; told the two mix lanes, the same core drains the ring it gates on and stays bounded.
        Lanes untold(2), told(2);
        auto untoldCore = nes(1, 1);
        auto toldCore = nes(1, 1);
        untoldCore->reserveAudio(8192);
        toldCore->reserveAudio(8192);
        for (int b = 0; b < 8; ++b) {
            for (float* const lane : untold.ptrs) std::fill_n(lane, 512, 0.0f);
            untoldCore->prepareForBlock(info(512));
            while (untoldCore->stepIfBelowTarget(512)) {
            }
            untoldCore->finishBlock(info(512), untold.ptrs.data(), 2);
            drainBlock(*toldCore, 512, told.ptrs.data(), 2, false, true);
        }
        CHECK(untoldCore->availableFrames() == 0);  // the gate's metric is the tap's rings, which the mix drain emptied
        // Each block's flush overshoot stayed in the mix ring, so the fill is several flushes deep.
        CHECK(untoldCore->audioRingFill() > 1 + oneFlush(*untoldCore));
        CHECK(toldCore->audioRingFill() <= 1 + oneFlush(*toldCore));
    }

    SECTION("a short pins block does not set the mix discard's chunk") {
        // The mix path discards the tap's rings through chanAccum_[0], whose size a pins block assigns to ITS block
        // size. A one-frame pins block must not leave the discard draining a frame at a time, and a zero-frame one
        // must not leave a zero-size scratch at all: that loop would never advance. The engine never hands a block of
        // zero frames to finishBlock, so the zero-frame half drives the triad directly, as a tool could.
        auto core = nes(1, 1);
        core->reserveAudio(8192);
        Lanes pins(3), mix(2);
        std::vector<float*> tap;
        for (std::size_t k = 0; k < 3; ++k) {
            tap.push_back(pins.ptrs[k]);
            tap.push_back(pins.ptrs[k]);
        }
        drainBlock(*core, 1, tap.data(), tap.size(), false, true);      // a pins block of one frame
        auto counted = drainBlock(*core, 512, mix.ptrs.data(), 2, true, true);
        INFO("after a 1-frame pins block: " << counted.allocations << " allocations, " << counted.frees << " frees");
        CHECK(counted.allocations == 0);
        CHECK(counted.frees == 0);
        CHECK(core->tapDiscardChunk() == 8192);  // the reserved block, not the one-frame scratch the pins block left
        CHECK(core->audioRingOverflow() == 0);
        CHECK(core->tapOverflow() == 0);
        drainBlock(*core, 0, tap.data(), tap.size(), false, false);     // a pins block of none: the scratch empties
        counted = drainBlock(*core, 512, mix.ptrs.data(), 2, true, true);
        INFO("after a 0-frame pins block: " << counted.allocations << " allocations, " << counted.frees << " frees");
        CHECK(counted.allocations == 0);
        CHECK(counted.frees == 0);
        // The chunk is observed, not assumed: a zero one ends the discard with frames left (the guard in finishBlock)
        // and fails here, where relying on the loop's progress would have hung the suite instead.
        CHECK(core->tapDiscardChunk() == 8192);
        CHECK(core->audioRingOverflow() == 0);
        CHECK(core->tapOverflow() == 0);
    }

    SECTION("a narrow host hears exactly what the mix alone hears") {
        // What the fix buys the user: continuing audio at one block's latency. The mix path drains the mix ring and
        // gates on it, exactly as an unarmed NES does, so the tap changes nothing the host hears: no drift, no dropped
        // frames, no discontinuity at a block boundary. The comparison carries sound, which is what makes a drift
        // audible: BlipToaster ignores MIDI until 1.5 s have rendered, so both cores get the first note after that
        // boot, and notes keep coming for the rest of the run (a tracker note sustains until another replaces it).
        Engine wide(kRate), narrow(kRate);
        rp::host::test::Roms wideRoms, narrowRoms;
        std::vector<rp::host::test::DocSystem> systems{{1, rp::host::test::kNesType}};
        rp::host::test::withRoms(wide, systems, wideRoms);
        REQUIRE(wide.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1})).ok);
        auto* const plain = wide.findSystem(1);
        REQUIRE(plain != nullptr);
        auto* const tapped = narrowHost(narrow, narrowRoms);
        // 400 blocks of 512 are 4.27 s: the ROM's boot, then over two seconds of the notes sounding, which is 200+
        // blocks of drift on the old gate (about 25 frames a block, so the mix ring's tail is heard a block late).
        Lanes mixed(2), stereo(2);
        constexpr std::uint8_t kKeys[] = {60, 64, 67, 72};
        std::uint64_t at = 0;
        bool heard = false;
        for (int b = 0; b < 400; ++b, at += 512) {
            for (const std::uint8_t key : kKeys)
                if (at >= static_cast<std::uint64_t>(1.5 * kRate) + std::uint64_t(key - kKeys[0]) * 12000 &&
                    at - 512 < static_cast<std::uint64_t>(1.5 * kRate) + std::uint64_t(key - kKeys[0]) * 12000) {
                    noteOn(*plain, 0, key);
                    noteOn(*tapped, 0, key);
                }
            std::fill_n(mixed.ptrs[0], 512, 0.0f);
            std::fill_n(mixed.ptrs[1], 512, 0.0f);
            std::fill_n(stereo.ptrs[0], 512, 0.0f);
            std::fill_n(stereo.ptrs[1], 512, 0.0f);
            narrow.processBlock(512, mixed.ptrs.data(), 2);
            wide.processBlock(512, stereo.ptrs.data(), 2);
            INFO("block " << b);
            CHECK(mixed.data[0] == stereo.data[0]);
            CHECK(mixed.data[1] == stereo.data[1]);
            for (float v : stereo.data[0]) heard = heard || v != 0.0f;
        }
        REQUIRE(heard);  // the comparison above held two silent lanes would hold nothing
        CHECK(tapped->audioRingOverflow() == 0);
        // The console ran the same frames too: the tap changes nothing the game sees either (the two rings fill in
        // lockstep, and the block that drains the mix gates on the mix).
        CHECK(ramHash(*tapped) == ramHash(*plain));
    }
}

TEST_CASE("a Mesen core's audio path drains without allocating at every block size",
          "[engine-graph][system-node][alloc]") {
    // The audio path's own promise. Warm, then one block at each ragged size with the ring already holding what the
    // block drains: the drain, the scratches and the tap's rings, with no emulated frame in the window. The step phase
    // is where Mesen's frame plumbing allocates, and it is outside every window here.
    SECTION("NES, mixed") {
        auto core = nes(1, 0);
        core->reserveAudio(8192);  // what a node's activate does with its plan's maximum
        Lanes lanes(2);
        checkRaggedDrainsAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]});
        CHECK(core->audioRingOverflow() == 0);
    }
    SECTION("NES, tap armed, its pins wired") {
        auto core = nes(1, 1);
        core->reserveAudio(8192);
        Lanes lanes(3);
        // The node points both lanes of a mono stream at its one port buffer.
        std::vector<float*> tap;
        for (std::size_t k = 0; k < 3; ++k) {
            tap.push_back(lanes.ptrs[k]);
            tap.push_back(lanes.ptrs[k]);
        }
        checkRaggedDrainsAreFree(*core, tap);
        CHECK(core->tapOverflow() == 0);
    }
    SECTION("NES, tap armed, driven through the mix") {
        // A split render's boot settle: the tap captures while the block is fanned out as the mix, so finishBlock also
        // drains and discards the tap's rings, a block at a time.
        auto core = nes(1, 1);
        core->reserveAudio(8192);
        Lanes lanes(2);
        checkRaggedDrainsAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]});
        CHECK(core->tapOverflow() == 0);
        CHECK(core->audioRingOverflow() == 0);
    }
    SECTION("SMS") {
        auto core = sms(1);
        core->reserveAudio(8192);
        Lanes lanes(2);
        checkRaggedDrainsAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]});
        CHECK(core->audioRingOverflow() == 0);
    }
    SECTION("GBA") {
        // A sibling fixture, so this SKIPs, counted, on a run without one (as CI's) rather than passing silently.
        const auto rom = resourceFixture("/roms/nanoloop287d.gba");
        if (rom.empty()) SKIP("the GBA fixture ROM is absent from the resources tree");
        auto core = std::make_unique<MesenGbaSystem>(1, MesenGbaConfig{}, rom);
        core->onActivate(kRate);
        core->reserveAudio(8192);
        Lanes lanes(2);
        checkRaggedDrainsAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]});
        CHECK(core->audioRingOverflow() == 0);
    }
}

TEST_CASE("a warmed core's whole block allocates and frees nothing, emulation included",
          "[engine-graph][system-node][alloc]") {
    // The promise's other half: a REAL block, with the step phase in the counted window, so the video frames,
    // controller polls and notifications a block carries are counted too, and with the block's host input staged
    // inside the window (the calls a node's applyInputs makes before the core's prepareForBlock), so what a MIDI run,
    // a byte run and a press burst do to a core's ingress queues is counted rather than assumed. Each core is built
    // directly (as the cases above build theirs), warmed by 4 x 1024 frames and then run at every ragged size, and
    // every core is expected to be free at every size.
    SECTION("NES, mixed") {
        auto core = nes(1, 0);
        core->reserveAudio(8192);  // what a node's activate does with its plan's maximum
        Lanes lanes(2);
        // Every block stages the same 271 non-press bytes (two notes, two clocks, a 7-byte raw run and a 256-byte
        // SysEx), so the twelve the helper runs must have taken exactly that many: presses alone would leave a refused
        // MIDI or raw run invisible.
        const auto bytesBefore = core->acceptedBytes();
        const auto droppedBefore = core->inputDropped();
        checkWholeBlocksAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]}, NesInput{});
        CHECK(core->audioRingOverflow() == 0);
        CHECK(core->acceptedBytes() - bytesBefore == 12 * (3 + 3 + 1 + 1 + 7 + 256));
        CHECK(core->inputDropped() == droppedBefore);
    }
    SECTION("NES, its reliable link on") {
        // The link's own block-thread work: every message the host sends is framed as the block's emulation reaches its
        // offset (a window entry, a sync ahead of the first frame, a re-send when one is due), and the 256-byte SysEx,
        // longer than a frame can carry, is dropped by the link's framing: its bytes never reach the ROM, which is why
        // the run is in the accepted count below but not in the frames sent.
        auto core = linkedNes(1);
        core->reserveAudio(8192);
        Lanes lanes(2);
        const auto bytesBefore = core->acceptedBytes();
        const auto droppedBefore = core->inputDropped();
        checkWholeBlocksAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]}, NesInput{});
        CHECK(core->acceptedBytes() - bytesBefore == 12 * (3 + 3 + 1 + 1 + 7 + 256));
        CHECK(core->inputDropped() == droppedBefore);
        const auto stats = core->coreTransportStats();
        CHECK(stats.linkFrames > 0);  // the frames really were built and sent, not skipped by a link that stayed off
    }
    SECTION("NES, tap armed, its pins wired") {
        // The tap's own per-block work: the pin blips, the per-stream resamplers and the capture rings, every block.
        auto core = nes(1, 1);
        core->reserveAudio(8192);
        Lanes lanes(3);
        std::vector<float*> tap;
        for (std::size_t k = 0; k < 3; ++k) {
            tap.push_back(lanes.ptrs[k]);
            tap.push_back(lanes.ptrs[k]);
        }
        const auto bytesBefore = core->acceptedBytes();
        const auto droppedBefore = core->inputDropped();
        checkWholeBlocksAreFree(*core, tap, NesInput{});
        CHECK(core->tapOverflow() == 0);
        CHECK(core->acceptedBytes() - bytesBefore == 12 * (3 + 3 + 1 + 1 + 7 + 256));
        CHECK(core->inputDropped() == droppedBefore);
    }
    SECTION("NES, tap armed, driven through the mix") {
        // The narrow host's path (a split routing on a host too narrow for it): the tap captures every block and
        // finishBlock discards it, while the block itself is the mix's. Its drain, the discard, the scratches and the
        // discard's chunking are this section's subject, with a block's host input staged inside the window.
        auto core = nes(1, 1);
        core->reserveAudio(8192);
        Lanes lanes(2);
        const auto bytesBefore = core->acceptedBytes();
        const auto droppedBefore = core->inputDropped();
        checkWholeBlocksAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]}, NesInput{});
        CHECK(core->audioRingOverflow() == 0);
        CHECK(core->tapOverflow() == 0);
        CHECK(core->acceptedBytes() - bytesBefore == 12 * (3 + 3 + 1 + 1 + 7 + 256));
        CHECK(core->inputDropped() == droppedBefore);
    }
    SECTION("NES, through the engine") {
        // The same promise at the level the host sees: the whole graph block, so the system node's own per-block work
        // (params, settings, the tap's sync, its message digests) is inside the window too, together with the
        // core-side release of what the host staged. The staging goes through the invoker's stageNodeMidi, which
        // Engine::applyCommand hands to the system node's rp.stage/1 - its core's raw byte door, the same one a
        // protocol node's bytes input reaches: a short message and a settings-length SysEx, staged between blocks
        // - the invoker applies every push inline while the audio thread owns nothing, so the window holds the
        // release, not the staging, and the acceptance below is what holds the staging reached the core. The core is
        // the Engine's, so it publishes a whole savestate every 0.5 s: that capture allocates in Mesen's serializer,
        // and is charged a full interval here before the window, which stays under one interval (~12.5 k of 24 k
        // frames).
        Engine e(kRate);
        rp::host::test::Roms roms;
        auto systems = std::vector<rp::host::test::DocSystem>{{1, rp::host::test::kNesType}};
        rp::host::test::withRoms(e, systems, roms);
        REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1})).ok);
        QueuedInvoker invoker(e);
        auto* const core = dynamic_cast<MesenNesSystem*>(e.findSystem(1));
        REQUIRE(core != nullptr);
        Lanes lanes(2);
        for (int b = 0; b < 8; ++b) e.processBlock(1024, lanes.ptrs.data(), 2);  // warm
        chargeSnapshotInterval(*core);
        const std::uint64_t bytesBefore = core->acceptedBytes();
        const std::uint64_t droppedBefore = core->inputDropped();
        std::size_t stagedBytes = 0;
        for (const std::uint32_t frames : kRaggedBlocks) {
            std::fill_n(lanes.ptrs[0], frames, 0.0f);
            std::fill_n(lanes.ptrs[1], frames, 0.0f);
            const std::uint64_t before = ramHash(*core);
            const std::uint8_t note[] = {0x90, 64, 100, 0xF8, 0x80, 64, 0x00};
            invoker.stageNodeMidi("sys-1", std::vector<std::uint8_t>(note, note + sizeof note));
            stagedBytes += sizeof note;
            std::array<std::uint8_t, 192> sysex{};
            sysex.front() = 0xF0;
            sysex.back()  = 0xF7;
            invoker.stageNodeMidi("sys-1", std::vector<std::uint8_t>(sysex.begin(), sysex.end()));
            stagedBytes += sysex.size();
            rp::test::beginCountingAllocations(true);
            e.processBlock(frames, lanes.ptrs.data(), 2);
            const auto counted = rp::test::endCountingAllocations();
            INFO(frames << " frames: " << counted.allocations << " allocations, " << counted.frees << " frees");
            REQUIRE(counted.allocations == 0);
            REQUIRE(counted.frees == 0);
            // The plan's largest block ran: it was not refused as too long, and the core's RAM moved.
            if (frames == 8192) REQUIRE(ramHash(*core) != before);
        }
        CHECK(core->acceptedBytes() - bytesBefore == stagedBytes);  // the staged bytes reached the core's queue
        CHECK(core->inputDropped() == droppedBefore);
    }
    SECTION("SMS") {
        auto core = sms(1);
        core->reserveAudio(8192);
        Lanes lanes(2);
        // Four levels a block: the transport's own door, which a press-only check would not see landed.
        const auto levelsBefore = core->levelsAccepted();
        const auto droppedBefore = core->inputDropped();
        checkWholeBlocksAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]}, SmsInput{});
        CHECK(core->levelsAccepted() - levelsBefore == 12 * 4);
        CHECK(core->inputDropped() == droppedBefore);
    }
    SECTION("Game Boy") {
        // Both shapes, on a core each: the mixed stream, and the four stem pairs a plan that wires ch1..ch4 hands it.
        // A fresh core for the second because a Game Boy schedules each press 10 ms after the previous one and carries
        // the queue across blocks, so two long runs on one core would fill it and every later window would measure
        // refusals rather than a free block. 72 serial bytes land a block (24 messages of three), on the queue a press
        // burst does not reach.
        {
            auto core = mgb(1);
            core->reserveAudio(8192);
            Lanes mix(2);
            const auto bytesBefore = core->serialInAccepted();
            const auto droppedBefore = core->inputDropped();
            checkWholeBlocksAreFree(*core, {mix.ptrs[0], mix.ptrs[1]}, GameBoyInput{});
            CHECK(core->serialInAccepted() - bytesBefore == 12 * 24 * 3);
            CHECK(core->inputDropped() == droppedBefore);
        }
        {
            auto core = mgb(1);
            core->reserveAudio(8192);
            Lanes stems(8);
            const auto bytesBefore = core->serialInAccepted();
            const auto droppedBefore = core->inputDropped();
            checkWholeBlocksAreFree(*core, stems.ptrs, GameBoyInput{});
            CHECK(core->serialInAccepted() - bytesBefore == 12 * 24 * 3);
            CHECK(core->inputDropped() == droppedBefore);
        }
    }
    SECTION("Game Boy, through the engine, across its state publishes") {
        // The publish path's own promise, which every section above stays under: the registry republishes a core's
        // savestate every 0.5 s, reading the core's snapshot slot straight into its own, and that read is what a block
        // of a warmed core must not grow either. The window starts at the first block after warm-up and runs two
        // intervals, so the FIRST periodic publish and a later one both land inside it. (A core's first block publishes
        // its boot capture, which is discounted there; the registry's own accumulator starts at claim, so its publishes
        // here are counted indices 15 and 39.) Each publish is proved by the blob the control plane reads changing; the
        // read itself is between blocks, outside every window.
        //
        // A Game Boy holds the core's periodic capture too: it writes into the scratch the core sized at its first
        // block, and at 1024 frames the core's captures land at counted indices 16 and 40, inside the window. A Mesen
        // core's capture allocates inside its serializer by nature, which is why the NES section above charges a whole
        // interval outside its window, and why the next section holds a NES's publish alone. Host input is the
        // sections above's subject, so this one stages none.
        Engine e(kRate);
        rp::host::test::Roms roms;
        auto systems = std::vector<rp::host::test::DocSystem>{{1, rp::host::test::kGbType}};
        rp::host::test::withRoms(e, systems, roms);
        REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1})).ok);
        Lanes lanes(2);
        constexpr std::uint32_t kFrames = 1024;
        for (int b = 0; b < 8; ++b) e.processBlock(kFrames, lanes.ptrs.data(), 2);  // warm
        // The claim's boot seed is already published, so a change below is a publish rather than the first read.
        auto published = e.registry().readState(1);
        REQUIRE(published.has_value());
        int publishes = 0;
        for (int b = 0; b < 48; ++b) {
            std::fill_n(lanes.ptrs[0], kFrames, 0.0f);
            std::fill_n(lanes.ptrs[1], kFrames, 0.0f);
            rp::test::beginCountingAllocations(true);
            e.processBlock(kFrames, lanes.ptrs.data(), 2);
            const auto counted = rp::test::endCountingAllocations();
            auto now = e.registry().readState(1);
            if (now && *now != *published) ++publishes;
            published = std::move(now);
            INFO(b << ": " << counted.allocations << " allocations, " << counted.frees << " frees");
            REQUIRE(counted.allocations == 0);
            REQUIRE(counted.frees == 0);
        }
        CHECK(publishes == 2);
    }
    SECTION("NES, through the engine, its state publish") {
        // The registry's publish of a Mesen core, without the core's own capture: the registry counts its interval
        // from claim and the core from its first block, so at 1024 frames the registry's first periodic publish is
        // block 24 and the core's next capture block 25. The window runs from the first block after warm-up to block
        // 24, counted index 15, with no interval charged outside it, and holds the read a NES's publish goes through
        // to nothing, as the Game Boy section above holds a Game Boy's. The publish is proved by the blob the control
        // plane reads changing, at that block and no other.
        Engine e(kRate);
        rp::host::test::Roms roms;
        auto systems = std::vector<rp::host::test::DocSystem>{{1, rp::host::test::kNesType}};
        rp::host::test::withRoms(e, systems, roms);
        REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1})).ok);
        Lanes lanes(2);
        constexpr std::uint32_t kFrames = 1024;
        for (int b = 0; b < 8; ++b) e.processBlock(kFrames, lanes.ptrs.data(), 2);  // warm
        auto published = e.registry().readState(1);
        REQUIRE(published.has_value());
        std::vector<int> publishedAt;
        for (int b = 0; b < 16; ++b) {
            std::fill_n(lanes.ptrs[0], kFrames, 0.0f);
            std::fill_n(lanes.ptrs[1], kFrames, 0.0f);
            rp::test::beginCountingAllocations(true);
            e.processBlock(kFrames, lanes.ptrs.data(), 2);
            const auto counted = rp::test::endCountingAllocations();
            auto now = e.registry().readState(1);
            if (now && *now != *published) publishedAt.push_back(b);
            published = std::move(now);
            INFO(b << ": " << counted.allocations << " allocations, " << counted.frees << " frees");
            REQUIRE(counted.allocations == 0);
            REQUIRE(counted.frees == 0);
        }
        CHECK(publishedAt == std::vector<int>{15});
    }
    SECTION("GBA") {
        // A sibling fixture, so this SKIPs, counted, on a run without one (as CI's) rather than passing silently.
        const auto rom = resourceFixture("/roms/nanoloop287d.gba");
        if (rom.empty()) SKIP("the GBA fixture ROM is absent from the resources tree");
        auto core = std::make_unique<MesenGbaSystem>(1, MesenGbaConfig{}, rom);
        core->onActivate(kRate);
        core->reserveAudio(8192);
        Lanes lanes(2);
        // Presses are the one door a GBA core offers, so acceptance is theirs to hold: the twelve blocks stage twelve
        // each, and the helper's own warm-up pair (one press and its release) sits outside them.
        const auto pressesBefore = core->inputAccepted();
        const auto droppedBefore = core->inputDropped();
        checkWholeBlocksAreFree(*core, {lanes.ptrs[0], lanes.ptrs[1]}, GbaInput{});
        CHECK(core->inputAccepted() - pressesBefore == 2 + 12 * 12);
        CHECK(core->inputDropped() == droppedBefore);
    }
}

TEST_CASE("a freshly built core's first block costs the boot savestate and nothing else",
          "[engine-graph][system-node][alloc]") {
    // A system added while others play is built and activated on the control thread, and a Mesen core's FIRST block
    // then runs on the block thread with its frame plumbing still cold: the control manager's port snapshot, the four
    // persistent RenderedFrame input copies (the PPU's, the decoder's two and the renderer's last) and the video
    // filter's output buffer (about 240 KiB for a NES). Those are warmed at activation now, so a core built directly
    // (as the cases above build theirs) has a first block that allocates and frees nothing, and a core the Engine
    // builds pays exactly one more thing: the boot savestate capture that enableStateSnapshot arms for the first
    // block. A Mesen console's capture allocates inside Mesen's serializer by nature, and it cannot move off the block
    // thread without moving emulation: NesApu::Serialize calls EndFrame, so capturing at activation instead of at the
    // first block's end would end an APU frame at a different point (and parity catches it). A Game Boy has neither
    // that plumbing nor that serializer: its capture is one resize of the scratch it reuses, so its first block pays
    // that one allocation and every later capture is free. Either way the capture is pinned rather than moved: the
    // first block's count is measured against one capture on a twin core, so a regression that puts any other
    // allocation back into that block fails here, on every platform, without pinning a platform's allocator.
    const auto firstBlock = [](SystemBase& core, const std::vector<float*>& lanes) {
        return wholeBlock(core, 1024, lanes, true, NoInput{});
    };
    // What one boot capture costs, measured on a twin that has run the same first block: the capture's allocation
    // count follows its state's size, so a capture taken at boot (before the block) is not the same measurement.
    const auto captureAfterFirstBlock = [](SystemBase& twin, const std::vector<float*>& lanes) {
        wholeBlock(twin, 1024, lanes, false, NoInput{});
        rp::test::beginCountingAllocations(true);
        const auto bytes = twin.saveStateBytes();
        const auto counted = rp::test::endCountingAllocations();
        REQUIRE(!bytes.empty());
        return counted;
    };
    std::string why;
    // The request's own build function, as the node that made it hands it over: what rp.host.systems/2 runs.
    const auto build = [&why](SystemBuildRequest request) {
        const SystemBuildFn fn = request.build;
        return fn(1, std::move(request), kRate, &why);
    };
    // The configs the requests point at, held here for their calls: a build reads the config's address and copies it,
    // so one whole test can share them.
    MesenNesConfig nesConfig;
    nesConfig.romPath = RP_BLIPTOASTER_ROM_PATH;
    MesenSmsConfig smsConfig, ggConfig, smsFmConfig;
    smsConfig.romPath = smsFmConfig.romPath = RP_SMS_ROM_PATH;
    ggConfig.romPath = RP_GG_ROM_PATH;
    ggConfig.gameGear = true;
    smsFmConfig.enableFm = true;
    MesenGbaConfig gbaConfig;
    SameBoyConfig  gbConfig;
    gbConfig.model = SameBoyModel::DmgB;  // the DMG boot ROM plays its chime: real signal on the first block
    gbConfig.fastBoot = false;
    const auto nesRequest = [&] {
        SystemBuildRequest request;
        request.rom = readFile(RP_BLIPTOASTER_ROM_PATH);
        request.maxBlockFrames = 8192;
        request.build = rp::mesen::buildNesCore;
        request.config = &nesConfig;
        return request;
    };
    const auto smsRequest = [&](bool gameGear, bool enableFm) {
        SystemBuildRequest request;
        request.rom = readFile(gameGear ? RP_GG_ROM_PATH : RP_SMS_ROM_PATH);
        request.maxBlockFrames = 8192;
        request.build = rp::mesen::buildSmsCore;
        request.config = gameGear ? &ggConfig : enableFm ? &smsFmConfig : &smsConfig;
        return request;
    };
    const auto gbRequest = [&] {
        SystemBuildRequest request;
        request.rom = readFile(RP_MGB_ROM_PATH);
        request.maxBlockFrames = 8192;
        request.build = rp::sameboy::buildCore;
        request.config = &gbConfig;
        return request;
    };

    // A core built directly has no state snapshot enabled, so its first block is the frame plumbing's, which is warm.
    SECTION("NES, built directly") {
        auto core = nes(1, 0);
        core->reserveAudio(8192);
        Lanes lanes(2);
        const auto counted = firstBlock(*core, {lanes.ptrs[0], lanes.ptrs[1]});
        INFO("first block: " << counted.allocations << " allocations, " << counted.frees << " frees");
        CHECK(counted.allocations == 0);
        CHECK(counted.frees == 0);
    }
    SECTION("NES, built by the Engine") {
        auto twin = build(nesRequest());
        REQUIRE(twin != nullptr);
        Lanes twinLanes(2);
        const auto capture = captureAfterFirstBlock(*twin, {twinLanes.ptrs[0], twinLanes.ptrs[1]});
        auto core = build(nesRequest());
        REQUIRE(core != nullptr);
        Lanes lanes(2);
        const auto counted = firstBlock(*core, {lanes.ptrs[0], lanes.ptrs[1]});
        INFO("first block: " << counted.allocations << " allocations, " << counted.frees << " frees; one capture: "
                             << capture.allocations << " allocations, " << capture.frees << " frees");
        REQUIRE(capture.allocations > 0);
        CHECK(counted.allocations == capture.allocations);
        CHECK(counted.frees == capture.frees);
    }
    SECTION("SMS, built directly") {
        auto core = sms(1);
        core->reserveAudio(8192);
        Lanes lanes(2);
        const auto counted = firstBlock(*core, {lanes.ptrs[0], lanes.ptrs[1]});
        INFO("first block: " << counted.allocations << " allocations, " << counted.frees << " frees");
        CHECK(counted.allocations == 0);
        CHECK(counted.frees == 0);
    }
    SECTION("SMS, built by the Engine") {
        auto twin = build(smsRequest(false, false));
        REQUIRE(twin != nullptr);
        Lanes twinLanes(2);
        const auto capture = captureAfterFirstBlock(*twin, {twinLanes.ptrs[0], twinLanes.ptrs[1]});
        auto core = build(smsRequest(false, false));
        REQUIRE(core != nullptr);
        Lanes lanes(2);
        const auto counted = firstBlock(*core, {lanes.ptrs[0], lanes.ptrs[1]});
        INFO("first block: " << counted.allocations << " allocations, " << counted.frees << " frees; one capture: "
                             << capture.allocations << " allocations, " << capture.frees << " frees");
        REQUIRE(capture.allocations > 0);
        CHECK(counted.allocations == capture.allocations);
        CHECK(counted.frees == capture.frees);
    }
    // A settings change that rebuilds a Master System (gameGear, enableFm) builds the core the same way, so the
    // rebuilt core's first block is the same shape: the frame plumbing warm, one boot capture.
    SECTION("SMS rebuilt as a Game Gear, built directly") {
        auto core = std::make_unique<MesenSmsSystem>(1, ggConfig, readFile(RP_GG_ROM_PATH));
        core->onActivate(kRate);
        core->reserveAudio(8192);
        Lanes lanes(2);
        const auto counted = firstBlock(*core, {lanes.ptrs[0], lanes.ptrs[1]});
        INFO("first block: " << counted.allocations << " allocations, " << counted.frees << " frees");
        CHECK(counted.allocations == 0);
        CHECK(counted.frees == 0);
    }
    SECTION("SMS rebuilt as a Game Gear, built by the Engine") {
        auto twin = build(smsRequest(true, false));
        REQUIRE(twin != nullptr);
        Lanes twinLanes(2);
        const auto capture = captureAfterFirstBlock(*twin, {twinLanes.ptrs[0], twinLanes.ptrs[1]});
        auto core = build(smsRequest(true, false));
        REQUIRE(core != nullptr);
        Lanes lanes(2);
        const auto counted = firstBlock(*core, {lanes.ptrs[0], lanes.ptrs[1]});
        INFO("first block: " << counted.allocations << " allocations, " << counted.frees << " frees; one capture: "
                             << capture.allocations << " allocations, " << capture.frees << " frees");
        REQUIRE(capture.allocations > 0);
        CHECK(counted.allocations == capture.allocations);
        CHECK(counted.frees == capture.frees);
    }
    SECTION("SMS rebuilt with FM on, built directly") {
        auto core = std::make_unique<MesenSmsSystem>(1, smsFmConfig, readFile(RP_SMS_ROM_PATH));
        core->onActivate(kRate);
        core->reserveAudio(8192);
        Lanes lanes(2);
        const auto counted = firstBlock(*core, {lanes.ptrs[0], lanes.ptrs[1]});
        INFO("first block: " << counted.allocations << " allocations, " << counted.frees << " frees");
        CHECK(counted.allocations == 0);
        CHECK(counted.frees == 0);
    }
    SECTION("SMS rebuilt with FM on, built by the Engine") {
        auto twin = build(smsRequest(false, true));
        REQUIRE(twin != nullptr);
        Lanes twinLanes(2);
        const auto capture = captureAfterFirstBlock(*twin, {twinLanes.ptrs[0], twinLanes.ptrs[1]});
        auto core = build(smsRequest(false, true));
        REQUIRE(core != nullptr);
        Lanes lanes(2);
        const auto counted = firstBlock(*core, {lanes.ptrs[0], lanes.ptrs[1]});
        INFO("first block: " << counted.allocations << " allocations, " << counted.frees << " frees; one capture: "
                             << capture.allocations << " allocations, " << capture.frees << " frees");
        REQUIRE(capture.allocations > 0);
        CHECK(counted.allocations == capture.allocations);
        CHECK(counted.frees == capture.frees);
    }
    SECTION("GBA, built directly") {
        // A sibling fixture, so this SKIPs, counted, on a run without one (as CI's) rather than passing silently.
        const auto rom = resourceFixture("/roms/nanoloop287d.gba");
        if (rom.empty()) SKIP("the GBA fixture ROM is absent from the resources tree");
        auto core = std::make_unique<MesenGbaSystem>(1, MesenGbaConfig{}, rom);
        core->onActivate(kRate);
        core->reserveAudio(8192);
        Lanes lanes(2);
        const auto counted = firstBlock(*core, {lanes.ptrs[0], lanes.ptrs[1]});
        INFO("first block: " << counted.allocations << " allocations, " << counted.frees << " frees");
        CHECK(counted.allocations == 0);
        CHECK(counted.frees == 0);
    }
    SECTION("GBA, built by the Engine") {
        const auto rom = resourceFixture("/roms/nanoloop287d.gba");
        if (rom.empty()) SKIP("the GBA fixture ROM is absent from the resources tree");
        SystemBuildRequest request;
        request.rom = rom;
        request.maxBlockFrames = 8192;
        request.build = rp::mesen::buildGbaCore;
        request.config = &gbaConfig;
        auto twin = build(request);
        REQUIRE(twin != nullptr);
        Lanes twinLanes(2);
        const auto capture = captureAfterFirstBlock(*twin, {twinLanes.ptrs[0], twinLanes.ptrs[1]});
        request.rom = rom;
        auto core = build(std::move(request));
        REQUIRE(core != nullptr);
        Lanes lanes(2);
        const auto counted = firstBlock(*core, {lanes.ptrs[0], lanes.ptrs[1]});
        INFO("first block: " << counted.allocations << " allocations, " << counted.frees << " frees; one capture: "
                             << capture.allocations << " allocations, " << capture.frees << " frees");
        REQUIRE(capture.allocations > 0);
        CHECK(counted.allocations == capture.allocations);
        CHECK(counted.frees == capture.frees);
    }
    SECTION("Game Boy, built directly") {
        auto core = mgb(1);
        core->reserveAudio(8192);
        Lanes lanes(2);
        const auto counted = firstBlock(*core, {lanes.ptrs[0], lanes.ptrs[1]});
        INFO("first block: " << counted.allocations << " allocations, " << counted.frees << " frees");
        CHECK(counted.allocations == 0);
        CHECK(counted.frees == 0);
    }
    SECTION("Game Boy, built by the Engine") {
        auto twin = build(gbRequest());
        REQUIRE(twin != nullptr);
        Lanes twinLanes(2);
        const auto capture = captureAfterFirstBlock(*twin, {twinLanes.ptrs[0], twinLanes.ptrs[1]});
        auto core = build(gbRequest());
        REQUIRE(core != nullptr);
        Lanes lanes(2);
        const auto counted = firstBlock(*core, {lanes.ptrs[0], lanes.ptrs[1]});
        INFO("first block: " << counted.allocations << " allocations, " << counted.frees << " frees; one capture: "
                             << capture.allocations << " allocations, " << capture.frees << " frees");
        REQUIRE(capture.allocations > 0);
        CHECK(counted.allocations == capture.allocations);
        CHECK(counted.frees == capture.frees);
    }
}

TEST_CASE("the ingress capacities match the queue capacities ports.ts declares",
          "[engine-graph][system-node][inputs]") {
    // specs/02's "queue capacities" are TS's, and a core sizes its queues from InputCapacity.hpp: read the four
    // declarations here so a change on either side is visible, rather than a silently mismatched bound.
    const auto capacity = [](const std::string& name) {
        const auto bytes = readFile(RP_PORTS_TS_PATH);
        const std::string ts(bytes.begin(), bytes.end());
        const std::string head = "export const " + name + ": QueueCapacity = {";
        const auto at = ts.find(head);
        REQUIRE(at != std::string::npos);
        const auto open = ts.find('{', at);
        const auto close = ts.find('}', open);
        REQUIRE(open != std::string::npos);
        REQUIRE(close != std::string::npos);
        const std::string body = ts.substr(open + 1, close - open - 1);
        const auto field = [&](const char* key) {
            const auto p = body.find(std::string(key) + ":");
            REQUIRE(p != std::string::npos);
            return static_cast<std::uint32_t>(std::strtoul(body.c_str() + p + std::strlen(key) + 1, nullptr, 10));
        };
        return std::pair{field("messages"), field("bytes")};
    };

    const auto midi = capacity("CORE_MIDI");
    CHECK(midi.first == rp::kMidiInput.messages);
    CHECK(midi.second == rp::kMidiInput.bytes);
    const auto serialIn = capacity("SERIAL_IN");
    CHECK(serialIn.first == rp::kSerialInInput.messages);
    CHECK(serialIn.second == rp::kSerialInInput.bytes);
    const auto bytes = capacity("CORE_BYTES");
    CHECK(bytes.first == rp::kBytesInput.messages);
    CHECK(bytes.second == rp::kBytesInput.bytes);
    const auto buttons = capacity("BUTTONS");
    CHECK(buttons.first == rp::kButtonsInput);
    // A routed MIDI stream and the host's own MIDI-in output reach a system's midi input too, so both hold no more
    // than the core's queue (InputCapacity.hpp's contract: the bound is what the feeding outputs declare).
    const auto routed = capacity("ROUTED_MIDI");
    CHECK(routed.first == rp::host::kHostMidiInMessages);
    CHECK(routed.second == rp::host::kHostMidiInBytes);
    CHECK(routed.first <= rp::kMidiInput.messages);
    CHECK(routed.second <= rp::kMidiInput.bytes);
}

TEST_CASE("a core's ingress queues drop what is past their bound, count it, and keep running",
          "[engine-graph][system-node][input-drops]") {
    // Each queue is sized from what a block's doors can deliver (InputCapacity.hpp). A push past that bound is dropped
    // and counted rather than growing the queue on the block thread, and the core still runs the next block. The count
    // is the core's inputDropped(); inputAccepted() is the other half, holding that the pushes before the bound landed.
    const auto ran = [](SystemBase& core, const std::vector<float*>& lanes) {
        wholeBlock(core, 1024, lanes, true, NoInput{});  // the REQUIRE inside holds the block was really filled
    };

    // A Mesen core's press queue is the exception: it holds one transition a button (the block applies the whole queue
    // before the emulator polls its controller), so a burst coalesces instead of overrunning it, and it refuses
    // nothing. Its bound is the button range, which kPressInput covers (InputCapacity.hpp's static_assert).
    SECTION("NES presses") {
        auto core = nes(1, 0);
        core->reserveAudio(8192);
        Lanes lanes(2);
        for (std::uint32_t i = 0; i < rp::kPressInput + 3; ++i)
            core->pressButton(static_cast<std::uint8_t>(i % 8), (i % 2) == 0);
        CHECK(core->inputAccepted() == rp::kPressInput + 3);
        CHECK(core->inputDropped() == 0);
        ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
    }
    SECTION("SMS presses") {
        auto core = sms(1);
        core->reserveAudio(8192);
        Lanes lanes(2);
        for (std::uint32_t i = 0; i < rp::kPressInput + 2; ++i)
            core->pressButton(static_cast<std::uint8_t>(i % 8), (i % 2) == 0);
        CHECK(core->inputAccepted() == rp::kPressInput + 2);
        CHECK(core->inputDropped() == 0);
        ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
    }
    SECTION("GBA presses") {
        const auto rom = resourceFixture("/roms/nanoloop287d.gba");
        if (rom.empty()) SKIP("the GBA fixture ROM is absent from the resources tree");
        auto core = std::make_unique<MesenGbaSystem>(1, MesenGbaConfig{}, rom);
        core->onActivate(kRate);
        core->reserveAudio(8192);
        Lanes lanes(2);
        for (std::uint32_t i = 0; i < rp::kPressInput + 1; ++i)
            core->pressButton(static_cast<std::uint8_t>(i % 8), (i % 2) == 0);
        CHECK(core->inputAccepted() == rp::kPressInput + 1);
        CHECK(core->inputDropped() == 0);
        ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
    }
    SECTION("Game Boy presses") {
        // A Game Boy's queue cannot coalesce, since every edge is meant to be seen in turn, so it keeps the last slots
        // for the releases of what it holds: a press, and a release of a button already up, is taken only with more
        // than the console's 8 buttons free, i.e. while fewer than 1024 - 8 = 1016 entries are queued. Every edge of
        // this loop is one of those (the even ones press buttons 0, 2, 4 and 6, the odd ones release 1, 3, 5 and 7,
        // which nothing pressed), so the 1028 edges fill 1016 entries and the last 12 are refused.
        auto core = mgb(1);
        core->reserveAudio(8192);
        Lanes lanes(2);
        constexpr std::uint32_t kCapacity = rp::kButtonsInput * 16;
        for (std::uint32_t i = 0; i < kCapacity + 4; ++i)
            core->pressButton(static_cast<std::uint8_t>(i % 8), (i % 2) == 0);
        CHECK(core->inputAccepted() == kCapacity - 8);
        CHECK(core->inputDropped() == 12);
        ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
    }
    SECTION("Game Boy serial bytes") {
        // The ring holds two blocks' bytes; one past it is refused, whatever its run's frame.
        auto core = mgb(1);
        core->reserveAudio(8192);
        Lanes lanes(2);
        constexpr std::uint32_t kCapacity = 2 * rp::kSerialInInput.bytes;
        for (std::uint32_t i = 0; i < kCapacity + 1; ++i) core->pushSerialIn(0, static_cast<std::uint8_t>(i));
        CHECK(core->inputAccepted() == kCapacity);
        CHECK(core->inputDropped() == 1);
        ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
    }
    SECTION("Game Boy serial runs") {
        // Each byte at its own frame is a run; the run queue holds two blocks' messages (or one serialOut block), and
        // the ring has room for all of them, so the 8257th is the queue's refusal.
        auto core = mgb(1);
        core->reserveAudio(8192);
        Lanes lanes(2);
        for (std::uint32_t i = 0; i < 8257; ++i) core->pushSerialIn(i, 0x90);
        CHECK(core->inputAccepted() == 8256);
        CHECK(core->inputDropped() == 1);
        ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
    }
    SECTION("SMS levels") {
        // A level is a byte. The queue holds the midi door's longest run plus the bytes door's block, so one level
        // past that is dropped while the rest stay.
        auto core = sms(1);
        core->reserveAudio(8192);
        Lanes lanes(2);
        constexpr std::size_t kCapacity = std::size_t(rp::kMidiInput.bytes) + rp::kBytesInput.bytes;
        const std::vector<std::uint8_t> levels(kCapacity + 1, 0x5A);
        core->pushCoreBytes(0, levels.data(), levels.size(), false);
        CHECK(core->inputAccepted() == kCapacity);
        CHECK(core->inputDropped() == 1);
        ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
    }
    SECTION("NES N8 pending runs") {
        auto core = nes(1, 0);
        core->reserveAudio(8192);
        Lanes lanes(2);
        constexpr std::size_t kCapacity = std::size_t(rp::kMidiInput.messages) + rp::kBytesInput.messages;
        const std::uint8_t byte = 0xF8;
        for (std::size_t i = 0; i < kCapacity + 1; ++i) core->pushCoreBytes(0, &byte, 1, false);
        CHECK(core->inputAccepted() == kCapacity);
        CHECK(core->inputDropped() == 1);
        ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
    }
    SECTION("NES N8 pending bytes") {
        auto core = nes(1, 0);
        core->reserveAudio(8192);
        Lanes lanes(2);
        constexpr std::size_t kCapacity = std::size_t(rp::kMidiInput.bytes) + rp::kBytesInput.bytes;
        const std::vector<std::uint8_t> bytes(kCapacity + 2, 0xF8);
        core->pushCoreBytes(0, bytes.data(), bytes.size(), false);
        CHECK(core->inputDropped() == kCapacity + 2);   // a run past the arena is dropped whole, as a graph queue's is
        CHECK(core->inputAccepted() == 0);              // and none of it was taken, so the two halves agree
        ran(*core, {lanes.ptrs[0], lanes.ptrs[1]});
    }
}

TEST_CASE("a Mesen press burst cannot push a release out of its queue", "[engine-graph][system-node][input-drops]") {
    // In a quiescent session (the CLI, an offline render) every push applies inline on the control thread, so the
    // command ring bounds nothing between two renders: a host can queue more transitions than a block's doors deliver.
    // With a plain queue that dropped the tail, a burst of presses followed by a release could lose the release and
    // leave the button held. A Mesen core's queue holds one transition a button and is applied whole before the
    // emulator polls its controller, so a transition for a button already queued replaces its entry: the burst
    // collapses and the release always lands.
    const auto burst = [](auto& core) {
        for (std::uint32_t i = 0; i < rp::kPressInput + 64; ++i) core.pressButton(0, true);
        for (int i = 0; i < 4; ++i) core.pressButton(0, false);   // the releases, behind a queue a plain FIFO filled
        for (int i = 0; i < 4; ++i) core.pressButton(1, true);    // a button the burst never named still gets in
        CHECK(core.inputDropped() == 0);
        CHECK(core.inputAccepted() == rp::kPressInput + 72);
        // The counts alone cannot tell a coalescing that replaces a button's entry from one that keeps the first: both
        // refuse nothing and accept everything. What the fix buys is the surviving entry's value, so read it: button
        // 0's stands as a release (one that kept the press would leave that button held for the rest of the session),
        // the button the burst never named stands as its press, and a button nothing named holds nothing.
        bool down = true;
        REQUIRE(core.peekPendingPress(0, down));
        CHECK_FALSE(down);
        REQUIRE(core.peekPendingPress(1, down));
        CHECK(down);
        CHECK_FALSE(core.peekPendingPress(2, down));
    };
    // What the block does with the whole queue is the other half: it applies it, so the pad holds the two transitions
    // that stood, and empties it. Reading the queue alone would pass a block that cleared it without a SetBitValue.
    const auto padQuiet = [](auto& core) {
        CHECK_FALSE(core.buttonDown(0));
        CHECK_FALSE(core.buttonDown(1));
    };
    const auto applied = [](auto& core) {
        bool down = true;
        CHECK_FALSE(core.peekPendingPress(0, down));
        CHECK_FALSE(core.peekPendingPress(1, down));
        CHECK_FALSE(core.buttonDown(0));  // the release the burst ended on
        CHECK(core.buttonDown(1));        // the press the burst queued behind it
    };
    SECTION("NES") {
        auto core = nes(1, 0);
        core->reserveAudio(8192);
        Lanes lanes(2);
        burst(*core);
        padQuiet(*core);
        wholeBlock(*core, 1024, {lanes.ptrs[0], lanes.ptrs[1]}, true, NoInput{});  // applied, in a free block
        CHECK(core->inputDropped() == 0);
        applied(*core);
    }
    SECTION("SMS") {
        auto core = sms(1);
        core->reserveAudio(8192);
        Lanes lanes(2);
        burst(*core);
        padQuiet(*core);
        wholeBlock(*core, 1024, {lanes.ptrs[0], lanes.ptrs[1]}, true, NoInput{});
        CHECK(core->inputDropped() == 0);
        applied(*core);
    }
    SECTION("Game Boy Advance") {
        const auto rom = resourceFixture("/roms/nanoloop287d.gba");
        if (rom.empty()) SKIP("the GBA fixture ROM is absent from the resources tree");
        auto core = std::make_unique<MesenGbaSystem>(1, MesenGbaConfig{}, rom);
        core->onActivate(kRate);
        core->reserveAudio(8192);
        Lanes lanes(2);
        burst(*core);
        padQuiet(*core);
        wholeBlock(*core, 1024, {lanes.ptrs[0], lanes.ptrs[1]}, true, NoInput{});
        CHECK(core->inputDropped() == 0);
        applied(*core);
    }
}

TEST_CASE("a Game Boy's press queue keeps room for the releases of what it holds",
          "[engine-graph][system-node][input-drops]") {
    // The queue holds 1024 entries and a press is taken only with more than the console's 8 buttons free, so a flood
    // of presses stops at 1016 and leaves exactly the 8 slots a release may need. At most 8 buttons are held then, and
    // each release frees the button it lets go, so the flood can never push out the edge that lets a button go: a
    // button held after the flood comes back up. A press past that point is refused, since nothing is free any more.
    auto core = mgb(1);
    constexpr std::uint32_t kCapacity = rp::kButtonsInput * 16;
    constexpr std::uint32_t kReserve = static_cast<std::uint32_t>(GameboyButton::Count);
    for (std::uint32_t i = 0; i < kCapacity + 16; ++i) core->pressButton(static_cast<std::uint8_t>(i % 8), true);
    CHECK(core->inputAccepted() == kCapacity - kReserve);
    CHECK(core->inputDropped() == 16 + kReserve);
    for (std::uint32_t button = 0; button < kReserve; ++button)
        core->pressButton(static_cast<std::uint8_t>(button), false);
    CHECK(core->inputAccepted() == kCapacity);
    CHECK(core->inputDropped() == 16 + kReserve);
    for (std::uint32_t button = 0; button < kReserve; ++button) {
        bool down = true;
        REQUIRE(core->peekPendingPress(static_cast<std::uint8_t>(button), down));
        CHECK_FALSE(down);   // the flood's press, and behind it the release that stands
    }
    core->pressButton(0, true);   // the queue's bound reached exactly: the reserve is empty and this needs it
    CHECK(core->inputAccepted() == kCapacity);
    CHECK(core->inputDropped() == 17 + kReserve);
}

TEST_CASE("a press of a byte the console has no button for drives no button",
          "[engine-graph][system-node][input-drops]") {
    // The press queue coalesces by the wire byte, so the entry that stands for a button is the last one naming it -
    // which is what the console ends up with only while no two entries drive one console bit. A byte outside a
    // console's button range therefore has to drive no bit rather than fall through onto one: a press of it queued
    // behind a release would else re-press the button that release released, the stuck button the coalescing exists to
    // prevent arriving by another road. The Master System already maps this way, its pad having no Select at all.
    const auto strayByte = [](auto& core, const std::uint8_t stray) {
        core.pressButton(4, true);      // A, the shared wire byte 4
        core.pressButton(stray, true);  // a byte outside this console's table
        core.pressButton(4, false);     // the release, which must be the last word on A
        CHECK(core.inputAccepted() == 3);  // the queue took all three: a byte it has no bit for is still a press
        CHECK(core.inputDropped() == 0);
        bool down = true;
        REQUIRE(core.peekPendingPress(4, down));
        CHECK_FALSE(down);
    };
    SECTION("NES") {
        auto core = nes(1, 0);
        strayByte(*core, 200);
    }
    SECTION("Master System") {
        auto core = sms(1);
        strayByte(*core, 200);
    }
    SECTION("Game Boy Advance") {
        const auto rom = resourceFixture("/roms/nanoloop287d.gba");
        if (rom.empty()) SKIP("the GBA fixture ROM is absent from the resources tree");
        auto core = std::make_unique<MesenGbaSystem>(1, MesenGbaConfig{}, rom);
        core->onActivate(kRate);
        strayByte(*core, 200);
    }
    SECTION("Game Boy") {
        // A Game Boy cannot coalesce, so a byte past its table also has to leave the queue alone: it is taken (a press
        // the console has no bit for), it drives no key past the pad's arrays, and the two entries that stand are A's
        // press and its release.
        auto core = mgb(1);
        core->pressButton(4, true);      // A
        core->pressButton(200, true);    // past GameboyButton::Count
        core->pressButton(4, false);
        CHECK(core->inputAccepted() == 3);
        CHECK(core->inputDropped() == 0);
        CHECK(core->pendingPresses() == 2);
        bool down = true;
        REQUIRE(core->peekPendingPress(4, down));
        CHECK_FALSE(down);
    }
}

TEST_CASE("the tap is a capability only a NES offers", "[engine-graph][system-node][tap]") {
    // SystemBase::channelTap() is how the engine finds the tap a split routing arms
    // (docs/modular-graph-phase-c4-design.md section 5), so a core without one answers null and the engine arms
    // nothing. The NES is the only core with one: its tap is the core itself, so the interface and the core's own mode
    // accessors are one mode.
    SECTION("NES") {
        auto core = nes(1, 0);
        REQUIRE(core->channelTap() != nullptr);
        CHECK(core->channelTap()->exportMode() == 0);
        // A core built at mode 0 and armed later sizes nothing: onActivate sized the tap's rings, its resample scratch
        // and each stream's blip, so an arm only switches the streams on. The blips are C blocks (CountingAllocator),
        // which only a glibc build counts; the rings and scratches are vectors and count in every build.
        rp::test::beginCountingAllocations(true);
        core->channelTap()->setExportMode(1);
        const auto arm = rp::test::endCountingAllocations();
        INFO("arm: " << arm.allocations << " allocations, " << arm.frees << " frees");
        CHECK(arm.allocations == 0);
        CHECK(arm.frees == 0);
        CHECK(core->channelExportMode() == 1);
    }
    SECTION("Game Boy") {
        auto core = mgb(1);
        CHECK(core->channelTap() == nullptr);
    }
    SECTION("Master System") {
        auto core = sms(1);
        CHECK(core->channelTap() == nullptr);
    }
    SECTION("GBA") {
        auto core = std::make_unique<MesenGbaSystem>(1, MesenGbaConfig{}, rp::host::test::romFor("rp.mesen.gba"));
        core->onActivate(kRate);
        CHECK(core->channelTap() == nullptr);
    }
}

TEST_CASE("a tap armed live on an activated NES sizes nothing on the block thread, and its audio path stays free",
          "[engine-graph][system-node][mesen][tap][alloc]") {
    // A plugin routing change arms the tap through the plan's SyncTap trailer (Engine::syncTap), which the block thread
    // runs as it adopts the new plan - i.e. BETWEEN two blocks, on the audio thread. Nothing may be SIZED or allocated
    // there: the ring, the per-block scratches, the tap's rings, its resample scratch and each stream's blip were all
    // created at activation (reserveAudio), so an arm only re-initialises the streams it switches on. The counted
    // window is the arm itself, C allocations included, because a stream's blip would be blip_new's malloc: what
    // the counting allocator's C half is for, and this case measures it. The operator-new half runs in every build. The
    // C half is only visible where countsCAllocations() is true (a plain glibc build): under ASan/TSan the sanitizer
    // owns the allocator and the C interposer is off by design, and Windows' static CRT and macOS have no glibc name to
    // forward to, so on those the zero below holds the C++ half and the C half is unmeasured rather than assumed. The
    // blocks after it are held the way the case above holds a core's: their drain is counted, their step is not.
    Engine e(kRate);
    rp::host::test::Roms roms;
    auto systems = std::vector<rp::host::test::DocSystem>{{1, rp::host::test::kNesType}};
    rp::host::test::withRoms(e, systems, roms);
    const auto document = [&] { return rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1}); };
    REQUIRE(e.setGraph(document()).ok);
    auto* const core = dynamic_cast<MesenNesSystem*>(e.findSystem(1));
    REQUIRE(core != nullptr);
    REQUIRE(core->channelExportMode() == 0);
    CHECK(core->audioRingCapacity() == 8192 + MesenAudioDevice::kMaxPlayBufferFrames);

    std::vector<std::vector<float>> lanes(2, std::vector<float>(8192, 0.0f));
    float* outs[2] = {lanes[0].data(), lanes[1].data()};
    for (int b = 0; b < 4; ++b) e.processBlock(4096, outs, 2);  // real blocks: the ring has sound in flight

    // The audio thread owns the next plan's adoption, so the arm is queued and the block thread runs it.
    e.setAudioThreadOwns(true);
    REQUIRE(e.setGraph(rp::host::test::rearming(document(), 1, 1)).ok);
    rp::test::beginCountingAllocations(true);  // the arm's blips are C blocks (see CountingAllocator)
    e.drainCommands();
    const auto arm = rp::test::endCountingAllocations();
    e.setAudioThreadOwns(false);
    INFO("arm: " << arm.allocations << " allocations, " << arm.frees << " frees");
    CHECK(arm.allocations == 0);
    CHECK(arm.frees == 0);
    REQUIRE(core->channelExportMode() == 1);
    CHECK(core->channelLayout().size() == 3);

    // The blocks after it: the pins the arm just armed, drained through the node's six lanes.
    Lanes pins(3);
    std::vector<float*> tap;
    for (std::size_t k = 0; k < 3; ++k) {
        tap.push_back(pins.ptrs[k]);
        tap.push_back(pins.ptrs[k]);
    }
    chargeSnapshotInterval(*core);
    checkRaggedDrainsAreFree(*core, tap);
    CHECK(core->tapOverflow() == 0);

    // A long armed run, its drain counted a block at a time: the tap's rings take on more than a ringful (so their head
    // and tail wrap, and DrainChannel crosses the end mid-drain) and never drop or allocate.
    chargeSnapshotInterval(*core);
    for (int b = 0; b < 46; ++b) {
        const auto counted = drainBlock(*core, 512, tap.data(), tap.size(), true, true);
        INFO("block " << b << ": " << counted.allocations << " allocations, " << counted.frees << " frees");
        REQUIRE(counted.allocations == 0);
        REQUIRE(counted.frees == 0);
    }
    CHECK(core->tapOverflow() == 0);

    // And through the mix, which discards what the tap captured, a block at a time.
    Lanes mix(2);
    chargeSnapshotInterval(*core);
    checkRaggedDrainsAreFree(*core, {mix.ptrs[0], mix.ptrs[1]});
    CHECK(core->tapOverflow() == 0);
    CHECK(core->audioRingOverflow() == 0);
}

TEST_CASE("a warmed Mega Drive block allocates nothing", "[engine-graph][system-node][alloc][blastem-md]") {
    NodeRig rig(kRate);
    // The port claimed: the level this case stages for the block reaches the core's byte path, which is open only for
    // a system whose node says a role took port 2.
    REQUIRE(rig.set(R"({"nodes":[{"id":"md","type":"rp.blastem.md","settings":{)" +
                        mdNode(rig, 1, R"("coreBytesIn":true)") +
                        R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"md.mix","to":"out.out.1-2"}]})").ok);
    Lanes out(2);
    const rp_transport t{kRate, 120.0, 0.0, 0};
    // Warm: the boot's savestate capture, the audio rings and the blip buffers are all first-block costs, and the eight
    // warm blocks stay well under SystemBase's half-second snapshot interval, so no publish lands in the window below.
    for (int b = 0; b < 8; ++b) rig.host->processBlock(512, t, out.ptrs.data(), 2);
    // A level staged for the block, so the port device's queue and its sort are in the counted window too.
    const std::uint8_t level = 0x00;
    REQUIRE(rig.builtCore(1) != nullptr);
    rig.builtCore(1)->pushCoreBytes(0, &level, 1);
    rp::test::beginCountingAllocations();
    for (int b = 0; b < 2; ++b) rig.host->processBlock(512, t, out.ptrs.data(), 2);
    const auto counted = rp::test::endCountingAllocations();
    INFO("block: " << counted.allocations << " allocations, " << counted.frees << " frees");
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
    // The level was accepted and the block that applied it allocated nothing: the port device's queue is part of the
    // same no-allocation window as the rest of the block.
    CHECK(static_cast<rp::blastem::BlastemMdSystem*>(rig.builtCore(1))->levelsAccepted() == 1);
    CHECK(static_cast<rp::blastem::BlastemMdSystem*>(rig.builtCore(1))->clampedBlocks() == 0);
}

TEST_CASE("a node re-activation grows a Mesen core's audio ring to the new block",
          "[engine-graph][system-node][alloc]") {
    // Engine::setMaxBlockFrames re-activates the graph while it is quiescent, which is where the core's ring learns the
    // plan's new maximum: the node's activate returns early for a core it holds and reserves for the new block.
    NodeRig rig(kRate);
    REQUIRE(rig.set(R"({"nodes":[{"id":"nes","type":"rp.mesen.nes","settings":{)" + rig.boot("rp.mesen.nes", 1) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"nes.mix","to":"out.out.1-2"}]})").ok);
    auto* const core = dynamic_cast<MesenNesSystem*>(rig.builtCore(1));
    REQUIRE(core != nullptr);
    CHECK(core->audioRingCapacity() == 8192 + MesenAudioDevice::kMaxPlayBufferFrames);
    Lanes small(2);
    const rp_transport t{kRate, 120.0, 0.0, 0};
    rig.host->processBlock(512, t, small.ptrs.data(), 2);  // sound in flight when the plan is re-activated
    std::string why;
    REQUIRE(rig.host->reactivate(kRate, 16384, &why));
    CHECK(core->audioRingCapacity() == 16384 + MesenAudioDevice::kMaxPlayBufferFrames);
    Lanes big(2, 16384);
    rig.host->processBlock(16384, t, big.ptrs.data(), 2);
    CHECK(core->audioRingOverflow() == 0);
}

TEST_CASE("a node sizes its core's block buffers to the plan it was built by, not the engine's default",
          "[engine-graph][system-node][alloc]") {
    // The plan's maximum reaches the core in its build request, so activation sizes the ring and the per-block
    // scratches to it: a DAW that runs 64-frame blocks does not keep the memory an 8192-frame plan would need until a
    // host grows its block. A core built outside a plan (the direct twins the rest of this file builds) keeps the
    // console's default.
    SECTION("NES, at a 64-frame plan") {
        NodeRig rig(kRate, 64);
        REQUIRE(rig.set(R"({"nodes":[{"id":"nes","type":"rp.mesen.nes","settings":{)" + rig.boot("rp.mesen.nes", 1) +
                        R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                            "wires":[{"from":"nes.mix","to":"out.out.1-2"}]})").ok);
        auto* const core = dynamic_cast<MesenNesSystem*>(rig.builtCore(1));
        REQUIRE(core != nullptr);
        CHECK(core->audioRingCapacity() == 64 + MesenAudioDevice::kMaxPlayBufferFrames);
    }
    SECTION("Game Boy, at a 64-frame plan") {
        NodeRig rig(kRate, 64);
        REQUIRE(rig.set(R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb","settings":{)" +
                        rig.boot("rp.sameboy.gb", 1, kGbDmg) +
                        R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                            "wires":[{"from":"gb.mix","to":"out.out.1-2"}]})").ok);
        auto* const core = dynamic_cast<SameBoySystem*>(rig.builtCore(1));
        REQUIRE(core != nullptr);
        CHECK(core->stereoAccum_.size() == std::size_t(2 * 64 + 16));
        for (const auto& channel : core->chanAccum_) CHECK(channel.size() == std::size_t(2 * 64 + 16));
    }
    SECTION("a core built outside a plan keeps the default") {
        auto mesen = nes(1, 0);
        CHECK(mesen->audioRingCapacity() ==
              MesenAudioDevice::kDefaultBlockFrames + MesenAudioDevice::kMaxPlayBufferFrames);
        auto gameboy = mgb(1);
        CHECK(gameboy->stereoAccum_.size() == std::size_t(2 * SameBoySystem::kDefaultBlockFrames + 16));
    }
}

namespace {

// A hash of a frame's bytes, dimensions included, as FNV-1a. The frame the core last published is what a host reads.
std::uint64_t frameHash(SystemBase& core) {
    FrameBufferTriple* fb = core.framebuffer();
    REQUIRE(fb != nullptr);
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(fb->width()) * fb->height());
    REQUIRE(fb->readInto(pixels.data(), static_cast<std::uint32_t>(pixels.size())));
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](const void* bytes, std::size_t size) {
        const auto* p = static_cast<const std::uint8_t*>(bytes);
        for (std::size_t i = 0; i < size; ++i) {
            hash ^= p[i];
            hash *= 1099511628211ull;
        }
    };
    const std::uint32_t dims[2] = {fb->width(), fb->height()};
    mix(dims, sizeof dims);
    mix(pixels.data(), pixels.size() * sizeof(std::uint32_t));
    return hash;
}

// `blocks` real blocks on `core`, the hash of the frame the core published in each.
std::vector<std::uint64_t> frameHashes(SystemBase& core, int blocks, std::uint32_t frames = 4096) {
    std::vector<std::uint64_t> hashes;
    Lanes lanes(2, frames);
    for (int b = 0; b < blocks; ++b) {
        wholeBlock(core, frames, {lanes.ptrs[0], lanes.ptrs[1]}, false, NoInput{});
        hashes.push_back(frameHash(core));
    }
    return hashes;
}

}  // namespace

TEST_CASE("each console renders the frames it always has, bit for bit",
          "[engine-graph][system-node][video]") {
    // The video path this stage made reuse its buffers is held to the bytes it produced before the change: these are
    // the frames four consoles publish over their first blocks, recorded at 903f2371 (the parent of the commit that
    // made a frame's buffers reusable) and equal after it. Parity is audio plus integer state, so without this a
    // change to RenderedFrame, VideoDecoder or a PPU's frame plumbing would be read, not checked.
    SECTION("NES") {
        auto core = nes(1, 0);
        const std::vector<std::uint64_t> got = frameHashes(*core, 3);
        INFO("NES: " << got[0] << ", " << got[1] << ", " << got[2]);
        CHECK(got == std::vector<std::uint64_t>{14252490387203307000ull, 14252490387203307000ull,
                                               14252490387203307000ull});
    }
    SECTION("Master System") {
        auto core = sms(1);
        const std::vector<std::uint64_t> got = frameHashes(*core, 3);
        INFO("SMS: " << got[0] << ", " << got[1] << ", " << got[2]);
        CHECK(got == std::vector<std::uint64_t>{16816052646246439624ull, 16816052646246439624ull,
                                               668230188373870873ull});
    }
    SECTION("Game Boy") {
        auto core = mgb(1);
        const std::vector<std::uint64_t> got = frameHashes(*core, 3);
        INFO("GB: " << got[0] << ", " << got[1] << ", " << got[2]);
        CHECK(got == std::vector<std::uint64_t>{8259550387572866035ull, 10074299620259861403ull,
                                               13782190143996438851ull});
    }
    SECTION("GBA") {
        // A sibling fixture, so this SKIPs, counted, on a run without one (as CI's) rather than passing silently.
        const auto rom = resourceFixture("/roms/nanoloop287d.gba");
        if (rom.empty()) SKIP("the GBA fixture ROM is absent from the resources tree");
        auto core = std::make_unique<MesenGbaSystem>(1, MesenGbaConfig{}, rom);
        core->onActivate(kRate);
        const std::vector<std::uint64_t> got = frameHashes(*core, 3);
        INFO("GBA: " << got[0] << ", " << got[1] << ", " << got[2]);
        CHECK(got == std::vector<std::uint64_t>{3100562949282106931ull, 3100562949282106931ull,
                                               2667698829920294451ull});
    }
}
