// The kit path's two TPDF dithers, seeded from the samples they quantise.
//
// `KitUtil::compileSample` hands the dither the WINDOWED, POST-EFFECT, RESAMPLED buffer at the Game Boy kit rate
// (11468 Hz), before `frameAlign` trims it to whole 32-sample frames, so that is the input the seed is drawn from.
// Every case here calls the effect directly on a buffer at that rate, the way the kit path does, and holds what the
// seed has to give: a compile gives the same bytes for the same input, two different samples do not share one noise
// sequence (which a single fixed seed would), and the noise is a specified function of the input and the algorithm,
// so it is the same on any host whose float arithmetic is IEEE-754 - the draw comes from raw `mt19937` output and the
// hash reads the floats' bytes little-endian, neither of which depends on the standard library's or the host's choices.
// A whole kit bank is byte-identical only where the resampler and the filters agree as well, which is why the exact
// bank bytes are compared on one reference target.
//
// Run via `pnpm test:plugin`.

#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "lsdj/Effects.hpp"
#include "lsdj/KitUtil.hpp"

namespace {

// The rate the dither runs at: the kit's target rate, which is also its input rate once `compileSample` has resampled.
constexpr float kKitRate = static_cast<float>(rp::lsdj::kGameboySampleRate);

/** A decaying tone as a source sample: something with real 4-bit structure for the dither to quantise. */
std::vector<float> tone(std::size_t frames) {
    std::vector<float> out(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / kKitRate;
        out[i] = 0.5f * std::sin(2.0f * rp::lsdj::kPi * 220.0f * t) * std::exp(-t * 4.0f);
    }
    return out;
}

std::vector<float> dithered(std::vector<float> buf, rp::lsdj::DitherType type) {
    rp::lsdj::processEffect(rp::lsdj::DitherEffect{type}, buf, kKitRate);
    return buf;
}

/**
 * The index of the first sample two runs disagree on, or -1. Comparing the bytes keeps a failing `CHECK` down to one
 * number: the vectors are thousands of floats and dumping them buries the message.
 */
std::ptrdiff_t firstDifference(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return 0;
    const std::size_t bytes = a.size() * sizeof(float);
    if (std::memcmp(a.data(), b.data(), bytes) == 0) return -1;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0) return static_cast<std::ptrdiff_t>(i);
    }
    return 0;
}

} // namespace

TEST_CASE("a TPDF dither quantises the same input to the same codes twice", "[dither][kit]") {
    for (const auto type : {rp::lsdj::DitherType::HighPassTPDF, rp::lsdj::DitherType::ShapedTPDF}) {
        INFO("algorithm " << static_cast<int>(type));
        const auto first  = dithered(tone(1024), type);
        const auto second = dithered(tone(1024), type);
        CHECK(firstDifference(first, second) == -1);
    }
}

TEST_CASE("a TPDF dither draws different noise for a different input", "[dither][kit]") {
    // Silence is the cleanest probe: every code comes from the dither, so a differing noise sequence shows up as
    // differing codes. The two buffers differ in ONE sample by a millionth, far below a 4-bit quantum, so a fixed
    // seed would leave the two passes agreeing almost everywhere, while a seed drawn from the input walks a
    // different sequence from sample 0. The threshold is loose on purpose: it only has to tell "the noise changed"
    // apart from "only the one sample changed".
    std::vector<float> a(1024, 0.0f);
    std::vector<float> b = a;
    b[0] = 1.0e-6f;

    const auto da = dithered(a, rp::lsdj::DitherType::HighPassTPDF);
    const auto db = dithered(b, rp::lsdj::DitherType::HighPassTPDF);

    std::size_t differing = 0;
    for (std::size_t i = 0; i < da.size(); ++i) {
        if (da[i] != db[i]) ++differing;
    }
    CHECK(differing > 16);
}

TEST_CASE("each TPDF dither's noise from a fixed input is those exact codes", "[dither][kit]") {
    // Eight silent samples, and the codes each algorithm quantises them to, recorded from the implementation. This
    // pins the whole seed derivation - the hash, its fold, the per-algorithm salt, the draw from `mt19937` - so a
    // drift in any of them fails here with a number instead of surfacing later as a kit byte diff. The codes hold on
    // any host: nothing in either pass is anything but exact binary32 arithmetic on a silent buffer.
    const std::vector<float> silence(8, 0.0f);

    const std::vector<float> highPass{
        7.0f, 8.0f, 7.0f, 7.0f, 8.0f, 7.0f, 8.0f, 8.0f,
    };
    const std::vector<float> shaped{
        7.0f, 7.0f, 7.0f, 7.0f, 8.0f, 7.0f, 8.0f, 7.0f,
    };

    const auto hp = dithered(silence, rp::lsdj::DitherType::HighPassTPDF);
    const auto sp = dithered(silence, rp::lsdj::DitherType::ShapedTPDF);
    CHECK(firstDifference(hp, highPass) == -1);
    CHECK(firstDifference(sp, shaped) == -1);
    // Over one buffer the two algorithms do not give the same codes. This alone does not prove the salt (their shaping
    // and error terms differ too); the exact codes above are what pin each algorithm's salt, since either constant
    // moving changes them.
    CHECK(firstDifference(hp, sp) != -1);
}
