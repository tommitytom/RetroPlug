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
#include "util/ExpSmoother.hpp"

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

    // Build runtime `RomRole` instances from `config_.roles`. Called once at
    // the end of onActivate (after the sniffer has had a chance to fill in
    // defaults). Idempotent: clears `roles_` first.
    void instantiateRoles();

    // Pop the next bit (MSB-first) from `serialIn_` and return it. Returns
    // `true` (idle high) when the queue is empty. Called from the SameBoy
    // serial-end callback in standalone mode.
    bool nextSerialInBit();

    // Append the routed events to `pendingMidi_` (cleared at the top of each
    // onProcess) and fan them out to every attached role. Roles that care
    // about timing read MidiEvent::frame from the events themselves.
    void onMidi(const ::MidiEvent* events, std::uint32_t count) override;

    // Queue a button transition. Called from DSP-thread command-drain at the
    // top of each block. The byte is reinterpreted as GameboyButton; pending
    // transitions are spread across the block in applyPending() (port of old
    // SameBoyUtil.cpp:149-163).
    void pressButton(std::uint8_t button, bool down) override;

    FrameBufferTriple* framebuffer() override { return &frames_; }

    // MemoryType → GB_DIRECT_ACCESS_* mapping. Returns invalid for
    // NametableRam / ExtWorkRam (NES / GBA only). For ROM/SRAM the returned
    // accessor spans the full backing buffer; the current banking window
    // is not reflected.
    rp::MemoryAccessor getMemory(rp::MemoryType type, rp::AccessType access) override;

    // -- CPU state (SystemBase virtuals; see base for contracts) -------------
    //
    // Live SM83 state, valid only while activated (gb_ != nullptr). SameBoy
    // supports the full set; runUntilPc is provided by the base class.
    std::vector<rp::CpuRegister> getCpuRegisters() const override;
    bool setCpuRegister(std::string_view name, std::uint32_t value) override;
    std::optional<std::uint32_t> getProgramCounter() const override;
    std::optional<std::uint8_t>  readCpuByte(std::uint32_t addr) const override;
    std::uint64_t                stepInstruction() override;

    SystemConfig snapshotConfig() const override;

    // Per-block driver split out of onProcess so LinkGroup can interleave
    // GB_run() across linked peers. Standalone path: onProcess() calls
    // prepareForBlock → spin stepIfBelowTarget → finishBlock. Linked path:
    // LinkGroup does the same but interleaves stepIfBelowTarget across all
    // members.
    ExpSmoother& gainSmoother() noexcept { return gainSmoother_; }
    void prepareForBlock(const AudioBlockInfo& info);
    bool stepIfBelowTarget(std::uint32_t framesNeeded);
    void finishBlock(const AudioBlockInfo& info, float* const* outs);

    // Set the per-system gain target (dB). Smoothed at audio rate inside
    // finishBlock so live edits don't click. Not realtime-safe to call from
    // the audio thread, but a simple atomic store would make it so if needed.
    void setGainDb(float dB);

    // Tear down `gb_` and rebuild it with the current `config_`. Snapshots
    // SRAM through `config_.sram` so it survives the cycle; clears
    // `config_.savestate` (model-specific). Called from the DSP command
    // drain when the user changes Model from the menu — the allocations
    // inside `GB_init` are the same ones a fresh Load Project does, so
    // accepting them on the audio thread for an explicit user action is
    // consistent with that precedent.
    void restartEmulator();

    // Push `config_.highpass` into the SameBoy core. Safe to call at runtime
    // (the filter samples its mode every audio frame). Called from
    // `onActivate` and from the SetHighpass command handler.
    void applyHighpassMode();

    // SystemBase virtuals — see base class for contracts.
    const std::string&        romPath() const override          { return config_.romPath; }
    std::optional<bool>       fastBoot() const override         { return config_.fastBoot; }
    void                      setFastBoot(bool on) override     { config_.fastBoot = on; }
    bool                      wantsRomReload() const override   { return config_.reloadOnRomChange; }
    void                      setRomReload(bool on) override    { config_.reloadOnRomChange = on; }
    void                      clearSram() override;
    std::vector<std::uint8_t> saveSramBytes() const override;
    bool                      loadSramBytes(const std::vector<std::uint8_t>& bytes) override;
    std::vector<std::uint8_t> saveStateBytes() const override;
    bool                      loadStateBytes(const std::vector<std::uint8_t>& bytes) override;
    std::size_t               stateSnapshotSize() const override;
    bool                      captureStateSnapshot(std::vector<std::uint8_t>& dst) override;
    StateRegionTable          stateSnapshotRegions() const override;
    std::unique_ptr<SystemBase> clone(SystemId newId, double sampleRate) const override;
    std::unique_ptr<SystemBase> cloneFromState(SystemId newId, double sampleRate,
                                               const std::vector<std::uint8_t>& savestate) const override;

    // Internal hooks invoked from the C callbacks (made public so the
    // free-function trampolines can reach them; not part of the public API).
    void writeAudioSample(int16_t left, int16_t right);
    void onVblank();
    void serialBitReceived(bool bit);
    bool serialBitFromPeer() const;
    void serialBroadcastBit() const;

    // True when any attached role wants LSDJ's outgoing serial bytes (e.g.
    // the Arduinoboy MI.OUT decoder). Cached on each instantiateRoles() so
    // the per-bit serial callback doesn't re-walk roles_.
    bool serialOutCaptureEnabled() const { return serialOutEnabled_; }
    void captureSerialOutBit(bool bit);

    // TODO: Not a fan of the public vars

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
        GameboyButton button;   // reinterpreted from the uint8_t opcode
        bool          down;
    };
    std::deque<PendingButton> pendingButtons_;

    // Default per-press duration (samples) used to space queued transitions.
    // Mirrors the old code's "10 ms at sampleRate" default.
    std::uint32_t buttonSpacingSamples_ = 0;

    // Serial-link state. Empty => standalone (no linking). Populated by
    // Project::rebuildLinkGroups when this system shares a nonzero
    // SameBoyConfig::linkGroupId with one or more peers. Pointers are owned
    // by Project; lifetime matches the linked peers' lifetimes inside the
    // same Project. See LinkGroup.hpp for the runtime stepping policy.
    std::vector<SameBoySystem*> linkPeers_;
    bool                        bitToSend_ = false;

    std::vector<std::unique_ptr<RomRole>> roles_;

    // MIDI events that landed on this system in the current block. Cleared
    // at the top of onProcess; drained by roles inside onProcessBlock.
    std::vector<::MidiEvent> pendingMidi_;

    // Bytes queued by roles for the GB serial port. Drained one bit at a
    // time (MSB first) by `nextSerialInBit()` from the SameBoy serial-end
    // callback. `serialBitsRemaining_` counts how many bits are left in the
    // current front byte (8 → fresh byte, 0 → next call pops & reloads).
    std::deque<std::uint8_t> serialIn_;
    int                      serialBitsRemaining_ = 0;

    // Outgoing-serial byte accumulator. Bits arrive MSB-first from the GB;
    // on every 8th bit we fan the assembled byte out via
    // RomRole::onSerialOutByte for the master-mode decoder.
    std::uint8_t serialOutByte_ = 0;
    int          serialOutBits_ = 0;
    bool         serialOutEnabled_ = false; // cached from roles_ in instantiateRoles()

    // Diagnostic raw-byte log. Every completed serial-out byte while
    // serialOutEnabled_ is also appended here keyed by the in-block
    // sample offset at byte-completion time (approximated by
    // audioFrameCount_). The CLI drains this per block into a per-system
    // raw byte log so master-mode verification has ground truth even
    // when the byte→MIDI decoder doesn't yet translate a given LSDJ
    // output value. Cleared at block boundaries inside prepareForBlock.
    std::vector<std::pair<std::uint32_t, std::uint8_t>> serialOutLog_;

private:
    ExpSmoother gainSmoother_;
};
