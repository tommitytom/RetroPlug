#include "risa/RisaDmcCodec.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "dsp/Dpcm.hpp"
#include "dsp/Resample.hpp"

namespace rp::risa {
namespace {

// --- DMC / kit-bank constants (tools/rom_patcher/src/kit_editor/constants.js) ---
// The bank size and the index table's offset live on RisaDmcCodec, so a bank reader and this assembler share them.
constexpr std::size_t kSampleRegion    = 0x1EC0; // 7872 B for packed DPCM
constexpr std::size_t kNameOffset      = 0x1EC0;
constexpr std::size_t kNameSize        = 16;
constexpr std::size_t kNameMaxChars    = 6;
constexpr std::size_t kSampleNames     = 0x1ED0;
constexpr std::size_t kSampleNameLen   = 3;
constexpr std::size_t kSlotCount       = 16;
constexpr std::size_t kMagicOffset     = 0x1F40;
constexpr std::uint8_t kMagic          = 0xA5;
constexpr std::uint8_t kSlotEmpty      = 0xFF;
constexpr std::uint8_t kFlagLoop       = 0x01;
constexpr int          kSampleAlign    = 64;
constexpr int          kLengthStep     = 16;
constexpr std::size_t  kDmcMaxBytes    = 4081;

// PAL DPCM playback rates (Hz), index 0..15 (wav2dmc.py PAL_DPCM_RATES_HZ).
constexpr double kPalRates[16] = {
    4177.40,  4696.63,  5261.41,  5579.22,  6023.94,  7044.94,  7917.18,  8397.01,
    9446.63,  11233.80, 12595.50, 14089.89, 16965.40, 21315.52, 25191.00, 33252.09,
};

// Pad a bit-packed DMC stream to a legal 16k+1 length with 0x00 bytes.
std::vector<std::uint8_t> padToDmcLength(std::vector<std::uint8_t> data) {
    if (data.empty()) return { 0 };
    const std::size_t rem = (data.size() - 1) % kLengthStep;
    if (rem != 0) data.resize(data.size() + (kLengthStep - rem), 0);
    return data;
}

// A 3-char, uppercased, right-space-padded sample name.
void writeSampleName(std::uint8_t* dst, const std::string& name) {
    for (std::size_t i = 0; i < kSampleNameLen; ++i) {
        char c = i < name.size() ? name[i] : ' ';
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        dst[i] = static_cast<std::uint8_t>(c);
    }
}

} // namespace

std::vector<std::uint8_t> RisaDmcCodec::encode(std::size_t i, const rp::kit::SampleData& decoded) const {
    if (decoded.buffer.empty() || decoded.sampleRate == 0) return {};
    const CompileDmcSampleSpec& spec = samples_[i];
    const double targetRate = kPalRates[spec.rate & 0x0F];

    // Clip the source window (offset/length), capped so the DMC output can't exceed the hardware max
    // (kDmcMaxBytes bytes = kDmcMaxBytes*8 output samples; scale back to source frames by the rate ratio).
    const double srcRate = static_cast<double>(decoded.sampleRate);
    const std::size_t maxOutFrames =
        static_cast<std::size_t>(std::ceil(static_cast<double>(kDmcMaxBytes * 8) * srcRate / targetRate));
    const std::size_t length0 = spec.length == 0 ? decoded.buffer.size() : spec.length;
    const std::size_t start    = std::min(spec.offset, decoded.buffer.size());
    const std::size_t frames   = std::min(maxOutFrames, std::min(length0, decoded.buffer.size() - start));

    std::vector<float> window(decoded.buffer.begin() + start, decoded.buffer.begin() + start + frames);

    // Pre-resample effects (gain, filter). Dither is 4-bit-only — skip it.
    const float inputRate = static_cast<float>(decoded.sampleRate);
    for (const auto& ef : spec.effects) {
        ef.visit([&](const auto& concrete) {
            using T = std::decay_t<decltype(concrete)>;
            if constexpr (!std::is_same_v<T, rp::lsdj::DitherEffect>) {
                rp::lsdj::processEffect(concrete, window, inputRate);
            }
        });
    }

    std::vector<float> resampled;
    rp::dsp::resample(srcRate, targetRate, window.data(), window.size(), resampled);

    // The encoder's delta bits, packed least-significant bit first into DMC bytes and padded to a legal length. The
    // arithmetic is rp::dsp::encodeDpcm's; the packing and the pad are this bank's format, not the encoder's.
    std::vector<std::uint8_t> bits(resampled.size());
    rp::dsp::encodeDpcm(resampled.data(), resampled.size(), spec.normalize, bits.data());
    std::vector<std::uint8_t> packed((bits.size() + 7) / 8, 0);
    for (std::size_t bit = 0; bit < bits.size(); ++bit)
        if (bits[bit]) packed[bit >> 3] |= static_cast<std::uint8_t>(1u << (bit & 7));

    std::vector<std::uint8_t> dmc = padToDmcLength(std::move(packed));
    if (dmc.size() > kDmcMaxBytes) dmc.resize(kDmcMaxBytes); // 4081 = 16*255+1 (still a legal length)
    return dmc;
}

std::vector<std::uint8_t> RisaDmcCodec::assemble(
    const std::vector<std::vector<std::uint8_t>>& encoded) const {
    std::vector<std::uint8_t> bank(kBankSize, 0);
    // Sample-name region defaults to spaces (kit_bank_parser modelToBank).
    std::fill(bank.begin() + kSampleNames, bank.begin() + kSampleNames + kSlotCount * kSampleNameLen, std::uint8_t{0x20});

    std::size_t cursor = 0;
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
        std::uint8_t* idx = bank.data() + kIndexOffset + slot * kIndexEntry;
        idx[0] = kSlotEmpty; // default: empty entry [0xFF, 0, 0, 0]

        if (slot >= encoded.size() || encoded[slot].empty()) continue;
        const std::vector<std::uint8_t>& data = encoded[slot];

        if (cursor % kSampleAlign) cursor += kSampleAlign - (cursor % kSampleAlign);
        if (cursor + data.size() > kSampleRegion) continue; // doesn't fit — leave the slot empty

        std::copy(data.begin(), data.end(), bank.begin() + cursor);
        idx[0] = static_cast<std::uint8_t>(cursor / kSampleAlign);        // $4012 address
        idx[1] = static_cast<std::uint8_t>((data.size() - 1) / kLengthStep); // $4013 length reg
        idx[2] = static_cast<std::uint8_t>(samples_[slot].rate & 0x0F);
        idx[3] = static_cast<std::uint8_t>(samples_[slot].loop ? kFlagLoop : 0);
        writeSampleName(bank.data() + kSampleNames + slot * kSampleNameLen, samples_[slot].name);
        cursor += data.size();
    }

    // Kit name: uppercased, keep A-Z 0-9 '-', up to 6 chars, NUL-padded to 16.
    std::size_t w = 0;
    for (char c : kitName_) {
        if (w >= kNameMaxChars) break;
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') bank[kNameOffset + w++] = static_cast<std::uint8_t>(c);
    }
    (void)kNameSize;

    bank[kMagicOffset] = kMagic; // populated marker, stamped last
    return bank;
}

} // namespace rp::risa
