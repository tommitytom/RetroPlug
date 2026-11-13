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
#include "system/blastem/BlastemCore.hpp"    // genesis_context, and the console's freeze-log budget
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 1024;

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom, SystemId id = 1) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(id, cfg, std::move(rom));
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

TEST_CASE("a cart that loops on an unmapped read logs a bounded number of lines", "[blastem][faults]") {
    // The diagnostics a cart can trigger once per ACCESS (machine_freeze's library branch, and the Z80's banked-write
    // warning) used to reach vfprintf on every one of them, and this host runs the console on its block thread, which
    // is the audio thread: a cart poking an unmapped address in a loop was unbounded, blocking stderr I/O inside
    // run_until. The image below reads one unmapped address in its loop, so two seconds of it is thousands of freezes;
    // the console's own budget prints RP_FREEZE_LOG_LIMIT of them, then one notice that the rest are suppressed, and
    // every later one is only counted.
    auto sys = boot(rp::blastem::test::unmappedLoopRom());
    REQUIRE(sys->activated());
    std::vector<float> l(kFrames), r(kFrames);
    const int blocks = 94;  // about two seconds at 48 kHz in 1024-frame blocks
    for (int i = 0; i < blocks; ++i) runBlock(*sys, l, r);
    const std::uint32_t seen = sys->core()->freeze_log_seen;
    const std::uint32_t printed = sys->core()->freeze_log_printed;
    CHECK(printed <= RP_FREEZE_LOG_LIMIT + 1);  // the budget, plus the one line that says the rest are suppressed
    CHECK(printed >= RP_FREEZE_LOG_LIMIT);      // and it printed that much: the policy bounds the lines, it is not silent
    CHECK(seen > 1000);                         // on a run that reached the path far more often than the budget
    CHECK(sys->activated());
    CHECK(sys->mclkNow() > 0);
    // The budget is the console's, not the process's: a second instance starts with its own lines to spend.
    auto other = boot(rp::blastem::test::unmappedLoopRom(), 2);
    for (int i = 0; i < 4; ++i) runBlock(*other, l, r);
    CHECK(other->core()->freeze_log_printed >= RP_FREEZE_LOG_LIMIT);
}

TEST_CASE("a cart's own write to the HV counter does not end the machine", "[blastem][faults]") {
    // The review's reproducer for the freeze family: one `move.b #$12,$C00008`, which BlastEm's frontend answers with
    // machine_freeze, and in that build machine_freeze exits the process (its guard tested a macro nothing defines). The
    // image writes the port once at reset and spins, so a live core that ran blocks is the whole check.
    auto sys = boot(rp::blastem::test::hvCounterWriteRom());
    REQUIRE(sys->activated());
    std::vector<float> l(kFrames), r(kFrames);
    for (int i = 0; i < 8; ++i) runBlock(*sys, l, r);
    CHECK(sys->activated());
    CHECK(sys->mclkNow() > 0);
    CHECK(sys->video().framesPublished() > 0);
}
