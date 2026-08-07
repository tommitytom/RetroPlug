#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "system/SystemBase.hpp"
#include "system/mesen/MesenSmsConfig.hpp"
#include "system/mesen/roles/SmsSyncRole.hpp"
#include "transport/FrameBufferTriple.hpp"
#include "util/ExpSmoother.hpp"

class Emulator;
class MesenAudioDevice;
class MesenVideoDevice;
class SmsConsole;
class SmsControlManager;

// Master System / Game Gear system, via the Mesen backend. ONE class serves
// both machines (config_.gameGear picks which); they differ in visible
// geometry, region field, frame blending and - later - the sync transport, but
// not in anything structural.
//
// The one place this is NOT a copy of MesenGbaSystem is stepIfBelowTarget, and
// it is the reason this backend exists at all. See the comment there.
class MesenSmsSystem final : public SystemBase {
public:
    MesenSmsSystem(SystemId id,
                   MesenSmsConfig config,
                   std::vector<std::uint8_t> romBytes);
    ~MesenSmsSystem() override;

    SystemKind kind() const override { return SystemKind::MesenSms; }

    void onActivate(double sampleRate) override;
    void onDeactivate() override;
    void onSampleRateChanged(double sampleRate) override;
    void onReset() override;

    // SystemBase per-block triad (see base for the contract); the runner
    // (runUnit) drives these directly. SMS is a degenerate 1-member unit:
    // stepIfBelowTarget runs the whole block once and returns false.
    void prepareForBlock(const AudioBlockInfo& info) override;
    bool stepIfBelowTarget(std::uint32_t framesNeeded) override;
    void finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) override;

    // Audio-thread: queue a button transition. The byte is the position-aligned
    // name index (SmsButton wire byte); remapped to Mesen's native
    // SmsController::Buttons ordering at apply time. Applied in prepareForBlock.
    void pressButton(std::uint8_t button, bool down) override;

    // Audio-thread: schedule host-transport sync levels at intra-block sample offsets. Each byte is
    // a controller-port level word (see SmsSyncRole for the encoding), NOT a protocol byte - this
    // reuses the generic byte seam rather than adding a SystemBase virtual, exactly as the NES
    // routes risa's sync stream into its N8 FIFO.
    //
    // `flush` is ignored: a held level has no undelivered-stream hazard to clear. See SmsSyncRole.
    void pushCoreBytes(std::uint32_t frame, const std::uint8_t* data, std::size_t size,
                       bool flush = false) override;

    FrameBufferTriple* framebuffer() override { return &frames_; }

    // True once onActivate has booted a live core. False if the ROM failed
    // Mesen's LoadRom (a corrupt/mislabelled file that passed the format gate),
    // so the backend can reject the build instead of adopting a dead system.
    bool activated() const { return activated_; }

    // Diagnostic: dimensions of the last frame the CORE emitted, which should
    // equal framebuffer()'s own. They can only disagree if the overscan set in
    // configureSms and the kSmsPixel*/kGgPixel* constants have drifted apart -
    // a disagreement MesenVideoDevice's min-clamp would otherwise hide as a
    // silently cropped picture. Zero before the first frame.
    std::uint32_t coreFrameWidth() const;
    std::uint32_t coreFrameHeight() const;

    // Audio-thread: drive the host-controlled levels on controller `port`,
    // active low (bit clear = line asserted, 0xFF = all released). This is the
    // sync transport: smsggdj reads a 2-bit counter off $DD as TH (bit 7) and
    // TR (bit 3), polled once per video frame.
    //
    // Set-and-HOLD, never pulse. The ROM samples a level, not an edge, so a
    // change that lands and reverts inside one emulated frame is invisible to
    // it (SmsController::RefreshStateBuffer is empty and ReadPort reads devices
    // live, so a level IS visible to the very next IN - but only if it is still
    // set when that IN runs).
    //
    // The sync role that schedules these by sample offset lands next; this is
    // the call it will make from its pumpUntil.
    void setExternalInput(std::uint8_t port, std::uint8_t levels);

    // Intra-block output-sample position derived from the Z80 cycle counter
    // rather than the audio ring depth. THIS is what makes SMS event delivery
    // finer than the NES's: the ring only advances when the PSG flushes (up to
    // kCoarseCycles behind), while CycleCount never lags. Public for the same
    // reason SameBoySystem::audioFrameCount_ is - it is the gate metric, and
    // the only way to assert the step loop advances it at the right rate is to
    // read it against availableFrames() from a test driving a live core.
    std::uint32_t intraBlockSamplePos() const;

    // Companion to the above: how many sample frames are actually sitting in
    // the audio ring. The two together are the whole story of this step loop -
    // the ring is what the block target is measured against, and the cycle
    // position is what events are scheduled against. They are deliberately
    // decoupled (the ring lags by up to one flush window), so asserting the
    // relationship between them needs both.
    std::uint32_t availableFrames() const;

    // MemoryType -> Mesen MemoryType. Maps to Sms* regions; returns invalid for
    // OAM (SMS sprites live in VRAM), IORegisters, HRam, NametableRam and
    // ExtWorkRam. Colour RAM and the boot ROM have no rp::MemoryType tag - see
    // the matrix comment in system/MemoryType.hpp.
    rp::MemoryAccessor getMemory(rp::MemoryType type, rp::AccessType access) override;

    // SystemBase virtuals - see base class for contracts. fastBoot() is
    // deliberately NOT overridden: Mesen's SmsConfig has no boot-screen skip,
    // so the base nullopt hides the Fast boot row.
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

    // Visible resolution after overscan. The VDP always emits a 256x240
    // RenderedFrame (SmsVdp.cpp:648) and BaseVideoFilter subtracts the
    // configured overscan, which Mesen defaults to ZERO because its real
    // per-console defaults live in the .NET UI this build does not compile. So
    // these constants and the overscan set in configureSms/configureGg must
    // agree, or the picture is silently wrong (MesenVideoDevice min-clamps
    // rather than failing).
    static constexpr std::uint32_t kSmsPixelWidth  = 256;
    static constexpr std::uint32_t kSmsPixelHeight = 192;
    static constexpr std::uint32_t kGgPixelWidth   = 160;
    static constexpr std::uint32_t kGgPixelHeight  = 144;

