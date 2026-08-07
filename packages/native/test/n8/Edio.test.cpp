// Guards the Everdrive N8 Pro protocol framing (Edio) without hardware: a FakeSerialPort captures every
// byte Edio writes, so we assert fifoWR emits the exact krikzz command stream and the connect handshake
// accepts / rejects the 0xA5 status word. This is the CI-able half of the N8 bridge (the live serial +
// MIDI path needs a real N8). Mirrors test/midi/MidiPortSelect.test.cpp (a header-level, backend-free guard).
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "host/n8/Edio.hpp"

using retroplug::Edio;
using retroplug::ISerialPort;

namespace {

// An in-memory ISerialPort: writes append to `written`; reads pop from a scripted `toRead` queue (an empty
// queue yields 0 bytes, which Edio treats as a timeout / no response).
struct FakeSerialPort : ISerialPort {
    std::vector<std::uint8_t> written;
    std::deque<std::uint8_t>  toRead;
    bool                      flushed = false;

    std::size_t write(const std::uint8_t* data, std::size_t size) override {
        written.insert(written.end(), data, data + size);
        return size;
    }
    std::size_t read(std::uint8_t* buffer, std::size_t size, int /*timeoutMs*/) override {
        std::size_t n = 0;
        while (n < size && !toRead.empty()) {
            buffer[n++] = toRead.front();
            toRead.pop_front();
        }
        return n;  // 0 => timeout
    }
    void flushInput() override { flushed = true; }

    // Queue a little-endian 16-bit status word (what the N8 returns for CMD_STATUS).
    void queueStatus(std::uint16_t v) {
        toRead.push_back(static_cast<std::uint8_t>(v & 0xFF));
        toRead.push_back(static_cast<std::uint8_t>(v >> 8));
    }
};

}  // namespace

TEST_CASE("fifoWR emits the exact krikzz CMD_MEM_WR frame to ADDR_FIFO", "[n8]") {
    FakeSerialPort port;
    Edio           edio(port);

    const std::vector<std::uint8_t> midi = {0x90, 0x3C, 0x7F};  // note-on, middle C, velocity 127
    edio.fifoWR(midi);

    // frame('+', '+'^0xFF, CMD_MEM_WR, CMD_MEM_WR^0xFF) | addr LE | len LE | exec | payload
    const std::vector<std::uint8_t> expected = {
        0x2B, 0xD4, 0x1A, 0xE5,  // '+' , 0xD4 , 0x1A , 0xE5
        0x00, 0x00, 0x81, 0x01,  // ADDR_FIFO = 0x01810000, little-endian
        0x03, 0x00, 0x00, 0x00,  // len = 3, little-endian
        0x00,                    // exec flag
        0x90, 0x3C, 0x7F,        // the MIDI bytes
    };
    REQUIRE(port.written == expected);
}

TEST_CASE("fifoWR on empty input writes nothing", "[n8]") {
    FakeSerialPort port;
    Edio           edio(port);
    edio.fifoWR(std::vector<std::uint8_t>{});
    REQUIRE(port.written.empty());
}

TEST_CASE("connect flushes, sends CMD_STATUS, and accepts a 0xA5xx reply", "[n8]") {
    FakeSerialPort port;
    port.queueStatus(0xA500);  // high byte 0xA5, status 0 = OK
    Edio edio(port);

    const int status = edio.connect();
    REQUIRE(status == 0);
    REQUIRE(port.flushed);
    // The only bytes written are the CMD_STATUS frame (0x10 ^ 0xFF = 0xEF).
    const std::vector<std::uint8_t> expected = {0x2B, 0xD4, 0x10, 0xEF};
    REQUIRE(port.written == expected);
}

TEST_CASE("connect surfaces the low status byte", "[n8]") {
    FakeSerialPort port;
    port.queueStatus(0xA5C3);  // high byte OK, status code 0xC3
    Edio edio(port);
    REQUIRE(edio.connect() == 0xC3);
}

TEST_CASE("connect throws on a non-0xA5 status word", "[n8]") {
    FakeSerialPort port;
    port.queueStatus(0x1234);  // wrong high byte
    Edio edio(port);
    REQUIRE_THROWS(edio.connect());
}

TEST_CASE("connect throws when the device does not answer (read timeout)", "[n8]") {
    FakeSerialPort port;  // no queued reply => read returns 0 => timeout
    Edio           edio(port);
    REQUIRE_THROWS(edio.connect());
}
