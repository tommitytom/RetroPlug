#include "BlockDriver.hpp"

namespace rp::box {

void ElapsedDriver::start(Clock clock, void* ctx, std::uint32_t sampleRate, std::uint32_t maxFrames) {
    clock_ = clock;
    ctx_ = ctx;
    rate_ = sampleRate;
    maxFrames_ = maxFrames;
    origin_ = now_ = clock_(ctx_);
    due_ = done_ = first_ = blocks_ = skipped_ = 0;
    frames_ = 0;
}

// Whole frames from the start to `us`: 64-bit, so a box up for years at 48 kHz neither wraps nor rounds.
std::uint64_t ElapsedDriver::frameAt(std::uint64_t us) const {
    return us <= origin_ ? 0 : (us - origin_) * rate_ / 1000000u;
}

bool ElapsedDriver::next(std::uint32_t* frames, rp_transport* transport) {
    if (!clock_ || rate_ == 0 || maxFrames_ == 0) return false;
    now_ = clock_(ctx_);
    due_ = frameAt(now_);
    if (due_ <= done_) return false;
    const std::uint64_t bound = std::uint64_t{kCarryBlocks} * maxFrames_;
    if (due_ - done_ > bound) {
        skipped_ += due_ - done_ - bound;
        done_ = due_ - bound;
    }
    const std::uint64_t owed = due_ - done_;
    frames_ = owed > maxFrames_ ? maxFrames_ : static_cast<std::uint32_t>(owed);
    first_ = done_;
    done_ += frames_;
    ++blocks_;
    *frames = frames_;
    // The box keeps no musical time: nothing plays, at the tempo every host gives a stopped transport.
    *transport = rp_transport{static_cast<double>(rate_), 120.0, 0.0, 0u};
    return true;
}

std::uint32_t ElapsedDriver::frameOf(std::uint32_t us) const {
    if (frames_ == 0) return 0;
    // The moment on the 64-bit clock: `us` is at most one wrap of the 32-bit microseconds (71 minutes) before now_,
    // which a timestamp of a message staged since the last block always is.
    const std::uint32_t ago = static_cast<std::uint32_t>(now_) - us;
    const std::uint64_t at = now_ >= ago ? now_ - ago : 0;
    const std::uint64_t frame = frameAt(at);
    if (frame <= first_) return 0;
    const std::uint64_t offset = frame - first_;
    return offset >= frames_ ? frames_ - 1 : static_cast<std::uint32_t>(offset);
}

}  // namespace rp::box
