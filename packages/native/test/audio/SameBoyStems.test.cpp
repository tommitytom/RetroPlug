// Guards the SameBoy per-channel tap (deps/sameboy/Core/apu.{c,h}, shipped as a tracked patch +
// SameBoySystem's capture/fan-out). The four per-channel stems the patched core emits must sum back
// to the mixed stereo output the plugin ships, in the same highpass mode the user selected — the
// Phase-2 fidelity claim of spec/10. Drives a real mGB ROM (the DMG boot chime supplies real
// multi-channel signal, so a dead tap can't pass) and checks the raw captured accumulators directly,
// then exercises the real 8-lane finishBlock split through a PerChannelRouter.
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
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

#ifndef RP_MGB_ROM_PATH
#error "RP_MGB_ROM_PATH must be defined (path to resources/roms/mGB.gb)"
#endif

namespace {

constexpr double   kSampleRate = 48000.0;
constexpr std::uint32_t kFrames = 800;
// The DMG boot chime plays near the end of the ~2.5 s logo scroll; drive well past it so the peak
// tracker is guaranteed to have seen signal.
constexpr int      kBlocks = 240;

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

std::unique_ptr<SameBoySystem> buildMgb(SameBoyHighpass hp) {
    SameBoyConfig cfg;
    cfg.model    = SameBoyModel::DmgB; // the DMG boot ROM plays the startup chime
    cfg.highpass = hp;
    cfg.fastBoot = false;              // run the full boot ROM so the chime sounds
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

// Drive one block through the fused mix entry (2-lane path), then assert the four raw per-channel
// accumulators sum to the raw mix accumulator sample-for-sample. Both are pre-gain captures, so gain
// never enters the comparison. Updates `peak` with the largest |stem| seen.
void checkSumsToMix(SameBoySystem& sys, float tol, float& peak) {
    std::array<float, kFrames> l{}, r{};
    float* outs[2] = {l.data(), r.data()};
    AudioBlockInfo info = blockInfo();
    sys.onProcess(info, outs);

    for (std::uint32_t j = 0; j < kFrames * 2; ++j) {
        float sum = 0.0f;
        for (std::size_t k = 0; k < SameBoySystem::kAudioChannelCount; ++k) {
            const float v = sys.chanAccum_[k][j];
            sum += v;
            peak = std::max(peak, std::abs(v));
        }
        CHECK(std::abs(sum - sys.stereoAccum_[j]) <= tol);
    }
}

} // namespace

TEST_CASE("SameBoy per-channel stems sum back to the mixed output", "[audio][sameboy]") {
    // Off and Accurate are linear filters, so the per-stem outputs sum to the mix (Remove-DC-Offset is
    // documented not to — each stem removes its own DC — so it is not asserted here). Tolerance absorbs
    // the int16 highpass rounding + the asymmetric s16->f32 normalisation; a routing bug (missing /
    // swapped / silent stem) misses by orders of magnitude more.
    for (SameBoyHighpass hp : {SameBoyHighpass::Off, SameBoyHighpass::Accurate}) {
        auto sys = buildMgb(hp);
        float peak = 0.0f;
        for (int blk = 0; blk < kBlocks; ++blk) {
            checkSumsToMix(*sys, /*tol=*/3.0e-3f, peak);
        }
        // The boot chime must have produced audible per-channel signal — else the tap is dead and the
        // sum-check above passed only because everything was zero.
        CHECK(peak > 0.02f);
    }
}

TEST_CASE("SameBoy reports 4 stereo streams and the 8-lane split sums to the mix", "[audio][sameboy]") {
    // Two identical instances driven identically are deterministic (the same property the CLI byte-for-byte
    // render relies on): A fans its 4 channels across 8 lanes via a PerChannelRouter (the real finishBlock
    // split branch); B renders the plain mix. A's summed lane-pairs must equal B's mix.
    std::vector<std::unique_ptr<SystemBase>> sysA, sysB;
    sysA.push_back(buildMgb(SameBoyHighpass::Off));
    sysB.push_back(buildMgb(SameBoyHighpass::Off));
    auto* a = static_cast<SameBoySystem*>(sysA[0].get());

    CHECK(a->channelLayout().size() == 4);

    std::array<std::vector<float>, 8> lane;
    for (auto& v : lane) v.assign(kFrames, 0.0f);
    std::vector<float> mixL(kFrames, 0.0f), mixR(kFrames, 0.0f);

    float* splitL[4] = {lane[0].data(), lane[2].data(), lane[4].data(), lane[6].data()};
    float* splitR[4] = {lane[1].data(), lane[3].data(), lane[5].data(), lane[7].data()};
    PerChannelRouter routerA(splitL, splitR, 4);
    StereoRouter routerB(mixL.data(), mixR.data());

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
    // Signal really flowed through the 8-lane split path (not an all-zero pass).
    CHECK(peak > 0.02f);
}
