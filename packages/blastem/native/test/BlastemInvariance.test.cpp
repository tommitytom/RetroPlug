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
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemCore.hpp"
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double        kRate  = 48000.0;
constexpr std::uint64_t kTotal = 96000;  // 2 s: about 120 frames, so every image's VINT, HINT, Z80 and DMA run many times

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
