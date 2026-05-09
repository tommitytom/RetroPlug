#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "system/RomRole.hpp"
#include "system/SystemBase.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoyConstants.hpp"
#include "transport/FrameBufferTriple.hpp"

// Forward declare the C type so this header doesn't drag <gb.h> into the
// rest of the codebase.
struct GB_gameboy_s;
using GB_gameboy_t_fwd = GB_gameboy_s;

class SameBoySystem final : public SystemBase {
public:
    // Construct from a config + ROM bytes. The ROM data is moved in (the
    // emulator copies into its own buffer at activation time but we keep the
    // bytes around for resets).
    SameBoySystem(SystemId id,
                  SameBoyConfig config,
                  std::vector<std::uint8_t> romBytes);
    ~SameBoySystem() override;

    SystemKind kind() const override { return SystemKind::SameBoy; }

    void onActivate(double sampleRate) override;
    void onDeactivate() override;
    void onSampleRateChanged(double sampleRate) override;
    void onReset() override;
    void onProcess(const AudioBlockInfo& info, float* const* outs) override;

    FrameBufferTriple* framebuffer() override { return &frames_; }

    SystemConfig snapshotConfig() const override;

    // Internal hooks invoked from the C callbacks (made public so the
    // free-function trampolines can reach them; not part of the public API).
    void writeAudioSample(int16_t left, int16_t right);
    void onVblank();

    // Fields accessed by the C callbacks. Public for callback access only.
    SameBoyConfig             config_;
    std::vector<std::uint8_t> rom_;
    GB_gameboy_t_fwd*         gb_ = nullptr;
    FrameBufferTriple         frames_{sameboy::kPixelWidth, sameboy::kPixelHeight};

    // Stereo accumulator, sized to 2 * maxBlockSize. Resized lazily in
    // onProcess if a host hands us a larger block than we've seen.
    std::vector<float>        stereoAccum_;
    std::uint32_t             audioFrameCount_ = 0;

    bool                      activated_  = false;
    double                    sampleRate_ = 44100.0;

    std::vector<std::unique_ptr<RomRole>> roles_;
};
