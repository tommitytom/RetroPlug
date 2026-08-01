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

std::unique_ptr<MesenNesSystem> buildNes(double latencyMs = 1.4) {
    MesenNesConfig cfg;  // defaults: Mix mode
    cfg.romPath = RP_N8_MIDI_ROM_PATH;
    cfg.apuLatencyMs = latencyMs;  // exercises the construct→onActivate seed path (MesenBackend copies this)
    auto sys = std::make_unique<MesenNesSystem>(1, cfg, readRom());
    sys->onActivate(kSampleRate);
    REQUIRE(sys->activated());
    return sys;
}

::MidiEvent noteOnCh1() {
    ::MidiEvent e{};
    e.frame = 0;
    e.size  = 3;
    e.data[0] = 0x90;  // NoteOn ch1 → APU Pulse1
    e.data[1] = 60;
    e.data[2] = 100;
    return e;
}

// Drive `blocks` audio blocks through the mix path, priming the note (n8-midi drops its first MIDI
// message), and return the peak |L| seen after warmup.
float drive(MesenNesSystem& sys, int blocks) {
    ::MidiEvent note = noteOnCh1();
    std::vector<float> l(kFrames, 0.0f), r(kFrames, 0.0f);
    float* outs[2] = {l.data(), r.data()};
    AudioBlockInfo info{};
    info.frames     = kFrames;
    info.sampleRate = kSampleRate;
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b) {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        if (b == 88 || b == 89 || (b >= 90 && b % 20 == 0)) sys.onMidi(&note, 1);
        sys.onProcess(info, outs);
        if (b >= 95) {
            for (std::uint32_t i = 0; i < kFrames; ++i) peak = std::max(peak, std::abs(l[i]));
        }
    }
    return peak;
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

    // Absurdly large latency (bypasses the TS clamp, e.g. a corrupted project field) must not UB the
    // double→uint32 conversion; it saturates at the high rail.
    sys->setApuLatencyMs(1e30);
    CHECK(sys->apuFlushCycleLength() == hiSat);
}

TEST_CASE("NES construct-time apuLatencyMs seeds the flush window", "[audio][nes]") {
    // The config value must reach the mixer at onActivate (MesenBackend copy → SetLatencyMs), not just via
    // a later live setApuLatencyMs. A non-default 5.0 ms build must land far from the 2500-cycle field
    // default — so a broken seed (leaving the field default) would fail here.
    auto def = buildNes(1.4);
    auto coarse = buildNes(5.0);
    CHECK(coarse->apuFlushCycleLength() > 2 * def->apuFlushCycleLength());
    CHECK(coarse->apuFlushCycleLength() > 6000);  // ~8949 @ NTSC; well above any near-default value
}

TEST_CASE("NES renders valid audio across APU latencies", "[audio][nes]") {
    // A render at a small vs a large flush window must both produce non-silent output — the flush window
    // must not break, stall, or silence the mix path at either extreme.
    CHECK(drive(*buildNes(0.5), 210) > 0.01f);
    CHECK(drive(*buildNes(5.0), 210) > 0.01f);
}

TEST_CASE("NES APU latency lowered mid-render re-thresholds live without stalling", "[audio][nes]") {
    // The `==`→`>=` flush change exists so a LIVE decrease flushes promptly instead of waiting for a
    // threshold _currentCycle already passed. Boot + play coarse, then drop the window mid-stream and keep
    // rendering: the window must shrink and audio must keep flowing (no stall/silence from the shrink).
    auto sys = buildNes(5.0);
    const std::uint32_t coarse = sys->apuFlushCycleLength();
    CHECK(drive(*sys, 160) > 0.01f);            // warm up + sound at the coarse window

    sys->setApuLatencyMs(0.5);                   // live decrease, mid-run
    const std::uint32_t fine = sys->apuFlushCycleLength();
    CHECK(fine < coarse);

    // Keep rendering at the new (smaller) window — still non-silent (re-send the note so it doesn't hinge
    // on the ROM sustaining across the change).
    CHECK(drive(*sys, 120) > 0.01f);
}
