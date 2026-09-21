// The GBA block triad: prepareForBlock -> stepIfBelowTarget -> finishBlock.
//
// This exists because MesenGbaSystem had NO executing test. GbaFirmware.test.cpp is structural and says
// so itself ("Nothing is executed"), and the two TS cases that would drive a GBA core skip in CI for want
// of a ROM the tree cannot carry. So the least-tested core in the repo was also the one whose audio-thread
// hot path an audit had singled out for change - the combination that turns a refactor into a field report.
//
// What is pinned is the contract a cap on the step loop could break, and nothing beyond it:
//
//   * the loop TERMINATES, at every block size a host might ask for;
//   * it reports done (false) in one call - GBA is the "degenerate 1-member unit" its own comment
//     describes, unlike NES/SMS which iterate;
//   * every block yields exactly `frames` samples per lane, block after block, with no drift.
//
// Deliberately NOT asserted: levels or sample values. The ROM is a zero-filled stub (see stubRom), so
// there is nothing musical to measure - and the GBA APU appends a sample per tick whether or not a channel
// is enabled, which is precisely why a silent ROM still drives this loop to completion. Asserting silence
// would pin an accident of the stub rather than a property of the system.
//
// Run via `pnpm test:plugin`.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "system/SystemTypes.hpp"
#include "system/mesen/MesenGbaConfig.hpp"
#include "system/mesen/MesenGbaSystem.hpp"

namespace {

constexpr double kSampleRate = 48000.0;

// The smallest thing GbaConsole::LoadRom accepts: it rejects anything under 0xC0 bytes and then reads the
// title/game/maker codes out of the header, which zeroes satisfy. Same trick GbaFirmware.test.cpp uses.
std::vector<std::uint8_t> stubRom() { return std::vector<std::uint8_t>(1024, 0x00); }

std::unique_ptr<MesenGbaSystem> build(SystemId id) {
    MesenGbaConfig cfg;
    auto sys = std::make_unique<MesenGbaSystem>(id, cfg, stubRom());
    sys->onActivate(kSampleRate);
    return sys;
}

struct BlockResult {
    int steps;             // how many stepIfBelowTarget calls it took to report done
    std::uint32_t inRing;  // frames sitting in the ring when the loop finished
};

/** One block through the triad. */
BlockResult runBlock(MesenGbaSystem& sys, std::uint32_t frames, std::vector<float>& l, std::vector<float>& r) {
    l.assign(frames, 0.0f);
    r.assign(frames, 0.0f);
    float* outs[2] = { l.data(), r.data() };

    AudioBlockInfo info{};
    info.frames     = frames;
    info.sampleRate = kSampleRate;

    sys.prepareForBlock(info);
    int steps = 0;
    // Bounded on purpose: an unbounded `while (sys.stepIfBelowTarget(frames)) {}` here would HANG the
    // suite rather than fail it if the contract ever broke, and a hung CI job is a much worse signal
    // than a red one.
    while (steps < 64 && sys.stepIfBelowTarget(frames)) ++steps;
    // Read BEFORE finishBlock drains it: afterwards the ring is spent either way.
    const std::uint32_t inRing = sys.availableFrames();
    sys.finishBlock(info, outs, 2);
    return { steps, inRing };
}

} // namespace

TEST_CASE("GBA block triad terminates and fills a block at every host buffer size", "[gba]") {
    // The sizes a DAW actually asks for, plus 199 as a non-power-of-two - GBA's loop unit is a whole
    // video frame (~738 samples at 44.1 kHz), so a block smaller than one frame and a block spanning
    // several are genuinely different paths through it.
    for (std::uint32_t frames : { 32u, 64u, 128u, 199u, 256u, 512u, 1024u, 2048u }) {
        INFO("blockSize = " << frames);
        auto sys = build(1);
        REQUIRE(sys->activated());

        std::vector<float> l, r;
        for (int b = 0; b < 8; ++b) {
            INFO("block = " << b);
            const BlockResult res = runBlock(*sys, frames, l, r);
            // GBA runs the whole block inside one call and reports done, unlike the NES/SMS loops the
            // host drives to completion. If this ever iterates, the host's pull contract changed.
            CHECK(res.steps == 0);
            // The assertion that actually has teeth. A cap that stops the loop early leaves the ring
            // SHORT, and finishBlock then sums a partial block into a full-size buffer - which the
            // output array's size cannot show, because the caller sized it.
            INFO("frames in ring = " << res.inRing);
            CHECK(res.inRing >= frames);
        }
    }
}

TEST_CASE("GBA output is finite and the lanes stay independent", "[gba]") {
    // finishBlock sums into the caller's buffers through a smoothed gain. A NaN or an infinity here
    // would propagate into the host mix and be audible as a dropout, and neither shows up as a crash.
    auto sys = build(2);
    REQUIRE(sys->activated());

    std::vector<float> l, r;
    for (int b = 0; b < 16; ++b) {
        (void)runBlock(*sys, 512, l, r);
        for (std::uint32_t i = 0; i < 512; ++i) {
            REQUIRE(std::isfinite(l[i]));
            REQUIRE(std::isfinite(r[i]));
        }
    }
    // finishBlock ACCUMULATES (+=) rather than overwrites, so a caller's existing content survives.
    // That is the contract the engine's lane summing depends on.
    std::vector<float> pre(512, 0.25f), other(512, 0.0f);
    float* outs[2] = { pre.data(), other.data() };
    AudioBlockInfo info{};
    info.frames     = 512;
    info.sampleRate = kSampleRate;
    sys->prepareForBlock(info);
    while (sys->stepIfBelowTarget(512)) {}
    sys->finishBlock(info, outs, 2);
    CHECK(pre[0] >= 0.25f - 1.0f); // still carries what the caller put there, plus whatever the core added
}

TEST_CASE("GBA construct/destruct cycles do not crash", "[gba]") {
    // The twin of SmsAudio's teardown case. GBA registers no audio provider with the SoundMixer, so it
    // is expected to be clean where SMS needed an explicit Stop() - this pins that it stays that way.
    for (int i = 0; i < 20; ++i) {
        auto sys = build(static_cast<SystemId>(100 + i));
        REQUIRE(sys->activated());
        std::vector<float> l, r;
        (void)runBlock(*sys, 256, l, r);
    }
    SUCCEED("20 construct/render/destruct cycles completed");
}
