// Guards the shared N8Host helper (config + persistence over an N8Link) without hardware: a fake ISerialPort
// answers the Edio handshake so connect() succeeds, and an injected port lister feeds the enumeration. Covers
// auto-pick, live port-switch, lookahead clamp, and the n8.cfg round-trip that both the SDL standalone and the
// DAW plugin rely on.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "host/n8/Edio.hpp"  // ISerialPort
#include "host/n8/N8Host.hpp"

using retroplug::ISerialPort;
using retroplug::N8Host;
using retroplug::N8Link;
using retroplug::N8PortDto;

namespace {

// A fake serial port that answers the Edio handshake (CMD_STATUS -> 0xA500 OK) so N8Link::connect succeeds
// without hardware; writes are ignored, the serial thread only writes.
struct FakePort : ISerialPort {
    std::deque<std::uint8_t> toRead{0x00, 0xA5};  // 0xA500 = handshake OK
    std::size_t write(const std::uint8_t*, std::size_t n) override { return n; }
    std::size_t read(std::uint8_t* b, std::size_t n, int) override {
        std::size_t i = 0;
        while (i < n && !toRead.empty()) { b[i++] = toRead.front(); toRead.pop_front(); }
        return i;
    }
    void flushInput() override {}
};

N8Link::PortFactory okFactory() {
    return [](const std::string&) -> std::unique_ptr<ISerialPort> { return std::make_unique<FakePort>(); };
}
N8Host::PortLister listerWith(std::vector<N8PortDto> ports) {
    return [ports]() { return ports; };
}
std::string tempCfgDir() {
    const auto dir = std::filesystem::temp_directory_path() / "rp-n8host-test";
    std::filesystem::create_directories(dir);
    std::filesystem::remove(dir / "n8.cfg");  // start clean
    return dir.string();
}

}  // namespace

TEST_CASE("N8Host.connect auto-picks the attached N8 and streams", "[n8host]") {
    N8Host host(okFactory(), listerWith({{"/dev/ttyS0", false}, {"/dev/ttyACM0", true}}), tempCfgDir());
    host.connect(true);
    const auto c = host.getConfig();
    REQUIRE(c.enabled);
    REQUIRE(c.connected);
    REQUIRE(c.selectedPort == "/dev/ttyACM0");  // the isN8 port, auto-picked
    REQUIRE(c.ports.size() == 2);
    host.connect(false);
    REQUIRE_FALSE(host.getConfig().connected);
    REQUIRE_FALSE(host.getConfig().enabled);
}

TEST_CASE("N8Host.setPort live-switches while streaming", "[n8host]") {
    N8Host host(okFactory(), listerWith({{"/dev/ttyACM0", true}}), tempCfgDir());
    host.connect(true);
    REQUIRE(host.getConfig().connected);
    host.setPort("/dev/ttyUSB9");
    const auto c = host.getConfig();
    REQUIRE(c.selectedPort == "/dev/ttyUSB9");
    REQUIRE(c.connected);  // still streaming, now on the new port (fake handshake OK)
}

TEST_CASE("N8Host.setLookahead is reflected and clamped", "[n8host]") {
    N8Host host(okFactory(), listerWith({}), tempCfgDir());
    host.setLookahead(25);
    REQUIRE(host.getConfig().lookaheadMs == 25);
    host.setLookahead(-5);
    REQUIRE(host.getConfig().lookaheadMs == 0);  // clamped
}

TEST_CASE("N8Host persists to n8.cfg and restore() reconnects", "[n8host]") {
    const std::string dir = tempCfgDir();
    {
        N8Host host(okFactory(), listerWith({{"/dev/ttyACM0", true}}), dir);
        host.setLookahead(15);
        host.connect(true);  // writes n8.cfg: port / lookahead / enabled=1
        REQUIRE(host.getConfig().connected);
    }
    // A fresh host over the same configDir restores the saved port/lookahead/enabled and reconnects.
    N8Host restored(okFactory(), listerWith({{"/dev/ttyACM0", true}}), dir);
    restored.restore();
    const auto c = restored.getConfig();
    REQUIRE(c.enabled);
    REQUIRE(c.connected);
    REQUIRE(c.selectedPort == "/dev/ttyACM0");
    REQUIRE(c.lookaheadMs == 15);
}
