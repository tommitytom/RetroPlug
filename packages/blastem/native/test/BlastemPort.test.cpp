// The port-2 device (design section 3): a level lands at its scheduled master clock, not at the block boundary; a level
// staged past a block's end keeps its timing into the next; a pin the ROM drives as an output reads the ROM's own
// latch; and the ROM's own poll count pins the sample mapping across a mid-session rate change.
//
// Every case boots portPollLoopRom (a read and a store, forever): portPollRom reads ONCE, so after the boot blocks it
// has nothing left to show and a case that used it would fail whatever the device did (the design's first draft did).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemAudio.hpp"  // AudioSink::kPrime, the edge case's latency bound
#include "system/blastem/BlastemCore.hpp"
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 512;
constexpr std::uint8_t  kIdle   = 0x60;  // TR and TH released: the level a disconnected pad leaves
// A level on the host's side of port 2: 0x40 is TR low, TH high.
constexpr std::uint8_t kLevel = 0x40;

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, std::move(rom));
    sys->onActivate(kRate);
    return sys;
}

void runBlock(rp::blastem::BlastemMdSystem& sys, std::vector<float>& l, std::vector<float>& r) {
    float*         lanes[2] = {l.data(), r.data()};
    AudioBlockInfo info{};
    info.frames = kFrames;
    std::fill(l.begin(), l.end(), 0.0f);
    std::fill(r.begin(), r.end(), 0.0f);
    sys.prepareForBlock(info);
    while (sys.stepIfBelowTarget(kFrames)) {}
    sys.finishBlock(info, lanes, 2);
}

std::uint8_t ramByte(rp::blastem::BlastemMdSystem& sys, std::uint32_t addr) {
    // BlastEm stores work RAM as 16-bit words: the 68k's byte at an even address is the word's high half, so the byte
    // view is swapped. getMemory presents address order; here the raw view is what this case reads.
    return static_cast<std::uint8_t*>(static_cast<void*>(sys.core()->work_ram))[addr ^ 1u];
}

std::uint16_t ramWord(rp::blastem::BlastemMdSystem& sys, std::uint32_t addr) {
    return static_cast<std::uint16_t>((ramByte(sys, addr) << 8) | ramByte(sys, addr + 1));
}

// The largest |sample| in a render: how a case tells "the tone is playing" from "the sink gave silence" (and so keeps
// a search for an edge from passing on a silent render).
double peak(const std::vector<float>& samples) {
    double p = 0.0;
    for (float s : samples) p = std::max(p, std::fabs(static_cast<double>(s)));
    return p;
}

}  // namespace

TEST_CASE("a level reaches the ROM's port read at its own sample", "[blastem][port]") {
    auto             sys = boot(rp::blastem::test::portPollLoopRom());
    std::vector<float> l(kFrames), r(kFrames);
    // Let the ROM boot and settle on its poll loop; the idle level reads $60, or $FF with the pull-ups' bits counted.
    for (int i = 0; i < 4; ++i) runBlock(*sys, l, r);
    CHECK((ramByte(*sys, 0x0000) & 0x60) == kIdle);
    sys->pushCoreBytes(100, &kLevel, 1);  // inside this block, mid-block
    CHECK(sys->levelsAccepted() == 1);
    runBlock(*sys, l, r);
    CHECK((ramByte(*sys, 0x0000) & 0x60) == (kLevel & 0x60));
    CHECK(sys->starvedSamples() == 0);
}

