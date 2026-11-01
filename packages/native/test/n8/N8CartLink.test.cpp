// Guards the cart device link rp.host.devices/1 hands rp.n8.nes (host/n8/N8CartLink.hpp), over a real N8Host and its
// N8Link: a push reaches the cart as the FIFO bytes the timed scheduler writes, timed at the block's rate; a push
// with no cart loses its bytes and is counted; the expansion volume a node's param drives goes out through the link's
// control path and again on every connect; and the connected state is the link's own.
//
// The fake serial port answers the Edio commands the link writes and records every byte, as N8Host.test.cpp's does: no
// `serial` lib, no hardware. The link's reliable-link probe is off (setLinkAllowed), so the wire holds only what a push
// and a control write put there.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "host/devices/DeviceLinks.hpp"
#include "host/n8/Edio.hpp"  // ISerialPort
#include "host/n8/N8CartLink.hpp"
#include "host/n8/N8Host.hpp"

using retroplug::Edio;
using retroplug::ISerialPort;
using retroplug::N8CartLink;
using retroplug::N8Host;
using retroplug::N8Link;
using retroplug::N8PortDto;

namespace {

// The cart's side of the wire: every byte that reached it, the FPGA config block the host reads (its map_idx: 255
// while the menu shows, the running game's mapper otherwise), and the writes a port that has stopped answering
// refuses. Mutex-guarded, because the link's serial thread writes and reads it while the test does.
struct FakeDevice {
    std::vector<std::uint8_t> wire() {
        std::lock_guard<std::mutex> l(mu);
        return written;
    }

    std::mutex                mu;
    std::vector<std::uint8_t> written;
    int                       mapper             = 0;  // the config block's map_idx
    int                       expVolAttempts     = 0;  // every write to ADDR_EXP_VOL the host made
    int                       refuseExpVolWrites = 0;  // the next N of those throw instead of reaching the device
};

// A fake serial port that answers the Edio commands the link writes - the status handshake, a memory read of the
// config block, a one-byte memory write - the way the cart does. A refused expansion-volume write is taken back off
// the wire: it never reached the device.
struct FakePort : ISerialPort {
    explicit FakePort(FakeDevice& dev) : dev_(dev) {}

    std::size_t write(const std::uint8_t* data, std::size_t n) override {
        std::lock_guard<std::mutex> l(dev_.mu);
        dev_.written.insert(dev_.written.end(), data, data + n);
        in_.insert(in_.end(), data, data + n);
        parse();
        return n;
    }
    std::size_t read(std::uint8_t* b, std::size_t n, int) override {
        std::lock_guard<std::mutex> l(dev_.mu);
        std::size_t i = 0;
        while (i < n && !out_.empty()) { b[i++] = out_.front(); out_.pop_front(); }
        return i;
    }
    void flushInput() override {
        std::lock_guard<std::mutex> l(dev_.mu);
        out_.clear();
    }

private:
    static std::uint32_t le32(const std::vector<std::uint8_t>& b, std::size_t at) {
        return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
    }
    // The config block's byte at `at`, for a read of the whole block: map_idx low 8 at 0, its high nibble at 2.
    std::uint8_t cfgByte(std::uint32_t at) const {
        const std::int64_t off = static_cast<std::int64_t>(at) - Edio::ADDR_CFG;
        if (off < 0 || off >= static_cast<std::int64_t>(Edio::SIZE_CFG)) return 0;
        if (off == 0) return static_cast<std::uint8_t>(dev_.mapper & 0xFF);
        if (off == 2) return static_cast<std::uint8_t>(((dev_.mapper >> 8) & 0x0F) << 4);
        return 0;
    }
    void parse() {
        for (;;) {
            if (in_.size() < 4) return;
            const std::uint8_t cmd  = in_[2];
            std::size_t        need = 4;
            if (cmd == Edio::CMD_MEM_RD) need = 13;
            if (cmd == Edio::CMD_MEM_WR) need = in_.size() >= 13 ? 13 + le32(in_, 8) : 13;
            if (in_.size() < need) return;
            const std::vector<std::uint8_t> frame(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(need));
            in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(need));
            if (cmd == Edio::CMD_STATUS) {
                out_.push_back(0x00);
                out_.push_back(0xA5);  // 0xA500: the handshake's OK
            } else if (cmd == Edio::CMD_MEM_RD) {
                const std::uint32_t addr = le32(frame, 4), size = le32(frame, 8);
                for (std::uint32_t i = 0; i < size; ++i) out_.push_back(cfgByte(addr + i));
            } else if (cmd == Edio::CMD_MEM_WR) {
                if (le32(frame, 4) != static_cast<std::uint32_t>(Edio::ADDR_EXP_VOL)) continue;
                ++dev_.expVolAttempts;
                if (dev_.refuseExpVolWrites > 0) {
                    --dev_.refuseExpVolWrites;
                    dev_.written.resize(dev_.written.size() - frame.size());  // never reached the device
                    throw std::runtime_error("the port is gone");
                }
            }
        }
    }

