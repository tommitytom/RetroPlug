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
#include "system/blastem/BlastemAudio.hpp"
#include "system/blastem/BlastemCore.hpp"  // genesis_context, for the source a chip holds (the routing case)
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 512;

std::unique_ptr<rp::blastem::BlastemMdSystem> bootAt(std::vector<std::uint8_t> rom, double rate = kRate,
                                                     std::uint32_t reserve = 0) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, std::move(rom));
    // A plan reserves before activation, which is what a node's reservation is: the sink is sized from it instead of
    // from the engine's default block.
    if (reserve) sys->reserveAudio(reserve);
    sys->onActivate(rate);
    return sys;
}

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom) { return bootAt(std::move(rom)); }

// `seconds` of interleaved stereo at the host rate, one block of `block` frames at a time (the last one short when the
// duration is not a whole number of blocks, which is the ordinary case for a duration in seconds).
std::vector<float> render(rp::blastem::BlastemMdSystem& sys, double seconds, std::uint32_t block = kFrames,
                          double rate = kRate) {
    std::vector<float>  out;
    std::vector<float>  left(block), right(block);
    float*              lanes[2] = {left.data(), right.data()};
    AudioBlockInfo      info{};
    const std::uint32_t total = static_cast<std::uint32_t>(seconds * rate);
    for (std::uint32_t done = 0; done < total; done += block) {
        info.frames = std::min(block, total - done);
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        sys.prepareForBlock(info);
        while (sys.stepIfBelowTarget(block)) {}
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

// The left lane's RMS, which is a level a case can pin: a peak threshold passes a gain error of any size up to it.
double rms(const std::vector<float>& interleaved) {
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i + 1 < interleaved.size(); i += 2, ++n) sum += double(interleaved[i]) * interleaved[i];
    return n ? std::sqrt(sum / double(n)) : 0.0;
}

}  // namespace

TEST_CASE("an image that writes the PSG is not silent", "[blastem][audio]") {
    // fmPsgRom's YM writes never land (no Z80 bus request, see its comment), so this is the PSG's level case: the tone
    // is channel 0 at full volume, and its RMS is the gain path's, which a peak threshold could not pin.
    auto sys = boot(rp::blastem::test::fmPsgRom());
    const std::vector<float> out = render(*sys, 0.5);
    CHECK(out.size() == static_cast<std::size_t>(0.5 * kRate) * 2);
    CHECK(peak(out) > 0.01);  // measured 0.0747: the check rejects silence and nothing subtler
    // Measured -33.45 dBFS: the PSG's own full-volume square (2,449 in the 16-bit domain) through the 3390 Hz one-pole
    // and blip's kernel. The windows are a decibel either side, so a gain that is wrong by anything audible fails.
    CHECK(rms(out) > 0.0189);  // -34.5 dBFS
    CHECK(rms(out) < 0.0239);  // -32.4 dBFS
    // The sink must never have run dry: a starved sample is a zero nobody's ROM asked for, and its position would be
    // a function of the block partition.
    CHECK(sys->starvedSamples() == 0);
}

TEST_CASE("the YM2612's own image is what sounds", "[blastem][audio]") {
    // fmOnlyRom asserts the Z80's bus request before it writes the YM, which is what makes genesis.c's io_write hand
    // its bytes to the chip (without the busack they are dropped, and fmPsgRom is the image that shows it), and it
    // writes no PSG register at all. So this render's audio can only be the YM2612's: the four-carrier patch measured
    // -18.61 dBFS RMS at fnum 617, block 4, algorithm 7, and a sink that dropped, mis-rated or mis-gained the YM source
    // has nothing else to fall back on.
    auto sys = boot(rp::blastem::test::fmOnlyRom());
    const std::vector<float> out = render(*sys, 0.25);
    CHECK(peak(out) > 0.1);    // measured 0.166: rejects silence and a gain that is 20 dB out alike
    CHECK(rms(out) > 0.104);   // -19.6 dBFS
    CHECK(rms(out) < 0.132);   // -17.6 dBFS
    // The chip's own rate, from its render_audio_source call: 53,693,175 / (7 * 6 * 24) is the YM2612's sample rate.
    CHECK(sys->audio().sourceNativeRate("YM2612") == 53693175.0 / 1008.0);
    CHECK(sys->starvedSamples() == 0);
    CHECK(sys->audio().overrunSamples() == 0);
}

