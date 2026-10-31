#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "system/InputCapacity.hpp"
#include "system/SystemBase.hpp"
#include "system/mesen/MesenGbaConfig.hpp"
#include "transport/FrameBufferTriple.hpp"
#include "util/ExpSmoother.hpp"
#include "util/FixedQueue.hpp"

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

    // The console a system node of this type builds: the node compares a built core's kind() with this before it casts
    // it to this class, and kind() answers it, so the two cannot drift.
    static constexpr SystemKind kKind = SystemKind::MesenGba;
    SystemKind kind() const override { return kKind; }

    void onActivate(double sampleRate) override;
    void onDeactivate() override;
    void onSampleRateChanged(double sampleRate) override;

    // SystemBase per-block triad (see base for the contract); the engine's
    // system node drives these directly. A GBA is never linked, so
    // stepIfBelowTarget runs the whole block once and returns false; finishBlock
    // drains/sums the audio and publishes snapshots.
    void prepareForBlock(const AudioBlockInfo& info) override;
    bool stepIfBelowTarget(std::uint32_t framesNeeded) override;
    void finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) override;
    void reserveAudio(std::uint32_t maxBlockFrames) override;

    // Audio-thread: queue a GBA button transition. The byte is the
    // position-aligned name index (GbaButton wire byte); remapped to
    // Mesen's native GbaController::Buttons ordering at apply time, a byte outside the GBA's table driving no button.
    // Applied at the top of the next onProcess.
    void pressButton(std::uint8_t button, bool down) override;

    // The transition this queue will leave on the console button `button` names when it applies, false when it leaves
    // none (a queue with no entry for it, or a byte that names no button of this console). The queue is applied entry
    // by entry in order, so the entry that stands is the last one naming that button. A test seam of the shape
    // SameBoySystem::peekSerialByte is: a block applies the queue and empties it, so a false after a block is the read
    // that the transition was consumed, and buttonDown below is the read that the pad then holds it.
    bool peekPendingPress(std::uint8_t button, bool& down) const;

    // Whether the console's controller 0 holds the button `button` now, through BaseControlDevice::IsPressed: the bit
    // the last block applied, not the queue. False for a byte that names no button of this console, and before
    // activation, where there is no pad to read. A read-only test seam beside peekPendingPress.
    bool buttonDown(std::uint8_t button) const;
    std::uint64_t inputAccepted() const override { return pressesAccepted_; }
    std::uint64_t inputDropped() const override { return pendingButtons_.dropped(); }

    FrameBufferTriple* framebuffer() override { return &frames_; }

    // True once onActivate has booted a live core. False if the ROM failed
    // Mesen's LoadRom (a corrupt/mislabelled file that passed the format gate),
    // so the backend can reject the build instead of adopting a dead system.
    bool activated() const { return activated_; }

    // How many sample frames are sitting in the audio ring. The block target is measured against this,
    // so it is the only way a test can tell "the step loop reached the target" from "the loop gave up
    // and finishBlock drained a short ring" - which look identical from the output buffer, since
    // finishBlock sums into a caller-sized array either way. The SMS twin exists for the same reason.
    std::uint32_t availableFrames() const;
    // Frames the audio ring dropped for want of room since activation. A GBA's step overshoots by a whole video frame,
    // the largest of the three cores, so a test reads this to hold that even that stays inside the ring.
    std::uint64_t audioRingOverflow() const;

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

    // SystemBase virtuals — see base class for contracts.
    void                      setFastBoot(bool on) override;
    std::vector<std::uint8_t> saveSramBytes() const override;
    std::vector<std::uint8_t> saveStateBytes() const override;
    std::size_t               stateSnapshotSize() const override;
    bool                      captureStateSnapshot(std::vector<std::uint8_t>& dst) override;

    void setGainDb(float dB, bool immediate = false) override;

    // GBA native resolution (240x160). Public so callers can construct a
    // FrameBufferTriple-sized read buffer without hard-coding the constant.
    static constexpr std::uint32_t kPixelWidth  = 240;
    static constexpr std::uint32_t kPixelHeight = 160;

private:
    // Restore a savestate seed over the battery seedSramAndState() already put into the core.
    bool loadStateBytes(const std::vector<std::uint8_t>& bytes) override;
    void sizeBlockScratch(std::uint32_t frames);  // stereoAccum_: at activation, never on the block thread

    MesenGbaConfig                   config_;
    std::vector<std::uint8_t>         rom_;
    std::unique_ptr<Emulator>         emu_;
    std::shared_ptr<MesenAudioDevice> audioDevice_;
    std::shared_ptr<MesenVideoDevice> videoDevice_;
    FrameBufferTriple                 frames_{kPixelWidth, kPixelHeight};
    bool                              activated_      = false;
    double                            sampleRate_     = 44100.0;
    ExpSmoother                       gainSmoother_;
    // Sized at activation (reserveAudio), never on the block thread.
    std::vector<float>                stereoAccum_;   // 2 * the largest block's frames

    // Pending GBA button transitions. The byte is the GbaButton wire value
    // (Right=0..Start=7, L=8, R=9); remapped to Mesen's native
    // GbaController::Buttons enum in onProcess. One entry a button: the whole queue is applied before the poll, so a
    // transition for a button already queued replaces it, and the queue is cleared at each prepare
    // (docs/modular-graph-phase-c4-design.md section 11).
    struct PendingGbaButton { std::uint8_t button; bool down; };
    rp::FixedQueue<PendingGbaButton>  pendingButtons_{rp::kPressInput};
    std::uint64_t                     pressesAccepted_ = 0;

    // This system's private firmware directory: which one it owns, and where it ended up once a BIOS
    // was actually installed there (empty when none was). Private per INSTANCE because Mesen's own
    // firmware folder is process-global while a host runs several emulators - see installGbaBios.
    const std::uint64_t               firmwareSlot_;
    std::string                       firmwareDir_;
};
