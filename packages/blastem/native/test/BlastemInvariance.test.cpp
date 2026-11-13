// Block-size invariance on the timing-observing images (design section 2): the same 2 s of emulation rendered with host
// blocks of 1, 64, 1024 and 4096 frames is byte-identical, in the audio it produces and in the machine state it ends on.
// Every host hands a plugin a different partition of the same time, and the parity baseline compares recorded bytes: an
// output that moves with the partition makes both meaningless.
//
// The stimulus is the four images SyntheticRom.hpp calls timing images: a VDP status/HV read loop feeding PSG volume
// writes, the same with VINT and HINT, the same with the Z80 running behind a bus request, and the same with a 68k-to-VDP
// DMA started from the VINT handler. They are what a `bra .` spin is not: their components are synced from port handlers,
// interrupt entry and the Z80's own bus grants, so the stop points a host block creates land in the middle of the 68k's
// instructions. This is the case that proves the stop schedule depends only on absolute time.
//
// The two cases after that one put the review's fifth input class on the same footing: one sync level staged at a fixed
// absolute frame, with syncEdgeRom answering it by silencing its PSG tone. The level's sample lands inside a block of
// every size, and the render with the level is compared against the render at every other block size and against the
// same render with no level at all.
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

constexpr double        kRate  = 48000.0;
constexpr std::uint64_t kTotal = 96000;  // 2 s: about 120 frames, so every image's VINT, HINT, Z80 and DMA run many times

// The staged-level cases: one level, staged at one absolute host sample, so its offset inside the block differs
// between every partition this case runs. Three frames, because one is a frame a timing bug can pass by luck: 9000 and
// 41003 are frames where a level lands inside a stop run's overshoot at one block size and not at another, and 41000 is
// a multiple of none of 64, 1024 and 4096. syncEdgeRom holds PSG channel 0 at full volume until TR/TH leave their idle
// level, so the level is an audio edge a case can time.
constexpr std::uint64_t kLevelFrames[] = {9000, 41000, 41003};

// The largest |sample| in a render: a silent render is invariant for the wrong reason, so the case checks this first.
double peak(const std::vector<float>& samples) {
    double p = 0.0;
    for (float s : samples) p = std::max(p, std::fabs(static_cast<double>(s)));
    return p;
}

// The serialized machine state as one hash: the 68k's registers and cycle count, the Z80, the VDP, the YM2612, the PSG
// and the bus arbiter. genesis_serialize is the core's own serializer, so the hash covers what a savestate would carry
// (and not cycle_base, which the serializer leaves out).
std::uint32_t stateHash(genesis_context* gen) {
    serialize_buffer buf;
    init_serialize(&buf);
    genesis_serialize(gen, &buf, gen->m68k->pc, 1);
    const std::vector<std::uint8_t> bytes(buf.data, buf.data + buf.size);
    std::free(buf.data);
    return rp::blastem::test::fnv1a(bytes);
}

// A render of the whole 2 s, as two lanes INDEXED BY FRAME (not interleaved, and not concatenated a block at a time:
// block = 1 would make a per-block `left`-then-`right` vector frame-interleaved, so an index would address the wrong
// frame).
struct Render {
    std::vector<float> left, right;
    std::uint32_t      state = 0;
    std::uint64_t      starved = 0;
};

Render renderAt(const std::vector<std::uint8_t>& rom, std::uint32_t block) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, rom);
    sys->onActivate(kRate);
    Render out;
    out.left.reserve(kTotal);
    out.right.reserve(kTotal);
    std::vector<float> left(block), right(block);
    float* lanes[2] = {left.data(), right.data()};
    AudioBlockInfo info{};
    for (std::uint64_t at = 0; at < kTotal; at += info.frames) {
        info.frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(block, kTotal - at));
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        sys->prepareForBlock(info);
        while (sys->stepIfBelowTarget(info.frames)) {}
        sys->finishBlock(info, lanes, 2);
        out.left.insert(out.left.end(), left.begin(), left.begin() + info.frames);
        out.right.insert(out.right.end(), right.begin(), right.begin() + info.frames);
    }
    out.state = stateHash(sys->core());
    out.starved = sys->starvedSamples();
    return out;
}

// syncEdgeRom over the whole 2 s, in lanes INDEXED BY FRAME like renderAt's, with both sync lines driven low from
// `frame` on when `staged`. The offset handed to pushCoreBytes is the level's frame minus this block's first
// sample, so the level keeps the absolute sample (and so the absolute master clock) the same case stages at, whichever
// block delivers it.
struct Staged {
    std::vector<float> left, right;
    std::uint64_t      starved = 0;
};

