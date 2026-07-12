// Guards the ChannelSplit ENGINE gating (spec/10 step 4 — the load-bearing correctness rule):
// Engine::processBlock builds the ChannelSplitRouter ONLY for a single system; any other project falls
// back to the per-instance MultiOutRouter and the wide channel layout stays inert, so a multi-instance
// project can never mis-route. Uses a fake 4-stream SystemBase (deterministic per-lane markers) so the
// router CHOICE is asserted directly on the output lanes — no emulator core, no MIDI, no DSP kernel.
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
