#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "system/SystemBase.hpp"
#include "system/mesen/MesenGbaConfig.hpp"
#include "transport/FrameBufferTriple.hpp"
#include "util/ExpSmoother.hpp"

class Emulator;
class MesenAudioDevice;
class MesenVideoDevice;

// GBA system, via the Mesen backend. Mirrors MesenNesSystem's shape: per-block
// onProcess drives the CPU until enough samples are queued in the audio device,
// then drains them into the planar L/R outs with smoothed gain. Native GBA
// resolution: 240x160. Audio runs at the host sample rate (Mesen's
// SoundMixer resamples GBA's native ~32 kHz APU stream internally).
class MesenGbaSystem final : public SystemBase {
public:
    MesenGbaSystem(SystemId id,
                   MesenGbaConfig config,
                   std::vector<std::uint8_t> romBytes);
    ~MesenGbaSystem() override;

    SystemKind kind() const override { return SystemKind::MesenGba; }

    void onActivate(double sampleRate) override;
    void onDeactivate() override;
    void onSampleRateChanged(double sampleRate) override;
    void onReset() override;

    // SystemBase per-block triad (see base for the contract); the runner
    // (runUnit) drives these directly. GBA is a degenerate 1-member unit:
    // stepIfBelowTarget runs the whole block once and returns false; finishBlock
    // drains/sums the audio and publishes snapshots.
    void prepareForBlock(const AudioBlockInfo& info) override;
    bool stepIfBelowTarget(std::uint32_t framesNeeded) override;
    void finishBlock(const AudioBlockInfo& info, float* const* outs) override;

    // Audio-thread: queue a GBA button transition. The byte is the
    // position-aligned name index (GbaButton wire byte); remapped to
    // Mesen's native GbaController::Buttons ordering at apply time.
    // Applied at the top of the next onProcess.
    void pressButton(std::uint8_t button, bool down) override;

    FrameBufferTriple* framebuffer() override { return &frames_; }

    // MemoryType → Mesen MemoryType. Maps to Gba* regions; returns invalid
    // for IORegisters / HRam / NametableRam (GB / NES only). Ram = IWRAM
    // (32KB, fast on-chip); ExtWorkRam = EWRAM (256KB, slower off-chip).
    rp::MemoryAccessor getMemory(rp::MemoryType type, rp::AccessType access) override;

    // -- CPU state (SystemBase virtuals) -------------------------------------
    //
    // GBA (ARM7): registers r0..r15 + cpsr (32-bit) + pc (= r15), register
    // writes (pc/r15 via SetProgramCounter; cpsr write unsupported), single-step
    // via GbaCpu::Exec(), and side-effect-free CPU-address reads via
    // GbaMemoryManager::DebugRead(). Inspect memory regions via getMemory().
    std::vector<rp::CpuRegister> getCpuRegisters() const override;
    bool setCpuRegister(std::string_view name, std::uint32_t value) override;
    std::optional<std::uint32_t> getProgramCounter() const override;
    std::optional<std::uint8_t>  readCpuByte(std::uint32_t addr) const override;
    std::uint64_t stepInstruction() override;

    SystemConfig snapshotConfig() const override;

    // SystemBase virtuals — see base class for contracts.
    const std::string&        romPath() const override        { return config_.romPath; }
    std::uint32_t             savSuffix() const override      { return config_.savSuffix; }
    void                      setSavSuffix(std::uint32_t s) override { config_.savSuffix = s; }
    const std::string&        savPath() const override        { return config_.savPath; }
    void                      setSavPath(const std::string& p) override { config_.savPath = p; }
    std::optional<bool>       fastBoot() const override       { return config_.skipBootScreen; }
    void                      setFastBoot(bool on) override;
    bool                      wantsRomReload() const override { return config_.reloadOnRomChange; }
    void                      setRomReload(bool on) override  { config_.reloadOnRomChange = on; }
    std::vector<std::uint8_t> saveSramBytes() const override;
    void                      clearSram() override;
    std::vector<std::uint8_t> saveStateBytes() const override;
    bool                      loadStateBytes(const std::vector<std::uint8_t>& bytes) override;
    std::size_t               stateSnapshotSize() const override;
    bool                      captureStateSnapshot(std::vector<std::uint8_t>& dst) override;
    std::unique_ptr<SystemBase> clone(SystemId newId, double sampleRate) const override;

    void setGainDb(float dB);

    // GBA native resolution (240x160). Public so callers can construct a
    // FrameBufferTriple-sized read buffer without hard-coding the constant.
    static constexpr std::uint32_t kPixelWidth  = 240;
    static constexpr std::uint32_t kPixelHeight = 160;

private:
    MesenGbaConfig                   config_;
    std::vector<std::uint8_t>         rom_;
    std::unique_ptr<Emulator>         emu_;
    std::shared_ptr<MesenAudioDevice> audioDevice_;
    std::shared_ptr<MesenVideoDevice> videoDevice_;
    FrameBufferTriple                 frames_{kPixelWidth, kPixelHeight};
    bool                              activated_      = false;
    double                            sampleRate_     = 44100.0;
    ExpSmoother                       gainSmoother_;
    std::vector<float>                stereoAccum_;   // sized lazily to 2*blockSize

    // Pending GBA button transitions. The byte is the GbaButton wire value
    // (Right=0..Start=7, L=8, R=9); remapped to Mesen's native
    // GbaController::Buttons enum in onProcess.
    struct PendingGbaButton { std::uint8_t button; bool down; };
    std::vector<PendingGbaButton>     pendingButtons_;
};
