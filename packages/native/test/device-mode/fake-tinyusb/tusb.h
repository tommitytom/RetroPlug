#pragma once

// A fake of the TinyUSB host calls the box's UsbCdcTransport makes (pico/n8-host/UsbCdcTransport.cpp), and nothing
// more: tuh_task and the CDC host's write, flush, read and free-space calls, with TinyUSB's signatures. What they do is
// FakeTinyUsb.hpp's, which a test scripts. Only UsbCdcTransport.cpp's compile sees this directory
// (packages/native/cmake/tests.cmake).

#include <cstdint>

extern "C" {
void          tuh_task(void);
std::uint32_t tuh_cdc_write_available(std::uint8_t idx);
std::uint32_t tuh_cdc_write(std::uint8_t idx, void const* buffer, std::uint32_t bufsize);
std::uint32_t tuh_cdc_write_flush(std::uint8_t idx);
std::uint32_t tuh_cdc_read(std::uint8_t idx, void* buffer, std::uint32_t bufsize);
}
