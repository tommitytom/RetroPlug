#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "system/InputCapacity.hpp"
#include "system/SystemBase.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "transport/FrameBufferTriple.hpp"
#include "util/ExpSmoother.hpp"
#include "util/FixedQueue.hpp"

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
class MesenNesSystem final : public SystemBase, public ChannelTap {
public:
    MesenNesSystem(SystemId id,
                   MesenNesConfig config,
                   std::vector<std::uint8_t> romBytes);
    ~MesenNesSystem() override;

    // The console a system node of this type builds: the node compares a built core's kind() with this before it casts
    // it to this class, and kind() answers it, so the two cannot drift.
    static constexpr SystemKind kKind = SystemKind::MesenNes;
    SystemKind kind() const override { return kKind; }

    void onActivate(double sampleRate) override;
    void onDeactivate() override;
    void onSampleRateChanged(double sampleRate) override;

    // SystemBase per-block triad (see base for the contract); the engine's
    // system node drives these directly. A NES is never linked, so
    // stepIfBelowTarget runs the whole block once and returns false; finishBlock
    // drains/sums the audio and publishes snapshots.
    void prepareForBlock(const AudioBlockInfo& info) override;
    bool stepIfBelowTarget(std::uint32_t framesNeeded) override;
    void finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) override;
    void reserveAudio(std::uint32_t maxBlockFrames) override;

    // Default = one stereo "Mix" stream. Under channelExportMode == StereoModPins (CLI-only), reports
    // the three mono pin streams (Pulse | TND | Expansion) the NesSoundMixer tap captures (spec/10 §5).
    std::vector<ChannelStream> channelLayout() const override;

    // Audio-thread: forward host MIDI events to the attached N8 role (if any).
    // The role pushes bytes into the FIFO RX queue so the ROM's polling loop
    // sees them at the next read of $40F0.
    void onMidi(const ::MidiEvent* events, std::uint32_t count) override;
    void pushCoreBytes(std::uint32_t frame, const std::uint8_t* data, std::size_t size,
                       bool flush = false) override;
    bool hasCoreBytesIn() const override { return n8Role_ != nullptr; }  // the N8 FIFO is the raw ingress

    // Control-thread: the N8 FIFO's CMD_USB_WR payload — what the ROM has sent host-ward since the
    // last drain. Empty before activation or when the ROM has sent nothing.
    std::vector<std::uint8_t> drainCoreBytes() override;
    CoreTransportStats coreTransportStats() override;
    std::uint64_t inputAccepted() const override;
    std::uint64_t inputDropped() const override;
    // The bytes the N8 role's queues have taken since the core was built: the non-press half of inputAccepted(), for
    // a test that holds a staged MIDI run or raw message reached the core rather than only a press.
    std::uint64_t acceptedBytes() const;

    // Audio-thread: queue a NES button transition. The byte is reinterpreted
    // as NesButton (Right/Left/Up/Down/A/B/Select/Start, positions 0..7).
    // Applied to Mesen's NesController at the top of the next onProcess.
    void pressButton(std::uint8_t button, bool down) override;

    // The transition this queue will leave on the console button `button` names when it applies, false when it leaves
    // none (a queue with no entry for it, or a byte that names no button of this console). The queue is applied entry
    // by entry in order, so the entry that stands is the last one naming that button. A test seam of the shape
    // SameBoySystem::peekSerialByte is: a block applies the queue and empties it, so a false after a block is the read
    // that the transition reached the controller.
    bool peekPendingPress(std::uint8_t button, bool& down) const;

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
    std::vector<std::uint8_t> saveSramBytes() const override;
    std::vector<std::uint8_t> saveStateBytes() const override;
    std::size_t               stateSnapshotSize() const override;
    bool                      captureStateSnapshot(std::vector<std::uint8_t>& dst) override;

    void setGainDb(float dB, bool immediate = false) override;

    // "mesen" system-role knobs (coreRoles.ts), applied live by the NES's system node (its settings, and its
    // expansionVolume param). Each but setRemoveSpriteLimit is value-guarded, and that one is a plain flag write.
    // removeSpriteLimit is a live PPU toggle; region reconfigures timing so it forces a reset.
    void setRemoveSpriteLimit(bool on);
    void setRegion(std::uint32_t region);
    // APU flush window as a latency (ms) → NesSoundMixer::SetLatencyMs. Live, no reset (scalar re-threshold).
    void setApuLatencyMs(double ms);
    // Cartridge-accuracy switches (0 = chip, 1 = Everdrive N8). Live - the core reads them in place.
    void setS5bNoise(std::uint32_t mode);
    void setMmc5PhaseReset(std::uint32_t mode);
    // The cartridge sound chip's level as a percentage (100 = unity), applied to every expansion channel
    // Mesen mixes. Live - the mixer re-reads its volume scalars each buffer.
    void setExpansionVolume(std::uint32_t percent);
    // Arm/disarm the per-channel tap AFTER construct, so a plugin routing change (Engine::syncTap)
    // can reach the split modes without rebuilding the core the way setRegion does — a routing flip must
    // not reset a playing game. Same modes as MesenNesConfig::channelExportMode (0 = off/Mix, 1 = pins,
    // 3 = 5 individual mono); channelLayout() follows it. Value-guarded, and pre-onActivate it only records
    // the mode (onActivate arms from config_). Allocates nothing on the block thread: the tap's rings, its resample
    // scratch, each stream's blip and the per-block scratches are sized by reserveAudio at activation, so an arm only
    // switches modes and re-initialises the streams (blip_reset puts a stream's blip back in the state a fresh one
    // starts in). A routing change therefore costs nothing on whichever thread the plan's trailer runs on.
    void setChannelExportMode(std::uint32_t mode);
    /** The armed per-channel export mode (0 = off). Lets the Engine record what it found before arming,
     *  so it can restore a construct-time (CLI / render) value it did not set. */
    std::uint32_t channelExportMode() const { return config_.channelExportMode; }
    // The tap as the engine sees it (SystemBase::channelTap): this core is its own, so the two methods above ARE the
    // capability, and a split routing arms a NES without the engine naming the console.
    ChannelTap*   channelTap() override { return this; }
    std::uint32_t exportMode() const override { return channelExportMode(); }
    void          setExportMode(std::uint32_t mode) override { setChannelExportMode(mode); }
    // Which of the two rings this block's step fills: the tap's streams when the block drains them, else the mix. A
    // split routing on a host too narrow for the split is wired to the mix and still arms the tap, so the core cannot
    // read one from the other: drainsTap() decides, and finishBlock's branch uses the same count.
    void          setBlockLanes(std::size_t laneCount) override { drainLanes_ = laneCount; }
    // The live APU flush window in CPU cycles (the mixer's conversion of apuLatencyMs against the region
    // clock). 0 before onActivate. Exposed for tests / introspection.
    std::uint32_t apuFlushCycleLength() const;
    // The config the setters above keep, read-only. Exposed for tests / introspection.
    const MesenNesConfig& config() const { return config_; }
    // The audio ring's capacity in frames (a plan's maximum block plus one Mesen flush), and the frames it had to drop
    // (0 unless a block ran bigger than the ring it was given, which activation prevents). The tap's own rings report
    // their drops the same way. For tests / introspection.
    std::uint32_t audioRingCapacity() const;
    // The mix ring's fill in frames, whether or not the tap is armed. For tests / introspection: the narrow-host path
    // (the tap armed, the block drained through the mix) is the one where the ring is not the step loop's clock, so a
    // case reads this rather than availableFrames() to hold that path's fill stays bounded.
    std::uint32_t audioRingFill() const;
    // Frames captured for the block in progress, before finishBlock drains them - the same metric stepIfBelowTarget
    // gates on: the tap's streams when this block drains them, else the mix ring. For tests / introspection: it is what
    // a whole-block case reads to hold that the step phase filled the block.
    std::uint32_t availableFrames() const;
    std::uint64_t audioRingOverflow() const;
    std::uint64_t tapOverflow() const;
    // The chunk the mix path's tap discard drained through, as its last block computed it. For tests / introspection:
    // a zero-size chunk ends the discard with frames left rather than spinning, and this is what a case reads to see
    // the chunk the ring is drained in at all.
    std::size_t tapDiscardChunk() const { return tapDiscardChunk_; }

    // NES native resolution (256x240). Public so callers can construct a
    // FrameBufferTriple-sized read buffer without hard-coding the constant.
    static constexpr std::uint32_t kPixelWidth  = 256;
    static constexpr std::uint32_t kPixelHeight = 240;

