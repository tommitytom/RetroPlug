#include "system/blastem/BlastemMdSystem.hpp"

#include <algorithm>

#include "system/blastem/BlastemCore.hpp"
#include "system/blastem/BlastemRom.hpp"

namespace rp::blastem {

static std::uint8_t blastemRegion(std::uint32_t region) {
    switch (static_cast<MdRegion>(region)) {
        case MdRegion::NtscUs: return REGION_U;
        case MdRegion::NtscJp: return REGION_J;
        case MdRegion::Pal:    return REGION_E;
        case MdRegion::Auto:
        default:               return 0;  // the header's own region (set_region's region == 0)
    }
}

BlastemMdSystem::BlastemMdSystem(SystemId id, BlastemMdConfig config, std::vector<std::uint8_t> romBytes)
    : SystemBase(id), config_(std::move(config)), rom_(std::move(romBytes)) {}

BlastemMdSystem::~BlastemMdSystem() { destroyCore(); }

std::uint32_t BlastemMdSystem::masterClock() const { return gen_ ? gen_->master_clock : 0; }

std::uint64_t BlastemMdSystem::masterClockOf(std::uint64_t sample) const {
    if (sample <= sampleAnchor_) return mclkAnchor_;
    const std::uint64_t mclk = gen_ ? gen_->master_clock : 53693175u;
    // floor(n * MCLK / R) in exact integer arithmetic, from the anchor rather than from zero, so a rate change only
    // re-anchors and never introduces a rounding step (design section 2).
    return mclkAnchor_ + ((sample - sampleAnchor_) * mclk) / static_cast<std::uint64_t>(sampleRate_ + 0.5);
}

void BlastemMdSystem::onActivate(double sampleRate) {
    // A second activation on a live object keeps the machine. boot() would overwrite gen_, the only pointer to the
    // context, and so leak it whole with its aligned cart while silently dropping the machine's state; the sibling
    // backends guard the same way.
    if (activated_) return;
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    gainSmoother_.setSampleRate(static_cast<float>(sampleRate_));
    // Sized for a core built outside a plan (a test, a tool), which reserves nothing: the engine's default plan block.
    const std::uint32_t frames = blockFrames(SystemBase::kDefaultBlockFrames);
    mixLeft_.resize(frames);
    mixRight_.resize(frames);
    audio_.prepare(frames);
    audio_.setHostRate(sampleRate_);  // before the chips exist: they ask for their source during alloc_config_genesis
    boot();
    gainSmoother_.setTargetValue(dbToLin(config_.gainDb));
    gainSmoother_.clearToTargetValue();  // the first value is the node's, not a ramp from unity
    SystemBase::reserveAudio(frames);    // LAST, per its contract
}

void BlastemMdSystem::boot() {
    if (!isMdRom(rom_)) return;
    // The callbacks BlastEm makes while allocating (render_audio_source, render_set_video_standard,
    // render_get_framebuffer) belong to this instance: the shim and the sinks read this thread's pointer.
    currentInstance = this;
    void* cart = blastemRomCopy(rom_);
    if (!cart) {
        // An allocation failure is a refused build, not a crash: alloc_config_genesis byteswaps the cart in place.
        return;
    }
    // BEFORE alloc_config_genesis: the VDP asks render_get_framebuffer for its buffer at init (vdp.c:524) and points
    // context->output into it immediately (vdp.c:529-530), so a sink with no buffers yet is a null write on the first
    // line. `headless` stays 0 and is never written anywhere: vdp.c reads it at init, at free and at every frame end, so
    // a value that changed between those three would leak or free a buffer the sink still owns.
    video_.prepareBuffers();
    gen_ = alloc_config_genesis(cart, static_cast<std::uint32_t>(rom_.size()), nullptr, 0, 0,
                                blastemRegion(config_.region));
    if (!gen_) {
        aligned_free(cart);
        return;
    }
    m68k_reset(gen_->m68k);
    // The frame, once, for the region the core just chose: 240 lines on a PAL machine, 224 on an NTSC one (version_reg's
    // HZ50 bit, set by set_region during the allocation above). The shell's frame slot is sized at claim and never
    // resized, so this is the only place its size is decided.
    video_.prepareFrame((gen_->version_reg & 0x40) ? 240u : 224u);
    sampleBase_ = 0;
    sampleAnchor_ = 0;
    mclkAnchor_ = 0;
    activated_ = true;
    setGainDb(config_.gainDb, true);
}

// The frame the sink allocated IS the picture's size from boot on. Before a core exists the region is unknown, so the
// answer is the frame a boot defaults to (the NTSC one), not zero: a caller that sizes anything off these reads a
// picture the machine could actually produce.
std::uint32_t BlastemMdSystem::coreFrameWidth() const { return video_.triple() ? video_.width() : 320u; }

std::uint32_t BlastemMdSystem::coreFrameHeight() const {
    // The region's visible lines (vdp.c's NTSC_INACTIVE_START and PAL_INACTIVE_START); prepareFrame() allocated the
    // frame from version_reg's HZ50 bit, so the triple's height is already the region's answer.
    return video_.triple() ? video_.height() : 224u;
}

std::uint64_t BlastemMdSystem::mclkNow() const {
    return gen_ ? static_cast<std::uint64_t>(gen_->cycle_base) + gen_->m68k->cycles : 0;
}

void BlastemMdSystem::destroyCore() {
    if (gen_) {
        currentInstance = this;
        // Frees the aligned ROM copy too, and the chips' own frees hand their audio sources back to audio_ through
        // render_free_source, which is why currentInstance is set before this and the sink outlives the context.
        gen_->header.free_context(&gen_->header);
        gen_ = nullptr;
        activated_ = false;
    }
    // Ends by clearing the thread-local, and not inside the branch: a boot that failed set it too, and a pointer left
    // here outlives the instance it named, so the next BlastEm callback on this thread (a context built by whoever
    // runs next, with no system of its own) would dereference a freed system.
    currentInstance = nullptr;
}

void BlastemMdSystem::onDeactivate() { destroyCore(); }

void BlastemMdSystem::onSampleRateChanged(double sampleRate) {
    if (sampleRate <= 0.0) return;
    // Re-anchor at the CURRENT sample, and move the anchor rather than mapping the anchor to itself (which is a no-op
    // and the bug this body exists for): masterClockOf reads sampleAnchor_, so the new anchor's clock has to be taken
    // BEFORE the rate changes, and the sample anchor moves with it so the next sample maps from here at the new rate.
    const std::uint64_t at = masterClockOf(sampleBase_);
    sampleAnchor_ = sampleBase_;
    mclkAnchor_ = at;
    sampleRate_ = sampleRate;
    gainSmoother_.setSampleRate(static_cast<float>(sampleRate_));
    // After the re-anchor, so the ring's 16 primed samples are silence at the NEW rate and every blip resamples from
    // the native rate to this one (which is also what restates each blip frame's budget in native clocks).
    audio_.setHostRate(sampleRate_);
}

void BlastemMdSystem::setGainDb(float dB, bool immediate) {
    // ExpSmoother's own shape (util/ExpSmoother.hpp), as MesenSmsSystem::setGainDb uses it: the smoother holds a linear
    // gain, so the dB is converted once, and `immediate` snaps rather than ramps.
    config_.gainDb = dB;  // config() is what a later activation boots from, so the knob is the record
    gainSmoother_.setTargetValue(dbToLin(dB));
    if (immediate) gainSmoother_.clearToTargetValue();
}

// The block triad, and the core's stop: one run a block, ended at the block's own master clock rather than at a frame
// boundary. The audio the run leaves behind is drained by the audio sink.
void BlastemMdSystem::prepareForBlock(const AudioBlockInfo& info) {
    blockFrames_ = info.frames;
    // The pad's transitions are applied as they arrive (BlastEm's gamepad state is a level its port read samples
    // live), so nothing is queued here: a press that precedes a block is what that block's polls see.
}

bool BlastemMdSystem::stepIfBelowTarget(std::uint32_t /*framesNeeded*/) {
    // One stop a block: the port device answers from a queue stamped in master clocks, so nothing else needs the 68k
    // to pause mid-block. Always false: an unlinked system is the 1-member case of the round-robin.
    run_until(masterClockOf(sampleBase_ + blockFrames_));
    return false;
}

void BlastemMdSystem::finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) {
    if (!gen_) {
        sampleBase_ += info.frames;  // not activated: nothing to drain
        return;
    }
    // Drained EVERY time, lanes or none. A lane-less block still ends the sink's frame: blip's buffer is sized for one
    // block plus the prime, so a `return` before the drain would leave the second block's deltas past its end and
    // either corrupt memory or trip blip's own assert (the rebase case and the video case both pass no lanes).
    audio_.drain(info.frames, mixLeft_.data(), mixRight_.data());
    const bool wired = laneCount >= 2 && outs && outs[0] && outs[1];
    if (wired) {
        // The block's samples are sized at activation and at every growth (reserveAudio), never here: the engine's
        // [alloc] cases count a block's allocations, emulation included.
        for (std::uint32_t i = 0; i < info.frames; ++i) {
            const float gain = gainSmoother_.next();
            outs[0][i] += mixLeft_[i] * gain;  // added, not written: SystemBase's contract
            outs[1][i] += mixRight_[i] * gain;
        }
    }
    sampleBase_ += info.frames;
}

