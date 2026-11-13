#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "system/SystemBase.hpp"
#include "system/SystemCapabilities.hpp"
#include "system/blastem/BlastemAudio.hpp"
#include "system/blastem/BlastemMdConfig.hpp"
#include "system/blastem/BlastemMemory.hpp"      // MdMemory (a member here), WorkRamView
#include "system/blastem/BlastemPortDevice.hpp"  // PortDevice (a member here)
#include "system/blastem/BlastemVideo.hpp"       // VideoSink (a member here), pixel_t and vdp_context
#include "transport/FrameBufferTriple.hpp"
#include "util/ExpSmoother.hpp"
#include "util/Gain.hpp"  // dbToLin

struct genesis_context;

namespace rp::blastem {

// A Sega Mega Drive (or Genesis), run by BlastEm 1.0. One instance owns one `genesis_context`; every call into the
// core sets `currentInstance` first, because BlastEm's frontend callbacks take no instance of their own.
//
// The clock model is section 2 of the design: a 64-bit absolute master clock, host sample n at rate R being master
// clock floor(n * MCLK / R). MCLK is the core's own `gen->master_clock`, read after boot.
class BlastemMdSystem final : public SystemBase {
public:
    BlastemMdSystem(SystemId id, BlastemMdConfig config, std::vector<std::uint8_t> romBytes);
    ~BlastemMdSystem() override;

    void onActivate(double sampleRate) override;
    void onDeactivate() override;
    void onSampleRateChanged(double sampleRate) override;

    void prepareForBlock(const AudioBlockInfo& info) override;
    bool stepIfBelowTarget(std::uint32_t framesNeeded) override;
    void finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) override;
    void reserveAudio(std::uint32_t maxBlockFrames) override;

    // The wire-byte table's button, as InputTypes.hpp orders it.
    void pressButton(std::uint8_t button, bool down) override;
    void setGainDb(float dB, bool immediate = false) override;

    // Controller port 2's host device: one LEVEL per byte of `data`, each held from the absolute master clock of
    // `frame`'s sample (the intra-block offset) on. `flush` is accepted and ignored, as it is on the SMS's sync role:
    // a held level has no undelivered-stream hazard to clear, and dropping one on a seek would only leave a stale pin.
    void pushCoreBytes(std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool flush = false) override;
    // True whether or not a role is wired to the port: the queue exists either way, and an unwired port simply never
    // has anything in it (as MesenSmsSystem's answer is).
    bool hasCoreBytesIn() const override { return true; }

    // The frame the engine reads: the video sink's triple, allocated at boot, or null before one exists.
    FrameBufferTriple* framebuffer() override { return video_.triple(); }

    // -- The control plane's memory views --------------------------------------
    //
    // Ram is the 68k's 64 KiB in ADDRESS order (a copy: BlastEm's storage is a word array), ExtWorkRam is the Z80's own
    // 8 KiB, and Sram is the cart's battery as BlastEm stores it. Anything else is the invalid accessor the base returns.
    rp::MemoryAccessor getMemory(rp::MemoryType type, rp::AccessType access) override;
    // BlastEm's own battery, byte for byte, so an odd-byte cart keeps its compacted layout: byte `i` of the result is
    // address $200001 + 2i (design section 5). Empty for a cart with no battery.
    std::vector<std::uint8_t> saveSramBytes() const override;

    // The three capabilities (SystemCapabilities.hpp). The memory one is this package's own because of the work RAM's
    // word order; the battery and the ROM are the generic ones over getMemory and rom_.
    rp::MemoryCapability* memory() override { return &memoryCap_; }
    rp::SramCapability*   sram() override { return &sramCap_; }
    rp::RomCapability*    rom() override { return &romCap_; }

    // True once onActivate has booted a live core: a ROM the gate accepted can still fail to boot, and the backend
    // then rejects the build rather than adopting a dead system (as MesenSmsSystem::activated).
    bool activated() const { return activated_; }

    const BlastemMdConfig& config() const { return config_; }
    // The core's master clock after boot (53,693,175 NTSC, 53,203,395 PAL). 0 before activation.
    std::uint32_t masterClock() const;

    // The picture's frame size: 320 wide (the widest mode) by the region's 224 or 240 lines. The frame is allocated
    // once because the shell sizes its frame slot at claim and never resizes it, so a mode change moves the
    // picture inside the frame rather than the frame.
    std::uint32_t coreFrameWidth() const;
    std::uint32_t coreFrameHeight() const;

    // The core, for this package's own tests only: the sibling screens (audio, the port device) read BlastEm's state
    // directly rather than growing a seam apiece.
    genesis_context* core() const { return gen_; }
    // The VDP, for the render callbacks: a published frame's picture is placed by the VDP's own mode registers, and
    // null before a core exists.
    vdp_context* vdp() const { return gen_ ? gen_->vdp : nullptr; }
    // The instance's framebuffer sink: the render callbacks hand it the buffer the VDP drew into (BlastemShim.cpp).
    VideoSink& video() { return video_; }
    // The instance's audio sink: render_audio.h's source entry points feed it (BlastemShim.cpp) and finishBlock drains
    // it.
    AudioSink& audio() { return audio_; }
    // Host samples the sink ran dry on since activation: 0 on a driven core, and the audio cases assert it.
    std::uint64_t starvedSamples() const { return audio_.starvedSamples(); }
    // Blocks the engine handed over larger than the reservation they were sized for, since activation. The contract says
    // it never does; such a block is mixed up to the reservation and the rest of the lanes are left alone, which is a
    // short block instead of a read past the mix's end.
    std::uint64_t clampedBlocks() const { return clampedBlocks_; }

