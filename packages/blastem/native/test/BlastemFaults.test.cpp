// A cart's own mistakes are the cart's, not the host's (design section 2): an unmapped access, an illegal port read or a
// write the hardware does not have is a warning and a defined answer here, never the end of the process that runs the
// plugin. BlastEm's frontend answers every one of them through `machine_freeze`, which in its own build ends the machine;
// the image below does three of them and then runs a loop, so "still running" is something a case can read.
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 1024;

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, std::move(rom));
    sys->onActivate(kRate);
    return sys;
}

void runBlock(rp::blastem::BlastemMdSystem& sys, std::vector<float>& l, std::vector<float>& r) {
    float* lanes[2] = {l.data(), r.data()};
    AudioBlockInfo info{};
    info.frames = kFrames;
    std::fill(l.begin(), l.end(), 0.0f);
    std::fill(r.begin(), r.end(), 0.0f);
    sys.prepareForBlock(info);
    while (sys.stepIfBelowTarget(kFrames)) {}
    sys.finishBlock(info, lanes, 2);
}

}  // namespace

TEST_CASE("a cart's unmapped and illegal accesses do not end the machine", "[blastem][faults]") {
    auto sys = boot(rp::blastem::test::unmappedAccessRom());
    REQUIRE(sys->activated());
    std::vector<float> l(kFrames), r(kFrames);
    bool               sounded = false;
    for (int i = 0; i < 8; ++i) {
        runBlock(*sys, l, r);
        sounded = sounded || std::any_of(l.begin(), l.end(), [](float v) { return v != 0.0f; });
    }
    // Every fault the image performs is behind the run, so a live core, a counter that moved and audio are all "the
    // block after them ran": the faults are on the 68k's path to the loop, not after it.
    CHECK(sys->activated());
    CHECK(sys->mclkNow() > 0);
    CHECK(sys->video().framesPublished() > 0);
    CHECK(sounded);
    // The counter at $FF0002, in the address order getMemory(Ram) publishes (so byte 2 is the high half): the loop the
    // image reaches only after all three faults, having run it many times.
    const auto ram = sys->getMemory(rp::MemoryType::Ram, rp::AccessType::Read);
    REQUIRE(ram.size() >= 4);
    const auto counter = std::uint16_t(ram[2] << 8 | ram[3]);
    CHECK(counter > 1);
}