void BlastemMdSystem::reserveAudio(std::uint32_t maxBlockFrames) {
    const std::uint32_t frames = std::max(maxBlockFrames, blockFrames(0));
    if (frames > mixLeft_.size()) {
        mixLeft_.resize(frames);
        mixRight_.resize(frames);
        audio_.prepare(frames);
    }
    SystemBase::reserveAudio(maxBlockFrames);  // LAST: the maximum moves only once the sizing has succeeded
}

void BlastemMdSystem::switchPsgClock(bool fast) {
    // The two lines genesis.c runs around vdp_test_port_write when the VDP test register's bit 0 changes its mind about
    // the Z80's clock source: the PSG's divider becomes MCLKS_PER_YM * 16 (112) or MCLKS_PER_PSG (240), and
    // psg_adjust_master_clock tells the sink. Writing the test register directly does NOT do this (the before/after
    // comparison is genesis.c's), which is why a case calls this instead. Out of line because it names both
    // psg_adjust_master_clock and gen_->psg, and this header includes no BlastEm header of its own.
    if (!gen_ || !gen_->psg) return;
    gen_->psg->clock_inc = fast ? 112u : 240u;
    psg_adjust_master_clock(gen_->psg, gen_->master_clock);
}

void BlastemMdSystem::run_until(std::uint64_t mclk) {
    if (!gen_ || mclk <= mclkNow()) return;
    currentInstance = this;
    // The stop is a 32-bit OFFSET from gen->m68k->cycles (cycle_base + cycles is the absolute clock), computed in that
    // domain, never as `mclk - cycle_base` truncated to 32 bits: the difference is a 64-bit quantity, and it stays
    // exact only because the rebase keeps the count inside the window. Three terms cap the step: the clocks still
    // wanted, one quarter of the 32-bit space (2^30 clocks, about 20 s at 53.7 MHz, which keeps the arithmetic clear
    // of the top), and UINT32_MAX minus the current count. The third can be the smallest: between crossing
    // MAX_NO_ADJUST and the frame end that rebases, the headroom is under one 8192-frame block at 48 kHz, so it binds
    // and adds a mid-block stop. A stop costs the clock nothing (the loop recomputes the step from the clock it reads
    // after the resume), and a block-end stop is a partition-dependent one, which is what the one-sample grid in
    // stepIfBelowTarget removes.
    constexpr std::uint64_t kWindow = 0x40000000u;
    while (mclkNow() < mclk) {
        const std::uint32_t cyc = gen_->m68k->cycles;
        const std::uint64_t step = std::min<std::uint64_t>({mclk - mclkNow(), kWindow, UINT32_MAX - cyc - 1});
        gen_->stop_cycle = cyc + static_cast<std::uint32_t>(step);
        // resume_context returns at stop_cycle, or at the frame end if that is sooner. The loop then recomputes the
        // step from the clock it just read, so a rebase inside a pass costs nothing: the rebase moves cycle_base,
        // cycles and stop_cycle together.
        gen_->header.resume_context(&gen_->header);
        gen_->stop_cycle = 0;
    }
}

// Port 1's pad: the button table and BlastEm's io_gamepad_down/up land with the pad's case, and until they do a press
// is dropped rather than remembered and applied late.
void BlastemMdSystem::pressButton(std::uint8_t /*button*/, bool /*down*/) {}

}  // namespace rp::blastem
