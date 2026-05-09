#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "system/InputTypes.hpp"
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

    // Queue a button transition. Called from DSP-thread command-drain at the
    // top of each block. Pending transitions are spread across the block in
    // applyPending() (port of old SameBoyUtil.cpp:149-163).
    void pressButton(GameboyButton button, bool down) override;

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

    // Pending button transitions, ordered by sample offset within the block.
    // Spread across each block so a press+release pair doesn't collapse to
    // zero duration (which the SameBoy joypad debouncer would miss).
    struct PendingButton {
        std::uint32_t offset;   // samples from block start
        GameboyButton button;
        bool          down;
    };
    std::deque<PendingButton> pendingButtons_;

    // Default per-press duration (samples) used to space queued transitions.
    // Mirrors the old code's "10 ms at sampleRate" default.
    std::uint32_t buttonSpacingSamples_ = 0;

    std::vector<std::unique_ptr<RomRole>> roles_;
};
