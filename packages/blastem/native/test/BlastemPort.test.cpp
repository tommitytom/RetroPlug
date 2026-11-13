// The port-2 device (design section 3): a level is on the pins from the stop grid's own sample, at every block size; a
// level staged past a block's end (and across a rate change) keeps its sample into the block that reaches it; a pin the
// ROM drives as an output reads the ROM's own latch, and one of each reads what it should; a queue nothing polls does
// not fill; and the ROM's own poll count pins the sample mapping across a mid-session rate change.
//
// Every case boots portPollLoopRom (a read and a store, forever): portPollRom reads ONCE, so after the boot blocks it
// has nothing left to show and a case that used it would fail whatever the device did (the design's first draft did).
// The placement case boots portLevelRom, whose PSG volume follows both sync lines, so every level is audible.
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

// Every case here but the last is the work of a cart that CLAIMED port 2 (the node's `coreBytesIn`, which TS writes
// from the sync role's protocol node), so the helper boots with the port open; the gate's own case boots without it.
std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, std::move(rom));
    sys->setPort2Claimed(true);
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
    // Let the ROM boot and settle on its poll loop. The idle level is TR and TH released, and io_data_read's tail pulls
    // every other bit high, so the read is $7F (bit 7 is always an output and the latch is 0).
    for (int i = 0; i < 4; ++i) runBlock(*sys, l, r);
    CHECK(ramByte(*sys, 0x0000) == 0x7F);
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
    CHECK(ramByte(*sys, 0x0000) == 0x7F);             // this block still sees the idle level
    runBlock(*sys, l, r);
    CHECK((ramByte(*sys, 0x0000) & 0x60) == (kLevel & 0x60));   // the next one sees it
}

