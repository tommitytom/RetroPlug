#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "system/SystemBase.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "transport/FrameBufferTriple.hpp"
#include "util/ExpSmoother.hpp"

class Emulator;
class MesenAudioDevice;
class MesenVideoDevice;
class NesN8MidiRole;

// NES system, via the Mesen backend. Mirrors SameBoySystem's shape: per-block
// onProcess drives the emulator until enough samples are queued in
// MesenAudioDevice, then drains them into the planar L/R outs with smoothed
// gain. Native NES resolution: 256x240. Audio runs at the host sample rate
// (Mesen's SoundMixer resamples internally). Input arrives as NesButton; host
// MIDI is forwarded to the N8 FIFO RomRole.
class MesenNesSystem final : public SystemBase {
public:
    MesenNesSystem(SystemId id,
                   MesenNesConfig config,
                   std::vector<std::uint8_t> romBytes);
    ~MesenNesSystem() override;

    SystemKind kind() const override { return SystemKind::MesenNes; }

    void onActivate(double sampleRate) override;
    void onDeactivate() override;
    void onSampleRateChanged(double sampleRate) override;
    void onReset() override;
    void onProcess(const AudioBlockInfo& info, float* const* outs) override;

    // Audio-thread: forward host MIDI events to the attached N8 role (if any).
    // The role pushes bytes into the FIFO RX queue so the ROM's polling loop
    // sees them at the next read of $40F0.
    void onMidi(const ::MidiEvent* events, std::uint32_t count) override;

    // Audio-thread: queue a NES button transition. The byte is reinterpreted
    // as NesButton (Right/Left/Up/Down/A/B/Select/Start, positions 0..7).
    // Applied to Mesen's NesController at the top of the next onProcess.
    void pressButton(std::uint8_t button, bool down) override;

    FrameBufferTriple* framebuffer() override { return &frames_; }

    // MemoryType → Mesen MemoryType. Maps to Nes* regions; returns invalid
    // for IORegisters / HRam / ExtWorkRam (GB / GBA only).
    rp::MemoryAccessor getMemory(rp::MemoryType type, rp::AccessType access) override;

    SystemConfig snapshotConfig() const override;

    // SystemBase virtuals — see base class for contracts.
    const std::string&        romPath() const override        { return config_.romPath; }
    bool                      wantsRomReload() const override { return config_.reloadOnRomChange; }
    void                      setRomReload(bool on) override  { config_.reloadOnRomChange = on; }
    std::vector<std::uint8_t> saveSramBytes() const override;
    void                      clearSram() override;
    std::vector<std::uint8_t> saveStateBytes() const override;
    bool                      loadStateBytes(const std::vector<std::uint8_t>& bytes) override;
    std::unique_ptr<SystemBase> clone(SystemId newId, double sampleRate) const override;

    void setGainDb(float dB);

    // NES native resolution (256x240). Public so callers can construct a
    // FrameBufferTriple-sized read buffer without hard-coding the constant.
    static constexpr std::uint32_t kPixelWidth  = 256;
    static constexpr std::uint32_t kPixelHeight = 240;

private:
    MesenNesConfig                       config_;
    std::vector<std::uint8_t>         rom_;
    std::unique_ptr<Emulator>         emu_;
    std::shared_ptr<MesenAudioDevice> audioDevice_;
    std::shared_ptr<MesenVideoDevice> videoDevice_;
    std::unique_ptr<NesN8MidiRole>    n8Role_;
    FrameBufferTriple                 frames_{kPixelWidth, kPixelHeight};
    bool                              activated_      = false;
    bool                              threadIdSet_    = false;
    double                            sampleRate_     = 44100.0;
    ExpSmoother                       gainSmoother_;
    std::vector<float>                stereoAccum_;   // sized lazily to 2*blockSize

    // Pending NES button transitions (pos 0..7 from NesButton enum). Applied
    // at the top of onProcess via NesController::SetBitValue. Single-shot —
    // we don't bother spreading across the block since NES input polling
    // happens once per frame, not per sample.
    struct PendingNesButton { std::uint8_t button; bool down; };
    std::vector<PendingNesButton>     pendingButtons_;
};
