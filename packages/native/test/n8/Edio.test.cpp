// Guards the Everdrive N8 Pro protocol framing (Edio) without hardware: a FakeSerialPort captures every
// byte Edio writes, so we assert fifoWR emits the exact krikzz command stream and the connect handshake
// accepts / rejects the 0xA5 status word. This is the CI-able half of the N8 bridge (the live serial +
// MIDI path needs a real N8). Mirrors test/midi/MidiPortSelect.test.cpp (a header-level, backend-free guard).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <memory>
#include <thread>
#include <vector>

#include "host/n8/Edio.hpp"
#include "host/n8/N8Link.hpp"

using retroplug::Edio;
using retroplug::ISerialPort;
using retroplug::N8Link;

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

// The expected CMD_MEM_WR frame targeting ADDR_FIFO for a given payload (what fifoWR emits).
static std::vector<std::uint8_t> memWrFifo(const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> f = {0x2B, 0xD4, 0x1A, 0xE5, 0x00, 0x00, 0x81, 0x01};  // frame + ADDR_FIFO LE
    const std::uint32_t n = static_cast<std::uint32_t>(payload.size());
    f.push_back(n & 0xFF); f.push_back((n >> 8) & 0xFF); f.push_back((n >> 16) & 0xFF); f.push_back((n >> 24) & 0xFF);
    f.push_back(0x00);  // exec
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}
static void append(std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) { a.insert(a.end(), b.begin(), b.end()); }

TEST_CASE("fifoTxString emits a 2-byte LE length then the bytes, each as a FIFO write", "[n8]") {
    FakeSerialPort port;
    Edio           edio(port);
    edio.fifoTxString("ab");
    std::vector<std::uint8_t> expected;
    append(expected, memWrFifo({0x02, 0x00}));  // length = 2, little-endian
    append(expected, memWrFifo({'a', 'b'}));    // the string bytes
    REQUIRE(port.written == expected);
}

TEST_CASE("fileOpen sends CMD_F_FOPN + mode + length-prefixed path, then polls status", "[n8]") {
    FakeSerialPort port;
    port.queueStatus(0xA500);  // checkStatus poll -> ok
    Edio edio(port);
    edio.fileOpen("ab", Edio::FA_WRITE | Edio::FA_CREATE_ALWAYS | Edio::FS_MAKEPATH);
    const std::vector<std::uint8_t> expected = {
        0x2B, 0xD4, 0xC9, 0x36,  // frame CMD_F_FOPN (0xC9 ^ 0xFF = 0x36)
        0x8A,                    // mode = FA_WRITE|FA_CREATE_ALWAYS|FS_MAKEPATH
        0x02, 0x00,              // path length = 2 (tx16)
        'a', 'b',                // path bytes
        0x2B, 0xD4, 0x10, 0xEF,  // checkStatus -> CMD_STATUS frame
    };
    REQUIRE(port.written == expected);
}

TEST_CASE("fileWrite sends CMD_F_FWR + length, one ack-gated block, then polls status", "[n8]") {
    FakeSerialPort port;
    port.toRead.push_back(0x00);  // txDataACK: ack byte for the first (only) block
    port.queueStatus(0xA500);     // checkStatus poll -> ok
    Edio edio(port);
    edio.fileWrite(std::vector<std::uint8_t>{0xDE, 0xAD});
    const std::vector<std::uint8_t> expected = {
        0x2B, 0xD4, 0xCC, 0x33,  // frame CMD_F_FWR (0xCC ^ 0xFF = 0x33)
        0x02, 0x00, 0x00, 0x00,  // length = 2 (tx32)
        0xDE, 0xAD,              // the block (after the ack byte was read)
        0x2B, 0xD4, 0x10, 0xEF,  // checkStatus -> CMD_STATUS frame
    };
    REQUIRE(port.written == expected);
}

TEST_CASE("fileClose sends CMD_F_FCLOSE then polls status", "[n8]") {
    FakeSerialPort port;
    port.queueStatus(0xA500);
    Edio edio(port);
    edio.fileClose();
    const std::vector<std::uint8_t> expected = {
        0x2B, 0xD4, 0xCE, 0x31,  // frame CMD_F_FCLOSE (0xCE ^ 0xFF = 0x31)
        0x2B, 0xD4, 0x10, 0xEF,  // checkStatus -> CMD_STATUS frame
    };
    REQUIRE(port.written == expected);
}

// --- N8Link: the host serial thread + ring + timed scheduler (standalone/plugin forward) ---