TEST_CASE("a pin the ROM drives as an output reads the ROM's latch", "[blastem][port]") {
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

TEST_CASE("one pin reads the host and the other the ROM's latch", "[blastem][port]") {
    // $A1000B = $20: TR (bit 5) is an OUTPUT and TH (bit 6) an INPUT. The host drives TH low, the ROM's own data latch
    // holds TR high, and the read is $20. Three wrong answers are distinguishable here: a host that answered both pins
    // gives $00 (its TR too), a pad on the port gives $60 (TH floats high and a pad's bit 5 is high), and a device that
    // answered neither gives $40. The direction rule between the host and the latch is the only way to $20.
    auto               sys = boot(rp::blastem::test::portPollLoopRom());
    std::vector<float> l(kFrames), r(kFrames);
    for (int i = 0; i < 4; ++i) runBlock(*sys, l, r);
    const std::uint8_t host = 0x00;  // TH low, and TR low for the pin the host does not own
    sys->pushCoreBytes(0, &host, 1);
    sys->writePortControl(0x20);  // $A1000B = $20: TR output, TH input
    sys->writePortData(0x20);     // $A10005 = $20: the ROM's latch drives TR high
    runBlock(*sys, l, r);
    CHECK((ramByte(*sys, 0x0000) & 0x60) == 0x20);  // TH from the host, TR from the latch
}

TEST_CASE("a ROM that never polls port 2 does not fill the queue", "[blastem][port]") {
    // vdpPollRom reads the VDP and nothing else, so nothing drains the queue but the grid itself. A queue that only
    // drained on a read would hold a level per sample forever, and its first read (the ROM switching into a mode that
    // polls, minutes in) would answer with the oldest survivor instead of the current level.
    auto               sys = boot(rp::blastem::test::vdpPollRom());
    std::vector<float> l(kFrames), r(kFrames);
    for (int block = 0; block < 4; ++block) {
        for (std::uint32_t frame = 0; frame < kFrames; frame += 2) {
            const std::uint8_t level = std::uint8_t((frame / 2) & 1 ? 0x20 : 0x40);
            sys->pushCoreBytes(frame, &level, 1);
        }
        runBlock(*sys, l, r);
        INFO("block " << block);
        CHECK(sys->portDevice().empty());
        CHECK(sys->portDevice().dropped() == 0);
        // The held level is the last one the grid passed, which is what a first read in the next block answers with.
        CHECK(sys->portDevice().lastApplied() == 0x20);
    }
}

TEST_CASE("a re-activation leaves the port's pins idle", "[blastem][port]") {
    // A level is host state, not machine state: a fresh machine from onActivate must not inherit the level the previous
    // session was holding, or a parked pin from a stopped transport reads as a held button from boot.
    auto               sys = boot(rp::blastem::test::portPollLoopRom());
    std::vector<float> l(kFrames), r(kFrames);
    for (int i = 0; i < 4; ++i) runBlock(*sys, l, r);
    CHECK(ramByte(*sys, 0x0000) == 0x7F);  // the pull-ups: bit 7 is an output and the latch is 0
    const std::uint8_t low = 0x00;
    sys->pushCoreBytes(10, &low, 1);
    runBlock(*sys, l, r);
    CHECK((ramByte(*sys, 0x0000) & 0x60) == 0x00);
    CHECK(sys->portDevice().lastApplied() == 0x00);
    sys->onDeactivate();
    sys->onActivate(kRate);
    for (int i = 0; i < 4; ++i) runBlock(*sys, l, r);
    CHECK(sys->portDevice().lastApplied() == kIdle);
    CHECK(ramByte(*sys, 0x0000) == 0x7F);  // the old session's low level is not on the pins
    CHECK(sys->levelsAccepted() == 1);     // counted since construction, so clear() keeps the total
}

TEST_CASE("a port read with no instance, or another port, drives nothing", "[blastem][port]") {
    // The entry point is BlastEm's, so a context built outside this package can reach it with no host at all: the
    // answer then has to be the pull-ups ($7F through io_data_read's tail), not both pins driven low. And a port this
    // package does not drive must never be answered: only port 2 carries the host device.
    rp::blastem::currentInstance = nullptr;
    CHECK(io_custom_input(nullptr, 0) == kIdle);
    auto sys = boot(rp::blastem::test::portPollLoopRom());
    rp::blastem::currentInstance = sys.get();
    CHECK(io_custom_input(&sys->core()->io.ports[0], 0) == kIdle);
    CHECK(io_custom_input(&sys->core()->io.ports[1], 0) == kIdle);  // the queue is empty: the idle level
    rp::blastem::currentInstance = nullptr;
}

TEST_CASE("a port read answers for its own console, whatever ran last on this thread", "[blastem][port]") {
    // The console names its host (io_port's IO_CUSTOM device), so a read belongs to the console it came out of: an
    // interleaved pair where B polls port 2 must get B's level even though A was the instance that ran last on this
    // thread. Routing through the thread-local instance pointer instead answers B with A's device.
    auto a = boot(rp::blastem::test::portPollLoopRom());
    auto b = boot(rp::blastem::test::portPollLoopRom());
    b->pushCoreBytes(0, &kLevel, 1);  // B's device holds TR low from host sample 0
    rp::blastem::currentInstance = a.get();
    CHECK(io_custom_input(&b->core()->io.ports[1], 0) == kLevel);
    CHECK(io_custom_input(&a->core()->io.ports[1], 0) == kIdle);  // A's queue is its own, and empty
    rp::blastem::currentInstance = nullptr;
}

// A level as a case stages it: the absolute host sample it goes on at, and the byte the host drives.
struct Stage {
    std::uint64_t sample;
    std::uint8_t  level;
};

struct Placed {
    std::vector<float>                   left;
    std::vector<rp::blastem::PortAccess> reads;
};

// A run of portLevelRom: `total` host frames in `block`-frame blocks, every staged level handed to pushCoreBytes in the
// block that carries its sample, and the device's own record of every read. Two runs of the same stages differ only in
// how the host cut the timeline, which is the whole question: a level's placement must not move with it.
Placed place(const std::vector<Stage>& stages, std::uint32_t block, std::uint64_t total) {
    auto               sys = boot(rp::blastem::test::portLevelRom());
    std::vector<float> l(block), r(block);
    float*             lanes[2] = {l.data(), r.data()};
    Placed             out;
    out.left.reserve(total);
    // About three polls a host sample on this image, and the log refuses rather than grows: four times the frames is
    // headroom for a slow first frame.
    std::vector<rp::blastem::PortAccess> reads(static_cast<std::size_t>(total) * 4);
    sys->portDevice().observe(reads);
    std::size_t   next = 0;
    AudioBlockInfo info{};
    for (std::uint64_t at = 0; at < total; at += info.frames) {
        info.frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(block, total - at));
        while (next < stages.size() && stages[next].sample < at + info.frames) {
            sys->pushCoreBytes(static_cast<std::uint32_t>(stages[next].sample - at), &stages[next].level, 1);
            ++next;
        }
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        sys->prepareForBlock(info);
        while (sys->stepIfBelowTarget(info.frames)) {}
        sys->finishBlock(info, lanes, 2);
        out.left.insert(out.left.end(), l.begin(), l.begin() + info.frames);
    }
    REQUIRE(next == stages.size());  // every level was delivered by a block, or the case is not about placement
    REQUIRE(sys->starvedSamples() == 0);
    REQUIRE(sys->portDevice().dropped() == 0);
    out.reads.assign(reads.begin(), reads.begin() + static_cast<std::ptrdiff_t>(sys->portDevice().recorded()));
    return out;
}

