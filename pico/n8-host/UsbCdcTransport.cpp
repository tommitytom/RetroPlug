#include "UsbCdcTransport.hpp"

#include "pico/time.h"
// TinyUSB's osal_none.h applies ++ and -- to a volatile, which C++20 deprecates: its warning, not this file's.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvolatile"
#include "tusb.h"
#pragma GCC diagnostic pop

namespace {

std::uint32_t nowMs() { return to_ms_since_boot(get_absolute_time()); }

}  // namespace

// Every wait pumps the USB host and the idle hook, which keeps the watchdog fed and the status LED alive through the
// long ones. The unmount callback runs in here, so each caller looks at the link after it.
void UsbCdcTransport::pump() const {
    tuh_task();
    if (idle_) idle_();
}

std::size_t UsbCdcTransport::write(const std::uint8_t* data, std::size_t size) {
    const std::uint32_t idleFree = tuh_cdc_write_available(idx_);  // the TX FIFO's free space with nothing queued
    std::size_t         sent     = 0;
    std::uint32_t       start    = nowMs();
    while (sent < size) {
        pump();
        if (!up_) return sent;
        const std::uint32_t n = tuh_cdc_write(idx_, data + sent, static_cast<std::uint32_t>(size - sent));
        if (n) {
            sent += n;
            tuh_cdc_write_flush(idx_);
            start = nowMs();
        } else if (nowMs() - start > kWriteMs) {
            return sent;
        }
    }
    // The FIFO's free space back at what it was is the transfer done.
    start = nowMs();
    while (nowMs() - start < kDrainMs) {
        pump();
        if (!up_) return 0;
        tuh_cdc_write_flush(idx_);
        if (tuh_cdc_write_available(idx_) >= idleFree) return size;
    }
    return size;
}

std::size_t UsbCdcTransport::read(std::uint8_t* buffer, std::size_t size, std::uint32_t timeoutMs) {
    std::size_t         got   = 0;
    const std::uint32_t start = nowMs();
    for (;;) {
        pump();
        if (!up_) return got;
        const std::uint32_t n = tuh_cdc_read(idx_, buffer + got, static_cast<std::uint32_t>(size - got));
        got += n;
        if (got == size || (n == 0 && got != 0)) return got;
        if (n == 0 && (timeoutMs == 0 || nowMs() - start > timeoutMs)) return 0;
    }
}

void UsbCdcTransport::flushInput() {
    std::uint8_t  drop[64];
    std::uint32_t quietSince = nowMs();
    while (nowMs() - quietSince < kFlushQuietMs) {
        pump();
        if (!up_) return;
        if (tuh_cdc_read(idx_, drop, sizeof drop)) quietSince = nowMs();
    }
}
