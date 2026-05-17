#include "lsdj/KitUtil.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstring>

#include <CDSPResampler.h>

#include "lsdj/SampleUtil.hpp"

namespace rp::lsdj::KitUtil {

void convertSamplerate(double inputSampleRate,
                       double outputSampleRate,
                       const std::vector<float>& buffer,
                       std::vector<float>& target) {
    if (buffer.empty()) {
        target.clear();
        return;
    }

    constexpr std::size_t kInBufCapacity = 1024;
    r8b::CFixedBuffer<double> inBuf;
    inBuf.alloc(static_cast<int>(buffer.size()));

    r8b::CPtrKeeper<r8b::CDSPResampler24*> resampler =
        new r8b::CDSPResampler24(inputSampleRate, outputSampleRate,
                                 static_cast<int>(buffer.size()));

    const std::size_t targetSize =
        static_cast<std::size_t>(buffer.size() * (outputSampleRate / inputSampleRate));
    target.resize(targetSize);

    std::size_t sourcePos = 0;
    std::size_t targetPos = 0;

    while (targetPos < targetSize) {
        std::fill_n(inBuf.getPtr(), kInBufCapacity, 0.0);

        const std::size_t chunkSize =
            std::min(kInBufCapacity, buffer.size() - sourcePos);
        for (std::size_t i = 0; i < chunkSize; ++i) {
            inBuf[i] = static_cast<double>(buffer[sourcePos++]);
        }

        double* targetBuffer = nullptr;
        std::size_t writeCount = static_cast<std::size_t>(
            resampler->process(inBuf.getPtr(),
                               static_cast<int>(kInBufCapacity),
                               targetBuffer));

        if (targetPos + writeCount > targetSize) {
            writeCount = targetSize - targetPos;
        }
        for (std::size_t i = 0; i < writeCount; ++i) {
            target[targetPos++] = static_cast<float>(targetBuffer[i]);
        }
    }
}

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
    convertSamplerate(static_cast<double>(sample.sampleRate),
                      static_cast<double>(kGameboySampleRate),
                      window, resampled);

    if (pendingDither) {
        processEffect(*pendingDither, resampled,
                      static_cast<float>(kGameboySampleRate));
        // dither outputs [0, 15] already.
    } else {
        // Manual clamp + scale + round so the buffer matches the dither
        // path's [0, 15] convention before nibble packing.
        for (float& v : resampled) {
            v = (std::clamp(v, -1.0f, 1.0f) + 1.0f) * 0.5f;
            v = std::round(v * 15.0f);
        }
    }

    // Trim to whole 32-sample frames so the packer can pack them flush.
    const std::size_t aligned = frameAlign(resampled.size());
    resampled.resize(aligned);

    SampleUtil::convertScaledF32ToNibbles(resampled, out);
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