// The device's schedule as an independent model: the last staged level whose sample the stop grid had reached. Bits
// outside the two sync lines are the device's business, and the schedule has no level before the first stage.
std::uint8_t expectedAt(const std::vector<Stage>& stages, std::uint64_t sample) {
    std::uint8_t level = kIdle;
    for (const Stage& s : stages) {
        if (s.sample > sample) break;
        level = std::uint8_t(s.level & 0x60);
    }
    return level;
}

TEST_CASE("a level is on the pins at its own sample at every block size", "[blastem][port]") {
    // The review's evidence in Catch2 form: levels at irregular samples (900 levels, one every 47 or so), including the
    // three frames a level's sample falls at a block boundary differently (9000, 41000 and 41003), rendered at blocks of
    // 1, 7, 64, 512 and 4096 frames. Two independent claims, per read: the answer is the last level whose sample the
    // stop grid had reached (never early, never a level late), and the reads and the audio are the same bytes at every
    // partition.
    constexpr std::uint64_t kMclk  = 53693175ull;
    constexpr std::uint64_t kTotal = 44000;  // past 41003, so every special frame is inside the render
    std::vector<Stage>      stages;
    const std::uint8_t      levels[4] = {0x00, 0x20, 0x40, 0x60};
    for (std::uint64_t s = 1000, k = 0; s < 41000; s += 47 + (k * 13) % 29, ++k) {
        stages.push_back({s, levels[k & 3]});
    }
    for (const std::uint64_t at : {9000ull, 41000ull, 41003ull}) stages.push_back({at, 0x00});
    std::sort(stages.begin(), stages.end(), [](const Stage& a, const Stage& b) { return a.sample < b.sample; });

    const Placed reference = place(stages, 4096, kTotal);
    // A silent render is invariant for the wrong reason: the image plays a tone whose volume every level moves.
    REQUIRE(peak(reference.left) > 0.01);
    REQUIRE(reference.reads.size() > 80000);
    REQUIRE(reference.left.size() == kTotal);
    std::size_t early = 0, late = 0, before = 0;
    std::size_t firstWrong = reference.reads.size(), firstBefore = reference.reads.size();
    for (std::size_t i = 0; i < reference.reads.size(); ++i) {
        const rp::blastem::PortAccess& read = reference.reads[i];
        if (read.mclk < (read.sample * kMclk) / 48000ull) {  // never before the sample's own clock
            ++before;
            if (firstBefore == reference.reads.size()) firstBefore = i;
        }
        const std::uint8_t want = expectedAt(stages, read.sample);
        if ((read.answer & 0x60) == want) continue;
        if ((read.answer & 0x60) == expectedAt(stages, read.sample + 1)) ++early;  // the next sample's level, early
        else ++late;
        if (firstWrong == reference.reads.size()) firstWrong = i;
    }
    INFO("block 4096: " << early << " early, " << late << " late of " << reference.reads.size());
    if (firstWrong < reference.reads.size()) {
        INFO("first wrong read " << firstWrong << " at sample " << reference.reads[firstWrong].sample << " (wanted "
                                  << int(expectedAt(stages, reference.reads[firstWrong].sample)) << ", got "
                                  << int(reference.reads[firstWrong].answer & 0x60) << ")");
    }
    if (firstBefore < reference.reads.size()) {
        INFO("first early-clock read " << firstBefore << " at sample " << reference.reads[firstBefore].sample
                                       << " mclk " << reference.reads[firstBefore].mclk);
    }
    CHECK(early == 0);
    CHECK(late == 0);
    CHECK(before == 0);
    for (const std::uint32_t block : {1u, 7u, 64u, 512u}) {
        INFO("block " << block);
        const Placed other = place(stages, block, kTotal);
        CHECK(other.left == reference.left);
        REQUIRE(other.reads.size() == reference.reads.size());
        // The same reads in the same order, answers included: a level that moved by one poll at one size is a
        // different log, and one applied at a block boundary instead of its sample is a different clock.
        std::size_t at = other.reads.size();
        for (std::size_t i = 0; i < other.reads.size(); ++i) {
            const rp::blastem::PortAccess& a = other.reads[i];
            const rp::blastem::PortAccess& b = reference.reads[i];
            if (a.sample != b.sample || a.mclk != b.mclk || a.answer != b.answer) {
                at = i;
                break;
            }
        }
        if (at < other.reads.size()) {
            INFO("read " << at << " is " << other.reads[at].sample << "/" << other.reads[at].mclk << "/"
                         << int(other.reads[at].answer) << ", the 4096 frame run has " << reference.reads[at].sample
                         << "/" << reference.reads[at].mclk << "/" << int(reference.reads[at].answer));
        }
        CHECK(at == other.reads.size());
    }
}

