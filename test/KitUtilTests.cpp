// Unit tests for the LSDJ kit-compile chain.
//
// Covers:
//   - SampleUtil nibble round-trip (float -> packed nibbles -> float).
//   - KitUtil::convertSamplerate length + zero-input safety.
//   - KitUtil::buildKit binary layout (offsets, names, empty-slot markers).
//   - Effects: gain normalisation, biquad stability, dither value range.
//
// These tests live in retroplug-tests because that target is the lightweight
// path that doesn't drag DPF / LVGL / SameBoy. The lsdj sources only depend
// on r8brain + the std lib.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "lsdj/Effects.hpp"
#include "lsdj/KitUtil.hpp"
#include "lsdj/SampleUtil.hpp"

using namespace rp::lsdj;

namespace {

// Generate a saw-wave fixture aligned to whole 32-sample frames. Frame-aligned
// inputs are what the production compile path produces — partial frames are
// discarded.
std::vector<float> makeSaw(std::size_t frameCount) {
    std::vector<float> out(frameCount * SampleUtil::SAMPLES_PER_FRAME);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = -1.0f + 2.0f * (static_cast<float>(i) / out.size());
    }
    return out;
}

// Pre-scale a [-1, 1] buffer into the [0, 15] domain the packer expects.
std::vector<float> scaleToNibbleDomain(const std::vector<float>& src) {
    std::vector<float> dst(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        const float clamped = std::clamp(src[i], -1.0f, 1.0f);
        dst[i] = std::round((clamped + 1.0f) * 0.5f * 15.0f);
    }
    return dst;
}

} // namespace

TEST_CASE("SampleUtil nibble round-trip preserves amplitude shape", "[lsdj][SampleUtil]") {
    auto src    = makeSaw(4);                     // 128 samples
    auto scaled = scaleToNibbleDomain(src);

    std::vector<std::uint8_t> packed;
    SampleUtil::convertScaledF32ToNibbles(scaled, packed);
    REQUIRE(packed.size() == scaled.size() / 2);  // 2 samples per byte

    std::vector<float> decoded;
    SampleUtil::convertNibblesToF32WithRotation(packed, decoded);
    REQUIRE(decoded.size() == src.size());

    // 4-bit means 16 levels in [-1, 1] -> step of 2/15. Allow one step of
    // quantisation error on top of the floor/ceil at the edges.
    constexpr float kStep = 2.0f / 15.0f;
    for (std::size_t i = 0; i < src.size(); ++i) {
        REQUIRE_THAT(decoded[i],
            Catch::Matchers::WithinAbs(src[i], kStep * 1.5f));
    }
}

TEST_CASE("SampleUtil round-trip preserves rotation orientation", "[lsdj][SampleUtil]") {
    // Encode + decode an impulse at sample 0 of a frame; after the encoder's
    // rightward rotation and the decoder's matching reverse, the impulse must
    // come back at index 0. The rest of the frame is left at value 0 in the
    // pre-scaled [0, 15] domain — which decodes to amplitude -1.0 (the
    // bottom of the [-1, 1] range), so the spike at index 0 is the unique
    // positive sample.
    std::vector<float> input(SampleUtil::SAMPLES_PER_FRAME, 0.0f);
    input[0] = 15.0f;  // max nibble value in pre-scaled domain

    std::vector<std::uint8_t> packed;
    SampleUtil::convertScaledF32ToNibbles(input, packed);

    std::vector<float> decoded;
    SampleUtil::convertNibblesToF32WithRotation(packed, decoded);

    // Index 0 is the impulse — decodes to amplitude +1.0.
    REQUIRE_THAT(decoded[0], Catch::Matchers::WithinAbs(1.0f, 1e-5f));
    // Every other index was 0 in the pre-scaled domain -> -1.0 after decode.
    for (std::size_t i = 1; i < decoded.size(); ++i) {
        REQUIRE_THAT(decoded[i], Catch::Matchers::WithinAbs(-1.0f, 1e-5f));
    }
}

TEST_CASE("KitUtil::convertSamplerate handles empty input", "[lsdj][KitUtil]") {
    std::vector<float> in;
    std::vector<float> out{1.0f, 2.0f, 3.0f};
    KitUtil::convertSamplerate(44100.0, 11468.0, in, out);
    REQUIRE(out.empty());
}

TEST_CASE("KitUtil::convertSamplerate ratio-correct length", "[lsdj][KitUtil]") {
    // 4096 samples at 44.1 kHz -> ~1064 at 11468 Hz. r8brain may be off by a
    // few samples; allow a 3% margin.
    std::vector<float> in(4096, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i)
        in[i] = std::sin(2.0f * 3.14159f * 220.0f * i / 44100.0f);

    std::vector<float> out;
    KitUtil::convertSamplerate(44100.0, 11468.0, in, out);

    const double expected = in.size() * (11468.0 / 44100.0);
    REQUIRE_THAT(static_cast<double>(out.size()),
                 Catch::Matchers::WithinRel(expected, 0.03));
}

