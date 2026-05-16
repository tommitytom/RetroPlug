#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "system/SystemBase.hpp"
#include "system/mesen/GbaConfig.hpp"
#include "transport/FrameBufferTriple.hpp"
#include "util/ExpSmoother.hpp"

class Emulator;
class MesenAudioDevice;
class MesenVideoDevice;

// GBA (Mesen2) system. Mirrors MesenSystem's (NES) shape: per-block onProcess
// drives the CPU until enough samples are queued in the audio device, then
// drains them into the planar L/R outs with smoothed gain. Native GBA
// resolution: 240x160. Audio runs at the host sample rate (Mesen's
// SoundMixer resamples GBA's native ~32 kHz APU stream internally).
//
// Phase 1 scope: ROM load + video + audio + keyboard input. No MIDI roles.
class GbaSystem final : public SystemBase {
public:
    GbaSystem(SystemId id,
              GbaSystemConfig config,
              std::vector<std::uint8_t> romBytes);
    ~GbaSystem() override;

    SystemKind kind() const override { return SystemKind::Gba; }

    void onActivate(double sampleRate) override;
    void onDeactivate() override;
    void onSampleRateChanged(double sampleRate) override;
    void onReset() override;
    void onProcess(const AudioBlockInfo& info, float* const* outs) override;

    // Audio-thread: queue a GBA button transition. The byte is the
    // position-aligned name index (GbaButton wire byte); remapped to
    // Mesen's native GbaController::Buttons ordering at apply time.
    // Applied at the top of the next onProcess.
    void pressButton(std::uint8_t button, bool down) override;

    FrameBufferTriple* framebuffer() override { return &frames_; }

    SystemConfig snapshotConfig() const override;

    void setGainDb(float dB);

    // GBA native resolution (240x160). Public so callers can construct a
    // FrameBufferTriple-sized read buffer without hard-coding the constant.
    static constexpr std::uint32_t kPixelWidth  = 240;
    static constexpr std::uint32_t kPixelHeight = 160;

private:
    GbaSystemConfig                   config_;
    std::vector<std::uint8_t>         rom_;
    std::unique_ptr<Emulator>         emu_;
    std::shared_ptr<MesenAudioDevice> audioDevice_;
    std::shared_ptr<MesenVideoDevice> videoDevice_;
    FrameBufferTriple                 frames_{kPixelWidth, kPixelHeight};
    bool                              activated_      = false;
    bool                              threadIdSet_    = false;
    double                            sampleRate_     = 44100.0;
    ExpSmoother                       gainSmoother_;
    std::vector<float>                stereoAccum_;   // sized lazily to 2*blockSize

    // Pending GBA button transitions. The byte is the GbaButton wire value
    // (Right=0..Start=7, L=8, R=9); remapped to Mesen's native
    // GbaController::Buttons enum in onProcess.
    struct PendingGbaButton { std::uint8_t button; bool down; };
    std::vector<PendingGbaButton>     pendingButtons_;
};
