#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "system/SystemBase.hpp"
#include "system/SystemCapabilities.hpp"
#include "system/blastem/BlastemAudio.hpp"
#include "system/blastem/BlastemMdConfig.hpp"
#include "system/blastem/BlastemVideo.hpp"  // VideoSink (a member here), pixel_t and vdp_context
#include "transport/FrameBufferTriple.hpp"
#include "util/ExpSmoother.hpp"
#include "util/Gain.hpp"  // dbToLin

struct genesis_context;

namespace rp::blastem {

class PortDevice;

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

    // The frame the engine reads: the video sink's triple, allocated at boot, or null before one exists.
    FrameBufferTriple* framebuffer() override { return video_.triple(); }

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

    // Runs the two lines genesis.c runs around vdp_test_port_write when the VDP test register's bit 0 changes the Z80's
    // clock source, so a case can move a chip's native rate without a register write whose handler is genesis.c's.
    void switchPsgClock(bool fast);

    // The absolute master clock of host sample `sample`, in this instance's time base.
    std::uint64_t masterClockOf(std::uint64_t sample) const;

    // The absolute number of master clocks the machine has run: gen->cycle_base + the 68k's 32-bit cycle count. A
    // test seam (a stop is checked against masterClockOf), and what the port device stamps from.
    std::uint64_t mclkNow() const;

private:
    void boot();
    void destroyCore();
    void run_until(std::uint64_t mclk);

    BlastemMdConfig           config_;
    // The ROM as a capability view; BlastEm gets its own aligned copy (see boot()) and owns it from then on.
    std::vector<std::uint8_t> rom_;
    // Declared before gen_ because the VDP holds raw pointers into its buffers, and the YM2612 and the PSG hold raw
    // pointers to their audio sources: declared first, a sink is destroyed after the context is freed, and a context
    // freed first never reads them (freeing it reaches the sink through render_free_source).
    VideoSink                 video_;
    AudioSink                 audio_;
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

    // The block's drained mix, before the gain: sized at activation and at every reserveAudio growth, never reallocated
    // on the block thread (the engine's [alloc] cases count a block's allocations, emulation included).
    std::vector<float> mixLeft_;
    std::vector<float> mixRight_;
};

}  // namespace rp::blastem