TEST_CASE("KitUtil::buildKit binary layout", "[lsdj][KitUtil]") {
    // Two samples + 13 empty slots.
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> samples;
    samples.emplace_back("KCK", std::vector<std::uint8_t>(32, 0xAB));
    samples.emplace_back("SNR", std::vector<std::uint8_t>(48, 0xCD));

    auto bank = KitUtil::buildKit("DRUMS!", samples);
    REQUIRE(bank.size() == Kit::kSize);

    // Offset table — first entry always points at the data region start.
    const auto* offsets = reinterpret_cast<const std::uint16_t*>(bank.data());
    REQUIRE(offsets[0] == Kit::kBankBase + Kit::kSampleDataOffset);
    // offsets[1] = end of slot 0 (32 bytes in).
    REQUIRE(offsets[1] == offsets[0] + 32);
    // offsets[2] = end of slot 1 (80 bytes from data start).
    REQUIRE(offsets[2] == offsets[0] + 80);
    // Every other slot must be marked empty (offset == 0).
    for (std::size_t i = 3; i <= Kit::kMaxSamples; ++i) {
        REQUIRE(offsets[i] == 0);
    }

    // Sample-name region: slot 0 = "KCK", slot 1 = "SNR", rest empty
    // (first byte = 0x00 sentinel).
    REQUIRE(bank[Kit::kSampleNameOffset + 0] == 'K');
    REQUIRE(bank[Kit::kSampleNameOffset + 1] == 'C');
    REQUIRE(bank[Kit::kSampleNameOffset + 2] == 'K');
    REQUIRE(bank[Kit::kSampleNameOffset + 3] == 'S');
    REQUIRE(bank[Kit::kSampleNameOffset + 4] == 'N');
    REQUIRE(bank[Kit::kSampleNameOffset + 5] == 'R');
    for (std::size_t i = 2; i < Kit::kMaxSamples; ++i) {
        REQUIRE(bank[Kit::kSampleNameOffset + i * 3] == 0);
    }

    // Kit name region: "DRUMS!" — no padding needed since len==6.
    REQUIRE(bank[Kit::kNameOffset + 0] == 'D');
    REQUIRE(bank[Kit::kNameOffset + 5] == '!');

    // Sample data placed contiguously starting at kSampleDataOffset.
    REQUIRE(bank[Kit::kSampleDataOffset + 0]  == 0xAB);
    REQUIRE(bank[Kit::kSampleDataOffset + 31] == 0xAB);
    REQUIRE(bank[Kit::kSampleDataOffset + 32] == 0xCD);
    REQUIRE(bank[Kit::kSampleDataOffset + 79] == 0xCD);
}

TEST_CASE("KitUtil::buildKit truncates sample data to bank capacity", "[lsdj][KitUtil]") {
    // 50 KB of "sample" — way more than the 0x3fa0 (~16 KB) max. Buildkit
    // must clip silently rather than write past the bank.
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> samples;
    samples.emplace_back("BIG", std::vector<std::uint8_t>(50 * 1024, 0xEE));
    auto bank = KitUtil::buildKit("HUGE", samples);
    REQUIRE(bank.size() == Kit::kSize);
}

TEST_CASE("GainEffect normalises peak to 1.0", "[lsdj][Effects]") {
    std::vector<float> buf{0.0f, 0.25f, -0.5f, 0.1f};
    GainEffect g;
    g.normalize = true;
    g.gain = 1.0f;
    processEffect(g, buf, 44100.0f);
    float peak = 0.0f;
    for (float v : buf) peak = std::max(peak, std::abs(v));
    REQUIRE_THAT(peak, Catch::Matchers::WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("FilterEffect leaves signal energy finite", "[lsdj][Effects]") {
    // A low-pass at ~5 kHz applied to a sine ought to leave a bounded signal.
    std::vector<float> buf(1024);
    for (std::size_t i = 0; i < buf.size(); ++i)
        buf[i] = std::sin(2.0f * 3.14159f * 880.0f * i / 44100.0f);

    FilterEffect f;
    f.filterType = FilterType::LowPass;
    f.frequency = 5000.0f;
    f.q = 1.0f;
    processEffect(f, buf, 44100.0f);

    for (float v : buf) {
        REQUIRE(std::isfinite(v));
        REQUIRE(std::abs(v) <= 1.0f);
    }
}

TEST_CASE("DitherEffect quantises to 4-bit domain", "[lsdj][Effects]") {
    // After dither the buffer should contain only integer values in [0, 15].
    std::vector<float> buf(512);
    for (std::size_t i = 0; i < buf.size(); ++i)
        buf[i] = std::sin(2.0f * 3.14159f * 220.0f * i / 11468.0f);

    DitherEffect d;
    d.ditherType = DitherType::HighPassTPDF;
    processEffect(d, buf, 11468.0f);

    for (float v : buf) {
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 15.0f);
        REQUIRE(v == std::round(v));
    }
}
