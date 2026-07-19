#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "system/SystemBase.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "transport/FrameBufferTriple.hpp"
#include "util/ExpSmoother.hpp"

class Emulator;
class NesSoundMixer;
class MesenAudioDevice;
class MesenVideoDevice;
class MesenNesDebugSession;
class NesN8FifoRole;

// NES system, via the Mesen backend. Mirrors SameBoySystem's shape: per-block
// onProcess drives the emulator until enough samples are queued in
// MesenAudioDevice, then drains them into the planar L/R outs with smoothed
// gain. Native NES resolution: 256x240. Audio runs at the host sample rate
// (Mesen's SoundMixer resamples internally). Input arrives as NesButton; host
// MIDI is forwarded to the N8 FIFO (NesN8FifoRole).
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

    // SystemBase per-block triad (see base for the contract); the runner
    // (runUnit) drives these directly. NES is a degenerate 1-member unit:
    // stepIfBelowTarget runs the whole block once and returns false; finishBlock
    // drains/sums the audio and publishes snapshots.
    void prepareForBlock(const AudioBlockInfo& info) override;
    bool stepIfBelowTarget(std::uint32_t framesNeeded) override;
    void finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) override;

    // Default = one stereo "Mix" stream. Under channelExportMode == StereoModPins (CLI-only), reports
    // the three mono pin streams (Pulse | TND | Expansion) the NesSoundMixer tap captures (spec/10 §5).
    std::vector<ChannelStream> channelLayout() const override;

    // Audio-thread: forward host MIDI events to the attached N8 role (if any).
    // The role pushes bytes into the FIFO RX queue so the ROM's polling loop
    // sees them at the next read of $40F0.
    void onMidi(const ::MidiEvent* events, std::uint32_t count) override;

    // Audio-thread: queue a NES button transition. The byte is reinterpreted
    // as NesButton (Right/Left/Up/Down/A/B/Select/Start, positions 0..7).
    // Applied to Mesen's NesController at the top of the next onProcess.
    void pressButton(std::uint8_t button, bool down) override;

    FrameBufferTriple* framebuffer() override { return &frames_; }

    // True once onActivate has booted a live core. False if the ROM failed
    // Mesen's LoadRom (a corrupt/mislabelled file that passed the format gate),
    // so the backend can reject the build instead of adopting a dead system.
    bool activated() const { return activated_; }

    // MemoryType → Mesen MemoryType. Maps to Nes* regions; returns invalid
    // for IORegisters / HRam / ExtWorkRam (GB / GBA only).
    rp::MemoryAccessor getMemory(rp::MemoryType type, rp::AccessType access) override;

    // -- CPU state (SystemBase virtuals) -------------------------------------
    //
    // NES (6502): registers a/x/y/sp/ps (8-bit) + pc (16-bit), register writes,
    // native single-step via NesCpu::Exec(), and side-effect-free CPU-address
    // reads via NesMemoryManager::DebugRead().
    std::vector<rp::CpuRegister> getCpuRegisters() const override;
    bool setCpuRegister(std::string_view name, std::uint32_t value) override;
    std::optional<std::uint32_t> getProgramCounter() const override;
    std::optional<std::uint8_t>  readCpuByte(std::uint32_t addr) const override;
    bool                         writeCpuByte(std::uint32_t addr, std::uint8_t value) override;
    std::uint64_t stepInstruction() override;

    // Mesen debugger / profiler. Lazily created on first call (so non-debug
    // renders never init Mesen's debugger). nullptr until activated.
    rp::IDebugTarget* debugTarget() override;

    // SystemBase virtuals — see base class for contracts.
    const std::string&        romPath() const override        { return config_.romPath; }
    std::uint32_t             savSuffix() const override      { return config_.savSuffix; }
    void                      setSavSuffix(std::uint32_t s) override { config_.savSuffix = s; }
    const std::string&        savPath() const override        { return config_.savPath; }
    void                      setSavPath(const std::string& p) override { config_.savPath = p; }
    bool                      wantsRomReload() const override { return config_.reloadOnRomChange; }
    void                      setRomReload(bool on) override  { config_.reloadOnRomChange = on; }
    std::vector<std::uint8_t> saveSramBytes() const override;
    void                      clearSram() override;
    std::vector<std::uint8_t> saveStateBytes() const override;
    bool                      loadStateBytes(const std::vector<std::uint8_t>& bytes) override;
    std::size_t               stateSnapshotSize() const override;
    bool                      captureStateSnapshot(std::vector<std::uint8_t>& dst) override;
    std::unique_ptr<SystemBase> clone(SystemId newId, double sampleRate) const override;

    void setGainDb(float dB) override;

    // "mesen" system-role knobs (coreRoles.ts), applied live via Engine::applyConfigField.
    // removeSpriteLimit is a live PPU toggle; region reconfigures timing so it forces a reset.
    void setRemoveSpriteLimit(bool on);
    void setRegion(std::uint32_t region);
    // APU flush window as a latency (ms) → NesSoundMixer::SetLatencyMs. Live, no reset (scalar re-threshold).
    void setApuLatencyMs(double ms);
    // The live APU flush window in CPU cycles (the mixer's conversion of apuLatencyMs against the region
    // clock). 0 before onActivate. Exposed for tests / introspection.
    std::uint32_t apuFlushCycleLength() const;

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
    std::unique_ptr<NesN8FifoRole>    n8Role_;
    std::unique_ptr<MesenNesDebugSession> debugSession_;
    FrameBufferTriple                 frames_{kPixelWidth, kPixelHeight};
    bool                              activated_      = false;
    double                            sampleRate_     = 44100.0;
    ExpSmoother                       gainSmoother_;
    std::vector<float>                stereoAccum_;   // sized lazily to 2*blockSize

    // nesMixer_ is owned by emu_'s NesConsole and borrowed for the whole activated lifetime (nulled in
    // onDeactivate) — used by the live APU-latency knob (all NES systems) AND per-channel capture (spec/10
    // §5 pins / §5b individual mono). chanAccum_ holds the per-stream mono drain each block: 3 pins (mode 1),
    // 4 with the mix-reference (mode 2), or 5 core channels (mode 3).
    static constexpr std::size_t      kMaxChannelStreams = 5;
    NesSoundMixer*                    nesMixer_ = nullptr;
    bool                              channelCapture_ = false;
    std::array<std::vector<float>, kMaxChannelStreams> chanAccum_;

    // Pending NES button transitions (pos 0..7 from NesButton enum). Applied
    // at the top of onProcess via NesController::SetBitValue. Single-shot —
    // we don't bother spreading across the block since NES input polling
    // happens once per frame, not per sample.
    struct PendingNesButton { std::uint8_t button; bool down; };
    std::vector<PendingNesButton>     pendingButtons_;
};
