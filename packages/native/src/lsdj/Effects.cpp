#include "lsdj/Effects.hpp"

#include <cstdint>

#include "dsp/Biquad.hpp"
#include "dsp/Dither.hpp"
#include "dsp/Gain.hpp"

namespace rp::lsdj {

namespace {

// The effect's filter list is the kernel's, value for value, so a filter type crosses by a cast: each cast the
// effect-to-kernel mapping makes must land on the matching filter.
static_assert(static_cast<dsp::BiquadType>(FilterType::LowPass) == dsp::BiquadType::LowPass);
static_assert(static_cast<dsp::BiquadType>(FilterType::HighPass) == dsp::BiquadType::HighPass);
static_assert(static_cast<dsp::BiquadType>(FilterType::BandPass) == dsp::BiquadType::BandPass);
static_assert(static_cast<dsp::BiquadType>(FilterType::BandStop) == dsp::BiquadType::BandStop);
static_assert(static_cast<dsp::BiquadType>(FilterType::Peak) == dsp::BiquadType::Peak);
static_assert(static_cast<dsp::BiquadType>(FilterType::LowShelf) == dsp::BiquadType::LowShelf);
static_assert(static_cast<dsp::BiquadType>(FilterType::HighShelf) == dsp::BiquadType::HighShelf);
static_assert(static_cast<dsp::BiquadType>(FilterType::AllPass) == dsp::BiquadType::AllPass);

constexpr int kBitDepth = 4;  // LSDJ kit samples are 4-bit

// The kernel has a sixth algorithm (`None`, plain rounding) the effect cannot name, so the dither list maps by name.
dsp::DitherAlgorithm algorithmOf(const DitherEffect* dither) {
    if (!dither) return dsp::DitherAlgorithm::None;
    switch (dither->ditherType) {
        case DitherType::HighPassTPDF:   return dsp::DitherAlgorithm::HighPassTPDF;
        case DitherType::ShapedTPDF:     return dsp::DitherAlgorithm::ShapedTPDF;
        case DitherType::ErrorDiffusion: return dsp::DitherAlgorithm::ErrorDiffusion;
        case DitherType::JJN:            return dsp::DitherAlgorithm::JJN;
        case DitherType::SierraLite:     return dsp::DitherAlgorithm::SierraLite;
    }
    return dsp::DitherAlgorithm::None;
}

} // namespace

void processEffect(const GainEffect& effect,
                   std::vector<float>& target,
                   float /*sampleRate*/) {
    float gain = effect.gain;
    if (effect.normalize) gain = dsp::normalizedGain(gain, dsp::peak(target.data(), target.size()));
    dsp::Gain kernel;
    kernel.init(gain);
    kernel.process(target.data(), target.data(), target.size());
}

void processEffect(const FilterEffect& effect,
                   std::vector<float>& target,
                   float sampleRate) {
    dsp::Biquad kernel;
    kernel.init({static_cast<dsp::BiquadType>(effect.filterType), sampleRate, effect.frequency, effect.q, effect.gain});
    kernel.process(target.data(), target.data(), target.size());
}

// The seed is the whole buffer's, hashed before the first code: the input the dither is handed, which for the kit path
// is the windowed, post-effect, resampled sample at the Game Boy kit rate (11468 Hz) BEFORE it is trimmed to whole
// 32-sample frames - not the source-rate window the gain and filter see, and not the trimmed bank. Drawing it from the
// input is what makes a compile reproducible: a kit bank is a file, and two compiles of the same sample have to be the
// same file. A whole bank is byte-identical across hosts only where the resampler and the filters agree too, which is
// why exact kit bytes are compared on one reference target.
std::vector<std::uint16_t> quantise(const std::vector<float>& buffer, const DitherEffect* dither) {
    std::vector<std::uint16_t> codes(buffer.size());
    dsp::Dither kernel;
    kernel.init(algorithmOf(dither), kBitDepth, dsp::inputSeed(buffer.data(), buffer.size()));
    kernel.process(buffer.data(), codes.data(), buffer.size());
    return codes;
}

void processEffect(const DitherEffect& effect,
                   std::vector<float>& target,
                   float /*sampleRate*/) {
    const std::vector<std::uint16_t> codes = quantise(target, &effect);
    for (std::size_t i = 0; i < target.size(); ++i) target[i] = static_cast<float>(codes[i]);
}

} // namespace rp::lsdj
