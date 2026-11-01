#include "lsdj/KitUtil.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstring>

#include "dsp/Resample.hpp"
#include "lsdj/SampleUtil.hpp"

namespace rp::lsdj::KitUtil {

namespace {

// Trim a length to the nearest lower multiple of the 32-sample frame size,
// since SampleUtil's nibble packers operate in whole frames.
std::size_t frameAlign(std::size_t n) {
    return (n / SampleUtil::SAMPLES_PER_FRAME) * SampleUtil::SAMPLES_PER_FRAME;
}

void writeString(std::uint8_t* dst, std::size_t maxLen,
                 std::string_view name, char fillChar) {
    for (std::size_t i = 0; i < maxLen; ++i) {
        dst[i] = i < name.size()
            ? static_cast<std::uint8_t>(std::toupper(static_cast<unsigned char>(name[i])))
            : static_cast<std::uint8_t>(fillChar);
    }
}

} // namespace

std::vector<std::uint8_t> compileSample(const SampleInput& sample) {
    std::vector<std::uint8_t> out;
    if (sample.data.empty() || sample.sampleRate == 0) return out;

    // Clip the source window per the requested offset / length, but cap at
    // the maximum frames a single LSDJ sample can occupy at the GB rate
    // (after resampling, that limit becomes `kMaxSampleFrames`; at the
    // source rate it scales by srRatio so anything beyond is unusable).
    const float srRatio = static_cast<float>(sample.sampleRate)
                        / static_cast<float>(kGameboySampleRate);
    const std::size_t maxFrames = static_cast<std::size_t>(
        std::ceil(static_cast<float>(Kit::kMaxSampleFrames) * srRatio));
    const std::size_t length0 =
        (sample.length == 0) ? sample.data.size() : sample.length;
    const std::size_t startFrame = std::min(sample.offset, sample.data.size());
    const std::size_t readFrames =
        std::min(maxFrames, std::min(length0, sample.data.size() - startFrame));

    std::vector<float> window(sample.data.begin() + startFrame,
                              sample.data.begin() + startFrame + readFrames);

    // Apply pre-resample effects (gain, filter). Dither runs after
    // resampling — track it here and defer until below.
    const DitherEffect* pendingDither = nullptr;
    const float inputRate = static_cast<float>(sample.sampleRate);
    for (const auto& ef : sample.effects) {
        ef.visit([&](const auto& concrete) {
            using T = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<T, DitherEffect>) {
                pendingDither = &concrete;
            } else {
                processEffect(concrete, window, inputRate);
            }
        });
    }

    // Resample to the GB target rate.
    std::vector<float> resampled;
    rp::dsp::resample(static_cast<double>(sample.sampleRate),
                      static_cast<double>(kGameboySampleRate),
                      window.data(), window.size(), resampled);

    // Quantise to 4-bit codes at the GB rate: through the dither when one is
    // set, plain rounding otherwise. The dither sees (and is seeded from) the
    // whole resampled sample, before the trim below.
    std::vector<std::uint16_t> codes = quantise(resampled, pendingDither);

    // Trim to whole 32-sample frames so the packer can pack them flush.
    codes.resize(frameAlign(codes.size()));

    SampleUtil::convertCodesToNibbles(codes, out, sample.rotate);
    return out;
}

std::vector<std::uint8_t> buildKit(
    std::string_view kitName,
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& samples) {
    std::vector<std::uint8_t> bank(Kit::kSize, 0);

    // Kit name (space-padded).
    writeString(bank.data() + Kit::kNameOffset, Kit::kNameSize, kitName, ' ');

    // Sample-data offset table sits at the start of the bank, stored as
    // 16-bit absolute-to-bank-base offsets. offsets[0] is always the
    // start of the sample-data region; offsets[i+1] is the byte just
    // past sample i. Empty slots get offsets[i+1] = 0 (matches legacy
    // "no sample" marker).
    std::uint16_t* offsets = reinterpret_cast<std::uint16_t*>(bank.data());
    std::uint8_t*  names   = bank.data() + Kit::kSampleNameOffset;
    std::uint8_t*  data    = bank.data() + Kit::kSampleDataOffset;
    const std::uint16_t baseOffset = Kit::kBankBase + Kit::kSampleDataOffset;
    offsets[0] = baseOffset;

    std::uint16_t cursor = 0;
    std::size_t   spaceRemaining = Kit::kMaxSampleSpace;

    for (std::size_t i = 0; i < Kit::kMaxSamples; ++i) {
        const std::size_t nameSlot = i * Kit::kSampleNameSize;
        if (i < samples.size() && spaceRemaining > 0) {
            const auto& src = samples[i].second;
            const std::size_t writeSize = std::min(src.size(), spaceRemaining);
            spaceRemaining -= writeSize;

            writeString(names + nameSlot, Kit::kSampleNameSize,
                        samples[i].first, '-');
            std::memcpy(data + cursor, src.data(), writeSize);

            cursor = static_cast<std::uint16_t>(cursor + writeSize);
            offsets[i + 1] = static_cast<std::uint16_t>(cursor + baseOffset);
        } else {
            // Empty-slot marker: first name byte = 0x00, rest of name '-',
            // offset entry = 0. Matches the legacy "no sample" sentinel
            // used by Kit::hasSample() and the LSDJ UI.
            names[nameSlot]     = 0;
            names[nameSlot + 1] = static_cast<std::uint8_t>('-');
            names[nameSlot + 2] = static_cast<std::uint8_t>('-');
            offsets[i + 1] = 0;
        }
    }

    return bank;
}

} // namespace rp::lsdj::KitUtil
