// Guards the NES stereo-mod pin tap (deps/mesen/Core/NES/NesSoundMixer.{h,cpp} + MesenNesSystem's
// capture/fan-out, spec/10 §5). The three mono pin streams — Pulse (Square1+Square2), TND
// (DMC/Triangle/Noise) and the lumped Expansion term — must reconstruct the mixed output. Because the NES
// core renders at 96 kHz and resamples to the host rate, a naïve "compare pins to the mix ring" drifts by
// a sample (two independent resampler phases). So the fidelity mode (channelExportMode 2, native-only)
// captures a 4th REFERENCE stream — the full mix scalar through the SAME per-stream resampler as the pins
// — inside ONE instance. Σ(pins) == reference is then exact by construction (blip + Hermite linearity),
// isolating "is the split correct?" from any resampler-phase question. Drives n8-midi with a ch1 note
// (→ APU Pulse1) so real signal flows; a dead or mis-split tap misses by orders of magnitude.
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

#include "system/BlockRunner.hpp"
#include "system/SystemTypes.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "transport/MidiTypes.hpp"

#ifndef RP_BLIPTOASTER_ROM_PATH
#error "RP_BLIPTOASTER_ROM_PATH must be defined (path to resources/roms/bliptoaster.nes)"
#endif

namespace {

constexpr double        kSampleRate = 48000.0;
constexpr std::uint32_t kFrames     = 512;
constexpr int           kBootBlocks = 90;    // ~1 s: boot + FIFO init before the note takes
constexpr int           kPlayBlocks = 120;   // ~1.3 s of held note to compare

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

std::unique_ptr<MesenNesSystem> buildNes(std::uint32_t channelExportMode) {
    MesenNesConfig cfg;
    cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
    cfg.channelExportMode = channelExportMode;
    auto sys = std::make_unique<MesenNesSystem>(1, cfg, readRom());
    sys->onActivate(kSampleRate);
    REQUIRE(sys->activated());
    return sys;
}

::MidiEvent noteOnCh1() {
    ::MidiEvent e;
    e.frame = 0;
    e.size  = 3;
    e.data[0] = 0x90;  // NoteOn, channel 1
    e.data[1] = 60;    // C4
    e.data[2] = 100;   // velocity
    return e;
}

} // namespace

TEST_CASE("NES stereo-mod pins reconstruct the mixed output", "[audio][nes]") {
    // Mode 2 reports 4 streams: [0] Pulse, [1] TND, [2] Expansion, [3] MixRef (the mix through the SAME
    // path). Drive the whole thing through a PerChannelRouter (the real finishBlock split branch) so all 4
    // are drained identically, then assert Σ(pins) == the reference per sample.
    auto sys = buildNes(/*channelExportMode=*/2);
    REQUIRE(sys->channelLayout().size() == 4);

    std::array<std::vector<float>, 8> lane;   // 4 streams × 2 lanes (mono → R unused)
    for (auto& v : lane) v.assign(kFrames, 0.0f);
    float* splitL[4] = {lane[0].data(), lane[2].data(), lane[4].data(), lane[6].data()};
    float* splitR[4] = {lane[1].data(), lane[3].data(), lane[5].data(), lane[7].data()};
    PerChannelRouter router(splitL, splitR, 4);

    std::vector<std::unique_ptr<SystemBase>> holder;
    holder.push_back(std::move(sys));
    SystemBase* m = holder[0].get();

    AudioBlockInfo info{};
    info.frames     = kFrames;
    info.sampleRate = kSampleRate;

    ::MidiEvent note = noteOnCh1();
    auto driveBlock = [&](bool sendNote) {
        for (auto& v : lane) std::fill(v.begin(), v.end(), 0.0f);
        if (sendNote) m->onMidi(&note, 1);
        runUnit(info, &m, 1, holder, router);
    };

    // Boot; prime the note near the end (n8-midi drops the first MIDI message).
    for (int b = 0; b < kBootBlocks; ++b) {
        driveBlock(/*sendNote=*/b >= kBootBlocks - 2);
    }

    // Play + compare. Σ(pins) == reference is exact by construction (same resampler path); the tolerance
    // only absorbs float rounding + the per-pin s16 quantisation. A routing bug misses by orders of magnitude.
    float peak = 0.0f;
    for (int b = 0; b < kPlayBlocks; ++b) {
        driveBlock(/*sendNote=*/b % 20 == 0);  // re-send so the check doesn't hinge on the ROM sustaining
        for (std::uint32_t i = 0; i < kFrames; ++i) {
            const float sumPins = lane[0][i] + lane[2][i] + lane[4][i]; // Pulse + TND + Expansion (L lanes)
            const float ref     = lane[6][i];                          // MixRef (L lane)
            peak = std::max(peak, std::abs(sumPins));
            CHECK(std::abs(sumPins - ref) <= 1.0e-3f);
        }
    }
    // Real signal flowed through the pins (not an all-zero pass) — the ch1 note reached APU Pulse1.
    CHECK(peak > 0.01f);
}
