#include "lsdj/LsdjKitCodec.hpp"

#include <utility>

#include "lsdj/KitUtil.hpp"

namespace rp::lsdj {

std::vector<std::uint8_t> LsdjKitCodec::encode(std::size_t i,
                                               const rp::kit::SampleData& decoded) const {
    const CompileSampleSpec& spec = samples_[i];
    KitUtil::SampleInput input;
    input.name       = spec.name;
    input.data       = decoded.buffer;   // copy: compileSample mutates locally
    input.sampleRate = decoded.sampleRate;
    input.offset     = spec.offset;
    input.length     = spec.length;
    input.effects    = spec.effects;
    input.rotate     = spec.rotate;
    return KitUtil::compileSample(input);
}

std::vector<std::uint8_t> LsdjKitCodec::assemble(
    const std::vector<std::vector<std::uint8_t>>& encoded) const {
    // Pair names + encoded bytes for the bank assembler (slot order; empty
    // bytes -> empty slot).
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> assembled;
    assembled.reserve(encoded.size());
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        assembled.emplace_back(samples_[i].name, encoded[i]);
    }
    return KitUtil::buildKit(kitName_, assembled);
}

} // namespace rp::lsdj
