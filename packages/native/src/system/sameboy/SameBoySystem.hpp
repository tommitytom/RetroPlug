#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "system/InputCapacity.hpp"
#include "system/InputTypes.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemCapabilities.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoyConstants.hpp"
#include "transport/FrameBufferTriple.hpp"
#include "util/ExpSmoother.hpp"
#include "util/FixedQueue.hpp"

// Forward declare the C type so this header doesn't drag <gb.h> into the
// rest of the codebase.
struct GB_gameboy_s;
using GB_gameboy_t_fwd = GB_gameboy_s;

// What serial-out capture does (setSerialOutCapture). Listen: capture the bits the Game Boy clocks out itself, an
// internal-clock transfer (SC=0x81), which is how LSDj as master sync's master sends its clock. Drive: that, and
// clock a transfer waiting on an external clock (SC=0x80) one bit an audio frame, capturing its bits too: the
// Arduinoboy's part in MI.OUT, where LSDj is the serial slave. Drive clocks whatever waits, so it is for MI.OUT only:
// a stopped LSDj in SYNC=LSDj waits on an external clock for another master, and clocked, it takes the idle bytes
// for that master's clock and stops answering START (docs/lsdj-master-sync-fix-design.md).
enum class SerialOutCapture : std::uint8_t { Off = 0, Listen = 1, Drive = 2 };

class SameBoySystem final : public SystemBase {
public:
    // Construct from a config + ROM bytes. The ROM data is moved in (the
    // emulator copies into its own buffer at activation time but we keep the
    // bytes around for resets).
    SameBoySystem(SystemId id,
                  SameBoyConfig config,
                  std::vector<std::uint8_t> romBytes);
    ~SameBoySystem() override;

    // The console a system node of this type builds: the node compares a built core's kind() with this before it casts
    // it to this class, and kind() answers it, so the two cannot drift.
    static constexpr SystemKind kKind = SystemKind::SameBoy;
    SystemKind kind() const override { return kKind; }

    void onActivate(double sampleRate) override;
    void onDeactivate() override;
    void onSampleRateChanged(double sampleRate) override;

    // Pop the next bit (MSB-first) from `serialIn_` and return it. Returns
    // `true` (idle high) when the queue is empty. Called from the SameBoy
    // serial-end callback in standalone mode.
    bool nextSerialInBit();

    // Queue a button transition. Called from DSP-thread command-drain at the
    // top of each block. The byte is reinterpreted as GameboyButton; pending
    // transitions are spread across the block in applyPending() (port of old
    // SameBoyUtil.cpp:149-163).
    void pressButton(std::uint8_t button, bool down) override;

    // Queue a raw byte for the GB serial port, scheduled at sample offset
    // `frame` within the block. Drained MSB-first by nextSerialInBit() / the
    // slave-mode pump in stepIfBelowTarget(), neither before `frame` is reached.
    // A message's bytes share its frame, so they extend one run; a byte with no room left in the run queue or the
    // byte ring is refused and counted, as a graph queue's overflow is.
    void pushSerialIn(std::uint32_t frame, std::uint8_t byte) override {
        if (!serialIn_.empty() && serialIn_.back().offset == frame && serialBytes_.size() < serialBytes_.capacity()) {
            serialBytes_.push(byte);
            ++serialIn_.back().count;
            ++serialInAccepted_;
            return;
        }
        if (serialIn_.size() < serialIn_.capacity() && serialBytes_.size() < serialBytes_.capacity() &&
            serialIn_.push({frame, 1})) {
            serialBytes_.push(byte);
            ++serialInAccepted_;
            return;
        }
        ++serialInDropped_;
    }
    // What this core's ingress queues have taken and dropped since it was built (SystemBase::inputAccepted /
    // inputDropped): serial-in bytes and button presses. A plan's block fits the queues, so a drop is a push outside
    // one: a core fed by a test or tool, or a press burst scheduled past the queue's carry.
    std::uint64_t inputAccepted() const override { return serialInAccepted_ + pressesAccepted_; }
    std::uint64_t inputDropped() const override { return serialInDropped_ + pendingButtons_.dropped(); }
    // The serial-in bytes the queues have taken since the core was built: the non-press half of inputAccepted(), for a
    // test that holds a staged serial run reached the core rather than only a press.
    std::uint64_t serialInAccepted() const { return serialInAccepted_; }
    // The byte the serial queue would deliver next. False, touching nothing, when it holds none: the queued runs only
    // name their bytes' offsets, so this is the test seam that holds a serial input reached the core.
    bool peekSerialByte(std::uint8_t& byte) const {
        if (serialBytes_.empty()) return false;
        byte = serialBytes_.front();
        return true;
    }

