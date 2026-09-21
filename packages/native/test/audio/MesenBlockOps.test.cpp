// sumStereoWithGain, the drain-and-sum tail all three Mesen systems share.
//
// The case worth a test is the one that used to be wrong. MesenAudioDevice::drain returns how many frames
// it ACTUALLY wrote, every caller discarded that, and the scratch buffer is only resized when it grows -
// so a ring that came up short left the tail of the block holding the PREVIOUS block's samples, and the
// loop summed those into the output. A stale echo where the caller expects silence.
//
// Reachable today only on SMS, whose step loop can give up at kInstructionBudget; NES and GBA loop until
// the ring is full. It becomes reachable on both the moment they gain a bound, which is where this
// refactor is going - hence pinning it now, directly, rather than hoping a core test wanders into it.

#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "system/mesen/MesenAudioDevice.hpp"
#include "system/mesen/MesenBlockOps.hpp"
#include "util/ExpSmoother.hpp"

namespace {

/** Push `frames` stereo frames of a constant int16 value into the ring, the way Mesen's mixer does. */
void fill(MesenAudioDevice& dev, std::uint32_t frames, std::int16_t value) {
    std::vector<std::int16_t> pcm(std::size_t(frames) * 2, value);
    dev.PlayBuffer(pcm.data(), frames, 48000, true);
}

/** A smoother pinned at unity, so the test measures the drain and not the gain ramp. */
ExpSmoother unityGain() {
    ExpSmoother g;
    g.setSampleRate(48000.0f);
    g.setTargetValue(1.0f);
    g.clearToTargetValue();
    return g;
}

} // namespace

TEST_CASE("sumStereoWithGain blanks the tail when the ring is short", "[mesen]") {
    MesenAudioDevice dev;
    std::vector<float> scratch;
    ExpSmoother gain = unityGain();

    // Block 1: a full ring of a loud value. This is what leaves residue in the scratch buffer.
    constexpr std::uint32_t kBlock = 64;
    fill(dev, kBlock, 32767);
    std::vector<float> l1(kBlock, 0.0f), r1(kBlock, 0.0f);
    float* outs1[2] = { l1.data(), r1.data() };
    sumStereoWithGain(dev, scratch, outs1, kBlock, gain);
    REQUIRE(l1[kBlock - 1] > 0.9f); // the scratch really is full of loud samples now

    // Block 2: only a QUARTER of the frames are available. The rest of the block must be silence, not
    // block 1's tail - which is exactly what it used to be.
    fill(dev, kBlock / 4, 0);
    std::vector<float> l2(kBlock, 0.0f), r2(kBlock, 0.0f);
    float* outs2[2] = { l2.data(), r2.data() };
    sumStereoWithGain(dev, scratch, outs2, kBlock, gain);

    for (std::uint32_t i = 0; i < kBlock; ++i) {
        INFO("frame " << i);
        CHECK(l2[i] == 0.0f);
        CHECK(r2[i] == 0.0f);
    }
}

TEST_CASE("sumStereoWithGain accumulates rather than overwriting", "[mesen]") {
    // The engine sums several systems into one pair of lanes, so a system must ADD to what is there.
    MesenAudioDevice dev;
    std::vector<float> scratch;
    ExpSmoother gain = unityGain();

    constexpr std::uint32_t kBlock = 16;
    fill(dev, kBlock, 16384); // half scale
    std::vector<float> l(kBlock, 0.25f), r(kBlock, -0.25f);
    float* outs[2] = { l.data(), r.data() };
    sumStereoWithGain(dev, scratch, outs, kBlock, gain);

    CHECK(l[0] > 0.25f);  // the caller's content survived and the core's was added on top
    CHECK(r[0] > -0.25f);
}
