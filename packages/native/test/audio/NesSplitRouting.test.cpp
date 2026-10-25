// Guards NES split routing end to end through the Engine: the documents TS builds for PinSplit,
// StereoPinSplit and ChannelSplit over a REAL Mesen NES core, mono-packed one stream per output lane.
//
// The load-bearing thing here is the LIVE ARM. MesenNesSystem only reports per-channel streams once
// NesSoundMixer::SetChannelCapture has run, and that used to happen at construct only (from the "mesen"
// role's channelExportMode), so selecting a split mode in the plugin would have silently rendered a
// plain stereo mix. The engine's SyncTap, riding the plan's swap, arms the tap for the mode being entered
// without rebuilding the core (a routing flip must not reset a playing game), and restores what it found
// on the way out so the CLI / render path (which DOES set channelExportMode at construct and never selects
// a split routing) is left alone. Every assertion below is really about that: a non-silent lane 2
// (Expansion) or lane 4 (DMC) can only exist if the tap armed after construct.
//
// Which routing wants which arm and which wiring is TS's (packages/retroplug/test/graph/build-graph.test.ts);
// the documents here are spelled the way it writes them (test/engine-graph/GraphDocs.hpp). Pin fidelity
// (Sum(pins) == the mix) is NesStems.test.cpp's; neither is re-litigated here.
//
// Drives bliptoaster with a ch1 note (-> APU Pulse1), which is the only way it makes sound.
//
// Run via `pnpm test:plugin`.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "../engine-graph/GraphDocs.hpp"
#include "host/engine/DspCommand.hpp"
#include "host/engine/Engine.hpp"
#include "system/SystemBase.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "transport/MidiTypes.hpp"

#ifndef RP_BLIPTOASTER_ROM_PATH
#error "RP_BLIPTOASTER_ROM_PATH must be defined (path to resources/roms/bliptoaster.nes)"
#endif

namespace {

constexpr double        kSampleRate = 48000.0;
constexpr std::uint32_t kFrames     = 512;
constexpr int           kBootBlocks = 90;   // ~1 s: boot + FIFO init before the note takes
constexpr int           kPlayBlocks = 60;   // ~0.6 s of held note to measure

std::vector<std::uint8_t> readRom() {
    std::FILE* f = std::fopen(RP_BLIPTOASTER_ROM_PATH, "rb");
    REQUIRE(f != nullptr);
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    REQUIRE(n > 0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(n));
    REQUIRE(std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size());
    std::fclose(f);
    return bytes;
}

// A booted NES, built the way buildCore builds one: constructed with the DEFAULT (Mix) export mode, so
// anything per-channel the Engine later reports had to be armed live.
std::unique_ptr<MesenNesSystem> buildNes(SystemId id, std::uint32_t exportMode = 0) {
    MesenNesConfig cfg;
    cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
    cfg.channelExportMode = exportMode;
    auto sys = std::make_unique<MesenNesSystem>(id, cfg, readRom());
    sys->onActivate(kSampleRate);
    REQUIRE(sys->activated());
    REQUIRE(sys->channelExportMode() == exportMode);  // 0: constructed as a plain Mix system
    return sys;
}

::MidiEvent noteOnCh1() {
    ::MidiEvent e{};
    e.frame   = 0;
    e.size    = 3;
    e.data[0] = 0x90;  // NoteOn, channel 1
    e.data[1] = 60;    // C4
    e.data[2] = 100;   // velocity
    return e;
}

// Eight output lanes plus the readouts the checks are phrased in: each lane's peak magnitude, and the
// biggest sample-level divergence within each stereo PAIR (0 => the pair's two lanes are identical,
// which is what "the mono stem was mirrored across the pair" means).
struct Lanes {
    std::array<std::vector<float>, 8> buf;
    std::array<float*, 8>             ptr{};
    std::array<float, 8>              peak{};
    std::array<float, 4>              pairDiff{};

