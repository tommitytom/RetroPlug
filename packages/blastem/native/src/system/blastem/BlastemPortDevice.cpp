#include "system/blastem/BlastemPortDevice.hpp"

#include "system/blastem/BlastemCore.hpp"      // io_port, uint8_t/uint32_t for the entry point below
#include "system/blastem/BlastemMdSystem.hpp"  // the instance whose port the core is asking

namespace rp::blastem {

void PortDevice::enqueue(std::uint64_t sample, std::uint8_t level) {
    if (pending_.push(make(sample, level))) ++accepted_;
    // Two doors of one block are not ordered against each other, so the queue sorts once before it drains rather than
    // keeping itself ordered on every push.
    needsSort_ = true;
}

void PortDevice::sortPending() {
    if (!needsSort_) return;
    needsSort_ = false;
    if (pending_.size() < 2) return;
    sortScratch_.sort(pending_.begin(), pending_.end(),
                      [](const Entry& a, const Entry& b) { return a.sample() < b.sample(); });
}

void PortDevice::retire() {
    sortPending();
    // The sample is absolute, so nothing here rebases: an entry staged past the grid's end is still due at its own
    // sample in a later block, which is what makes the schedule independent of how the host cut the timeline.
    while (!pending_.empty() && pending_.front().sample() <= grid_) {
        lastApplied_ = pending_.front().level();
        pending_.pop();
    }
}

std::uint8_t PortDevice::level(std::uint64_t mclk) {
    retire();
    // The two pins this device drives, and nothing more: the direction rule on $A1000B, the ROM's own output latch and
    // the pull-ups on every other bit are io_data_read's tail, which is why this answer is a level and not a byte.
    const std::uint8_t answer = std::uint8_t(lastApplied_ & 0x60);
    if (recorded_ < log_.size()) log_[recorded_++] = {grid_, mclk, answer};
    return answer;
}

}  // namespace rp::blastem

extern "C" {

// io_data_read's answer for a port whose device is IO_CUSTOM (deps/blastem, io.c). `cycle` is the 68k's own 32-bit
// count, the same clock every other device's answer is read at, so the host's schedule meets it in absolute time.
uint8_t io_custom_input(io_port* port, uint32_t cycle) {
    auto* sys = static_cast<rp::blastem::BlastemMdSystem*>(rp::blastem::currentInstance);
    // No instance (a context built outside this package) drives nothing: io_data_read's tail then reads the pull-ups.
    if (!sys) return 0;
    return sys->portDevice().level(sys->mclkOfCoreCycle(cycle));
}
}
