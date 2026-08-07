#include "host/n8/Edio.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace retroplug {

void Edio::txData(const std::uint8_t* data, std::size_t size) {
    std::size_t offset = 0;
    while (size > 0) {
        const std::size_t block = std::min<std::size_t>(size, 8192);
        port_.write(data + offset, block);
        offset += block;
        size   -= block;
    }
}

void Edio::txCMD(std::uint8_t cmd) {
    const std::uint8_t frame[4] = {
        static_cast<std::uint8_t>('+'),
        static_cast<std::uint8_t>('+' ^ 0xFF),
        cmd,
        static_cast<std::uint8_t>(cmd ^ 0xFF),
    };
    txData(frame, 4);
}

void Edio::tx8(std::uint8_t v) { txData(&v, 1); }

void Edio::tx16(std::uint32_t v) {
    const std::uint8_t b[2] = { static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8) };
    txData(b, 2);
}

void Edio::tx32(std::uint32_t v) {
    const std::uint8_t b[4] = {
        static_cast<std::uint8_t>(v),       static_cast<std::uint8_t>(v >> 8),
        static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 24),
    };
    txData(b, 4);
}

void Edio::rxData(std::uint8_t* data, std::size_t size) {
    std::size_t received = 0;
    while (received < size) {
        const std::size_t n = port_.read(data + received, size - received, timeoutMs_);
        if (n == 0) throw std::runtime_error("Edio: serial read timeout (no N8 response)");
        received += n;
    }
}

std::uint16_t Edio::rx16() {
    std::uint8_t b[2];
    rxData(b, 2);
    return static_cast<std::uint16_t>(b[0] | (b[1] << 8));
}

int Edio::getStatus() {
    txCMD(CMD_STATUS);
    const int resp = rx16();
    if ((resp & 0xFF00) != 0xA500) {
        char msg[64];
        std::snprintf(msg, sizeof(msg), "Edio: unexpected status response (%04X)", resp & 0xFFFF);
        throw std::runtime_error(msg);
    }
    return resp & 0xFF;
}

int Edio::connect(int handshakeTimeoutMs) {
    timeoutMs_ = handshakeTimeoutMs;
    port_.flushInput();
    const int status = getStatus();  // throws on a bad / absent reply
    timeoutMs_ = 2000;               // the reference raises the timeout after a good handshake
    return status;
}

void Edio::memWR(std::int32_t addr, const std::uint8_t* data, std::size_t size) {
    if (size == 0) return;
    txCMD(CMD_MEM_WR);
    tx32(static_cast<std::uint32_t>(addr));
    tx32(static_cast<std::uint32_t>(size));
    tx8(0);              // exec flag
    txData(data, size);  // fire-and-forget: no status read (a failed FIFO write is silent, by design)
}

void Edio::fifoWR(const std::uint8_t* data, std::size_t size) {
    memWR(ADDR_FIFO, data, size);
}

}  // namespace retroplug