    Lanes() {
        for (std::size_t i = 0; i < buf.size(); ++i) {
            buf[i].assign(kFrames, 0.0f);
            ptr[i] = buf[i].data();
        }
    }
    void resetPeaks() { peak.fill(0.0f); pairDiff.fill(0.0f); }
    void accumulatePeaks() {
        for (std::size_t L = 0; L < buf.size(); ++L)
            for (float x : buf[L]) peak[L] = std::max(peak[L], std::abs(x));
        for (std::size_t p = 0; p < pairDiff.size(); ++p)
            for (std::uint32_t i = 0; i < kFrames; ++i)
                pairDiff[p] = std::max(pairDiff[p], std::abs(buf[2 * p][i] - buf[2 * p + 1][i]));
    }
};

// Boot the core, then hold a note for kPlayBlocks, tracking each lane's peak over the held section only.
// The note is re-sent periodically so the measurement doesn't hinge on the ROM sustaining, and primed
// near the end of boot because bliptoaster drops its first MIDI message.
void driveAndMeasure(Engine& eng, SystemId id, Lanes& lanes, std::size_t numOutputs) {
    SystemBase* sys = eng.findSystem(id);
    REQUIRE(sys != nullptr);
    ::MidiEvent note = noteOnCh1();

    for (int b = 0; b < kBootBlocks; ++b) {
        if (b >= kBootBlocks - 2) sys->onMidi(&note, 1);
        eng.processBlock(kFrames, lanes.ptr.data(), numOutputs);
    }
    lanes.resetPeaks();
    for (int b = 0; b < kPlayBlocks; ++b) {
        if (b % 20 == 0) sys->onMidi(&note, 1);
        eng.processBlock(kFrames, lanes.ptr.data(), numOutputs);  // processBlock zeroes the lanes itself
        lanes.accumulatePeaks();
    }
}

constexpr float kSignal = 1.0e-4f;  // "this lane carried audio"; a routed NES stem is orders above it

// An engine placing the documents TS would send: a core is parked as buildCore parks one, then planned in.
struct NesRig {
    Engine eng{kSampleRate};
    void park(std::unique_ptr<SystemBase> core) {
        REQUIRE(eng.parkCore(std::move(core)));
    }
    void place(Engine::GraphRequest r) { REQUIRE(eng.setGraph(r).ok); }
};

using namespace rp::host::test;

} // namespace

TEST_CASE("Engine PinSplit arms a real NES tap live and fans 3 mono pins across outs 0..2",
          "[audio][channelsplit][nes]") {
    NesRig rig;
    Engine& eng = rig.eng;
    rig.park(buildNes(1));
    rig.place(rearming(graphRequest(nesSplitDocument(1, "pins"), {1}), 1, 1));

    // The arm rode the plan's swap, against an already-booted core — no rebuild, no reset.
    auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
    REQUIRE(nes != nullptr);
    CHECK(nes->channelExportMode() == 1);           // StereoModPins
    CHECK(nes->channelLayout().size() == 3);        // Pulse | TND | Expansion

    Lanes lanes;
    driveAndMeasure(eng, 1, lanes, 8);

    // Pulse carries the note. TND and Expansion are present as lanes whatever they contain — the real
    // claim is the negative one below, that nothing spilled past the three pins.
    CHECK(lanes.peak[0] > kSignal);
    for (int L = 3; L < 8; ++L)
        CHECK(lanes.peak[L] == 0.0f);
    // Pin 2 (TND) sits on the RIGHT of the same pair, so out 1/2 is one DAW track carrying two different
    // pins. That is exactly what StereoPinSplit exists to avoid, and the contrast the next case asserts.
    CHECK(lanes.pairDiff[0] > kSignal);
}

TEST_CASE("Engine StereoPinSplit gives each pin its own PAIR, mirrored L=R",
          "[audio][channelsplit][nes]") {
    NesRig rig;
    Engine& eng = rig.eng;
    rig.park(buildNes(1));
    rig.place(rearming(graphRequest(nesSplitDocument(1, "stereoPins"), {1}), 1, 1));

    // Same tap as PinSplit — the modes differ only in how many lanes the three streams spread over.
    auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
    REQUIRE(nes != nullptr);
    CHECK(nes->channelExportMode() == 1);
    CHECK(nes->channelLayout().size() == 3);

    Lanes lanes;
    driveAndMeasure(eng, 1, lanes, 8);

    // Pulse now fills BOTH lanes of pair 0 — a centred stereo track, not a hard-left one. The R lane
    // carrying signal at all is the whole feature: under PinSplit it would be TND.
    CHECK(lanes.peak[0] > kSignal);
    CHECK(lanes.peak[1] > kSignal);
    // Bit-identical across each pair (a copy of the L lane, never a second summed write — a doubled
    // stem would show as peak[1] == 2*peak[0] with pairDiff == peak[0]).
    CHECK(lanes.peak[1] == lanes.peak[0]);
    for (int p = 0; p < 3; ++p)
        CHECK(lanes.pairDiff[p] == 0.0f);
    // 3 pins × 2 lanes = 6, so the 4th pair is past the layout and untouched.
    CHECK(lanes.peak[6] == 0.0f);
    CHECK(lanes.peak[7] == 0.0f);
}

