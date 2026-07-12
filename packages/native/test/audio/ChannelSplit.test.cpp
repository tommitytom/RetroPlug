// Guards the ChannelSplitRouter (spec/10 step 4 — the plugin GB option): it fans ONE system's
// per-channel streams across fixed stereo PAIRS of the plugin's flat 8-lane output array (stream k ->
// outs 2k/2k+1). Two checks: a deterministic addressing proof over a fake 4-stream system (a collapse
// or mis-index would break it — which a sum-to-mix check alone cannot catch), and a real mGB fidelity
// proof that the summed pairs equal the mixed render, through the actual flat-lane router the Engine
// builds for the plugin (the twin of SameBoyStems' PerChannelRouter check).
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
#include "system/SystemBase.hpp"
#include "system/SystemTypes.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

#ifndef RP_MGB_ROM_PATH
#error "RP_MGB_ROM_PATH must be defined (path to resources/roms/mGB.gb)"
#endif

namespace {

constexpr double        kSampleRate = 48000.0;
constexpr std::uint32_t kFrames     = 800;
constexpr int           kBlocks     = 240;

// A SystemBase double reporting `streams` stereo streams; finishBlock SUMS a per-lane marker (lane + 1)
// into every lane it's handed, so lane L holds (L+1) iff it was routed + written. Mirrors ChannelStreams.
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

std::vector<std::uint8_t> readRom() {
    std::FILE* f = std::fopen(RP_MGB_ROM_PATH, "rb");
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

std::unique_ptr<SameBoySystem> buildMgb() {
    SameBoyConfig cfg;
    cfg.model    = SameBoyModel::DmgB; // the DMG boot ROM plays the startup chime → real multi-channel signal
    cfg.highpass = SameBoyHighpass::Off;
    cfg.fastBoot = false;
    auto sys = std::make_unique<SameBoySystem>(1, cfg, readRom());
    sys->onActivate(kSampleRate);
    return sys;
}

AudioBlockInfo blockInfo() {
    AudioBlockInfo info{};
    info.frames     = kFrames;
    info.sampleRate = kSampleRate;
    return info;
}

} // namespace

TEST_CASE("ChannelSplitRouter fans stream k into stereo pair k of the flat output array", "[audio][channelsplit]") {
    std::vector<std::unique_ptr<SystemBase>> systems;
    systems.push_back(std::make_unique<FakeSystem>(1, /*streams=*/4));
    SystemBase* member = systems[0].get();

    const std::uint32_t frames = 8;
    std::array<std::vector<float>, 8> lane;
    for (auto& v : lane) v.assign(frames, 0.0f);
    float* outs[8];
    for (int i = 0; i < 8; ++i) outs[i] = lane[i].data();

    ChannelSplitRouter router(outs, 8, /*nStreams=*/4);
    CHECK(router.streamCount(0) == 4);

    AudioBlockInfo info{};
    info.frames     = frames;
    info.sampleRate = kSampleRate;
    runUnit(info, &member, 1, systems, router);

    // stream k -> lanes 2k / 2k+1, so lane L carries marker (L+1). A collapse (all -> pair 0) or a
    // mis-index would break this exact mapping — the check a sum-to-mix comparison can't make.
    for (int L = 0; L < 8; ++L)
        for (float x : lane[L]) CHECK(x == static_cast<float>(L + 1));
}

TEST_CASE("ChannelSplitRouter over a real mGB: the 4 summed pairs equal the mixed render", "[audio][channelsplit]") {
    // Two identical instances driven identically are deterministic: A fans its 4 channels across the flat
    // 8-lane array via the ChannelSplitRouter (the real finishBlock split branch); B renders the plain
    // mix. A's summed lane-pairs must equal B's mix — the plugin-router twin of the SameBoyStems check.
    std::vector<std::unique_ptr<SystemBase>> sysA, sysB;
    sysA.push_back(buildMgb());
    sysB.push_back(buildMgb());
    auto* a = static_cast<SameBoySystem*>(sysA[0].get());
    CHECK(a->channelLayout().size() == 4);

    std::array<std::vector<float>, 8> lane;
    for (auto& v : lane) v.assign(kFrames, 0.0f);
    float* outs8[8];
    for (int i = 0; i < 8; ++i) outs8[i] = lane[i].data();
    std::vector<float> mixL(kFrames, 0.0f), mixR(kFrames, 0.0f);

    ChannelSplitRouter routerA(outs8, 8, 4); // the flat-lane router the Engine builds for the plugin
    StereoRouter       routerB(mixL.data(), mixR.data());

    AudioBlockInfo info = blockInfo();
    SystemBase* mA = sysA[0].get();
    SystemBase* mB = sysB[0].get();

    float peak = 0.0f;
    for (int blk = 0; blk < kBlocks; ++blk) {
        for (auto& v : lane) std::fill(v.begin(), v.end(), 0.0f);
        std::fill(mixL.begin(), mixL.end(), 0.0f);
        std::fill(mixR.begin(), mixR.end(), 0.0f);
        runUnit(info, &mA, 1, sysA, routerA);
        runUnit(info, &mB, 1, sysB, routerB);
        for (std::uint32_t i = 0; i < kFrames; ++i) {
            const float sumL = lane[0][i] + lane[2][i] + lane[4][i] + lane[6][i];
            const float sumR = lane[1][i] + lane[3][i] + lane[5][i] + lane[7][i];
            peak = std::max(peak, std::max(std::abs(sumL), std::abs(sumR)));
            CHECK(std::abs(sumL - mixL[i]) <= 3.0e-3f);
            CHECK(std::abs(sumR - mixR[i]) <= 3.0e-3f);
        }
    }
    CHECK(peak > 0.02f); // signal really flowed through the 8-lane split (not an all-zero pass)
}
