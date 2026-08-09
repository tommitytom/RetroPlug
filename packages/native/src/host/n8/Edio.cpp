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

void Edio::flushInput() { port_.flushInput(); }

std::uint8_t Edio::rx8() {
    std::uint8_t b;
    rxData(&b, 1);
    return b;
}

std::uint16_t Edio::rx16() {
    std::uint8_t b[2];
    rxData(b, 2);
    return static_cast<std::uint16_t>(b[0] | (b[1] << 8));
}

std::uint32_t Edio::rx32() {
    std::uint8_t b[4];
    rxData(b, 4);
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
}

std::string Edio::rxString() {
    const std::uint16_t n = rx16();
    std::string s(n, '\0');
    if (n) rxData(reinterpret_cast<std::uint8_t*>(&s[0]), n);
    return s;
}

std::vector<N8DirEntry> Edio::listDir(const std::string& path) {
    // Load the directory into the N8's buffer (sorted), then pull all records in one range read.
    txCMD(CMD_F_DIR_LD);
    tx8(1);            // sorted
    txString(path);
    checkStatus();

    txCMD(CMD_F_DIR_SIZE);
    const int count = rx16();

    std::vector<N8DirEntry> out;
    if (count <= 0) return out;
    out.reserve(static_cast<std::size_t>(count));

    txCMD(CMD_F_DIR_GET);
    tx16(0);            // start index
    tx16(static_cast<std::uint32_t>(count));
    tx16(255);          // max name length
    for (int i = 0; i < count; ++i) {
        const std::uint8_t resp = rx8();
        if (resp != 0) {
            char msg[48];
            std::snprintf(msg, sizeof(msg), "Edio: dir read error 0x%02X", resp);
            throw std::runtime_error(msg);
        }
        N8DirEntry e;
        e.size = rx32();
        rx16();  // date (unused)
        rx16();  // time (unused)
        const std::uint8_t attrib = rx8();
        e.name = rxString();
        e.isDir = (attrib & 0x10) != 0;  // FatFs AM_DIR
        out.push_back(std::move(e));
    }
    return out;
}

void Edio::fifoTxString(const std::string& s) {
    const std::uint8_t len[2] = {
        static_cast<std::uint8_t>(s.size()),
        static_cast<std::uint8_t>(s.size() >> 8),
    };
    fifoWR(len, 2);
    fifoWR(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
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

void Edio::memRD(std::int32_t addr, std::uint8_t* data, std::size_t size) {
    if (size == 0) return;
    txCMD(CMD_MEM_RD);
    tx32(static_cast<std::uint32_t>(addr));
    tx32(static_cast<std::uint32_t>(size));
    tx8(0);              // exec flag
    rxData(data, size);  // blocks until all bytes arrive (or throws on timeout)
}

void Edio::fifoWR(const std::uint8_t* data, std::size_t size) {
    memWR(ADDR_FIFO, data, size);
}

void Edio::txString(const std::string& s) {
    tx16(static_cast<std::uint32_t>(s.size()));
    txData(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

void Edio::txDataACK(const std::uint8_t* data, std::size_t size) {
    std::size_t offset = 0;
    while (size > 0) {
        const std::uint8_t ack = rx8();  // the device gates each block with a 0 ack byte
        if (ack != 0) {
            char msg[48];
            std::snprintf(msg, sizeof(msg), "Edio: tx ack error 0x%02X", ack);
            throw std::runtime_error(msg);
        }
        const std::size_t block = std::min<std::size_t>(size, ACK_BLOCK_SIZE);
        txData(data + offset, block);
        offset += block;
        size   -= block;
    }
}

void Edio::checkStatus() {
    const int resp = getStatus();  // sends CMD_STATUS, verifies the 0xA5xx frame, returns the low byte
    if (resp != 0) {
        char msg[48];
        std::snprintf(msg, sizeof(msg), "Edio: operation error 0x%02X", resp);
        throw std::runtime_error(msg);
    }
}

void Edio::fileOpen(const std::string& path, std::uint8_t mode) {
    txCMD(CMD_F_FOPN);
    tx8(mode);
    txString(path);
    checkStatus();
}

void Edio::fileWrite(const std::uint8_t* data, std::size_t size) {
    txCMD(CMD_F_FWR);
    tx32(static_cast<std::uint32_t>(size));
    txDataACK(data, size);
    checkStatus();
}

void Edio::fileClose() {
    txCMD(CMD_F_FCLOSE);
    checkStatus();
}

}  // namespace retroplug