    // Runs the two lines genesis.c runs around vdp_test_port_write when the VDP test register's bit 0 changes the Z80's
    // clock source, so a case can move a chip's native rate without a register write whose handler is genesis.c's.
    void switchPsgClock(bool fast);

    // The absolute master clock of host sample `sample`, in this instance's time base.
    std::uint64_t masterClockOf(std::uint64_t sample) const;

    // The absolute number of master clocks the machine has run: gen->cycle_base + the 68k's 32-bit cycle count. A
    // test seam (a stop is checked against masterClockOf), and what the port device stamps from.
    std::uint64_t mclkNow() const;

    // The same clock a port access carried into the core (BlastEm's own 32-bit count) in absolute terms: the port
    // device's schedule is queued in this time base, so a read and the level due at it meet in absolute time.
    std::uint64_t mclkOfCoreCycle(std::uint32_t cycle) const {
        return gen_ ? std::uint64_t(gen_->cycle_base) + cycle : 0;
    }

    // This instance's port-2 device. The core reaches it the same way (through currentInstance in the entry point), so
    // this is the host's own handle: the seam a feed writes through is pushCoreBytes, not this.
    PortDevice& portDevice() { return port_; }
    // Levels the port device has taken since activation: how a caller tells a delivery from a queue that refused.
    std::uint64_t levelsAccepted() const { return port_.accepted(); }

    // Test seams for the direction rule: BlastEm's own io_control_write / io_data_write on port 2 at the current clock.
    // Those writes ARE the ROM's direction register ($A1000B) and data latch ($A10005) that io_data_read's tail mixes
    // with the host's level, so the host never sees them and a case cannot reach them any other way.
    void writePortControl(std::uint8_t value);
    void writePortData(std::uint8_t value);

private:
    void boot();
    void destroyCore();
    // Refills ramView_ with the 68k's work RAM in address order and returns it: the view every getMemory(Ram) hands out.
    // Empty (so an accessor reports itself invalid) once no core owns a work RAM to read.
    std::span<const std::uint8_t> workRamView();
    void run_until(std::uint64_t mclk);
    // Bring the YM2612 and the PSG to the block's end clock (or to where the 68k actually got to, whichever is less)
    // before the block is drained. A stop is only checked at the next sync, so a run can return with the chips short of
    // the 68k: finishBlock would then read audio the chips have not made.
    void syncSoundTo(std::uint64_t mclk);

    BlastemMdConfig           config_;
    // The ROM as a capability view; BlastEm gets its own aligned copy (see boot()) and owns it from then on.
    std::vector<std::uint8_t> rom_;
    // rom_ never changes after construction, which is what lets romCap_ hand its bytes to any thread. A Mega Drive's
    // title is its 48-byte domestic name at $120.
    MdMemory     memoryCap_{*this};
    rp::CoreSram sramCap_{*this};
    rp::CoreRom  romCap_{rom_, 0x120, 0x30};
    // The work RAM in address order: one buffer, kept at the region's size, refilled by workRamView() on every ask.
    std::vector<std::uint8_t> ramView_;
    // The sinks must still exist when the context is freed: that free hands the chips' audio sources back through
    // render_free_source. The destructor's own body does it first (destroyCore), before any member is destroyed, so the
    // DECLARATION ORDER below is not what keeps them alive and moving them would change nothing.
    VideoSink                 video_;
    AudioSink                 audio_;
    // Port 2 as the host drives it. Its queue and its sort scratch are sized at construction, before any block runs.
    PortDevice                port_;
    genesis_context*          gen_ = nullptr;
    bool                      activated_ = false;
    bool                      stateRefused_ = false;  // a seed state the serializer refused
    double                    sampleRate_ = 44100.0;
    ExpSmoother               gainSmoother_;

    // The absolute host sample the next block's first frame is, and the master clock that sample was mapped from. A
    // rate change re-anchors here, so every host sample already scheduled keeps the master clock it was given.
    std::uint64_t sampleBase_ = 0;
    std::uint64_t sampleAnchor_ = 0;
    std::uint64_t mclkAnchor_ = 0;
    std::uint32_t blockFrames_ = 0;  // the block prepareForBlock was told about
    // The first host sample of the grid the next stepIfBelowTarget runs: the stop schedule is a fixed one-sample grid
    // (absolute time), so a host block's partition cannot move a stop. See stepIfBelowTarget.
    std::uint64_t nextGridSample_ = 0;
    // Blocks the engine handed over larger than the reservation: finishBlock cuts them to it and counts them here.
    std::uint64_t clampedBlocks_ = 0;

    // The block's drained mix, before the gain: sized at activation and at every reserveAudio growth, never reallocated
    // on the block thread (the engine's [alloc] cases count a block's allocations, emulation included).
    std::vector<float> mixLeft_;
    std::vector<float> mixRight_;
};

}  // namespace rp::blastem