private:
    // Restore a savestate seed over the battery seedSramAndState() already put into the core.
    bool loadStateBytes(const std::vector<std::uint8_t>& bytes) override;

    // The mix path's drain scratch and the tap's per-stream drains, sized at activation (SystemBase::blockFrames):
    // a block's audio path must not allocate (docs/modular-graph-phase-c4-design.md section 4).
    void          sizePerBlockBuffers(std::uint32_t frames);
    std::size_t   captureStreamCount() const;  // streams the armed mode captures (0 = none)
    // Does this block's drain read the tap's streams (so the step must gate on their rings), or the mix ring?
    bool          drainsTap() const;

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
    std::vector<float>                stereoAccum_;   // 2 * one block of the largest block size

    // nesMixer_ is owned by emu_'s NesConsole and borrowed for the whole activated lifetime (nulled in
    // onDeactivate) — used by the live APU-latency knob (all NES systems) AND per-channel capture (spec/10
    // §5 pins / §5b individual mono). chanAccum_ holds the per-stream mono drain each block: 3 pins (mode 1),
    // 4 with the mix-reference (mode 2), or 5 core channels (mode 3). Each is one block of the largest block size,
    // sized at activation whether or not the tap is armed, so a routing change that arms it costs no allocation.
    static constexpr std::size_t      kMaxChannelStreams = 5;
    // The lane count a block the node drains through the tap arrives with: every captured stream on a pair of its own,
    // so three pins are six lanes and the five core channels are ten. Anything narrower is the mix's two.
    static constexpr std::size_t      kTapLanes = 6;
    NesSoundMixer*                    nesMixer_ = nullptr;
    bool                              channelCapture_ = false;
    // The lane count the last block was told to drain (SystemBase::setBlockLanes). It says which of the core's two
    // rings the step must fill: 0 means no driver said, and the core keeps its own rule (the tap's rings when they are
    // armed).
    std::size_t                       drainLanes_ = 0;
    std::array<std::vector<float>, kMaxChannelStreams> chanAccum_;
    std::size_t                       tapDiscardChunk_ = 0;

    // Pending NES button transitions (pos 0..7 from NesButton enum). Applied
    // at the top of onProcess via NesController::SetBitValue. Single-shot —
    // we don't bother spreading across the block since NES input polling
    // happens once per frame, not per sample. One entry a button: the whole queue is applied before the poll, so a
    // transition for a button already queued replaces it, and the queue is cleared at each prepare
    // (docs/modular-graph-phase-c4-design.md section 11).
    struct PendingNesButton { std::uint8_t button; bool down; };
    rp::FixedQueue<PendingNesButton>  pendingButtons_{rp::kPressInput};
    std::uint64_t                     pressesAccepted_ = 0;
};