    FrameBufferTriple* framebuffer() override { return &frames_; }

    // MemoryType → GB_DIRECT_ACCESS_* mapping. Returns invalid for
    // NametableRam / ExtWorkRam (NES / GBA only). For ROM/SRAM the returned
    // accessor spans the full backing buffer; the current banking window
    // is not reflected.
    rp::MemoryAccessor getMemory(rp::MemoryType type, rp::AccessType access) override;
    // The control plane's capabilities (SystemCapabilities.hpp): the work RAM, a battery dumped from the registry, and
    // the ROM this core keeps.
    rp::MemoryCapability* memory() override { return &memoryCap_; }
    rp::SramCapability*   sram() override { return &sramCap_; }
    rp::RomCapability*    rom() override { return &romCap_; }

    // -- CPU state (SystemBase virtuals; see base for contracts) -------------
    //
    // Live SM83 state, valid only while activated (gb_ != nullptr). SameBoy
    // supports the full set; runUntilPc is provided by the base class.
    std::vector<rp::CpuRegister> getCpuRegisters() const override;
    bool setCpuRegister(std::string_view name, std::uint32_t value) override;
    std::optional<std::uint32_t> getProgramCounter() const override;
    std::optional<std::uint8_t>  readCpuByte(std::uint32_t addr) const override;
    std::uint64_t                stepInstruction() override;