TEST_CASE("a level staged past the block's end keeps its timing", "[blastem][port]") {
    auto             sys = boot(rp::blastem::test::portPollLoopRom());
    std::vector<float> l(kFrames), r(kFrames);
    for (int i = 0; i < 4; ++i) runBlock(*sys, l, r);
    sys->pushCoreBytes(0, &kIdle, 1);
    // Four frames INTO the next block, not at its first frame. A stop lands at or past its target, and this image's
    // overshoot is one instruction's stall (it has no DMA and no bus request, so the worst case is the port read
    // itself): a level due exactly at the boundary could be applied by the last poll the first block runs past the
    // target, which would make "this block still sees idle" an assertion about where the instruction boundary fell.
    // Four frames (about 4,474 master clocks, four host samples) is past that by design, and an assertion about the
    // queue's timing.
    sys->pushCoreBytes(kFrames + 4, &kLevel, 1);
    runBlock(*sys, l, r);
    CHECK((ramByte(*sys, 0x0000) & 0x60) == kIdle);             // this block still sees the idle level
    runBlock(*sys, l, r);
    CHECK((ramByte(*sys, 0x0000) & 0x60) == (kLevel & 0x60));   // the next one sees it
}

TEST_CASE("a pin the ROM drives as an output reads the ROM's latch", "[blastem][port]") {
    // The direction rule is BlastEm's own tail ($A1000B's bits, then the port's data latch), so this asks the port
    // directly: the host holds both lines low, the ROM makes both outputs and drives them high, and the read is the
    // ROM's latch.
    auto             sys = boot(rp::blastem::test::portPollLoopRom());
    std::vector<float> l(kFrames), r(kFrames);
    for (int i = 0; i < 4; ++i) runBlock(*sys, l, r);
    const std::uint8_t low = 0x00;  // the host drives both lines low
    sys->pushCoreBytes(0, &low, 1);
    sys->writePortControl(0x60);  // $A1000B = $60: TR and TH are outputs
    sys->writePortData(0x60);     // $A10005 = $60: the ROM's own latch is high
    runBlock(*sys, l, r);
    CHECK((ramByte(*sys, 0x0000) & 0x60) == 0x60);  // the ROM's latch, not the host's level
}

// A render of syncEdgeRom's left lane at `blocks` blocks of `kFrames`, with a level staged at absolute frame `at` when
// `staged`, and nothing staged when not. Two runs of the same ROM differ only in whether the level reached the port, so
// their first differing sample IS the level's own sample, which is what the edge case measures.
std::vector<float> renderEdge(std::uint32_t at, bool staged, int blocks) {
    auto               sys = boot(rp::blastem::test::syncEdgeRom());
    std::vector<float> l(kFrames), r(kFrames), out;
    std::uint32_t      base = 0;
    for (int i = 0; i < blocks; ++i) {
        if (staged && base <= at && at < base + kFrames) {
            const std::uint8_t level = 0x00;
            sys->pushCoreBytes(at - base, &level, 1);
        }
        runBlock(*sys, l, r);
        out.insert(out.end(), l.begin(), l.end());
        base += kFrames;
    }
    REQUIRE(sys->starvedSamples() == 0);
    return out;
}

TEST_CASE("a level's audio edge lands at the frame it was staged for", "[blastem][port]") {
    // The detector is a DIFF against the same ROM with no level staged, not a magnitude threshold: blip high-passes its
    // output (bass_shift 9, a time constant of about 512 samples) and BlastEm's PSG is unipolar (0 or the volume), so
    // silencing the tone leaves a decaying DC tail of about 0.035 for thousands of samples. A "two samples under
    // 0.001" search therefore either fires thousands of samples late, or never.
    const std::uint32_t at = 9000;  // inside the 18th 512-frame block (9000 / 512 = 17.6)
    const std::vector<float> reference = renderEdge(at, false, 40);
    const std::vector<float> staged    = renderEdge(at, true, 40);
    REQUIRE(reference.size() == staged.size());
    REQUIRE(peak(reference) > 0.01);  // the tone plays: silence would make the search vacuous
    std::size_t edge = staged.size();
    for (std::size_t i = 0; i < staged.size(); ++i) {
        if (staged[i] != reference[i]) {
            edge = i;
            break;
        }
    }
    INFO("staged at " << at << ", first difference at " << edge);
    REQUIRE(edge < staged.size());  // found at all, never `edge == at` by default
    // The sink delays every sample by its kPrime-sample prime, and blip's kernel reaches up to 8 samples AHEAD of the
    // delta it belongs to (`half_width`, blip_buf.c), so the first sample the level can touch is at or after `at` (the
    // prime more than covers the lead) and its own sample is around at + kPrime. The ROM's poll (a fraction of a host
    // sample) and the PSG's next 240-clock sample add under one sample. A level applied at the block boundary instead
    // would land up to 511 samples late.
    CHECK(edge >= at);
    CHECK(edge <= at + rp::blastem::AudioSink::kPrime + 16u);
}