TEST_CASE("the same input renders the same bytes twice", "[blastem][audio]") {
    auto  first  = boot(rp::blastem::test::fmPsgRom());
    auto  second = boot(rp::blastem::test::fmPsgRom());
    CHECK(render(*first, 0.25) == render(*second, 0.25));
}

TEST_CASE("a source's DMA-sized burst is buffered, not dropped or left behind", "[blastem][audio][sink]") {
    // The sink alone, fed the way a core feeds it: kDmaOvershoot host samples of one source arrive before the first
    // drain (a 68k-to-VDP DMA carries the 68k that far past a block's end, and genesis.c's own sync then runs the chip
    // to it in one go), and then the block drains. The ring is sized for exactly that burst, so nothing may be dropped
    // (overrunSamples) or starved, and the drained stream must be the stream a trickle would have produced: a surplus
    // left inside blip instead would surface as latency the block partition chose.
    constexpr std::uint32_t kBurst = 12000;  // the ring's DMA margin at 48 kHz: a quarter second
    // The source runs at the HOST rate, so one put is one host sample: the burst and the trickle then differ only in
    // when the samples arrive, which is the question. The resampler has its own cases.
    constexpr std::uint64_t kEqualRateClock = 48000ull * 240;
    const auto square = [](std::uint32_t i) -> std::int16_t { return (i / 30) % 2 ? 8191 : -8191; };

    rp::blastem::AudioSink burst;
    rp::blastem::AudioSink trickle;
    for (rp::blastem::AudioSink* sink : {&burst, &trickle}) {
        sink->prepare(kFrames);
        sink->setHostRate(kRate);
    }
    auto* burstSource   = burst.add("TEST", kEqualRateClock, 240, 1);
    auto* trickleSource = trickle.add("TEST", kEqualRateClock, 240, 1);
    REQUIRE(burstSource);
    REQUIRE(trickleSource);

    for (std::uint32_t i = 0; i < kBurst; ++i) burst.putMono(burstSource, square(i));

    std::vector<float> burstOut, trickleOut;
    std::vector<float> left(kFrames), right(kFrames);
    const std::uint32_t blocks = kBurst / kFrames;  // whole blocks only: what the burst has beyond them is never drained
    for (std::uint32_t block = 0; block < blocks; ++block) {
        for (std::uint32_t i = block * kFrames; i < (block + 1) * kFrames; ++i) {
            trickle.putMono(trickleSource, square(i));
        }
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        burst.drain(kFrames, left.data(), right.data());
        burstOut.insert(burstOut.end(), left.begin(), left.end());
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        trickle.drain(kFrames, left.data(), right.data());
        trickleOut.insert(trickleOut.end(), left.begin(), left.end());
    }
    CHECK(peak(burstOut) > 0.01);
    CHECK(peak(trickleOut) > 0.01);
    CHECK(burstOut == trickleOut);
    CHECK(burst.starvedSamples() == 0);
    CHECK(trickle.starvedSamples() == 0);
    CHECK(burst.overrunSamples() == 0);
    CHECK(trickle.overrunSamples() == 0);
}

TEST_CASE("a DMA that carries the 68k past a block neither starves nor overruns", "[blastem][audio]") {
    // The end-to-end shape of the case above: a ROM that keeps the 68k inside 64 KiB 68k-to-VRAM DMAs, so every stop is
    // overshot by the transfer, with a plan's one-block reservation under it. The chips trail the 68k when a run returns
    // (the stop is only checked at the next sync), so a drain without a sync first reads audio the chips have not made.
    // Nothing may be starved, nothing dropped, and the PSG tone the ROM wrote must be the output.
    auto sys = bootAt(rp::blastem::test::dmaLoopRom(true), kRate, kFrames);
    const std::vector<float> out = render(*sys, 1.0);
    CHECK(peak(out) > 0.01);
    CHECK(sys->starvedSamples() == 0);
    CHECK(sys->audio().overrunSamples() == 0);
}

