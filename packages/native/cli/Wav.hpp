#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

// Minimal 16-bit PCM stereo WAV writer. Linear quantize float[-1,1] -> int16
// with clamp. Sizes are back-patched on close.
class WavWriter {
public:
    WavWriter(const std::string& path, std::uint32_t sampleRate, std::uint16_t channels)
        : sampleRate_(sampleRate), channels_(channels) {
        f_ = std::fopen(path.c_str(), "wb");
        if (!f_) throw std::runtime_error("WAV: cannot open " + path);
        writeHeaderPlaceholder();
    }

    ~WavWriter() {
        if (f_) {
            patchSizes();
            std::fclose(f_);
        }
    }

    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;
    WavWriter(WavWriter&& o) noexcept
        : f_(o.f_), sampleRate_(o.sampleRate_), channels_(o.channels_),
          dataBytes_(o.dataBytes_) { o.f_ = nullptr; }

    // Planar float input. `outs[c]` is a buffer of `frames` samples for channel c.
    // Interleaves and writes int16. Caller decides clipping policy upstream.
    void writeBlockFloatPlanar(float* const* outs, std::uint32_t frames) {
        for (std::uint32_t i = 0; i < frames; ++i) {
            for (std::uint16_t c = 0; c < channels_; ++c) {
                const float v = std::clamp(outs[c][i], -1.0f, 1.0f);
                const std::int16_t s = static_cast<std::int16_t>(v * 32767.0f);
                std::fwrite(&s, sizeof(s), 1, f_);
                dataBytes_ += sizeof(s);
            }
        }
    }

private:
    static void writeU32(std::FILE* f, std::uint32_t v) { std::fwrite(&v, 4, 1, f); }
    static void writeU16(std::FILE* f, std::uint16_t v) { std::fwrite(&v, 2, 1, f); }

    void writeHeaderPlaceholder() {
        // RIFF chunk
        std::fwrite("RIFF", 1, 4, f_);
        writeU32(f_, 0);              // size — patched on close
        std::fwrite("WAVE", 1, 4, f_);

        // fmt subchunk (PCM, 16-bit)
        std::fwrite("fmt ", 1, 4, f_);
        writeU32(f_, 16);             // fmt size
        writeU16(f_, 1);              // PCM
        writeU16(f_, channels_);
        writeU32(f_, sampleRate_);
        writeU32(f_, sampleRate_ * channels_ * 2);  // byte rate
        writeU16(f_, channels_ * 2);                // block align
        writeU16(f_, 16);                           // bits/sample

        // data subchunk header
        std::fwrite("data", 1, 4, f_);
        writeU32(f_, 0);              // data size — patched on close
    }

    void patchSizes() {
        const std::uint32_t riffSize = 4 + (8 + 16) + (8 + dataBytes_);
        std::fseek(f_, 4, SEEK_SET);
        writeU32(f_, riffSize);
        std::fseek(f_, 40, SEEK_SET); // 12 (RIFF/WAVE) + 8 (fmt hdr) + 16 (fmt body) + 4 (data tag)
        writeU32(f_, dataBytes_);
    }

    std::FILE*    f_ = nullptr;
    std::uint32_t sampleRate_;
    std::uint16_t channels_;
    std::uint32_t dataBytes_ = 0;
};