TEST_CASE("a level staged past the block's end keeps its sample across a rate change", "[blastem][port]") {
    // A level delivered by a block BEFORE the one that reaches its sample (offset 700 of 512-frame blocks: the host may
    // stage ahead), and a sample-rate change between the two. Its key is the host sample, so the change re-anchors the
    // mapping under it without moving it: the level is on the pins at sample 700, before the grid reaches 701.
    auto               sys = boot(rp::blastem::test::portLevelRom());
    constexpr std::uint64_t kAhead = 700;
    std::vector<float> l(kFrames), r(kFrames);
    std::vector<rp::blastem::PortAccess> reads(8192);
    sys->portDevice().observe(reads);
    const std::uint8_t low = 0x00;
    sys->pushCoreBytes(static_cast<std::uint32_t>(kAhead), &low, 1);  // this block is 0..511: it is the next one's
    CHECK(sys->levelsAccepted() == 1);
    runBlock(*sys, l, r);
    sys->onSampleRateChanged(44100.0);  // between blocks, as a host makes it
    runBlock(*sys, l, r);
    runBlock(*sys, l, r);
    // The first read at or past the level's sample answered low, and no read before it did.
    const auto recorded = reads.begin() + static_cast<std::ptrdiff_t>(sys->portDevice().recorded());
    const auto first = std::find_if(reads.begin(), recorded, [](const rp::blastem::PortAccess& a) { return (a.answer & 0x60) == 0x00; });
    REQUIRE(first != recorded);
    CHECK(first->sample == kAhead);
    for (auto it = reads.begin(); it != first; ++it) CHECK(it->sample < kAhead);
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
    // prime more than covers the lead) and its own sample is around at + kPrime. Measured first differences here are
    // kPrime + 2 to kPrime + 5, the leftover being the ROM's poll (a fraction of a host sample), the PSG's next
    // 240-clock sample and the resampler's phase; the 8 is that leftover with room, not a range that could still hold a
    // level applied at the block boundary, which lands up to 511 samples late and is what this bound excludes.
    CHECK(edge >= at);
    CHECK(edge <= at + rp::blastem::AudioSink::kPrime + 8u);
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
    // An absolute anchor: the level staged at the machine's first sample is due before the first poll, so the ROM sees
    // it with no poll counted. The comparisons below are differences and a mapping error that moved every sample by
    // the same amount would cancel out of them; this one would not.
    const std::uint32_t   c0 = count(0, 0, 0.0);
    CHECK(c0 == 0);
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
    INFO("polls " << c0 << " " << c1 << " " << c2 << " " << c3 << ", per poll " << perPoll << ", expected shift "
                  << expected);
    CHECK(std::llabs((std::int64_t(c3) - std::int64_t(c2)) - std::int64_t(std::llround(expected))) <= 3);
}

TEST_CASE("port 2 drives the pins only for the cart that claimed it", "[blastem][port]") {
    // The gate the design's section 4 calls required rather than tidy: port 2 is player 2 on every Mega Drive cart, so
    // only a system whose role claimed the port may hold its lines. Unclaimed, a level is refused outright and the pins
    // read the pull-ups the cart's own poll has always read; claiming lets one through, and un-claiming takes the lines
    // back so a level held for a cart that went away cannot keep driving someone else's pad.
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto               sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, rp::blastem::test::portPollLoopRom());
    std::vector<float> l(kFrames), r(kFrames);
    sys->onActivate(kRate);
    CHECK_FALSE(sys->hasCoreBytesIn());
    for (int i = 0; i < 4; ++i) runBlock(*sys, l, r);
    CHECK(ramByte(*sys, 0x0000) == 0x7F);
    sys->pushCoreBytes(0, &kLevel, 1);
    CHECK(sys->levelsAccepted() == 0);  // refused at the door: the queue never saw a byte
    runBlock(*sys, l, r);
    CHECK(ramByte(*sys, 0x0000) == 0x7F);  // still the idle lines, never the level's bits

    sys->setPort2Claimed(true);
    CHECK(sys->hasCoreBytesIn());
    sys->pushCoreBytes(0, &kLevel, 1);
    CHECK(sys->levelsAccepted() == 1);
    runBlock(*sys, l, r);
    CHECK((ramByte(*sys, 0x0000) & 0x60) == (kLevel & 0x60));

    sys->setPort2Claimed(false);
    CHECK(sys->portDevice().lastApplied() == kIdle);  // the held level went with the claim
    runBlock(*sys, l, r);
    CHECK(ramByte(*sys, 0x0000) == 0x7F);
}
