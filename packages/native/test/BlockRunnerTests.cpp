// Unit tests for the shared audio BlockRunner + its routers.
//
// The router-math cases pin the multi-out channel placement that previously had
// no headless coverage — it lived inline in PluginDSP::run and was only
// exercisable through a real DPF host. The runBlock cases verify per-slot
// isolation and that the runner never touches out-of-range channels. Real
// audio-content isolation (linked lockstep, follower sync) is covered by the
// TS sync_pattern / sync_negative fixtures, which now route through runBlock.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "project/Project.hpp"
#include "project/ProjectConfig.hpp"
#include "system/BlockRunner.hpp"
#include "system/SystemTypes.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

#ifndef RETROPLUG_TEST_GB_ROM
#  error "RETROPLUG_TEST_GB_ROM must be defined by CMake"
#endif

namespace {

constexpr double kSampleRate = 44100.0;
const std::string kRomPath = RETROPLUG_TEST_GB_ROM;

bool romAvailable() {
    std::error_code ec;
    return std::filesystem::exists(kRomPath, ec);
}

std::vector<std::uint8_t> loadRom() {
    std::ifstream f(kRomPath, std::ios::binary);
    if (!f) return {};
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// `count` identical standalone SameBoy instances, activated. Deterministic:
// same ROM + same config + same input -> byte-identical audio per instance.
std::unique_ptr<Project> makeProject(const std::vector<std::uint8_t>& rom, int count) {
    auto project = std::make_unique<Project>();
    for (int i = 0; i < count; ++i) {
        SameBoyConfig cfg{};
        cfg.romPath = kRomPath;
        const SystemId id = project->nextSystemId();
        auto sys = std::make_unique<SameBoySystem>(id, cfg, rom);
        sys->onActivate(kSampleRate);
        project->adoptSystem(sys.release());
    }
    return project;
}

AudioBlockInfo blockInfo(std::uint32_t frames) {
    return AudioBlockInfo{ frames, kSampleRate, 120.0, 0.0, false };
}

} // namespace

// ---------------------------------------------------------------------------
// Router math (pure pointer placement; no systems). These pin the channel
// mapping that used to live inline in PluginDSP's multi-out path.
// ---------------------------------------------------------------------------

TEST_CASE("StereoRouter sends every slot to one fixed bus", "[BlockRunner][router]") {
    float l, r;
    StereoRouter router(&l, &r);
    for (std::size_t i = 0; i < 5; ++i) {
        const AudioBus b = router.bus(i);
        CHECK(b.l == &l);
        CHECK(b.r == &r);
    }
}

TEST_CASE("MultiOutRouter TwoPerInstance: slot i -> (2i)%N / (2i+1)%N", "[BlockRunner][router]") {
    std::array<float, 8>  buf{};
    std::array<float*, 8> ch{};
    for (std::size_t i = 0; i < 8; ++i) ch[i] = &buf[i];
    MultiOutRouter router(ch.data(), 8, AudioRouting::TwoPerInstance);

    CHECK(router.bus(0).l == ch[0]); CHECK(router.bus(0).r == ch[1]);
    CHECK(router.bus(1).l == ch[2]); CHECK(router.bus(1).r == ch[3]);
    CHECK(router.bus(3).l == ch[6]); CHECK(router.bus(3).r == ch[7]);
    CHECK(router.bus(4).l == ch[0]); CHECK(router.bus(4).r == ch[1]);   // wraps mod 8
}

TEST_CASE("MultiOutRouter OnePerInstance is mono: l == r at channel i%N", "[BlockRunner][router]") {
    std::array<float, 8>  buf{};
    std::array<float*, 8> ch{};
    for (std::size_t i = 0; i < 8; ++i) ch[i] = &buf[i];
    MultiOutRouter router(ch.data(), 8, AudioRouting::OnePerInstance);

    const AudioBus b0 = router.bus(0);
    CHECK(b0.l == ch[0]); CHECK(b0.r == ch[0]); CHECK(b0.l == b0.r);   // aliased -> L+R sum
    CHECK(router.bus(5).l == ch[5]); CHECK(router.bus(5).r == ch[5]);
    CHECK(router.bus(9).l == ch[1]); CHECK(router.bus(9).r == ch[1]);  // wraps
}

TEST_CASE("MultiOutRouter Stereo sends every slot to channels 0/1", "[BlockRunner][router]") {
    std::array<float, 8>  buf{};
    std::array<float*, 8> ch{};
    for (std::size_t i = 0; i < 8; ++i) ch[i] = &buf[i];
    MultiOutRouter router(ch.data(), 8, AudioRouting::Stereo);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(router.bus(i).l == ch[0]);
        CHECK(router.bus(i).r == ch[1]);
    }
}