TEST_CASE("StereoPinSplit needs 6 lanes, so a narrow host falls back rather than truncating",
          "[audio][channelsplit][nes]") {
    // The SDL 4-channel case: mono Pins (3 lanes) fits, the pair-per-pin layout does not, so TS wires it as
    // Stereo and arms the tap all the same (the narrow-host defect, docs/modular-graph-phase-b0-design.md 2.5).
    NesRig rig;
    Engine& eng = rig.eng;
    rig.park(buildNes(1));
    rig.place(rearming(graphRequest(stereoDocument({{1, kNesType}}, 4), {1}), 1, 1));

    Lanes lanes;
    driveAndMeasure(eng, 1, lanes, 4);  // only 4 lanes offered — 6 needed

    // Stereo wiring: the plain mixed stereo into pair 0, and pair 1 silent.
    CHECK(lanes.peak[0] > kSignal);
    CHECK(lanes.peak[2] == 0.0f);
    CHECK(lanes.peak[3] == 0.0f);
}

TEST_CASE("Engine ChannelSplit on a real NES fans 5 mono core channels across outs 0..4",
          "[audio][channelsplit][nes]") {
    NesRig rig;
    Engine& eng = rig.eng;
    rig.park(buildNes(1));
    rig.place(rearming(graphRequest(nesSplitDocument(1, "channels"), {1}), 1, 3));

    auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
    REQUIRE(nes != nullptr);
    CHECK(nes->channelExportMode() == 3);           // IndividualMono
    CHECK(nes->channelLayout().size() == 5);        // Square1 | Square2 | Triangle | Noise | DMC

    Lanes lanes;
    driveAndMeasure(eng, 1, lanes, 8);

    // Square1 is the voice the ch1 note drives. Lanes 5..7 are past the layout and must stay untouched —
    // as stereo pairs these 5 streams would have wanted 10 lanes, so this is the packing working.
    CHECK(lanes.peak[0] > kSignal);
    for (int L = 5; L < 8; ++L)
        CHECK(lanes.peak[L] == 0.0f);
}

TEST_CASE("Leaving a split routing restores the NES tap and the plain stereo mix",
          "[audio][channelsplit][nes]") {
    NesRig rig;
    Engine& eng = rig.eng;
    rig.park(buildNes(1));
    rig.place(rearming(graphRequest(stereoDocument({{1, kNesType}}, 8), {1})));
    auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
    REQUIRE(nes != nullptr);

    // Round-trip every split mode. Each leaves the core exactly as it was found, so a user flipping
    // through the Audio Routing cycler can't strand the tap armed (which would leave the mix path
    // draining capture streams forever).
    rig.place(rearming(graphRequest(nesSplitDocument(1, "pins"), {1}), 1, 1));
    CHECK(nes->channelExportMode() == 1);
    rig.place(rearming(graphRequest(nesSplitDocument(1, "channels"), {1}), 1, 3));
    CHECK(nes->channelExportMode() == 3);
    rig.place(rearming(graphRequest(stereoDocument({{1, kNesType}}, 8), {1})));
    CHECK(nes->channelExportMode() == 0);
    CHECK(nes->channelLayout().size() == 1);  // back to the single stereo "Mix" stream

    // And the mix still renders: pair 0 carries the note, the other pairs stay silent.
    Lanes lanes;
    driveAndMeasure(eng, 1, lanes, 8);
    CHECK(lanes.peak[0] > kSignal);
    for (int L = 2; L < 8; ++L)
        CHECK(lanes.peak[L] == 0.0f);
}

