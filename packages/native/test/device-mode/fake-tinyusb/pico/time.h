#pragma once

// A fake of the Pico SDK's clock as UsbCdcTransport reads it (get_absolute_time, to_ms_since_boot): FakeTinyUsb.hpp's
// clock, which each tuh_task moves on.

#include <cstdint>

typedef std::uint64_t absolute_time_t;

absolute_time_t get_absolute_time(void);
std::uint32_t   to_ms_since_boot(absolute_time_t t);
