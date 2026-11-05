#include "FakeTinyUsb.hpp"

#include <algorithm>

#include "fake-tinyusb/pico/time.h"
#include "fake-tinyusb/tusb.h"

namespace fakeusb {

Usb& usb() {
    static Usb u;
    return u;
}

void reset() { usb() = Usb{}; }

}  // namespace fakeusb

using fakeusb::usb;

extern "C" {

void tuh_task(void) {
    auto& u = usb();
    ++u.pumps;
    u.ms += u.msPerPump;
    if (u.flushed) {
        const std::uint32_t queued = u.fifoSize - u.fifoFree;
        const std::uint32_t n = std::min(queued, u.sendPerPump);
        const auto begin = u.taken.end() - queued;
        u.sent.insert(u.sent.end(), begin, begin + n);
        u.fifoFree += n;
        if (u.fifoFree == u.fifoSize) u.flushed = false;
    }
    while (!u.arriving.empty() && u.arriving.front().first <= u.ms) {
        u.input.push_back(u.arriving.front().second);
        u.arriving.pop_front();
    }
    if (u.onPump) u.onPump();
}

std::uint32_t tuh_cdc_write_available(std::uint8_t) { return usb().fifoFree; }

std::uint32_t tuh_cdc_write(std::uint8_t idx, void const* buffer, std::uint32_t bufsize) {
    auto&         u = usb();
    std::uint32_t n = std::min(bufsize, u.fifoFree);
    if (u.takeLimit) n = std::min(n, u.takeLimit);
    const auto* bytes = static_cast<const std::uint8_t*>(buffer);
    u.taken.insert(u.taken.end(), bytes, bytes + n);
    u.writeIdx.insert(u.writeIdx.end(), n, idx);
    u.fifoFree -= n;
    return n;
}

std::uint32_t tuh_cdc_write_flush(std::uint8_t) {
    auto& u = usb();
    ++u.flushes;
    if (u.fifoFree != u.fifoSize) u.flushed = true;
    return 0;
}

std::uint32_t tuh_cdc_read(std::uint8_t, void* buffer, std::uint32_t bufsize) {
    auto&         u = usb();
    std::uint32_t n = 0;
    auto*         out = static_cast<std::uint8_t*>(buffer);
    while (n < bufsize && !u.input.empty()) {
        out[n++] = u.input.front();
        u.input.pop_front();
    }
    return n;
}

}  // extern "C"

absolute_time_t get_absolute_time(void) { return usb().ms * 1000; }
std::uint32_t   to_ms_since_boot(absolute_time_t t) { return static_cast<std::uint32_t>(t / 1000); }
