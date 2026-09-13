// Guards the shared N8Host helper (config + persistence over an N8Link) without hardware: a fake ISerialPort
// answers the Edio handshake so connect() succeeds, and an injected port lister feeds the enumeration. Covers
// auto-pick, live port-switch, lookahead clamp, the expansion-audio master volume the host writes when the
// link comes up, and the n8.cfg round-trip that both the SDL standalone and the DAW plugin rely on.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "host/n8/Edio.hpp"  // ISerialPort
#include "host/n8/N8Host.hpp"

using retroplug::Edio;
using retroplug::ISerialPort;
using retroplug::N8Host;
using retroplug::N8Link;
using retroplug::N8PortDto;

namespace {

// The state a run of fake ports shares: what cart the N8 claims to be running (the FPGA config block the host
// reads to decide the volume) and every byte written across all of them, so a test can assert what went out.
struct FakeDevice {
    int                       mapper = 0;  // map_idx the config block reports (0 = no expansion audio)
    std::vector<std::uint8_t> written;
};

// A fake serial port that answers the Edio handshake (CMD_STATUS -> 0xA500 OK) so N8Link::connect succeeds
// without hardware, then serves the 16-byte config block at Edio::ADDR_CFG. Reads are served in order, which
// is all the device does on this path (handshake, then at most one block read).
struct FakePort : ISerialPort {
    explicit FakePort(FakeDevice& dev) : dev_(dev) {
        toRead = {0x00, 0xA5};  // 0xA500 = handshake OK
        std::uint8_t cfg[Edio::SIZE_CFG] = {0};
        cfg[0] = static_cast<std::uint8_t>(dev.mapper & 0xFF);                  // map_idx low 8
        cfg[2] = static_cast<std::uint8_t>(((dev.mapper >> 8) & 0x0F) << 4);    // map_idx high nibble
        for (std::uint8_t b : cfg) toRead.push_back(b);
    }
    std::size_t write(const std::uint8_t* data, std::size_t n) override {
        dev_.written.insert(dev_.written.end(), data, data + n);
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

N8Link::PortFactory okFactory() {
    static FakeDevice unused;  // the tests that don't care about the wire share one sink
    return [](const std::string&) -> std::unique_ptr<ISerialPort> { return std::make_unique<FakePort>(unused); };
}

N8Link::PortFactory factoryFor(FakeDevice& dev) {
    return [&dev](const std::string&) -> std::unique_ptr<ISerialPort> { return std::make_unique<FakePort>(dev); };
}

// The 14 bytes an Edio one-byte memWR to `addr` puts on the wire: the 4-byte command frame, LE32 address,
// LE32 size, the exec flag, then the value. Rebuilt from the protocol rather than hardcoded so a framing
// change shows up here as a failure, not a false pass.
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

// Every value written to the expansion-volume register in `wire`, in wire order (empty = the host left the
// register alone). Matches on the frame up to the value, so it finds a write of any value.
std::vector<int> expVolWrites(const std::vector<std::uint8_t>& wire) {
    const auto  full   = memWrFrame(Edio::ADDR_EXP_VOL, 0);
    const auto  prefix = std::vector<std::uint8_t>(full.begin(), full.end() - 1);  // all but the value byte
    std::vector<int> values;
    for (std::size_t i = 0; i + full.size() <= wire.size(); i++)
        if (std::equal(prefix.begin(), prefix.end(), wire.begin() + static_cast<std::ptrdiff_t>(i)))
            values.push_back(wire[i + prefix.size()]);
    return values;
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

TEST_CASE("N8Host.connect sets the expansion volume to unity on an expansion-audio cart", "[n8host]") {
    // The whole point of the setting: master_vol is 0 after a power-cycle, so a VRC6 cart streamed from the
    // menu would come up with its extra voices silent. Auto (the default) recognises the running cart from the
    // FPGA config block and writes unity.
    FakeDevice dev;
    dev.mapper = 24;  // VRC6
    N8Host host(factoryFor(dev), listerWith({{"/dev/ttyACM0", true}}), tempCfgDir());
    host.connect(true);
    REQUIRE(host.getConfig().connected);
    REQUIRE(host.getConfig().expVol == N8Host::EXP_VOL_AUTO);
    REQUIRE(expVolWrites(dev.written) == std::vector<int>{N8Host::EXP_VOL_UNITY});
}

TEST_CASE("N8Host.connect leaves the expansion volume alone on a cart without expansion audio", "[n8host]") {
    // Auto rescues only the case it can recognise: an NROM cart has nothing for master_vol to scale, so the
    // register stays wherever the N8's own menu left it.
    FakeDevice dev;
    dev.mapper = 0;
    N8Host host(factoryFor(dev), listerWith({{"/dev/ttyACM0", true}}), tempCfgDir());
    host.connect(true);
    REQUIRE(host.getConfig().connected);
    REQUIRE(expVolWrites(dev.written).empty());
}

TEST_CASE("N8Host.setExpVol writes an explicit pick whatever is running, and applies it while streaming", "[n8host]") {
    FakeDevice dev;
    dev.mapper = 0;  // no expansion audio: Auto would write nothing, so every write below is the explicit pick
    N8Host host(factoryFor(dev), listerWith({{"/dev/ttyACM0", true}}), tempCfgDir());
    host.connect(true);
    REQUIRE(expVolWrites(dev.written).empty());

    host.setExpVol(64);  // picked while streaming -> bounces the link, so it lands now rather than next Connect
    REQUIRE(host.getConfig().expVol == 64);
    REQUIRE(host.getConfig().connected);
    REQUIRE(expVolWrites(dev.written) == std::vector<int>{64});

    host.setExpVol(9999);  // clamped to the register's range
    REQUIRE(host.getConfig().expVol == 255);
    REQUIRE(expVolWrites(dev.written) == std::vector<int>{64, 255});
}

TEST_CASE("N8Host persists the expansion volume and applies it on the restored connect", "[n8host]") {
    const std::string dir = tempCfgDir();
    {
        N8Host host(okFactory(), listerWith({{"/dev/ttyACM0", true}}), dir);
        host.setExpVol(192);
        host.connect(true);
    }
    FakeDevice dev;
    dev.mapper = 0;  // an explicit pick is written regardless of the cart
    N8Host restored(factoryFor(dev), listerWith({{"/dev/ttyACM0", true}}), dir);
    restored.restore();
    REQUIRE(restored.getConfig().expVol == 192);
    REQUIRE(expVolWrites(dev.written) == std::vector<int>{192});
}

TEST_CASE("N8Host reads an n8.cfg written before the expansion volume existed as Auto", "[n8host]") {
    const std::string dir = tempCfgDir();
    if (FILE* f = std::fopen((dir + "/n8.cfg").c_str(), "w")) {
        std::fprintf(f, "/dev/ttyACM0\n15\n0\n");  // the old three-line file: port / lookahead / enabled
        std::fclose(f);
    }
    N8Host host(okFactory(), listerWith({{"/dev/ttyACM0", true}}), dir);
    host.restore();
    const auto c = host.getConfig();
    REQUIRE(c.lookaheadMs == 15);
    REQUIRE(c.expVol == N8Host::EXP_VOL_AUTO);
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