private:
    SmsConsole* smsConsole() const;

    // Write rom_ to a real file whose name Mesen can read three separate
    // meanings out of. Returns the path, or empty on failure. See the comment
    // on the definition.
    std::string stageRom();

    MesenSmsConfig                    config_;
    std::vector<std::uint8_t>         rom_;
    std::unique_ptr<Emulator>         emu_;
    std::shared_ptr<MesenAudioDevice> audioDevice_;
    std::shared_ptr<MesenVideoDevice> videoDevice_;
    FrameBufferTriple                 frames_;
    bool                              activated_      = false;
    double                            sampleRate_     = 44100.0;
    ExpSmoother                       gainSmoother_;
    std::vector<float>                stereoAccum_;   // sized lazily to 2*blockSize

    // The on-disk file handed to Mesen's LoadRom. NOT cosmetic: the VirtualFile
    // name is simultaneously the machine selector, the battery-file stem and
    // the source Reset() re-reads from. See stageRom().
    std::string                       stagedRomPath_;

    // Cached at activation so setExternalInput does not dynamic_cast on the
    // audio thread once a sync role is calling it per event. Owned by the
    // console; cleared in onDeactivate alongside emu_.
    SmsControlManager*                controlManager_ = nullptr;

    // Offset-scheduled host-transport sync. Always present and inert until something calls
    // pushCoreBytes, mirroring the NES's always-attached FIFO: a system with no sync role driving it
    // simply never has anything queued, and the controller lines stay released.
    SmsSyncRole                       syncRole_;

    // Step-loop state, reset each prepareForBlock.
    std::uint64_t                     blockStartCycle_ = 0;
    std::uint32_t                     blockCarry_      = 0;
    std::uint32_t                     masterRate_      = 3579545;
    std::uint64_t                     pendingCycles_   = 0;

    // Pending button transitions. The byte is the SmsButton wire value
    // (Right=0..Start=7); remapped to Mesen's native SmsController::Buttons
    // enum in prepareForBlock.
    struct PendingSmsButton { std::uint8_t button; bool down; };
    std::vector<PendingSmsButton>     pendingButtons_;
};