TEST_CASE("a mid-session rate change maps the same sample to the clock the anchor predicts", "[blastem][port]") {
    // portTimestampRom counts its own polls and stores the count when the level changes, so the count IS the master
    // clock at which the level was applied, in units of one poll loop (384.0 master clocks, measured on this core by the
    // perPoll line below). The prediction is built from the core, not from a constant: a run measures the poll period at
    // one rate, and a run that changes rate mid-session must move the count by exactly the clocks the change adds. A
    // mapping that ignored the change (or re-anchored wrongly) moves it by nothing, or by the wrong sign, which no
    // tolerance hides.
    //
    // Both stage points are early on purpose: the ROM counts into a WORD, and 8000 host samples reach about 23,000 polls
    // here (one poll is about 384 master clocks, so the count wraps after about 22,500 samples at 48 kHz).
    constexpr std::uint64_t kMclk = 53693175ull;
    const auto count = [](std::uint64_t stageAt, std::uint64_t changeAt, double newRate) {
        auto               sys = boot(rp::blastem::test::portTimestampRom());
        std::vector<float> l(kFrames), r(kFrames);
        float*             lanes[2] = {l.data(), r.data()};
        std::uint64_t      base     = 0;
        std::uint16_t      last     = 0;
        AudioBlockInfo     info{};
        info.frames = kFrames;
        while (base <= stageAt + kFrames) {
            if (changeAt && base <= changeAt && changeAt < base + kFrames) {
                sys->onSampleRateChanged(newRate);  // between blocks, as a host does
            }
            if (base <= stageAt && stageAt < base + kFrames) {
                const std::uint8_t level = kLevel;
                sys->pushCoreBytes(static_cast<std::uint32_t>(stageAt - base), &level, 1);
            }
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            sys->prepareForBlock(info);
            while (sys->stepIfBelowTarget(kFrames)) {}
            sys->finishBlock(info, lanes, 2);
            last = ramWord(*sys, 0x0000);
            base += kFrames;
        }
        return std::uint32_t(last);
    };
    const std::uint64_t   s1 = 3000, s2 = 8000;  // two staged levels, 5000 samples apart, at 48 kHz throughout
    const std::uint32_t   c1 = count(s1, 0, 0.0);
    const std::uint32_t   c2 = count(s2, 0, 0.0);
    REQUIRE(c1 > 0);
    REQUIRE(c2 > c1);
    const double          perPoll = (double(s2 - s1) * double(kMclk) / kRate) / double(c2 - c1);
    // Between s1 and s2, and ON a block boundary: the rate change is made between blocks (as a host makes it), and
    // onSampleRateChanged re-anchors at the current sample, which is the next block's first one. Predicting from a
    // change asked for mid-block would measure the clocks from the boundary, not from the request.
    const std::uint64_t   changeAt = 5120;  // = 10 blocks of kFrames
    const std::uint32_t   c3 = count(s2, changeAt, 44100.0);
    // The clocks the change adds over the samples between it and s2, in polls. Same staged sample both times, so the
    // comparison is c3 against c2, never against c1 (which staged a different sample).
    const double expected =
        double(s2 - changeAt) * (double(kMclk) / 44100.0 - double(kMclk) / kRate) / perPoll;
    INFO("polls " << c1 << " " << c2 << " " << c3 << ", per poll " << perPoll << ", expected shift " << expected);
    CHECK(std::llabs((std::int64_t(c3) - std::int64_t(c2)) - std::int64_t(std::llround(expected))) <= 3);
}
