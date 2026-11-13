// The triad stops the 68k at the block's end, and the stop lands within one instruction of the clock asked for. This
// is what the whole sample-accurate design stands on: a port level staged at frame f must be applied at f's clock, so
// the core must be able to stop there and nowhere coarser. The third case is the rebase: a session's clock is 64-bit
// even though the 68k's count is not.
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemCore.hpp"
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double kRate = 48000.0;

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, std::move(rom));
    sys->onActivate(kRate);
    return sys;
}

}  // namespace

TEST_CASE("a block stops the 68k at the block's end", "[blastem][stop]") {
    auto sys = boot(rp::blastem::test::portPollRom());
    AudioBlockInfo info{};
    info.frames = 256;
    sys->prepareForBlock(info);
    CHECK_FALSE(sys->stepIfBelowTarget(256));
    const std::uint64_t target = sys->masterClockOf(256);
    const std::uint64_t at = sys->mclkNow();
    // At or past the target, and within one 68k instruction of it: the longest 68k instruction is under 1200 master
    // clocks, so 2000 is "one instruction" with room.
    CHECK(at >= target);
    CHECK(at - target < 2000);
}

TEST_CASE("a second block continues from where the first stopped", "[blastem][stop]") {
    auto sys = boot(rp::blastem::test::portPollRom());
    AudioBlockInfo info{};
    info.frames = 64;
    sys->prepareForBlock(info);
    sys->stepIfBelowTarget(64);
    sys->finishBlock(info, nullptr, 0);  // the triad's third phase: it is what ends a block and starts the next
    const std::uint64_t first = sys->mclkNow();
    sys->prepareForBlock(info);
    sys->stepIfBelowTarget(64);
    CHECK(sys->mclkNow() > first);
    CHECK(sys->mclkNow() - first >= sys->masterClockOf(64) - sys->masterClockOf(0));
}

TEST_CASE("a session past the 32-bit rebase keeps one monotonic clock", "[blastem][stop]") {
    // MAX_NO_ADJUST is UINT32_MAX - 8*MCLKS_LINE*313 (genesis.c:561), so the 68k's own 32-bit count is rebased after
    // about 80 s of emulated time: a 32-bit base would wrap there and the session's clock would jump backwards, and a
    // stop left un-rebased would run the whole window inside one call. This is the suite's slowest case (a few seconds
    // on a host that runs the core at 22x) and it is the only one that can see either.
    auto sys = boot(rp::blastem::test::portPollRom());
    constexpr std::uint32_t kBlock = 8192;
    AudioBlockInfo info{};
    info.frames = kBlock;
    std::uint64_t previous = sys->mclkNow();
    std::uint64_t worst = 0;
    for (std::uint64_t s = 0; s < 90ull * 48000; s += kBlock) {  // 90 s: past the first rebase with margin
        sys->prepareForBlock(info);
        sys->stepIfBelowTarget(kBlock);
        const std::uint64_t now = sys->mclkNow();
        REQUIRE(now >= previous);  // monotonic: a 32-bit base fails here, at the rebase
        const std::uint64_t target = sys->masterClockOf(s + kBlock);
        const std::uint64_t over = now > target ? now - target : target - now;
        if (over > worst) worst = over;
        previous = now;
        sys->finishBlock(info, nullptr, 0);  // no lanes: the clock still advances
    }
    CHECK(worst < 2000);
    CHECK(previous > 0xFFFFFFFFull);  // the run really crossed the 32-bit boundary
}
