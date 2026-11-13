// The triad stops the 68k at the block's end, and the stop lands at the first instruction boundary at or past the clock
// asked for. This is what the whole sample-accurate design stands on: a port level staged at frame f must be applied at
// f's clock, so the core must be able to stop there and nowhere coarser. The bound is NOT "one instruction": a
// 68k-to-VDP DMA stalls the 68k inside its control-port write for about 8.8k master clocks (a 256-word DMA per VINT,
// measured), and VDP FIFO waits and Z80 bank accesses stall it too, so the bound is that stall with margin. The third
// case is the rebase: a session's clock is 64-bit even though the 68k's count is not.
#include <algorithm>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemCore.hpp"
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double kRate = 48000.0;

// The longest 68k stall this tree can produce, with margin: the DMA figure above, not an instruction's length. A case
// that tightens it below its ROM's own stall would be asserting something the core does not promise.
constexpr std::uint64_t kStallBound = 12000;

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, std::move(rom));
    sys->onActivate(kRate);
    return sys;
}

// One ROM's 90 s at `block` frames a block: past MAX_NO_ADJUST (UINT32_MAX minus ADJUST_BUFFER), where the 68k's 32-bit
// count is rebased. A broken rebase shows up in the distance from a block's target, or as a clock that went backwards.
struct LongRun {
    std::uint64_t worstOvershoot = 0;
    std::uint64_t finalClock = 0;
    bool          monotonic = true;
};

LongRun longRun(std::vector<std::uint8_t> rom, std::uint32_t block) {
    auto sys = boot(std::move(rom));
    constexpr std::uint64_t kSamples = 90ull * 48000;  // 90 s: past the first rebase with margin
    AudioBlockInfo info{};
    LongRun out;
    std::uint64_t previous = sys->mclkNow();
    for (std::uint64_t done = 0; done < kSamples; done += block) {
        info.frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(block, kSamples - done));
        sys->prepareForBlock(info);
        sys->stepIfBelowTarget(info.frames);
        const std::uint64_t now = sys->mclkNow();
        if (now < previous) out.monotonic = false;  // a 32-bit base fails here, at the rebase
        const std::uint64_t target = sys->masterClockOf(done + info.frames);
        out.worstOvershoot = std::max(out.worstOvershoot, now > target ? now - target : target - now);
        previous = now;
        sys->finishBlock(info, nullptr, 0);  // no lanes: the clock still advances
    }
    out.finalClock = previous;
    return out;
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
    // At or past the target: the stop is the first instruction boundary after it, and the 68k can be stalled there.
    CHECK(at >= target);
    CHECK(at - target < kStallBound);
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
    // The second block stopped at ITS OWN target, sample 128's clock. (An assertion about the two clocks' difference
    // would be an assertion about the two overshoots' order, which is the alignment of a `bra .`, not the stop.)
    const std::uint64_t target = sys->masterClockOf(128);
    CHECK(sys->mclkNow() > first);
    CHECK(sys->mclkNow() >= target);
    CHECK(sys->mclkNow() - target < kStallBound);
}

TEST_CASE("a session past the 32-bit rebase keeps one monotonic clock", "[blastem][stop]") {
    // Two ROMs, because the rebase's sync used to be the frame-change branch's, and a VDP status poll reaches that
    // branch from a port handler INSIDE m68k_execute, whose loop bound is a by-value copy of target_cycle: a rebase that
    // copy cannot see makes one block run the whole rebased window (about 4.28e9 clocks, 80 s of emulation) and the
    // session's clock jumps. portPollRom never syncs from a handler, so on its own it cannot see that.
    //
    // This is the suite's slowest case (the core runs at about 30x real time here) and the only one that can see either
    // failure.
    const LongRun idle = longRun(rp::blastem::test::portPollRom(), 8192);
    const LongRun polling = longRun(rp::blastem::test::vdpPollRom(), 8192);
    CHECK(idle.monotonic);
    CHECK(polling.monotonic);
    CHECK(idle.worstOvershoot < kStallBound);
    CHECK(polling.worstOvershoot < kStallBound);
    CHECK(idle.finalClock > 0xFFFFFFFFull);  // the run really crossed the 32-bit boundary
    CHECK(polling.finalClock > 0xFFFFFFFFull);
}
