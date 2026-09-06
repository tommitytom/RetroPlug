// Guards the split-routing ENGINE gating (spec/10 step 4 — the load-bearing correctness rule):
// Engine::processBlock builds the ChannelSplitRouter ONLY for a single system; any other project falls
// back to the per-instance MultiOutRouter and the wide channel layout stays inert, so a multi-instance
// project can never mis-route. Also covers the MONO lane packing the NES needs (a mono layout takes one
// lane per stream, not a pair) and the two ways a plan is refused: PinSplit on a console with no output
// pins, and a layout too wide for the host's actual output count.
//
// Uses fake SystemBases (deterministic per-lane markers) so the router CHOICE and the lane arithmetic are
// asserted directly on the output lanes — no emulator core, no MIDI, no DSP kernel. The REAL NES tap
// arming those modes depend on is NesSplitRouting.test.cpp's job.
// Needs retroplug-backend (the Engine lives there).
//
// Run via `pnpm test:plugin`.

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "host/engine/Engine.hpp"
#include "system/AudioRouting.hpp"
#include "system/SystemBase.hpp"

namespace {

// The same SystemBase double as ChannelStreams / ChannelSplit: `streams` stereo streams; finishBlock sums
// a per-lane marker (lane + 1) into every lane, so lane L holds (L+1) iff it was routed + written.
class FakeSystem final : public SystemBase {
public:
    FakeSystem(SystemId id, int streams) : SystemBase(id), streams_(streams) {}
    SystemKind kind() const override { return SystemKind::SameBoy; }
    void onActivate(double) override {}
    void onSampleRateChanged(double) override {}
    std::vector<ChannelStream> channelLayout() const override {
        std::vector<ChannelStream> layout;
        for (int i = 0; i < streams_; ++i) layout.push_back({"Stream", true});
        return layout;
    }
    void finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) override {
        for (std::size_t ln = 0; ln < laneCount; ++ln)
            for (std::uint32_t f = 0; f < info.frames; ++f)
                outs[ln][f] += static_cast<float>(ln + 1);
    }

private:
    int streams_;
};

// The MONO counterpart (a NES's layout shape): `streams` mono streams, and — like
// MesenNesSystem::finishBlock — it writes ONLY the L lane of each stream's bus (outs[2k]), leaving
// outs[2k+1] at the caller-zeroed 0. Under stride 1 both bus lanes point at the SAME output lane, so
// this is also what proves the shared pointer can't double a stem.
class FakeMonoSystem final : public SystemBase {
public:
    FakeMonoSystem(SystemId id, int streams) : SystemBase(id), streams_(streams) {}
    SystemKind kind() const override { return SystemKind::MesenNes; }
    void onActivate(double) override {}
    void onSampleRateChanged(double) override {}
    std::vector<ChannelStream> channelLayout() const override {
        std::vector<ChannelStream> layout;
        for (int i = 0; i < streams_; ++i) layout.push_back({"Stream", false});
        return layout;
    }
    void finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) override {
        for (std::size_t k = 0; 2 * k < laneCount; ++k)
            for (std::uint32_t f = 0; f < info.frames; ++f)
                outs[2 * k][f] += static_cast<float>(k + 1);
    }

private:
    int streams_;
};

} // namespace

TEST_CASE("Engine ChannelSplit fans one system across 8 lanes; a multi-system project falls back", "[audio][channelsplit][engine]") {
    Engine eng(48000.0);
    eng.adoptSystem(std::make_unique<FakeSystem>(1, /*streams=*/4));
    eng.setAudioRouting(AudioRouting::ChannelSplit);

    const std::uint32_t frames = 8;
    std::array<std::vector<float>, 8> lane;
    for (auto& v : lane) v.assign(frames, 0.0f);
    float* outs[8];
    for (int i = 0; i < 8; ++i) outs[i] = lane[i].data();

    // One system + ChannelSplit -> ChannelSplitRouter: stream k -> pair k, so lane L carries (L+1).
    // (processBlock zeroes the lanes itself, so no manual clear between calls.)
    eng.processBlock(frames, outs, 8);
    for (int L = 0; L < 8; ++L)
        for (float x : lane[L]) CHECK(x == static_cast<float>(L + 1));

    // Add a second system: ChannelSplit is gated to systemCount()==1, so it falls back to the
    // per-instance MultiOutRouter (mode ChannelSplit -> its Stereo default) — every system sums into
    // pair 0 and lanes 2..7 stay silent. The wide layout is inert; the project can't mis-route.
    eng.adoptSystem(std::make_unique<FakeSystem>(2, /*streams=*/4));
    eng.processBlock(frames, outs, 8);
    for (float x : lane[0]) CHECK(x == 2.0f); // two systems, each +1 into lane 0
    for (float x : lane[1]) CHECK(x == 4.0f); // …and +2 into lane 1
    for (int L = 2; L < 8; ++L)
        for (float x : lane[L]) CHECK(x == 0.0f); // no split → pairs 1..3 untouched
}

TEST_CASE("Engine packs a MONO layout one stream per lane, so 5 NES channels fit 8 outputs",
          "[audio][channelsplit][engine]") {
    const std::uint32_t frames = 8;
    std::array<std::vector<float>, 8> lane;
    for (auto& v : lane) v.assign(frames, 0.0f);
    float* outs[8];
    for (int i = 0; i < 8; ++i) outs[i] = lane[i].data();

    SECTION("5 mono streams occupy lanes 0..4, undoubled, leaving 5..7 silent") {
        // As stereo PAIRS this layout would need 10 lanes and be refused (the reason NES-in-plugin was
        // deferred, spec/10 §3). Stride 1 lands stream k on lane k.
        Engine eng(48000.0);
        eng.adoptSystem(std::make_unique<FakeMonoSystem>(1, /*streams=*/5));
        eng.setAudioRouting(AudioRouting::ChannelSplit);
        eng.processBlock(frames, outs, 8);
        for (int k = 0; k < 5; ++k)
            for (float x : lane[k]) CHECK(x == static_cast<float>(k + 1)); // NOT 2*(k+1) — see FakeMonoSystem
        for (int L = 5; L < 8; ++L)
            for (float x : lane[L]) CHECK(x == 0.0f);
    }

    SECTION("a layout wider than the host's outputs falls back rather than writing out of bounds") {
        // The narrow-device case: an SDL 4-channel pick can carry 3 NES pins but not 5 channels.
        Engine eng(48000.0);
        eng.adoptSystem(std::make_unique<FakeMonoSystem>(1, /*streams=*/5));
        eng.setAudioRouting(AudioRouting::ChannelSplit);
        eng.processBlock(frames, outs, 4);        // only 4 lanes offered — 5 needed
        for (float x : lane[0]) CHECK(x == 1.0f); // MultiOutRouter → the one system's stream 0 into pair 0
        for (int L = 1; L < 4; ++L)
            for (float x : lane[L]) CHECK(x == 0.0f);
    }

    SECTION("PinSplit on a system that is not a NES falls back to Stereo") {
        // Pins are a 2A03 property; a GB asked for them must not silently render its channel split.
        Engine eng(48000.0);
        eng.adoptSystem(std::make_unique<FakeSystem>(1, /*streams=*/4)); // a stereo (GB-shaped) layout
        eng.setAudioRouting(AudioRouting::PinSplit);
        eng.processBlock(frames, outs, 8);
        for (float x : lane[0]) CHECK(x == 1.0f);
        for (float x : lane[1]) CHECK(x == 2.0f);
        for (int L = 2; L < 8; ++L)
            for (float x : lane[L]) CHECK(x == 0.0f); // no split → pairs 1..3 untouched
    }
}
