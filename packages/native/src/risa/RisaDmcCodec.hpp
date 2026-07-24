#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "kit/KitCodec.hpp"
#include "lsdj/Effects.hpp"

// risa (NES/MMC5) kit codec: 1-bit delta-modulated DPCM (DMC) samples in an 8 KB kit bank. Implements the
// console-neutral rp::kit::KitCodec — it reuses the shared compiler's file decode + dedupe + per-slot
// threading + r8brain resampler + effect chain, and only supplies the DMC-specific encode + bank assembly.
//
// The encode pipeline (per slot, off-thread) mirrors risa's wav2dmc.py POST-resample stages — the user
// chose the native r8brain resampler over wav2dmc's fixed-point sinc, so this is NOT byte-parity with
// wav2dmc, but reproduces its DC-blocker / 7-bit map / ±2 delta / pad exactly. Bank layout matches
// tools/rom_patcher/src/kit_editor/constants.js.

namespace rp::risa {

// One source sample for the DMC kit compiler: a source audio file, a 3-char slot name, an optional source
// window, an effect chain (gain/filter — dither is 4-bit-only and skipped), the PAL DPCM playback-rate
// index (0..15), a loop flag, and whether to peak-normalize into the 7-bit domain.
struct CompileDmcSampleSpec {
    std::string                   path;
    std::string                   name;
    std::size_t                   offset = 0; // skip the first N source frames
    std::size_t                   length = 0; // 0 = everything from offset
    std::vector<rp::lsdj::LsdjEffect> effects;
    std::uint8_t                  rate      = 12;   // index into the PAL DPCM rate table (default 14089.89 Hz)
    bool                          loop      = false;
    bool                          normalize = true; // 7-bit peak-normalize to -3 dB
};

class RisaDmcCodec final : public rp::kit::KitCodec {
public:
    RisaDmcCodec(std::string kitName, std::vector<CompileDmcSampleSpec> samples)
        : kitName_(std::move(kitName)), samples_(std::move(samples)) {}

    std::size_t sampleCount() const override { return samples_.size(); }

    rp::kit::SampleSource source(std::size_t i) const override { return { samples_[i].path }; }

    std::vector<std::uint8_t> encode(std::size_t i, const rp::kit::SampleData& decoded) const override;

    std::vector<std::uint8_t> assemble(
        const std::vector<std::vector<std::uint8_t>>& encoded) const override;

private:
    std::string                       kitName_;
    std::vector<CompileDmcSampleSpec> samples_;
};

} // namespace rp::risa