namespace {
// A serial port that captures writes into a TEST-OWNED buffer, so it outlives N8Link's port (disconnect()
// destroys the port; the buffer survives + is safely readable after the join happens-before).
struct N8FakePort : ISerialPort {
    std::vector<std::uint8_t>& written;
    std::deque<std::uint8_t>   toRead;
    explicit N8FakePort(std::vector<std::uint8_t>& w) : written(w) {}
    std::size_t write(const std::uint8_t* d, std::size_t n) override {
        written.insert(written.end(), d, d + n);
        return n;
    }
    std::size_t read(std::uint8_t* b, std::size_t n, int) override {
        std::size_t i = 0;
        while (i < n && !toRead.empty()) { b[i++] = toRead.front(); toRead.pop_front(); }
        return i;
    }
    void flushInput() override {}
};

template <class Pred>
bool waitUntil(Pred pred, int ms = 1000) {
    for (int i = 0; i < ms && !pred(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return pred();
}

// Factory: capture writes into `out` (test-owned), priming the handshake reply on each opened port.
N8Link::PortFactory captureFactory(std::vector<std::uint8_t>& out, std::uint16_t status = 0xA500) {
    return [&out, status](const std::string&) -> std::unique_ptr<ISerialPort> {
        auto f = std::make_unique<N8FakePort>(out);
        f->toRead.push_back(static_cast<std::uint8_t>(status & 0xFF));
        f->toRead.push_back(static_cast<std::uint8_t>(status >> 8));
        return f;
    };
}
}  // namespace

TEST_CASE("N8Link forwards a pushed MIDI message to the serial port via fifoWR", "[n8]") {
    std::vector<std::uint8_t> captured;
    N8Link                    link(captureFactory(captured));
    REQUIRE(link.connect("fake"));
    REQUIRE(link.isConnected());
    link.setLookaheadMs(0);

    const std::uint8_t midi[] = {0x90, 0x3C, 0x7F};
    link.push(0, midi, sizeof(midi), 48000.0);
    REQUIRE(waitUntil([&] { return link.bytesForwarded() >= 3; }));
    link.disconnect();  // joins the serial thread -> `captured` is complete + safe to read

    const std::vector<std::uint8_t> expected = {
        0x2B, 0xD4, 0x10, 0xEF,  // connect(): CMD_STATUS handshake
        0x2B, 0xD4, 0x1A, 0xE5, 0x00, 0x00, 0x81, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x90, 0x3C, 0x7F,  // fifoWR
    };
    REQUIRE(captured == expected);
}

TEST_CASE("N8Link forwards multiple messages in FIFO order", "[n8]") {
    std::vector<std::uint8_t> captured;
    N8Link                    link(captureFactory(captured));
    REQUIRE(link.connect("fake"));
    link.setLookaheadMs(0);

    const std::uint8_t on[]  = {0x90, 0x3C, 0x7F};
    const std::uint8_t off[] = {0x80, 0x3C, 0x00};
    link.push(0, on, sizeof(on), 48000.0);
    link.push(0, off, sizeof(off), 48000.0);
    REQUIRE(waitUntil([&] { return link.bytesForwarded() >= 6; }));
    link.disconnect();

    const std::vector<std::uint8_t> expected = {
        0x2B, 0xD4, 0x10, 0xEF,  // handshake
        0x2B, 0xD4, 0x1A, 0xE5, 0x00, 0x00, 0x81, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x90, 0x3C, 0x7F,  // note-on
        0x2B, 0xD4, 0x1A, 0xE5, 0x00, 0x00, 0x81, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3C, 0x00,  // note-off
    };
    REQUIRE(captured == expected);
}

TEST_CASE("N8Link connect fails cleanly on a bad handshake", "[n8]") {
    std::vector<std::uint8_t> captured;
    N8Link                    link(captureFactory(captured, 0x1234));  // wrong high byte
    REQUIRE_FALSE(link.connect("fake"));
    REQUIRE_FALSE(link.isConnected());
    REQUIRE_FALSE(link.lastError().empty());
}

TEST_CASE("N8Link push is a no-op when not connected", "[n8]") {
    std::vector<std::uint8_t> captured;
    N8Link                    link(captureFactory(captured));
    const std::uint8_t        m[] = {0x90, 0x3C, 0x7F};
    link.push(0, m, sizeof(m), 48000.0);  // not connected -> dropped
    REQUIRE(link.bytesForwarded() == 0);
}
