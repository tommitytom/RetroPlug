#include "system/blastem/BlastemMdSystem.hpp"

#include <algorithm>
#include <cstring>

#include "system/blastem/BlastemCore.hpp"
#include "system/blastem/BlastemRom.hpp"

namespace rp::blastem {

// The ROM must be BlastEm's own allocation: alloc_config_genesis takes the pointer, byteswaps it in place over
// nearest_pow2(size) bytes (shared_init_gen), and free_genesis releases it with aligned_free, which reads the byte
// BEFORE the pointer to find the offset (util.c:51). A std::vector's buffer is not that allocation, and handing it
// over is a double free at teardown. So the image is copied into aligned_calloc's buffer, padded to the power of two
// BlastEm byteswaps, and BlastEm owns it from boot to free.
//
// The alignment is 16, BlastEm's own (romdb.c:274), and never more: aligned_calloc keeps it in ONE byte
// (`ret[-1] = (uint8_t)align`, util.c:44), so 4096 truncates to 0 and aligned_free then frees a misaligned pointer.
static void* blastemRomCopy(const std::vector<std::uint8_t>& rom) {
    std::size_t size = 1;
    while (size < rom.size()) size <<= 1;
    void* copy = aligned_calloc(1, size, 16);
    if (!copy) return nullptr;
    std::memcpy(copy, rom.data(), rom.size());
    return copy;
}

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
    return gen_ ? static_cast<std::uint64_t>(gen_->m68k->cycles) : 0u;  // the 64-bit base arrives with the stop
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
    gainSmoother_.setTargetValue(dbToLin(dB));
    if (immediate) gainSmoother_.clearToTargetValue();
}

// The block triad, and the core's stop: the bodies that advance the machine arrive with the block loop, and until they
// do a block steps nothing and drains nothing.
void BlastemMdSystem::prepareForBlock(const AudioBlockInfo& info) { blockFrames_ = info.frames; }

bool BlastemMdSystem::stepIfBelowTarget(std::uint32_t) { return false; }

void BlastemMdSystem::finishBlock(const AudioBlockInfo& info, float* const*, std::size_t) {
    sampleBase_ += info.frames;
}

void BlastemMdSystem::reserveAudio(std::uint32_t maxBlockFrames) { SystemBase::reserveAudio(maxBlockFrames); }

void BlastemMdSystem::run_until(std::uint64_t) {}  // later: the stop, and the 64-bit base it is measured from

// Port 1's pad: the button table and BlastEm's io_gamepad_down/up land with the pad's case, and until they do a press
// is dropped rather than remembered and applied late.
void BlastemMdSystem::pressButton(std::uint8_t /*button*/, bool /*down*/) {}

}  // namespace rp::blastem
