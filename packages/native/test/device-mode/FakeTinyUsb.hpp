#pragma once

// The fake TinyUSB host behind UsbCdcTransport's desktop test (fake-tinyusb/tusb.h and pico/time.h declare what it
// defines): a clock in milliseconds that every tuh_task moves on, a TX FIFO that takes what fits and sends what a flush
// let go, a pump at a time, and input that arrives at the moments a test scripts. A test reaches into it between calls
// and through onPump, which runs inside every tuh_task as TinyUSB's callbacks do (the CDC unmount among them).

#include <cstdint>
#include <deque>
#include <functional>
#include <utility>
#include <vector>

namespace fakeusb {

struct Usb {
    std::uint64_t ms = 0;            // the clock
    std::uint32_t msPerPump = 1;     // what one tuh_task moves it on by
    std::uint32_t fifoSize = 256;    // the TX FIFO (CFG_TUH_CDC_TX_BUFSIZE)
    std::uint32_t fifoFree = 256;
    std::uint32_t takeLimit = 0;     // the most one tuh_cdc_write takes (0: whatever fits)
    std::uint32_t sendPerPump = 64;  // what a flushed FIFO sends in one pump (one packet)
    bool          flushed = false;   // a flush let the FIFO go, until it is empty
    std::vector<std::uint8_t> taken; // what the FIFO took, in order
    std::vector<std::uint8_t> sent;  // what left it
    std::vector<std::uint8_t> writeIdx;  // the CDC index each byte was written to
    std::deque<std::pair<std::uint64_t, std::uint8_t>> arriving;  // input, at the moment it arrives
    std::deque<std::uint8_t> input;  // arrived, not yet read
    std::uint32_t pumps = 0;
    std::uint32_t flushes = 0;
    std::function<void()> onPump;    // inside each tuh_task, after the clock and the FIFO have moved
};

// The one fake, reset by a test before it starts.
Usb& usb();
void reset();

}  // namespace fakeusb
