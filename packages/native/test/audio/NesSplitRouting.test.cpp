// Guards NES-in-plugin split routing end to end through the Engine: AudioRouting::PinSplit and
// AudioRouting::ChannelSplit over a REAL Mesen NES core, mono-packed one stream per output lane.
//
// The load-bearing thing here is the LIVE ARM. MesenNesSystem only reports per-channel streams once
// NesSoundMixer::SetChannelCapture has run, and that used to happen at construct only (from the "mesen"
// role's channelExportMode) — so selecting a split mode in the plugin would have silently rendered a
// plain stereo mix. Engine::syncSplitPlan arms the tap for the mode being entered without rebuilding the
// core (a routing flip must not reset a playing game), and restores what it found on the way out so the
// CLI / render path — which DOES set channelExportMode at construct and never selects a split routing —
// is left alone. Every assertion below is really about that: a non-silent lane 2 (Expansion) or lane 4
// (DMC) can only exist if the tap armed after construct.
//
// Lane arithmetic itself (stride, budget, gating) is EngineChannelSplit.test.cpp's fake-system job, and
// pin fidelity (Sum(pins) == the mix) is NesStems.test.cpp's; neither is re-litigated here.
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

#include "host/engine/Engine.hpp"
#include "system/AudioRouting.hpp"
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

// A booted NES, adopted the way a host adopts one: constructed with the DEFAULT (Mix) export mode, so
// anything per-channel the Engine later reports had to be armed live.
std::unique_ptr<MesenNesSystem> buildNes(SystemId id) {
    MesenNesConfig cfg;
    cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
    auto sys = std::make_unique<MesenNesSystem>(id, cfg, readRom());
    sys->onActivate(kSampleRate);
    REQUIRE(sys->activated());
    REQUIRE(sys->channelExportMode() == 0);  // constructed as a plain Mix system
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

// Eight output lanes plus the peak-magnitude readout the checks are phrased in.
struct Lanes {
    std::array<std::vector<float>, 8> buf;
    std::array<float*, 8>             ptr{};
    std::array<float, 8>              peak{};

    Lanes() {
        for (std::size_t i = 0; i < buf.size(); ++i) {
            buf[i].assign(kFrames, 0.0f);
            ptr[i] = buf[i].data();
        }
    }
    void resetPeaks() { peak.fill(0.0f); }
    void accumulatePeaks() {
        for (std::size_t L = 0; L < buf.size(); ++L)
            for (float x : buf[L]) peak[L] = std::max(peak[L], std::abs(x));
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

} // namespace

TEST_CASE("Engine PinSplit arms a real NES tap live and fans 3 mono pins across outs 0..2",
          "[audio][channelsplit][nes]") {
    Engine eng(kSampleRate);
    eng.adoptSystem(buildNes(1));
    eng.setAudioRouting(AudioRouting::PinSplit);

    // The arm happened on the routing change, against an already-booted core — no rebuild, no reset.
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
}

TEST_CASE("Engine ChannelSplit on a real NES fans 5 mono core channels across outs 0..4",
          "[audio][channelsplit][nes]") {
    Engine eng(kSampleRate);
    eng.adoptSystem(buildNes(1));
    eng.setAudioRouting(AudioRouting::ChannelSplit);

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
    Engine eng(kSampleRate);
    eng.adoptSystem(buildNes(1));
    auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
    REQUIRE(nes != nullptr);

    // Round-trip every split mode. Each leaves the core exactly as it was found, so a user flipping
    // through the Audio Routing cycler can't strand the tap armed (which would leave the mix path
    // draining capture streams forever).
    eng.setAudioRouting(AudioRouting::PinSplit);
    CHECK(nes->channelExportMode() == 1);
    eng.setAudioRouting(AudioRouting::ChannelSplit);
    CHECK(nes->channelExportMode() == 3);
    eng.setAudioRouting(AudioRouting::Stereo);
    CHECK(nes->channelExportMode() == 0);
    CHECK(nes->channelLayout().size() == 1);  // back to the single stereo "Mix" stream

    // And the mix still renders: pair 0 carries the note, the other pairs stay silent.
    Lanes lanes;
    driveAndMeasure(eng, 1, lanes, 8);
    CHECK(lanes.peak[0] > kSignal);
    for (int L = 2; L < 8; ++L)
        CHECK(lanes.peak[L] == 0.0f);
}

TEST_CASE("A second system disarms the NES tap and falls back to the per-instance router",
          "[audio][channelsplit][nes]") {
    Engine eng(kSampleRate);
    eng.adoptSystem(buildNes(1));
    eng.setAudioRouting(AudioRouting::PinSplit);

    auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
    REQUIRE(nes != nullptr);
    REQUIRE(nes->channelExportMode() == 1);

    // The splits are single-system-only, so adopting a peer must retire the plan AND the tap it needed —
    // otherwise the mix path would keep draining capture streams nothing reads.
    eng.adoptSystem(buildNes(2));
    CHECK(nes->channelExportMode() == 0);
    CHECK(nes->channelLayout().size() == 1);

    // Removing the peer re-enters the split, re-arming from scratch: the plan tracks the system set, not
    // just the last setAudioRouting call.
    eng.removeSystem(2);
    CHECK(nes->channelExportMode() == 1);
    CHECK(nes->channelLayout().size() == 3);
}