TEST_CASE("a block above the reservation is clamped, not written past the mix", "[blastem][audio]") {
    // The engine promises a block no larger than the reservation, and this is the compare that makes a breach a short
    // block instead of a read past the end of mixLeft_: the lanes past the reservation are untouched, and the block is
    // counted so a caller can see it happened.
    auto sys = bootAt(rp::blastem::test::fmPsgRom(), kRate, 256);
    std::vector<float> left(1024, 1.0f), right(1024, 1.0f);
    float* lanes[2] = {left.data(), right.data()};
    AudioBlockInfo info{};
    info.frames = 1024;
    sys->prepareForBlock(info);
    while (sys->stepIfBelowTarget(1024)) {}
    sys->finishBlock(info, lanes, 2);
    CHECK(sys->clampedBlocks() == 1);
    CHECK(std::all_of(left.begin() + 256, left.end(), [](float v) { return v == 1.0f; }));
    CHECK(std::all_of(right.begin() + 256, right.end(), [](float v) { return v == 1.0f; }));
    // The clamp does not silence the block: the frames it could mix are still the core's.
    CHECK(std::any_of(left.begin(), left.begin() + 256, [](float v) { return v != 1.0f; }));
}

TEST_CASE("a chip's rate change ends its frame first", "[blastem][audio]") {
    // The VDP test register's bit 0 moves the PSG's divider from 240 to 112 and calls psg_adjust_master_clock
    // (genesis.c:1161-1163), which is the sink's adjustClock. Writing the test port's register is NOT the same thing:
    // the comparison before and after the write is genesis.c's, so vdp_test_port_write alone changes no clock and this
    // case would pass without exercising a line of the sink. switchPsgClock runs those two lines, and the native rate
    // the sink recorded is what the case reads, so a sink that ignored the call fails here.
    auto sys = boot(rp::blastem::test::psgOnlyRom());
    REQUIRE(sys->audio().sourceNativeRate("PSG") == 53693175.0 / 240.0);
    const std::vector<float> before = render(*sys, 0.1);
    sys->switchPsgClock(true);
    // 53,693,175 / 112 = 479,403.35: the divider really moved, and the sink took the new one.
    CHECK(sys->audio().sourceNativeRate("PSG") == 53693175.0 / 112.0);
    const std::vector<float> after = render(*sys, 0.5);  // long enough for a wrong rate to fill the ring and drop
    CHECK(peak(before) > 0.01);
    CHECK(peak(after) > 0.01);
    // The chip now emits twice as many samples per second of emulated time. A sink that recorded the new rate but never
    // re-rated blip would hand the ring 2.14 host samples for every one the block drains, so the ring would fill and
    // start dropping: the counter sees it, while a peak threshold (and the tone's own pitch, which really does rise,
    // because the PSG's clock really changed) would not.
    CHECK(sys->starvedSamples() == 0);
    CHECK(sys->audio().overrunSamples() == 0);
}

TEST_CASE("two instances keep their own chips' audio", "[blastem][audio]") {
    // A chip's callback names the source it holds, and the source names its sink, so a call reaches the right instance
    // even when another one is current on the thread. Routing by the current instance sent A's re-rate to B's sink,
    // which re-rated A's blips with B's HOST rate, and A then starved its next block by that ratio.
    auto a = bootAt(rp::blastem::test::fmPsgRom(), 48000.0);
    auto b = bootAt(rp::blastem::test::fmPsgRom(), 44100.0);  // B's boot leaves its instance current
    REQUIRE(a->core() != nullptr);
    REQUIRE(a->core()->psg != nullptr);
    REQUIRE(a->core()->psg->audio != nullptr);
    // render_audio.h's own entry point, as a state load's config_updated (or setHostRate) would call it. The values are
    // A's own, so only the routing decides which sink takes them.
    render_audio_adjust_clock(a->core()->psg->audio, a->core()->master_clock, 240);
    const std::vector<float> after = render(*a, 0.25);
    CHECK(peak(after) > 0.01);
    CHECK(a->starvedSamples() == 0);
    CHECK(b->audio().sourceNativeRate("PSG") == 53693175.0 / 240.0);
    // And after B is destroyed, which leaves the thread with no instance at all, a rate change on A still reaches A.
    b.reset();
    a->switchPsgClock(true);
    CHECK(a->audio().sourceNativeRate("PSG") == 53693175.0 / 112.0);
    const std::vector<float> fast = render(*a, 0.25);
    CHECK(peak(fast) > 0.01);
    CHECK(a->starvedSamples() == 0);
    CHECK(a->audio().overrunSamples() == 0);
}