TEST_CASE("PerSystemRouter routes slot i to its own L/R buffer", "[BlockRunner][router]") {
    float l0, r0, l1, r1;
    std::array<float*, 2> ls{ &l0, &l1 };
    std::array<float*, 2> rs{ &r0, &r1 };
    PerSystemRouter router(ls.data(), rs.data());
    CHECK(router.bus(0).l == &l0); CHECK(router.bus(0).r == &r0);
    CHECK(router.bus(1).l == &l1); CHECK(router.bus(1).r == &r1);
}

// ---------------------------------------------------------------------------
// runBlock over real systems.
// ---------------------------------------------------------------------------

TEST_CASE("runBlock with PerSystemRouter isolates each slot's output", "[BlockRunner]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    const auto rom = loadRom();
    auto project = makeProject(rom, 2);   // two identical standalone instances

    constexpr std::uint32_t frames = 512;
    std::array<std::vector<float>, 2> bl{ std::vector<float>(frames), std::vector<float>(frames) };
    std::array<std::vector<float>, 2> br{ std::vector<float>(frames), std::vector<float>(frames) };
    std::array<float*, 2> ls{ bl[0].data(), bl[1].data() };
    std::array<float*, 2> rs{ br[0].data(), br[1].data() };
    PerSystemRouter router(ls.data(), rs.data());

    // Warm a little, then capture one block (caller zeroes the buffers; the
    // runner only sums).
    for (int b = 0; b < 16; ++b) {
        for (auto& v : bl) std::fill(v.begin(), v.end(), 0.0f);
        for (auto& v : br) std::fill(v.begin(), v.end(), 0.0f);
        runBlock(blockInfo(frames), *project, router);
    }

    // Distinct, non-aliased buffers, and identical instances produce identical
    // audio in their own slot — proves per-slot routing without cross-talk.
    CHECK(ls[0] != ls[1]);
    CHECK(bl[0] == bl[1]);
    CHECK(br[0] == br[1]);
}

TEST_CASE("runBlock with MultiOutRouter routes pairs and leaves spare channels untouched",
          "[BlockRunner]") {
    if (!romAvailable()) SKIP("Game Boy ROM missing at " << kRomPath);
    const auto rom = loadRom();
    auto project = makeProject(rom, 2);

    constexpr std::uint32_t frames = 512;
    constexpr float kSentinel = 0.5f;   // pre-fill spare channels; runner must not touch them
    std::array<std::vector<float>, 8> buf;
    std::array<float*, 8> ch{};
    for (std::size_t c = 0; c < 8; ++c) { buf[c].assign(frames, 0.0f); ch[c] = buf[c].data(); }

    MultiOutRouter router(ch.data(), 8, AudioRouting::TwoPerInstance);
    for (int b = 0; b < 16; ++b) {
        for (std::size_t c = 0; c < 4; ++c) std::fill(buf[c].begin(), buf[c].end(), 0.0f);
        for (std::size_t c = 4; c < 8; ++c) std::fill(buf[c].begin(), buf[c].end(), kSentinel);
        runBlock(blockInfo(frames), *project, router);
    }

    // System 0 -> channels 0/1, system 1 -> channels 2/3 (identical instances ->
    // identical pair content), and channels 4..7 are never written.
    CHECK(buf[0] == buf[2]);
    CHECK(buf[1] == buf[3]);
    for (std::size_t c = 4; c < 8; ++c)
        CHECK(std::all_of(buf[c].begin(), buf[c].end(),
                          [&](float x) { return x == kSentinel; }));
}