    FakeDevice&              dev_;
    std::vector<std::uint8_t> in_;
    std::deque<std::uint8_t> out_;
};

N8Link::PortFactory factoryFor(FakeDevice& dev) {
    return [&dev](const std::string&) -> std::unique_ptr<ISerialPort> { return std::make_unique<FakePort>(dev); };
}

N8Host::PortLister noPorts() {
    return [] { return std::vector<N8PortDto>{}; };
}

std::string tempCfgDir() {
    const auto dir = std::filesystem::temp_directory_path() / "rp-n8-cart-link-test";
    std::filesystem::create_directories(dir);
    std::filesystem::remove(dir / "n8.cfg");  // start clean
    return dir.string();
}

// The 14 bytes an Edio one-byte memWR to `addr` puts on the wire, rebuilt from the protocol rather than hardcoded.
std::vector<std::uint8_t> memWrFrame(std::int32_t addr, std::uint8_t value) {
    const auto le32 = [](std::uint32_t v, std::vector<std::uint8_t>& out) {
        for (int i = 0; i < 4; i++) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    };
    std::vector<std::uint8_t> f{'+', static_cast<std::uint8_t>('+' ^ 0xFF), Edio::CMD_MEM_WR,
                                static_cast<std::uint8_t>(Edio::CMD_MEM_WR ^ 0xFF)};
    le32(static_cast<std::uint32_t>(addr), f);
    le32(1, f);
    f.push_back(0);  // exec flag
    f.push_back(value);
    return f;
}

// The values written to the expansion-volume register, in wire order.
std::vector<int> expVolWrites(const std::vector<std::uint8_t>& wire) {
    const auto full   = memWrFrame(Edio::ADDR_EXP_VOL, 0);
    const auto prefix = std::vector<std::uint8_t>(full.begin(), full.end() - 1);
    std::vector<int> values;
    for (std::size_t i = 0; i + full.size() <= wire.size(); ++i)
        if (std::equal(prefix.begin(), prefix.end(), wire.begin() + static_cast<std::ptrdiff_t>(i)))
            values.push_back(wire[i + full.size() - 1]);
    return values;
}