    // The SystemBase per-block triad (see base for the contract). A coupled link
    // group's members step round-robin (the executor's coupled group) so GB_run()
    // interleaves and serial bits ferry mid-block; an unlinked system is the
    // 1-member case of the same triad, driven by its own node.
    ExpSmoother& gainSmoother() noexcept { return gainSmoother_; }
    // Sizes the per-block accumulators for the largest block a node reserved for, so a block only ever zeroes them;
    // a block that had to grow one would allocate on the block thread (docs/modular-graph-phase-c4-design.md
    // section 4). Called at activation and when a host grows its block, never inside a block.
    void reserveAudio(std::uint32_t maxBlockFrames) override;
    void prepareForBlock(const AudioBlockInfo& info) override;
    bool stepIfBelowTarget(std::uint32_t framesNeeded) override;
    void finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) override;

    // The Game Boy's four sound channels as separate stereo streams (Pulse 1,
    // Pulse 2, Wave, Noise, in GB_channel_t order). Inert until the plan wires the
    // node's ch1..ch4 ports: with only mix wired, finishBlock keeps receiving the
    // 2-lane mix. See the per-channel tap below.
    std::vector<ChannelStream> channelLayout() const override;

    // Set the per-system gain target (dB). Smoothed at audio rate inside finishBlock so live edits don't click.
    // The block thread calls it, as a system node applies its params there, and it allocates nothing (a test
    // holds that); it is not callable from another thread.
    void setGainDb(float dB, bool immediate = false) override;

    // Tear down `gb_` and rebuild it with the current `config_`. Snapshots
    // SRAM through `config_.sram` so it survives the cycle; clears
    // `config_.savestate` (model-specific). Called by setModel, on the
    // block thread, when the user changes Model from the menu: the allocations
    // inside `GB_init` are the same ones a fresh Load Project does, so
    // accepting them on the audio thread for an explicit user action is
    // consistent with that precedent.
    void restartEmulator();

    // Push `config_.highpass` into the SameBoy core. Safe to call at runtime
    // (the filter samples its mode every audio frame). Called from
    // `onActivate` and from setHighpass.
    void applyHighpassMode();

    // Push the whole `config_` display group (colour correction, DMG palette, light temperature,
    // background/object rendering) into the core. Safe to call at runtime — every one lands on the
    // next rendered frame, so no restart. Called from `onActivate` and from the display setters below.
    void applyDisplayConfig();

    // The live "sameboy" role knobs, as the Game Boy's system node applies its settings. Each is value-guarded, a
    // same-value call doing nothing, since the node applies every knob its settings carry when it takes the core: a
    // model change restarts the core (restartEmulator), highpass re-applies the filter mode, and the three display
    // knobs re-apply the display group, none of those restarting anything. setFastBoot and setSerialOutCapture are
    // the other two.
    void setModel(SameBoyModel model);
    void setHighpass(SameBoyHighpass highpass);
    void setColorCorrection(SameBoyColorCorrection mode);
    void setDmgPalette(SameBoyDmgPalette palette);
    void setLightTemperature(double temperature);

    // SystemBase virtuals — see base class for contracts.
    void                      setFastBoot(bool on) override     { config_.fastBoot = on; }
    std::vector<std::uint8_t> saveSramBytes() const override;
    std::vector<std::uint8_t> saveStateBytes() const override;
    std::size_t               stateSnapshotSize() const override;
    bool                      captureStateSnapshot(std::vector<std::uint8_t>& dst) override;
    StateRegionTable          stateSnapshotRegions() const override;

    // Internal hooks invoked from the C callbacks (made public so the
    // free-function trampolines can reach them; not part of the public API).
    void writeAudioSample(int16_t left, int16_t right);
    // Per-channel tap: `samples` is 8 interleaved int16 (l0,r0,l1,r1,l2,r2,l3,r3),
    // one stereo pair per GB channel, captured in the same render() tick as the
    // mixed sample. The GB_sample_t unpack lives in the .cpp trampoline so this
    // header stays free of <apu.h>.
    void writeChannelSamples(const int16_t* samples);
    void onVblank();
    void serialBitReceived(bool bit);
    bool serialBitFromPeer() const;
    void serialBroadcastBit() const;

    // True when serial-out capture is armed, in either mode. The per-bit serial
    // callback reads this to decide whether to accumulate LSDJ's outgoing bytes.
    bool serialOutCaptureEnabled() const { return serialOutCapture_ != SerialOutCapture::Off; }
    SerialOutCapture serialOutCapture() const { return serialOutCapture_; }
    // Set serial-out capture's mode. The Game Boy's system node drives it
    // through this, from its serialOutCapture setting (TS picks it from the mode
    // the system's lsdj-sync node runs: Drive for MI.OUT, Listen for master
    // sync). Stays set across a model-change restart (onActivate leaves it). A
    // switch drops the half-assembled byte: Drive clocks continuously, so a
    // switch lands mid-byte 7 times in 8, and the old mode's bits would leave
    // every later byte straddling two transfers. A same-mode call keeps them:
    // MI.OUT's partial byte must survive a call that changes nothing.
    void setSerialOutCapture(SerialOutCapture mode) {
        if (mode != serialOutCapture_) {
            serialOutByte_ = 0;
            serialOutBits_ = 0;
        }
        serialOutCapture_ = mode;
    }
    void captureSerialOutBit(bool bit);

    // TODO: Not a fan of the public vars

    // Fields accessed by the C callbacks. Public for callback access only.
    SameBoyConfig             config_;
    std::vector<std::uint8_t> rom_;
    // rom_ is never changed after construction, which is what lets romCap_ hand its bytes to any thread. Its title:
    // the cartridge title, $0134-$0143.
    rp::CoreMemory memoryCap_{*this};
    rp::CoreSram   sramCap_;
    rp::CoreRom    romCap_{rom_, 0x134, 16};
    GB_gameboy_t_fwd*         gb_ = nullptr;
    FrameBufferTriple         frames_{sameboy::kPixelWidth, sameboy::kPixelHeight};

    // Stereo accumulator, sized to 2 * the plan's block at activation (or the default outside a plan) and on a larger
    // reserve (sizePerBlockBuffers); prepareForBlock still grows it for a block bigger than any reserve asked for.
    std::vector<float>        stereoAccum_;
    std::uint32_t             audioFrameCount_ = 0;

    // Per-channel audio tap: one interleaved-stereo accumulator per GB channel
    // (Pulse 1, Pulse 2, Wave, Noise), filled every block from the SameBoy
    // per-channel callback alongside stereoAccum_ and sized/zeroed with it in
    // prepareForBlock. Consumed by finishBlock only when the node hands us 8 lanes
    // (its stem ports are wired); otherwise carried along at negligible cost.
    static constexpr std::size_t kAudioChannelCount = 4;
    std::array<std::vector<float>, kAudioChannelCount> chanAccum_;

    bool                      activated_  = false;
    double                    sampleRate_ = 44100.0;

    // Pending button transitions, ordered by sample offset. Spread across each block so a press+release pair doesn't
    // collapse to zero duration (which the SameBoy joypad debouncer would miss): each press is scheduled 10 ms after
    // the one before it, so what a block adds can still be queued several blocks later. The bound is a block's buttons
    // input sixteen times over (about ten seconds of scheduled presses at 48 kHz), and a press beyond it is refused
    // and counted rather than growing the queue on the block thread.
    struct PendingButton {
        std::uint32_t offset;   // samples from block start
        GameboyButton button;   // reinterpreted from the uint8_t opcode
        bool          down;
    };
    rp::FixedQueue<PendingButton> pendingButtons_{rp::kButtonsInput * 16};
    static_assert(rp::kButtonsInput * 16 >= rp::kPressInput, "a Game Boy's press queue must hold both doors");
    std::uint64_t                 pressesAccepted_ = 0;

    // Default per-press duration (samples) used to space queued transitions.
    // Mirrors the old code's "10 ms at sampleRate" default.
    std::uint32_t buttonSpacingSamples_ = 0;

    // Serial-link state. Empty => standalone (no linking). The plan's `link` wires name the peers, and the system
    // node's set_peers (host/graph/SystemNodes.cpp) fills this from them at adoption: the other members of its coupled
    // group, which the graph steps in lockstep. Each peer's core is owned by its own node, which outlives the plan
    // that pairs them.
    std::vector<SameBoySystem*> linkPeers_;
    bool                        bitToSend_ = false;

    // Bytes queued by roles / host MIDI-in for the GB serial port, each run of them (a message's bytes share its
    // frame) tagged with the sample offset it should not be delivered before. Drained one bit at a time (MSB first) by
    // `nextSerialInBit()` from the SameBoy serial-end callback, or whole-byte by the slave-mode pump in
    // stepIfBelowTarget(), both gated on `offset <= audioFrameCount_` so a burst of host MIDI keeps its
    // intra-block timing instead of collapsing to the block start. `serialBitsRemaining_` counts how many bits are left
    // in the current front byte (8 → fresh byte, 0 → next call pops & reloads).
    //
    // The bytes are held in a fixed ring and the runs in a fixed queue, each sized below to two blocks of a script
    // serialIn input or one of a Game Boy serialOut (the larger single block), so a block's MIDI never grows one: a run
    // is a message's bytes at one frame (a slot per distinct frame), and the ring holds their bytes
    // (docs/modular-graph-phase-c4-design.md section 11).
    struct PendingSerial {
        std::uint32_t offset;   // samples from block start
        std::uint32_t count;    // bytes at this offset, front-first in serialBytes_
    };
    // Two blocks of a script serialIn stream, or one of a Game Boy serialOut (8256, the larger single block), in
    // runs; the same two blocks' bytes in the ring, or a serialOut block's. See InputCapacity.hpp.
    static constexpr std::size_t kSerialRuns =
        rp::kSerialOutInput.messages > 2 * rp::kSerialInInput.messages
            ? rp::kSerialOutInput.messages
            : 2 * rp::kSerialInInput.messages;
    static constexpr std::size_t kSerialBytes =
        rp::kSerialOutInput.bytes > 2 * rp::kSerialInInput.bytes
            ? rp::kSerialOutInput.bytes
            : 2 * rp::kSerialInInput.bytes;
    rp::FixedQueue<PendingSerial> serialIn_{kSerialRuns};
    rp::FixedByteRing             serialBytes_{kSerialBytes};
    std::uint64_t                 serialInAccepted_   = 0;
    std::uint64_t                 serialInDropped_    = 0;
    int                           serialBitsRemaining_ = 0;

    // Outgoing-serial byte accumulator. Bits arrive MSB-first from the GB;
    // on every 8th bit the assembled byte is appended to serialOutLog_ for the
    // host to drain (LSDJ MI.OUT → host MIDI).
    std::uint8_t serialOutByte_ = 0;
    int          serialOutBits_ = 0;
    SerialOutCapture serialOutCapture_ = SerialOutCapture::Off; // set via setSerialOutCapture()

    // Every serial-out byte completed while capture is armed, keyed by the
    // in-block sample offset at byte-completion time (audioFrameCount_, which
    // can run past the block by the core's overshoot). The Game Boy node's
    // serialOut port carries it each block (host/graph/SystemNodes.cpp).
    // Cleared at block boundaries inside prepareForBlock.
    std::vector<std::pair<std::uint32_t, std::uint8_t>> serialOutLog_;

private:
    // Size stereoAccum_ and chanAccum_ for `frames`, growth only: a smaller block keeps the memory the larger one took.
    void sizePerBlockBuffers(std::uint32_t frames);
    // Take the front byte of the serial queue (its run loses one, and the run goes when its last byte has).
    void popSerialByte();

    ExpSmoother gainSmoother_;
};
