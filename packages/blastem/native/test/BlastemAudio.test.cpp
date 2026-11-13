// The audio path, at the three points that fail silently: silence (a ROM that plays must not render zeros), the FM
// chip's contribution (a YM2612 write must change the output), and run-to-run determinism (the same input must give the
// same bytes, or nothing downstream can be compared).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 512;

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, std::move(rom));
    sys->onActivate(kRate);
    return sys;
}

// `seconds` of interleaved stereo at the host rate, one block of kFrames at a time (the last one short when the
// duration is not a whole number of blocks, which is the ordinary case for a duration in seconds).
std::vector<float> render(rp::blastem::BlastemMdSystem& sys, double seconds) {
    std::vector<float> out;
    std::vector<float> left(kFrames), right(kFrames);
    float*             lanes[2] = {left.data(), right.data()};
    AudioBlockInfo     info{};
    const std::uint32_t total = static_cast<std::uint32_t>(seconds * kRate);
    for (std::uint32_t done = 0; done < total; done += kFrames) {
        info.frames = std::min(kFrames, total - done);
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        sys.prepareForBlock(info);
        while (sys.stepIfBelowTarget(kFrames)) {}
        sys.finishBlock(info, lanes, 2);
        out.insert(out.end(), left.begin(), left.begin() + info.frames);
        out.insert(out.end(), right.begin(), right.begin() + info.frames);
    }
    return out;
}

double peak(const std::vector<float>& samples) {
    double p = 0.0;
    for (float s : samples) p = std::max(p, std::fabs(static_cast<double>(s)));
    return p;
}

}  // namespace

TEST_CASE("a ROM playing PSG and FM is not silent", "[blastem][audio]") {
    auto sys = boot(rp::blastem::test::fmPsgRom());
    const std::vector<float> out = render(*sys, 0.5);
    CHECK(out.size() == static_cast<std::size_t>(0.5 * kRate) * 2);
    CHECK(peak(out) > 0.01);  // about -40 dBFS: a full-volume PSG channel is far louder, the check only rejects silence
    // The sink must never have run dry: a starved sample is a zero nobody's ROM asked for, and its position would be
    // a function of the block partition.
    CHECK(sys->starvedSamples() == 0);
}

TEST_CASE("the YM2612's writes change the output", "[blastem][audio]") {
    auto  psg = boot(rp::blastem::test::psgOnlyRom());
    auto  both = boot(rp::blastem::test::fmPsgRom());
    const std::vector<float> a = render(*psg, 0.25);
    const std::vector<float> b = render(*both, 0.25);
    REQUIRE(a.size() == b.size());
    // Both halves must be audible on their own, or the inequality below could hold with one of them silent: the
    // prototype measured the FM patch's own peak at about 435 in the 16-bit source ring against the PSG's 2190, so a
    // quiet patch is expected and silence is not.
    CHECK(peak(a) > 0.01);
    CHECK(peak(b) > 0.01);
    CHECK(a != b);  // the FM patch is audible: the two renders differ
    CHECK(psg->starvedSamples() == 0);
    CHECK(both->starvedSamples() == 0);
}

TEST_CASE("the same input renders the same bytes twice", "[blastem][audio]") {
    auto  first  = boot(rp::blastem::test::fmPsgRom());
    auto  second = boot(rp::blastem::test::fmPsgRom());
    CHECK(render(*first, 0.25) == render(*second, 0.25));
}

TEST_CASE("a chip's rate change ends its frame first", "[blastem][audio]") {
    // The VDP test register's bit 0 moves the PSG's divider from 240 to 112 and calls psg_adjust_master_clock
    // (genesis.c:1146-1148), which is the sink's adjustClock. Writing the test port's register is NOT the same thing:
    // the comparison before and after the write is genesis.c's, so vdp_test_port_write alone changes no clock and this
    // case would pass without exercising a line of the sink. switchPsgClock runs those two lines, and the native rate
    // the sink recorded is what the case reads, so a sink that ignored the call fails here.
    auto sys = boot(rp::blastem::test::psgOnlyRom());
    REQUIRE(sys->audio().sourceNativeRate("PSG") == 53693175.0 / 240.0);
    const std::vector<float> before = render(*sys, 0.1);
    sys->switchPsgClock(true);
    // 53,693,175 / 112 = 479,403.35: the divider really moved, and the sink took the new one.
    CHECK(sys->audio().sourceNativeRate("PSG") == 53693175.0 / 112.0);
    const std::vector<float> after = render(*sys, 0.1);
    CHECK(peak(before) > 0.01);
    CHECK(peak(after) > 0.01);
    CHECK(sys->starvedSamples() == 0);
}
