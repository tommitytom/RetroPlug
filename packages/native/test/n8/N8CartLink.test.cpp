// Guards the cart device link rp.host.devices/1 hands rp.n8.nes (host/n8/N8CartLink.hpp), over a real N8Host and its
// N8Link: a push reaches the cart as the FIFO bytes the timed scheduler writes, timed at the block's rate; a push
// with no cart loses its bytes and is counted; the expansion volume a node's param drives goes out through the link's
// control path and again on every connect; and the connected state is the link's own.
//
// The fake serial port answers the Edio handshake and records every byte, as N8Host.test.cpp's does: no `serial` lib,
// no hardware. The link's reliable-link probe is off (setLinkAllowed), so the wire holds only what a push and a control
// write put there.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
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

// What every fake port written to one device shares: the bytes that reached the wire, mutex-guarded because the link's
// serial thread writes them while the test reads.
struct FakeDevice {
    void append(const std::uint8_t* data, std::size_t n) {
        std::lock_guard<std::mutex> l(mu_);
        written_.insert(written_.end(), data, data + n);
    }
    std::vector<std::uint8_t> wire() {
        std::lock_guard<std::mutex> l(mu_);
        return written_;
    }

private:
    std::mutex                mu_;
    std::vector<std::uint8_t> written_;
};

// The handshake (CMD_STATUS -> 0xA500) so connect succeeds, then the config block the host reads to decide the volume.
struct FakePort : ISerialPort {
    explicit FakePort(FakeDevice& dev) : dev_(dev) {
        toRead = {0x00, 0xA5};
        for (int i = 0; i < Edio::SIZE_CFG; ++i) toRead.push_back(0);  // map_idx 0: no expansion audio
    }
    std::size_t write(const std::uint8_t* data, std::size_t n) override {
        dev_.append(data, n);
        return n;
    }
    std::size_t read(std::uint8_t* b, std::size_t n, int) override {
        std::size_t i = 0;
        while (i < n && !toRead.empty()) { b[i++] = toRead.front(); toRead.pop_front(); }
        return i;
    }
    void flushInput() override {}

    std::deque<std::uint8_t> toRead;

private:
    FakeDevice& dev_;
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

    // The value is an atomic and a generation the serial thread consumes, so a knob on any thread (a node's param runs
    // on the block thread in stage 3) neither locks nor allocates here. Many changes from another thread, then a final
    // one: the serial thread must end on the final value, whatever it wrote on the way.
    std::thread writer([&link] {
        for (int v = 0; v < 64; ++v) link.setExpansionVolume(v);
        link.setExpansionVolume(200);
    });
    writer.join();
    REQUIRE(waitForExpVolValue(dev, 200));
    const auto values = expVolWrites(dev.wire());
    CHECK(values.back() == 200);
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
    // else pushes at it here. The old paths pushing at the same N8Link (until stage 6) do not reach the adapter's.
    CHECK(after == host.link().pushesDropped());
}
