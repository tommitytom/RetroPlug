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
#include "host/n8/N8Menu.hpp"
#include "host/n8/RisaSyncTranslator.hpp"

using retroplug::Edio;
using retroplug::ISerialPort;
using retroplug::N8Link;
using retroplug::N8Menu;
using retroplug::RisaSyncTranslator;

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

TEST_CASE("N8Menu::test sends '*t' to the FIFO and accepts 'k'", "[n8]") {
    FakeSerialPort port;
    port.toRead.push_back('k');
    Edio   edio(port);
    N8Menu menu(edio);
    menu.test();
    REQUIRE(port.written == memWrFifo({'*', 't'}));
}

TEST_CASE("N8Menu::test throws on a non-'k' reply", "[n8]") {
    FakeSerialPort port;
    port.toRead.push_back('x');
    Edio   edio(port);
    REQUIRE_THROWS(N8Menu(edio).test());
}

TEST_CASE("N8Menu::appInstall sends '*n' + length-prefixed path, returns the map index", "[n8]") {
    FakeSerialPort port;
    port.toRead.push_back(0x00);         // status = ok
    port.toRead.push_back(0x07);         // map index low
    port.toRead.push_back(0x00);         // map index high
    Edio   edio(port);
    N8Menu menu(edio);
    const int idx = menu.appInstall("x");
    REQUIRE(idx == 7);
    std::vector<std::uint8_t> expected;
    append(expected, memWrFifo({'*', 'n'}));
    append(expected, memWrFifo({0x01, 0x00}));  // path length = 1
    append(expected, memWrFifo({'x'}));
    REQUIRE(port.written == expected);
}

TEST_CASE("N8Menu::appInstall throws on a non-zero install status", "[n8]") {
    FakeSerialPort port;
    port.toRead.push_back(0x05);  // FR_NO_PATH
    Edio edio(port);
    REQUIRE_THROWS(N8Menu(edio).appInstall("bad/path.nes"));
}

TEST_CASE("N8Menu::appStart sends '*s'", "[n8]") {
    FakeSerialPort port;
    Edio   edio(port);
    N8Menu menu(edio);
    menu.appStart();
    REQUIRE(port.written == memWrFifo({'*', 's'}));
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

// -----------------------------------------------------------------------------------------------------
// RisaSyncTranslator: MIDI clock/transport -> risa host-sync bytes. The pure, testable half of `n8-sync`
// (the live RtMidi/serial loop needs a real N8). The expected byte packets are cross-checked against the
// golden pure-TS role test, packages/retroplug/test/dsp/risa-sync.test.ts.
// -----------------------------------------------------------------------------------------------------
namespace {
// Feed one MIDI message into the translator, appending any risa output to `out` (which is NOT cleared).
void feed(RisaSyncTranslator& t, std::initializer_list<std::uint8_t> msg, std::vector<std::uint8_t>& out) {
    const std::vector<std::uint8_t> m(msg);
    t.onMessage(m.data(), m.size(), out);
}
std::size_t countByte(const std::vector<std::uint8_t>& v, std::uint8_t b) {
    std::size_t c = 0;
    for (auto x : v) if (x == b) ++c;
    return c;
}
}  // namespace

TEST_CASE("RisaSyncTranslator armPacket matches the risaSync.ts locate mapping", "[n8]") {
    using A = std::array<std::uint8_t, 5>;
    // Golden values from test/dsp/risa-sync.test.ts (ppq*24 = absoluteClock).
    REQUIRE(RisaSyncTranslator::armPacket(0)    == A{0xF9, 0x52, 0x00, 0x00, 0x00}); // top
    REQUIRE(RisaSyncTranslator::armPacket(1536) == A{0xF9, 0x52, 0x01, 0x00, 0x00}); // ppq 64
    REQUIRE(RisaSyncTranslator::armPacket(120)  == A{0xF9, 0x52, 0x00, 0x01, 0x18}); // ppq 5 -> tick 24
    REQUIRE(RisaSyncTranslator::armPacket(95)   == A{0xF9, 0x52, 0x00, 0x00, 0x5F}); // last grid position
}

TEST_CASE("RisaSyncTranslator Start arms from the top then streams 23 clocks", "[n8]") {
    RisaSyncTranslator        t;
    std::vector<std::uint8_t> out;

    feed(t, {0xFA}, out);  // MIDI Start
    // Arm+locate at the top (barrier) then FA - exactly risaSync.ts's transport-rise packet.
    REQUIRE(out == std::vector<std::uint8_t>{0xF9, 0x52, 0x00, 0x00, 0x00, 0xFA});
    REQUIRE(t.playing());

    // 24 clocks in the first quarter, but risa primes the armed clock itself, so only 23 F8 are emitted.
    out.clear();
    for (int i = 0; i < 24; ++i) feed(t, {0xF8}, out);
    REQUIRE(out == std::vector<std::uint8_t>(23, 0xF8));

    // A second quarter re-arms nothing and streams all 24.
    out.clear();
    for (int i = 0; i < 24; ++i) feed(t, {0xF8}, out);
    REQUIRE(out == std::vector<std::uint8_t>(24, 0xF8));
}

TEST_CASE("RisaSyncTranslator Stop emits FC and gates further clocks", "[n8]") {
    RisaSyncTranslator        t;
    std::vector<std::uint8_t> out;

    feed(t, {0xFA}, out);
    for (int i = 0; i < 5; ++i) feed(t, {0xF8}, out);
    out.clear();

    feed(t, {0xFC}, out);  // Stop
    REQUIRE(out == std::vector<std::uint8_t>{0xFC});
    REQUIRE_FALSE(t.playing());

    // Clocks after a stop are ignored (transport-gated), no bytes emitted.
    out.clear();
    for (int i = 0; i < 8; ++i) feed(t, {0xF8}, out);
    REQUIRE(out.empty());
}

TEST_CASE("RisaSyncTranslator Continue arms from the Song Position", "[n8]") {
    RisaSyncTranslator        t;
    std::vector<std::uint8_t> out;

    // Song Position Pointer -> 256 sixteenths = 1536 clocks (ppq 64). F2 lsb msb, 14-bit: 256 = 0x0100.
    feed(t, {0xF2, 0x00, 0x02}, out);
    REQUIRE(out.empty());               // SPP itself emits nothing
    REQUIRE(t.absoluteClock() == 1536);

    feed(t, {0xFB}, out);               // Continue -> arm at the current position
    REQUIRE(out == std::vector<std::uint8_t>{0xF9, 0x52, 0x01, 0x00, 0x00, 0xFA});
    REQUIRE(t.playing());
}

TEST_CASE("RisaSyncTranslator ignores non-transport MIDI and stopped clocks", "[n8]") {
    RisaSyncTranslator        t;
    std::vector<std::uint8_t> out;

    feed(t, {0x90, 0x3C, 0x7F}, out);   // note-on
    feed(t, {0xB0, 0x07, 0x64}, out);   // CC volume
    feed(t, {0xF8}, out);               // clock while stopped
    REQUIRE(out.empty());
    REQUIRE_FALSE(t.playing());

    // After a start, note-on still emits nothing but clocks flow.
    feed(t, {0xFA}, out);
    out.clear();
    feed(t, {0x90, 0x40, 0x7F}, out);
    REQUIRE(out.empty());
    for (int i = 0; i < 3; ++i) feed(t, {0xF8}, out);
    REQUIRE(countByte(out, 0xF8) == 2); // 3 clocks, first (armed) suppressed
}