// Wait for the wire to hold `n` bytes: the serial thread writes each chunk at its scheduled moment, so a test reads
// after the block it pushed in rather than during it.
bool waitForBytes(FakeDevice& dev, std::size_t n) {
    for (int i = 0; i < 2000; ++i) {
        if (dev.wire().size() >= n) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool waitForExpVolWrites(FakeDevice& dev, std::size_t n) {
    for (int i = 0; i < 2000; ++i) {
        if (expVolWrites(dev.wire()).size() >= n) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool waitForExpVolValue(FakeDevice& dev, int value) {
    for (int i = 0; i < 2000; ++i) {
        const auto values = expVolWrites(dev.wire());
        if (!values.empty() && values.back() == value) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

}  // namespace

TEST_CASE("N8CartLink: a push reaches the cart as FIFO writes, timed at the block's rate", "[n8]") {
    FakeDevice dev;
    N8Host     host(factoryFor(dev), noPorts(), tempCfgDir());
    host.link().setLinkAllowed(false);  // no probe: the wire below is the pushes alone
    REQUIRE(host.link().connect("fake"));
    N8CartLink link(host);
    REQUIRE(link.connected());
    const std::size_t base = dev.wire().size();  // what connect put there: the pushes are the tail

    // Two one-byte pushes, the second 1 ms into the block at 48 kHz: the link's scheduler releases each at its offset,
    // so both are on the wire once the later one is due.
    const std::uint8_t first[1]  = {0x90};
    const std::uint8_t second[1] = {0x80};
    link.push(0, first, 1, false, 48000.0);
    link.push(48, second, 1, true, 48000.0);

    const auto firstFrame  = memWrFrame(Edio::ADDR_FIFO, 0x90);
    const auto secondFrame = memWrFrame(Edio::ADDR_FIFO, 0x80);
    REQUIRE(waitForBytes(dev, base + firstFrame.size() + secondFrame.size()));
    // The frames the pushes must put on the wire, after everything connect wrote (the status handshake, and the config
    // block the expansion volume's Auto reads off the running cart).
    const auto wire = dev.wire();
    REQUIRE(wire.size() == base + firstFrame.size() + secondFrame.size());
    CHECK(std::equal(firstFrame.begin(), firstFrame.end(), wire.begin() + static_cast<std::ptrdiff_t>(base)));
    CHECK(std::equal(secondFrame.begin(), secondFrame.end(),
                     wire.begin() + static_cast<std::ptrdiff_t>(base + firstFrame.size())));
    // `flush` is not a byte on the wire: on the physical side the arm byte is the barrier the emulated flag stands for.
    CHECK(link.dropped() == 0);
}

TEST_CASE("N8CartLink: a push with no cart loses its bytes and says so", "[n8]") {
    FakeDevice dev;
    N8Host     host(factoryFor(dev), noPorts(), tempCfgDir());
    N8CartLink link(host);
    REQUIRE_FALSE(link.connected());

    const std::uint8_t bytes[3] = {0x90, 0x40, 0x7F};
    link.push(0, bytes, sizeof bytes, false, 48000.0);
    link.push(0, bytes, sizeof bytes, false, 48000.0);
    CHECK(link.dropped() == 2);
    CHECK(dev.wire().empty());
}

TEST_CASE("N8CartLink: the expansion volume goes out on a live link and again on every connect", "[n8]") {
    FakeDevice dev;
    N8Host     host(factoryFor(dev), noPorts(), tempCfgDir());
    host.link().setLinkAllowed(false);

    // Set before the link comes up: the connect path applies it, the way the project's knob reaches a cart plugged in
    // afterwards.
    N8CartLink link(host);
    link.setExpansionVolume(200);
    REQUIRE(host.link().connect("fake"));
    REQUIRE(waitForExpVolWrites(dev, 1));
    CHECK(expVolWrites(dev.wire()) == std::vector<int>{200});

    // Moved on a live link: posted through the serial thread, without tearing the stream down.
    link.setExpansionVolume(64);
    REQUIRE(waitForExpVolWrites(dev, 2));
    CHECK(expVolWrites(dev.wire()) == std::vector<int>{200, 64});

    // And a reconnect re-applies the standing value.
    host.link().disconnect();
    REQUIRE(host.link().connect("fake"));
    REQUIRE(waitForExpVolWrites(dev, 3));
    CHECK(expVolWrites(dev.wire()) == std::vector<int>{200, 64, 64});
}

TEST_CASE("N8CartLink: the expansion volume may be set from another thread while the link streams", "[n8]") {
    FakeDevice dev;
    N8Host     host(factoryFor(dev), noPorts(), tempCfgDir());
    host.link().setLinkAllowed(false);
    N8CartLink link(host);
    link.setExpansionVolume(64);  // an explicit standing value: this fake cart reports no expansion-audio mapper, so
                                  // Auto would write nothing and there would be no connect-time baseline below
    REQUIRE(host.link().connect("fake"));
    REQUIRE(waitForExpVolWrites(dev, 1));

    // The value is an atomic and a generation the serial thread consumes, so a knob on any thread (a cart node's param
    // runs on the block thread) neither locks nor allocates here. Many changes from another thread, then a final one:
    // the serial thread must end on the final value, whatever it wrote on the way.
    std::thread writer([&link] {
        for (int v = 0; v < 64; ++v) link.setExpansionVolume(v);
        link.setExpansionVolume(200);
    });
    writer.join();
    REQUIRE(waitForExpVolValue(dev, 200));
    const auto values = expVolWrites(dev.wire());
    CHECK(values.back() == 200);
}

TEST_CASE("N8CartLink: a control write the port refuses is retried, and never takes the link down", "[n8]") {
    FakeDevice dev;
    dev.refuseExpVolWrites = 1;  // the standing value's connect-time write dies on the wire
    N8Host host(factoryFor(dev), noPorts(), tempCfgDir());
    host.link().setLinkAllowed(false);
    N8CartLink link(host);
    link.setExpansionVolume(200);
    REQUIRE(host.link().connect("fake"));  // a config write that fails must not fail the MIDI link
    // The generation is marked applied only after a write succeeds, so the serial thread's next control turn writes the
    // standing value again.
    REQUIRE(waitForExpVolWrites(dev, 1));
    CHECK(expVolWrites(dev.wire()) == std::vector<int>{200});
    CHECK(link.connected());
    {
        std::lock_guard<std::mutex> l(dev.mu);
        CHECK(dev.expVolAttempts == 2);
    }
}

TEST_CASE("N8CartLink: a control write that keeps failing is retried about once a second, not every turn", "[n8]") {
    FakeDevice dev;
    dev.refuseExpVolWrites = 1'000'000;  // a port that is open but refuses every config write
    N8Host host(factoryFor(dev), noPorts(), tempCfgDir());
    host.link().setLinkAllowed(false);
    N8CartLink link(host);
    link.setExpansionVolume(64);
    REQUIRE(host.link().connect("fake"));
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    int attempts;
    {
        std::lock_guard<std::mutex> l(dev.mu);
        attempts = dev.expVolAttempts;
    }
    // Two in 400 ms: the connect-time one, then the serial thread's first turn. The rest of the second waits, so a
    // wedged-but-open port cannot take the link's turns (and its config reads) from the MIDI stream.
    CHECK(attempts >= 1);
    CHECK(attempts <= 2);
    CHECK(link.connected());
}

TEST_CASE("N8CartLink: the last node to release the link puts the expansion volume back to the cart's own", "[n8]") {
    FakeDevice dev;
    dev.mapper = 69;  // Sunsoft 5B: a cart with its own sound chip, so Auto writes unity
    N8Host host(factoryFor(dev), noPorts(), tempCfgDir());
    host.link().setLinkAllowed(false);
    N8CartLink link(host);
    link.acquire();
    link.setExpansionVolume(64);
    REQUIRE(host.link().connect("fake"));
    REQUIRE(waitForExpVolWrites(dev, 1));
    CHECK(expVolWrites(dev.wire()) == std::vector<int>{64});

    // One node still drives the cart while the other releases: the register keeps the level it was given.
    link.acquire();
    link.release();
    link.setExpansionVolume(96);
    REQUIRE(waitForExpVolValue(dev, 96));

    // The last release: nothing in any plan drives the cart, so the register goes back to the cart's own setting
    // (unity for this mapper's Auto).
    link.release();
    REQUIRE(waitForExpVolValue(dev, N8Host::EXP_VOL_UNITY));
    CHECK(expVolWrites(dev.wire()).back() == N8Host::EXP_VOL_UNITY);
    CHECK(link.connected());
}

TEST_CASE("N8CartLink: a full ring loses and counts only the pushes it could not take", "[n8]") {
    FakeDevice dev;
    N8Host     host(factoryFor(dev), noPorts(), tempCfgDir());
    host.link().setLinkAllowed(false);
    REQUIRE(host.link().connect("fake"));
    N8CartLink link(host);

    // Every push is scheduled a second out, so the serial thread holds the first chunk and the ring fills behind it.
    // Each push the link refuses is counted by the adapter: a node reads the drops of ITS pushes, not every caller's
    // that shares the link.
    const std::uint8_t bytes[1] = {0x90};
    const auto         before   = link.dropped();
    for (int i = 0; i < 2000; ++i) link.push(48000, bytes, sizeof bytes, false, 48000.0);
    const auto after = link.dropped();
    REQUIRE(after > before);
    // The adapter counted exactly what the link would not take, and the link's own count is the same because nothing
    // else pushes at it here: a node keeps its own books on the push's answer, never on either count.
    CHECK(after == host.link().pushesDropped());
}