Staged renderStaged(std::uint32_t block, bool staged, std::uint64_t frame, std::uint64_t total) {
    const std::uint8_t level = 0x00;  // TR and TH both low: what makes syncEdgeRom leave its poll loop
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, rp::blastem::test::syncEdgeRom());
    sys->onActivate(kRate);
    Staged out;
    out.left.reserve(total);
    out.right.reserve(total);
    std::vector<float> left(block), right(block);
    float*             lanes[2] = {left.data(), right.data()};
    bool               sent = false;
    AudioBlockInfo     info{};
    for (std::uint64_t at = 0; at < total; at += info.frames) {
        info.frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(block, total - at));
        if (staged && !sent && at + info.frames > frame) {
            sys->pushCoreBytes(static_cast<std::uint32_t>(frame - at), &level, 1);
            sent = true;
        }
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        sys->prepareForBlock(info);
        while (sys->stepIfBelowTarget(info.frames)) {}
        sys->finishBlock(info, lanes, 2);
        out.left.insert(out.left.end(), left.begin(), left.begin() + info.frames);
        out.right.insert(out.right.end(), right.begin(), right.begin() + info.frames);
    }
    out.starved = sys->starvedSamples();
    return out;
}

}  // namespace

TEST_CASE("the timing images render the same bytes at 1, 64, 1024 and 4096 frame blocks", "[blastem][invariance]") {
    struct Image {
        const char* name;
        std::vector<std::uint8_t> bytes;
    };
    const Image images[] = {
        {"hvPsg", rp::blastem::test::hvPsgRom()},
        {"vintHint", rp::blastem::test::vintHintRom()},
        {"z80", rp::blastem::test::z80TimingRom()},
        {"dma", rp::blastem::test::vintDmaRom()},
    };
    for (const Image& image : images) {
        INFO(image.name);
        const Render reference = renderAt(image.bytes, 4096);
        // Before the comparison, the case has to be ABOUT something: a render that is silent (a broken image, a starved
        // sink) is identical at every partition and would pass while proving nothing.
        REQUIRE(peak(reference.left) > 0.01);
        CHECK(reference.starved == 0);
        for (const std::uint32_t block : {1u, 64u, 1024u}) {
            INFO("block " << block);
            const Render other = renderAt(image.bytes, block);
            CHECK(other.left == reference.left);
            CHECK(other.right == reference.right);
            CHECK(other.state == reference.state);
            CHECK(other.starved == 0);
        }
    }
}

TEST_CASE("a staged sync level renders identical bytes at 1, 64, 1024 and 4096 frame blocks",
          "[blastem][invariance]") {
    for (const std::uint64_t frame : kLevelFrames) {
        INFO("level staged at " << frame);
        // Past the level by a quarter second of host samples, not the whole 2 s: the claim is the level's placement,
        // and the grid case above already holds a whole render to a partition.
        const std::uint64_t total = frame + 2048;
        const Staged one = renderStaged(1, true, frame, total);
        const Staged sixtyFour = renderStaged(64, true, frame, total);
        const Staged thousand = renderStaged(1024, true, frame, total);
        const Staged fourThousand = renderStaged(4096, true, frame, total);
        REQUIRE(one.left.size() == total);
        REQUIRE(one.right.size() == total);
        // Same reason as the grid case: a render that is silent (a broken image, a starved sink) is identical at every
        // partition and would pass this test while proving nothing.
        REQUIRE(peak(one.left) > 0.01);
        // A starved sink's zeros are placed by the partition, so a starvation here is the sink failing the case rather
        // than the level moving: the audio cases pin the counter too.
        CHECK(one.starved == 0);
        CHECK(sixtyFour.starved == 0);
        CHECK(thousand.starved == 0);
        CHECK(fourThousand.starved == 0);
        CHECK(one.left == sixtyFour.left);
        CHECK(one.left == thousand.left);
        CHECK(one.left == fourThousand.left);
        CHECK(one.right == sixtyFour.right);
        CHECK(one.right == thousand.right);
        CHECK(one.right == fourThousand.right);
    }
}

TEST_CASE("the invariance case's stimulus really produces its edge", "[blastem][invariance]") {
    // The level staged at each of kLevelFrames silences syncEdgeRom's tone. The proof is a DIFF against the same render with no
    // level, not a magnitude threshold: blip's high-pass leaves a decaying DC tail (about 0.035 for thousands of
    // samples) after the tone stops, so "the peak after the level is under 0.001" is false for thousands of samples and
    // a window that skipped straight to a quiet stretch would be a different, weaker claim.
    for (const std::uint64_t frame : kLevelFrames) {
        INFO("level staged at " << frame);
        const std::uint64_t total = frame + 2048;
        const Staged staged = renderStaged(1, true, frame, total);
        const Staged reference = renderStaged(1, false, frame, total);
        REQUIRE(peak(reference.left) > 0.01);  // the tone really plays all the way through when nothing is staged
        std::size_t edge = staged.left.size();
        for (std::size_t i = 0; i < staged.left.size(); ++i) {
            if (staged.left[i] != reference.left[i]) { edge = i; break; }
        }
        INFO("first difference at " << edge);
        REQUIRE(edge < staged.left.size());
        // The same window the port case uses: at or after the staged frame (the 16-sample prime covers the kernel's
        // 8-sample lead), and no later than the prime plus 8, which is the measured leftover plus room (the port case
        // states the measurement). A bound this tight is what keeps the diff from passing on an edge the level only
        // reached through a block boundary, and three frames keep it from passing by luck at one of them.
        CHECK(edge >= frame);
        CHECK(edge <= frame + rp::blastem::AudioSink::kPrime + 8u);
    }
}
