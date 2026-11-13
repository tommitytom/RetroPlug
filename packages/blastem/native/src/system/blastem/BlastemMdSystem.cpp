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
    boot();
    gainSmoother_.setTargetValue(dbToLin(config_.gainDb));
    gainSmoother_.clearToTargetValue();  // the first value is the node's, not a ramp from unity
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
    gen_ = alloc_config_genesis(cart, static_cast<std::uint32_t>(rom_.size()), nullptr, 0, 0,
                                blastemRegion(config_.region));
    if (!gen_) {
        aligned_free(cart);
        return;
    }
    m68k_reset(gen_->m68k);
    sampleBase_ = 0;
    sampleAnchor_ = 0;
    mclkAnchor_ = 0;
    activated_ = true;
    setGainDb(config_.gainDb, true);
}

std::uint32_t BlastemMdSystem::coreFrameWidth() const { return 320; }

std::uint32_t BlastemMdSystem::coreFrameHeight() const {
    // The region's visible lines (vdp.c's NTSC_INACTIVE_START and PAL_INACTIVE_START). version_reg's 0x40 is HZ50,
    // set by set_region for a PAL machine; the video sink allocates its frame from the same expression.
    if (!gen_) return 0;
    return (gen_->version_reg & 0x40) ? 240u : 224u;
}

std::uint64_t BlastemMdSystem::mclkNow() const {
    return gen_ ? static_cast<std::uint64_t>(gen_->cycle_base) + gen_->m68k->cycles : 0;
}

void BlastemMdSystem::destroyCore() {
    if (gen_) {
        currentInstance = this;
        gen_->header.free_context(&gen_->header);  // frees the aligned ROM copy too
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
    // The sink's own rate change is the audio sink's line (audio_.setHostRate), added there beside this one.
    const std::uint64_t at = masterClockOf(sampleBase_);
    sampleAnchor_ = sampleBase_;
    mclkAnchor_ = at;
    sampleRate_ = sampleRate;
    gainSmoother_.setSampleRate(static_cast<float>(sampleRate_));
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

void BlastemMdSystem::finishBlock(const AudioBlockInfo& info, float* const*, std::size_t) {
    // A block's end is its counter and nothing else: the lanes stay untouched because the audio sink that fills them
    // is not here yet, so there is nothing to drain them from.
    sampleBase_ += info.frames;
}

void BlastemMdSystem::reserveAudio(std::uint32_t maxBlockFrames) { SystemBase::reserveAudio(maxBlockFrames); }

void BlastemMdSystem::run_until(std::uint64_t mclk) {
    if (!gen_ || mclk <= mclkNow()) return;
    currentInstance = this;
    // The stop is a 32-bit OFFSET from gen->m68k->cycles (cycle_base + cycles is the absolute clock), so it is computed
    // in that domain, never as `mclk - cycle_base` truncated to 32 bits: that difference is a 64-bit quantity and a
    // `uint32_t` stop would wrap at about 20 s of emulation, putting the target in the past and stopping after one
    // instruction forever. Three terms cap the step: the clocks still wanted, one quarter of the 32-bit space (which
    // keeps the arithmetic clear of the top), and UINT32_MAX minus the current count (which is what the rebase's own
    // window guarantees: a frame end lands within ADJUST_BUFFER of the top, so this third term never binds and the
    // rebase always happens before the count can wrap).
    constexpr std::uint64_t kWindow = 0x40000000u;
    while (mclkNow() < mclk) {
        const std::uint32_t cyc = gen_->m68k->cycles;
        const std::uint64_t step = std::min<std::uint64_t>({mclk - mclkNow(), kWindow, UINT32_MAX - cyc - 1});
        gen_->stop_cycle = cyc + static_cast<std::uint32_t>(step);
        // resume_context returns at stop_cycle (or at the frame end if that is sooner, and a frame end is where the
        // rebase happens); the loop then recomputes the step from the clock it just read, so a rebase inside a pass
        // costs nothing: the rebase moves cycle_base, cycles and stop_cycle together.
        gen_->header.resume_context(&gen_->header);
        gen_->stop_cycle = 0;
    }
}

// Port 1's pad: the button table and BlastEm's io_gamepad_down/up land with the pad's case, and until they do a press
// is dropped rather than remembered and applied late.
void BlastemMdSystem::pressButton(std::uint8_t /*button*/, bool /*down*/) {}

}  // namespace rp::blastem
