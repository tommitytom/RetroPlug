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
// count, the same clock every other device's answer is read at: the device logs it beside the grid's sample, which is
// what decides the level.
uint8_t io_custom_input(io_port* port, uint32_t cycle) {
    auto* sys = static_cast<rp::blastem::BlastemMdSystem*>(rp::blastem::currentInstance);
    // No instance (a context built outside this package), or a port this instance does not drive: the host drives
    // nothing, which is the released level $60. io_data_read's tail then reads the pull-ups, $7F, exactly as an
    // unplugged port does. Answering 0 here would drive TR and TH low instead.
    if (!sys || !sys->core() || port != &sys->core()->io.ports[1]) return 0x60;
    return sys->portDevice().level(sys->mclkOfCoreCycle(cycle));
}
}
