#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "kit/KitCodec.hpp"
#include "lsdj/Effects.hpp"

// LSDJ kit codec: Game Boy 4-bit inverted/rotated nibble PCM in a 16 KB bank.
// Implements the console-neutral rp::kit::KitCodec by delegating to the
// existing LSDJ packing (KitUtil::compileSample) and bank assembly
// (KitUtil::buildKit) — the byte format is unchanged, only the plumbing moved
// behind the shared compiler.

namespace rp::lsdj {

// One source sample for the LSDJ kit compiler: a source audio file, a 3-char
// uppercase slot name, an optional source window, and an effect chain.
struct CompileSampleSpec {
    std::string             path;       // source audio file
    std::string             name;       // 3-char uppercase slot name
    std::size_t             offset = 0; // skip the first N frames of the source
    std::size_t             length = 0; // 0 = use everything from offset
    std::vector<LsdjEffect> effects;
    bool                    rotate = true; // LSDJ 9.2.0+ frame rotation (per target ROM version)
};

class LsdjKitCodec final : public rp::kit::KitCodec {
public:
    LsdjKitCodec(std::string kitName, std::vector<CompileSampleSpec> samples)
        : kitName_(std::move(kitName)), samples_(std::move(samples)) {}

    std::size_t sampleCount() const override { return samples_.size(); }

    rp::kit::SampleSource source(std::size_t i) const override {
        return { samples_[i].path };
    }

    std::vector<std::uint8_t> encode(std::size_t i,
                                     const rp::kit::SampleData& decoded) const override;

    std::vector<std::uint8_t> assemble(
        const std::vector<std::vector<std::uint8_t>>& encoded) const override;

private:
    std::string                    kitName_;
    std::vector<CompileSampleSpec> samples_;
};

} // namespace rp::lsdj
