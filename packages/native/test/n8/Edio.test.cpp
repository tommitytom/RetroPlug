// Guards the Everdrive N8 Pro protocol framing (Edio) without hardware. The write-vector cases live in the
// SHARED golden (packages/retroplug/test/n8/edio-golden.json), which the TS twin (edio.test.ts) asserts
// against too - so a framing change in either impl fails the other's test until both + the golden agree. This
// file additionally covers the C++-side semantics the golden doesn't (connect return/throw) and the N8Link
// forward path (the standalone/plugin's realtime serial thread), which has no TS twin.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <rfl.hpp>
#include <rfl/json.hpp>

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

std::vector<std::uint8_t> fromHex(const std::string& h) {
    std::vector<std::uint8_t> out;
    out.reserve(h.size() / 2);
    for (std::size_t i = 0; i + 1 < h.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>(std::stoul(h.substr(i, 2), nullptr, 16)));
    return out;
}

// The shared golden schema (edio-golden.json). Field names match the JSON keys; every field the golden
// omits per op is std::optional.
struct EdioArgs {
    std::optional<std::string>  bytes;
    std::optional<std::string>  str;
    std::optional<std::string>  path;
    std::optional<int>          mode;
    std::optional<std::int64_t> addr;
    std::optional<int>          size;
};
struct EdioCase {
    std::string                id;
    std::string                op;
    std::optional<EdioArgs>    args;
    std::optional<std::string> reads;
    std::string                writes;
};
struct EdioGolden {
    std::vector<EdioCase> cases;
};

}  // namespace

TEST_CASE("Edio framing matches the shared golden (twins edio.test.ts)", "[n8]") {
    std::ifstream f(EDIO_GOLDEN_PATH);
    REQUIRE(f.good());
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    const auto parsed = rfl::json::read<EdioGolden>(text);
    REQUIRE(parsed);
    const EdioGolden g = parsed.value();
    REQUIRE_FALSE(g.cases.empty());

    for (const EdioCase& c : g.cases) {
        FakeSerialPort port;
        if (c.reads)
            for (std::uint8_t b : fromHex(*c.reads)) port.toRead.push_back(b);
        Edio           edio(port);
        const EdioArgs a = c.args.value_or(EdioArgs{});

        if (c.op == "fifoWR") {
            edio.fifoWR(fromHex(a.bytes.value_or("")));
        } else if (c.op == "fifoTxString") {
            edio.fifoTxString(a.str.value_or(""));
        } else if (c.op == "connect") {
            edio.connect();
        } else if (c.op == "fileOpen") {
            edio.fileOpen(a.path.value_or(""), static_cast<std::uint8_t>(a.mode.value_or(0)));
        } else if (c.op == "fileWrite") {
            edio.fileWrite(fromHex(a.bytes.value_or("")));
        } else if (c.op == "fileClose") {
            edio.fileClose();
        } else if (c.op == "memRD") {
            std::vector<std::uint8_t> buf(static_cast<std::size_t>(a.size.value_or(0)));
            if (!buf.empty()) edio.memRD(static_cast<std::int32_t>(a.addr.value_or(0)), buf.data(), buf.size());
        } else if (c.op == "fileRead") {
            std::vector<std::uint8_t> buf(static_cast<std::size_t>(a.size.value_or(0)));
            if (!buf.empty()) edio.fileRead(buf.data(), buf.size());
        } else {
            FAIL("unknown golden op: " << c.op);
        }

        INFO("golden case: " << c.id);
        REQUIRE(port.written == fromHex(c.writes));
    }
}

// --- C++-side semantics (not framing; no TS twin needed here) ---------------------------------------

TEST_CASE("connect flushes and returns 0 on a 0xA500 (OK) reply", "[n8]") {
    FakeSerialPort port;
    port.queueStatus(0xA500);
    REQUIRE(Edio(port).connect() == 0);
    REQUIRE(port.flushed);
}

TEST_CASE("connect surfaces the low status byte", "[n8]") {
    FakeSerialPort port;
    port.queueStatus(0xA5C3);  // high byte OK, status code 0xC3
    REQUIRE(Edio(port).connect() == 0xC3);
}

TEST_CASE("connect throws on a non-0xA5 status word", "[n8]") {
    FakeSerialPort port;
    port.queueStatus(0x1234);  // wrong high byte
    REQUIRE_THROWS(Edio(port).connect());
}

TEST_CASE("connect throws when the device does not answer (read timeout)", "[n8]") {
    FakeSerialPort port;  // no queued reply => read returns 0 => timeout
    REQUIRE_THROWS(Edio(port).connect());
}

TEST_CASE("fileRead loops resp-gated blocks over RD_BLOCK_SIZE", "[n8]") {
    FakeSerialPort port;
    port.toRead.push_back(0x00);                                     // block 1 resp
    for (int i = 0; i < Edio::RD_BLOCK_SIZE; ++i) port.toRead.push_back(0xAA);
    port.toRead.push_back(0x00);                                     // block 2 resp
    for (int i = 0; i < 4; ++i) port.toRead.push_back(0xBB);
    std::vector<std::uint8_t> buf(Edio::RD_BLOCK_SIZE + 4);
    Edio(port).fileRead(buf.data(), buf.size());
    REQUIRE(buf.front() == 0xAA);
    REQUIRE(buf[Edio::RD_BLOCK_SIZE - 1] == 0xAA);
    REQUIRE(buf.back() == 0xBB);
}

TEST_CASE("readFile finds the size via listDir, then reads the whole file", "[n8]") {
    FakeSerialPort port;
    const auto push = [&](std::initializer_list<std::uint8_t> bs) { for (std::uint8_t b : bs) port.toRead.push_back(b); };
    port.queueStatus(0xA500);                                          // listDir DIR_LD checkStatus
    push({0x01, 0x00});                                               // DIR_SIZE = 1 record
    push({0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x66});  // file "f", size 4
    port.queueStatus(0xA500);                                          // fileOpen(FA_READ) checkStatus
    push({0x00, 0xDE, 0xAD, 0xBE, 0xEF});                             // fileRead resp + 4 data bytes
    port.queueStatus(0xA500);                                          // fileClose checkStatus
    REQUIRE(Edio(port).readFile("d/f") == std::vector<std::uint8_t>{0xDE, 0xAD, 0xBE, 0xEF});
}

// --- N8Link: the host serial thread + ring + timed scheduler (standalone/plugin forward; no TS twin) ---

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
