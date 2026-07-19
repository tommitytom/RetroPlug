#include "risa/RisaDmcCodec.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "lsdj/KitUtil.hpp" // convertSamplerate (r8brain)

namespace rp::risa {
namespace {

// --- DMC / kit-bank constants (tools/rom_patcher/src/kit_editor/constants.js) ---
constexpr std::size_t kBankSize        = 0x2000; // 8 KB
constexpr std::size_t kSampleRegion    = 0x1EC0; // 7872 B for packed DPCM
constexpr std::size_t kNameOffset      = 0x1EC0;
constexpr std::size_t kNameSize        = 16;
constexpr std::size_t kNameMaxChars    = 6;
constexpr std::size_t kSampleNames     = 0x1ED0;
constexpr std::size_t kSampleNameLen   = 3;
constexpr std::size_t kIndexOffset     = 0x1F00;
constexpr std::size_t kIndexEntry      = 4;
constexpr std::size_t kSlotCount       = 16;
constexpr std::size_t kMagicOffset     = 0x1F40;
constexpr std::uint8_t kMagic          = 0xA5;
constexpr std::uint8_t kSlotEmpty      = 0xFF;
constexpr std::uint8_t kFlagLoop       = 0x01;
constexpr int          kSampleAlign    = 64;
constexpr int          kLengthStep     = 16;
constexpr std::size_t  kDmcMaxBytes    = 4081;
constexpr int          kNormalizeSwing = 45; // 63 * 10^(-3dB/20), rounded (wav2dmc NORMALIZE_TARGET_SWING)

// PAL DPCM playback rates (Hz), index 0..15 (wav2dmc.py PAL_DPCM_RATES_HZ).
constexpr double kPalRates[16] = {
    4177.40,  4696.63,  5261.41,  5579.22,  6023.94,  7044.94,  7917.18,  8397.01,
    9446.63,  11233.80, 12595.50, 14089.89, 16965.40, 21315.52, 25191.00, 33252.09,
};

// Floor-division toward -infinity (b > 0). C integer division truncates toward zero, which diverges from
// wav2dmc's Python `//` on negative numerators — load-bearing for the 7-bit map.
long floorDiv(long a, long b) {
    long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

// Pad a bit-packed DMC stream to a legal 16k+1 length with 0x00 bytes.
std::vector<std::uint8_t> padToDmcLength(std::vector<std::uint8_t> data) {
    if (data.empty()) return { 0 };
    const std::size_t rem = (data.size() - 1) % kLengthStep;
    if (rem != 0) data.resize(data.size() + (kLengthStep - rem), 0);
    return data;
}

// Encode resampled float audio into NES DMC bytes — wav2dmc.py's post-resample pipeline: DC blocker ->
// 7-bit map centered on 64 -> 1-bit ±2 delta (LSB-first) -> pad. Operates in the int16 domain (wav2dmc
// works on int16), so f32 is rounded/clamped to int16 first.
std::vector<std::uint8_t> convertF32ToDpcm(const std::vector<float>& f32, bool normalize) {
    const std::size_t n = f32.size();
    if (n == 0) return { 0 };

    std::vector<std::int32_t> s(n);
    for (std::size_t i = 0; i < n; ++i) {
        const float v = std::clamp(f32[i] * 32767.0f, -32768.0f, 32767.0f);
        s[i] = static_cast<std::int32_t>(std::lround(v));
    }

    // DC blocker: one-pole, R = 255/256, truncate toward zero, clamp int16. State x1 = y1 = 0.
    const double R = 255.0 / 256.0;
    double x1 = 0.0, y1 = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double x = static_cast<double>(s[i]);
        const double y = x - x1 + R * y1;
        x1 = x;
        y1 = y;
        long v = static_cast<long>(y); // truncates toward zero
        v = std::clamp(v, -32768L, 32767L);
        s[i] = static_cast<std::int32_t>(v);
    }

    // int16 -> 7-bit unsigned centered on 64.
    std::vector<std::uint8_t> u7(n);
    if (normalize) {
        long peak = 1;
        for (auto v : s) peak = std::max(peak, static_cast<long>(std::abs(v)));
        for (std::size_t i = 0; i < n; ++i) {
            const long q = 64 + floorDiv(static_cast<long>(s[i]) * kNormalizeSwing, peak);
            u7[i] = static_cast<std::uint8_t>(std::clamp(q, 0L, 127L));
        }
    } else {
        for (std::size_t i = 0; i < n; ++i) {
            const long q = 64 + floorDiv(static_cast<long>(s[i]) * 63, 32768);
            u7[i] = static_cast<std::uint8_t>(std::clamp(q, 0L, 127L));
        }
    }

    // 1-bit ±2 delta encode, LSB-first packing. Counter starts 64; guards <127 / >0 (the deliberate ±2
    // clamp quirk the decoder mirrors — NOT a clamp(0,127)).
    std::vector<std::uint8_t> out((n + 7) / 8, 0);
    int counter = 64;
    for (std::size_t i = 0; i < n; ++i) {
        if (static_cast<int>(u7[i]) >= counter) {
            out[i >> 3] |= static_cast<std::uint8_t>(1u << (i & 7));
            if (counter < 127) counter += 2;
        } else {
            if (counter > 0) counter -= 2;
        }
    }
    return padToDmcLength(std::move(out));
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
    rp::lsdj::KitUtil::convertSamplerate(srcRate, targetRate, window, resampled);

    std::vector<std::uint8_t> dmc = convertF32ToDpcm(resampled, spec.normalize);
    if (dmc.size() > kDmcMaxBytes) dmc.resize(kDmcMaxBytes); // 4081 = 16*255+1 (still a legal length)
    return dmc;
}

std::vector<std::uint8_t> RisaDmcCodec::assemble(
    const std::vector<std::vector<std::uint8_t>>& encoded) const {
    std::vector<std::uint8_t> bank(kBankSize, 0);
    // Sample-name region defaults to spaces (kit_bank_parser modelToBank).
    std::fill(bank.begin() + kSampleNames, bank.begin() + kSampleNames + kSlotCount * kSampleNameLen, 0x20);

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
