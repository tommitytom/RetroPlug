// Guards the runtime NES APU flush window (deps/mesen/Core/NES/NesSoundMixer — _cycleLength +
// SetLatencyMs, driven by the live "mesen" apuLatencyMs knob via MesenNesSystem::setApuLatencyMs).
// The window used to be a compile-time constant; it's now a latency-in-ms knob the user can change while
// playing. This checks the ms→cycles conversion tracks the knob (region-clock-scaled, clamped) AND that a
// real render at different latencies still produces valid audio — i.e. changing it live doesn't break or
// silence the mix path.
//
// Run via `pnpm test:plugin`.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "system/SystemTypes.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "transport/MidiTypes.hpp"

#ifndef RP_N8_MIDI_ROM_PATH
#error "RP_N8_MIDI_ROM_PATH must be defined (path to resources/roms/n8-midi.nes)"
#endif

namespace {

constexpr double        kSampleRate = 48000.0;
constexpr std::uint32_t kFrames     = 512;

std::vector<std::uint8_t> readRom() {
    std::FILE* f = std::fopen(RP_N8_MIDI_ROM_PATH, "rb");
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

std::unique_ptr<MesenNesSystem> buildNes() {
    MesenNesConfig cfg;  // defaults: Mix mode, apuLatencyMs = 1.4
    cfg.romPath = RP_N8_MIDI_ROM_PATH;
    auto sys = std::make_unique<MesenNesSystem>(1, cfg, readRom());
    sys->onActivate(kSampleRate);
    REQUIRE(sys->activated());
    return sys;
}

} // namespace

TEST_CASE("NES APU flush window tracks the latency knob and clamps", "[audio][nes]") {
    auto sys = buildNes();

    // Default 1.4 ms ≈ the historical 2500-cycle window (NTSC ~1.79 MHz → ~2506). Allow slack for the
    // resolved region clock without hardcoding it.
    const std::uint32_t base = sys->apuFlushCycleLength();
    CHECK(base > 2000);
    CHECK(base < 3200);

    // Conversion is linear in ms: 10× the latency → ~10× the cycles (same clock).
    sys->setApuLatencyMs(0.5);
    const std::uint32_t lo = sys->apuFlushCycleLength();
    sys->setApuLatencyMs(5.0);
    const std::uint32_t hi = sys->apuFlushCycleLength();
    CHECK(lo < base);
    CHECK(hi > base);
    CHECK(std::abs(static_cast<double>(hi) / static_cast<double>(lo) - 10.0) < 0.5);

    // Clamp: absurd values saturate at the mixer's bounds (two out-of-range highs land on the same value,
    // two lows likewise), and low != high — proves both rails engage without hardcoding the constants.
    sys->setApuLatencyMs(100.0);
    const std::uint32_t hiSat = sys->apuFlushCycleLength();
    sys->setApuLatencyMs(200.0);
    CHECK(sys->apuFlushCycleLength() == hiSat);   // saturated at MaxCycleLength
    CHECK(hiSat > hi);

    sys->setApuLatencyMs(0.0001);
    const std::uint32_t loSat = sys->apuFlushCycleLength();
    sys->setApuLatencyMs(0.00001);
    CHECK(sys->apuFlushCycleLength() == loSat);   // saturated at MinCycleLength
    CHECK(loSat < lo);
    CHECK(loSat < hiSat);
}

TEST_CASE("NES renders valid audio across APU latencies", "[audio][nes]") {
    // A render at a small vs a large flush window must both produce non-silent output of exactly `frames`
    // samples — the live re-threshold must not break, stall, or silence the mix path.
    ::MidiEvent note{};
    note.frame = 0;
    note.size  = 3;
    note.data[0] = 0x90;  // NoteOn ch1 → APU Pulse1
    note.data[1] = 60;
    note.data[2] = 100;

    auto renderPeak = [&](double latencyMs) {
        auto sys = buildNes();
        sys->setApuLatencyMs(latencyMs);

        std::vector<float> l(kFrames, 0.0f), r(kFrames, 0.0f);
        float* outs[2] = {l.data(), r.data()};
        AudioBlockInfo info{};
        info.frames     = kFrames;
        info.sampleRate = kSampleRate;

        float peak = 0.0f;
        // Boot (~1 s), prime the note near the end (n8-midi drops its first MIDI message), then play.
        for (int b = 0; b < 210; ++b) {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            if (b == 88 || b == 89 || (b >= 90 && b % 20 == 0)) sys->onMidi(&note, 1);
            sys->onProcess(info, outs);
            if (b >= 95) {
                for (std::uint32_t i = 0; i < kFrames; ++i) peak = std::max(peak, std::abs(l[i]));
            }
        }
        return peak;
    };

    CHECK(renderPeak(0.5) > 0.01f);
    CHECK(renderPeak(5.0) > 0.01f);
}