TEST_CASE("A second system disarms the NES tap and falls back to Stereo",
          "[audio][channelsplit][nes]") {
    NesRig rig;
    Engine& eng = rig.eng;
    rig.park(buildNes(1));
    rig.place(rearming(graphRequest(nesSplitDocument(1, "pins"), {1}), 1, 1));

    auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
    REQUIRE(nes != nullptr);
    REQUIRE(nes->channelExportMode() == 1);

    // The splits are single-system-only, so adding a peer must retire the wiring AND the tap it needed —
    // otherwise the mix path would keep draining capture streams nothing reads. TS sends a restore only.
    rig.park(buildNes(2));
    rig.place(rearming(graphRequest(stereoDocument({{1, kNesType}, {2, kNesType}}, 8), {1, 2})));
    CHECK(nes->channelExportMode() == 0);
    CHECK(nes->channelLayout().size() == 1);

    // Removing the peer re-enters the split, re-arming from scratch.
    rig.place(rearming(graphRequest(nesSplitDocument(1, "pins"), {1}), 1, 1));
    CHECK(nes->channelExportMode() == 1);
    CHECK(nes->channelLayout().size() == 3);
}

TEST_CASE("A re-arm restores and arms again, even with an unchanged document",
          "[audio][channelsplit][nes]") {
    NesRig rig;
    Engine& eng = rig.eng;
    rig.park(buildNes(1));
    const Engine::GraphRequest pins = rearming(graphRequest(nesSplitDocument(1, "pins"), {1}), 1, 1);
    rig.place(pins);
    auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
    REQUIRE(nes != nullptr);
    REQUIRE(nes->channelExportMode() == 1);

    // Knock the tap off the mode the routing wants, behind the engine's back: only a re-arm (restore, then
    // arm again, resetting the capture streams) puts it back. Every edit that builds or drops a core, and
    // every routing change, re-arms, even one that leaves the document as it was.
    nes->setChannelExportMode(2);
    rig.place(pins);
    CHECK(nes->channelExportMode() == 1);
    CHECK(eng.systemCount() == 1);
}

TEST_CASE("A replacing NES is armed afresh, never restored to the replaced core's mode",
          "[audio][channelsplit][nes]") {
    NesRig rig;
    Engine& eng = rig.eng;
    rig.park(buildNes(1, 2));  // built with the pins + mix-reference tap
    rig.place(rearming(graphRequest(nesSplitDocument(1, "channels"), {1}), 1, 3));  // armed to 3, remembering 2
    REQUIRE(dynamic_cast<MesenNesSystem*>(eng.findSystem(1))->channelExportMode() == 3);

    rig.park(buildNes(2));  // a fresh core, built as plain Mix, in the replaced one's slot
    rig.place(rearming(graphRequest(nesSplitDocument(2, "channels"), {2}), 2, 3));
    auto* fresh = dynamic_cast<MesenNesSystem*>(eng.findSystem(2));
    REQUIRE(fresh != nullptr);
    CHECK(fresh->channelExportMode() == 3);
    // Leaving the split puts back what THIS core was built with: the old core's 2 left with it.
    rig.place(rearming(graphRequest(stereoDocument({{2, kNesType}}, 8), {2})));
    CHECK(fresh->channelExportMode() == 0);
}

TEST_CASE("A routing edit's plan and its tap arming take one ring slot, so they land together",
          "[audio][channelsplit][nes]") {
    NesRig rig;
    Engine& eng = rig.eng;
    rig.park(buildNes(1));
    rig.place(rearming(graphRequest(stereoDocument({{1, kNesType}}, 8), {1})));
    auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
    REQUIRE(nes != nullptr);

    // The test thread is the audio thread from here: pushes only queue, and drainCommands applies them.
    eng.setAudioThreadOwns(true);
    DspCommand bpm;
    bpm.kind = DspCommand::Kind::SetBpm;
    bpm.setBpm = { 120.0 };
    std::size_t depth = 0;
    while (eng.pushCommand(bpm)) ++depth;
    eng.drainCommands();
    for (std::size_t i = 0; i + 1 < depth; ++i) REQUIRE(eng.pushCommand(bpm));

    // One slot left. Were the arming a command of its own it would be dropped here, and the pin wiring
    // would run against an unarmed tap.
    REQUIRE(eng.setGraph(rearming(graphRequest(nesSplitDocument(1, "pins"), {1}), 1, 1)).ok);
    CHECK_FALSE(eng.pushCommand(bpm));
    eng.drainCommands();
    CHECK(nes->channelExportMode() == 1);
    CHECK(nes->channelLayout().size() == 3);
    eng.setAudioThreadOwns(false);
}
