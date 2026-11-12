// The N8 pack's provider and facet (n8/desktop/N8Provider.hpp, N8Rpc.hpp; docs/modular-graph-phase-e2-design.md section
// 6), over FakeN8: the facet's four link methods read and set an N8Host's state, through the class and through a server
// they are mounted on (the products' own, over the QuickJS codec: QjsRpc.hpp), and a null service mounts
// nothing; its sessions open a port through the provider's factory, run the primitives, the menu and the operations on
// it and answer each as a reply (its status and the CLI's words), and do not open the streaming link's port; the
// provider installs its cart link under the N8's name and interface, hands the registry hooks that reach that link,
// brings n8.cfg back at restore, and clears the link before it frees it. Run by `pnpm test:n8`.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "FakeN8.hpp"
#include "QjsRpc.hpp"
#include "n8/CartLink.hpp"
#include "n8/desktop/N8CartLink.hpp"
#include "n8/desktop/N8Host.hpp"
#include "n8/desktop/N8Provider.hpp"
#include "n8/desktop/N8Rpc.hpp"

using retroplug::N8ConfigDto;
using retroplug::N8Host;
using retroplug::N8PortDto;
using retroplug::test::FakeN8;
using retroplug::test::factoryFor;

namespace {

N8Host::PortLister twoPorts() {
    return [] { return std::vector<N8PortDto>{{"/dev/ttyS0", false}, {"/dev/ttyACM0", true}}; };
}

std::filesystem::path cleanDir(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::create_directories(dir);
    std::filesystem::remove(dir / "n8.cfg");
    return dir;
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// A device host recording what the provider installs and clears.
struct Registry {
    void* link = nullptr;
    void (*acquire)(void*) = nullptr;
    void (*release)(void*) = nullptr;
    std::string name, interfaceId;
    std::vector<void*> cleared;
    // What the link read when it was cleared: clear comes before the link is freed, so reading through it here is
    // reading a live object (ASan would catch the provider freeing it first).
    std::vector<bool> connectedAtClear;
    bool refuse = false;
    std::string dir;

    rp_device_host host() {
        rp_device_host h{};
        h.abi_version = RP_PROVIDER_ABI_VERSION;
        h.ctx         = this;
        h.config_dir  = dir.c_str();
        h.install     = [](void* ctx, const char* n, const char* id, void* l, void (*a)(void*), void (*r)(void*)) {
            auto& self = *static_cast<Registry*>(ctx);
            if (self.refuse) return false;
            self.name = n, self.interfaceId = id, self.link = l, self.acquire = a, self.release = r;
            return true;
        };
        h.clear = [](void* ctx, void* l) {
            auto& self = *static_cast<Registry*>(ctx);
            self.cleared.push_back(l);
            self.connectedAtClear.push_back(static_cast<rp::n8::CartLink*>(l)->connected());
        };
        return h;
    }
};

}  // namespace

TEST_CASE("the N8 facet's link methods read and set the host's state", "[n8-rpc]") {
    FakeN8          dev;
    const auto      dir = cleanDir("rp-n8-rpc-test");
    N8Host          host(factoryFor(dev), twoPorts(), dir.string());
    rp::n8::N8Rpc   rpc(host, factoryFor(dev), twoPorts());

    N8ConfigDto c = rpc.n8LinkConfig();
    REQUIRE(c.ports.size() == 2);
    CHECK(c.ports[1].port == "/dev/ttyACM0");
    CHECK(c.ports[1].isN8);
    CHECK_FALSE(c.connected);
    CHECK_FALSE(c.enabled);

    CHECK(rpc.n8Connect(true));
    c = rpc.n8LinkConfig();
    CHECK(c.enabled);
    CHECK(c.connected);
    CHECK(c.selectedPort == "/dev/ttyACM0");  // auto-picked: the port flagged as an N8

    CHECK(rpc.n8SetLookahead(25));
    CHECK(rpc.n8LinkConfig().lookaheadMs == 25);
    CHECK(rpc.n8SetPort("/dev/ttyACM1"));
    c = rpc.n8LinkConfig();
    CHECK(c.selectedPort == "/dev/ttyACM1");
    CHECK(c.connected);  // a live link moves to the new port

    CHECK(rpc.n8Connect(false));
    CHECK_FALSE(rpc.n8LinkConfig().connected);
    // Each one is persisted where N8Host keeps it.
    std::ifstream cfg(dir / "n8.cfg");
    std::string   port;
    std::getline(cfg, port);
    CHECK(port == "/dev/ttyACM1");
}

TEST_CASE("registerRpc mounts the facet's methods on a server, and a null service mounts none", "[n8-rpc]") {
    FakeN8        dev;
    N8Host        host(factoryFor(dev), twoPorts(), cleanDir("rp-n8-rpc-test").string());
    rp::n8::N8Rpc rpc(host, factoryFor(dev), twoPorts());

    rp::n8::test::QjsRpc server;
    rp::n8::registerRpc(server.server(), &rpc);
    std::string reply = server.ask("n8LinkConfig");
    INFO(reply);
    CHECK(has(reply, R"({"port":"/dev/ttyS0","isN8":false})"));
    CHECK(has(reply, R"("connected":false)"));
    CHECK(has(server.ask("n8Connect", "[true]"), R"("result":true)"));
    CHECK(has(server.ask("n8SetLookahead", "[30]"), R"("result":true)"));
    CHECK(has(server.ask("n8SetPort", R"(["/dev/ttyACM0"])"), R"("result":true)"));
    reply = server.ask("n8LinkConfig");
    CHECK(has(reply, R"("connected":true)"));
    CHECK(has(reply, R"("lookaheadMs":30)"));
    CHECK(has(reply, R"("selectedPort":"/dev/ttyACM0")"));

    // The session half beside it: a port opened by name, an operation on its handle, a failure's reply.
    reply = server.ask("n8ListPorts");
    CHECK(has(reply, R"({"port":"/dev/ttyACM0","isN8":true})"));
    CHECK(rpc.n8Connect(false));  // the streaming link lets go of /dev/ttyACM0
    reply = server.ask("n8Open", R"(["/dev/ttyACM0"])");
    INFO(reply);
    CHECK(has(reply, R"("status":"ok")"));
    CHECK(has(reply, R"("value":0)"));
    CHECK(has(server.ask("n8Handshake", "[0, 0]"), R"("status":"ok")"));
    CHECK(has(server.ask("n8DirMake", R"([0, "songs"])"), R"("status":"ok")"));
    reply = server.ask("n8ListDir", R"([0, "songs"])");
    CHECK(has(reply, R"("entries":[])"));
    reply = server.ask("n8FileDelete", R"([0, "songs/none.sav"])");
    CHECK(has(reply, R"("status":"device")"));
    CHECK(has(reply, R"("deviceStatus":4)"));
    CHECK(has(reply, R"("message":"Edio: operation error 0x04")"));
    // Bytes cross as a Uint8Array each way, as the products' codec carries them.
    CHECK(has(server.ask("n8WriteFile", R"([0, "songs/a.bin", new Uint8Array([1, 2, 250])])"), R"("status":"ok")"));
    CHECK(has(server.ask("n8ReadFile", R"([0, "songs/a.bin"])"), R"("bytes":[1,2,250])"));
    CHECK(has(server.ask("n8Close", "[0]"), R"("result":true)"));
    CHECK(has(server.ask("n8Close", "[0]"), R"("result":false)"));

    rp::n8::test::QjsRpc other;
    rp::n8::registerRpc(other.server(), nullptr);
    CHECK(has(other.ask("n8LinkConfig"), "-32601"));  // Method not found
    CHECK(has(other.ask("n8Open", R"(["/dev/ttyACM0"])"), "-32601"));
}

namespace {
rfl::Bytestring bytesOf(const std::vector<std::uint8_t>& v) {
    const auto* b = reinterpret_cast<const std::byte*>(v.data());
    return rfl::Bytestring(b, b + v.size());
}
std::vector<std::uint8_t> vec(const rfl::Bytestring& b) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(b.data());
    return std::vector<std::uint8_t>(p, p + b.size());
}
}  // namespace

TEST_CASE("a session runs the primitives, the menu and the operations on its handle, each answered as a reply",
          "[n8-rpc]") {
    FakeN8        dev;
    N8Host        host(factoryFor(dev), twoPorts(), cleanDir("rp-n8-rpc-test").string());
    rp::n8::N8Rpc rpc(host, factoryFor(dev), twoPorts());
    for (std::size_t i = 0; i < dev.sysInfo.size(); ++i) dev.sysInfo[i] = static_cast<std::uint8_t>(i);
    dev.freeBytes = 123456789012ull;

    const rp::n8::N8Reply opened = rpc.n8Open("/dev/ttyACM0");
    REQUIRE(opened.status == "ok");
    const auto h = static_cast<std::int32_t>(opened.value);
    CHECK(rpc.n8Handshake(h, 0).status == "ok");
    CHECK(vec(rpc.n8SysInfo(h).bytes) == dev.sysInfo);
    CHECK(rpc.n8Vdc(h).bytes.size() == 8);
    CHECK(rpc.n8FreeSpace(h).value == 123456789012ull);

    // A file round trip, then a ROM booted with its battery.
    const std::vector<std::uint8_t> data = {1, 2, 3, 4, 5};
    CHECK(rpc.n8WriteFile(h, "d/x.bin", bytesOf(data)).status == "ok");
    CHECK(vec(rpc.n8ReadFile(h, "d/x.bin").bytes) == data);
    const rp::n8::N8Reply listed = rpc.n8ListDir(h, "d");
    REQUIRE(listed.entries.size() == 1);
    CHECK(listed.entries[0].name == "x.bin");
    CHECK(listed.entries[0].size == 5);
    const rp::n8::N8Reply missing = rpc.n8ReadFile(h, "d/y.bin");
    CHECK(missing.status == "notFound");
    CHECK(missing.step == "list");
    CHECK(missing.message == "Edio: file not found on SD: d/y.bin");

    CHECK(rpc.n8MenuTest(h).status == "ok");
    const std::vector<std::uint8_t> rom(2000, 0x4E), srm(100, 0x53);
    const rp::n8::N8Reply booted = rpc.n8BootRom(h, "usb-games/g.nes", bytesOf(rom), bytesOf(srm), true);
    CHECK(booted.status == "ok");
    CHECK(booted.value == 69);
    CHECK(dev.booted == "usb-games/g.nes");
    CHECK(dev.files["EDN8/gamedata/g.nes/bram.srm"] == srm);

    // A running game: the menu's test goes unanswered, and the reply is the CLI's N8TimeoutError.
    const rp::n8::N8Reply test = rpc.n8MenuTest(h);
    CHECK(test.status == "timeout");
    CHECK(test.message == "Edio: serial read timeout (no N8 response)");
    // The cart's FPGA config block, which says what runs (mapConfig.ts decodes it).
    const rp::n8::N8Reply cfg = rpc.n8MemRD(h, static_cast<std::uint32_t>(rp::n8::Edio::ADDR_CFG), 16);
    REQUIRE(cfg.bytes.size() == 16);
    CHECK(vec(cfg.bytes)[0] == 69);
    CHECK(rpc.n8FifoWR(h, bytesOf({0x90, 0x3C, 0x7F})).status == "ok");
    CHECK(dev.count("fifo:90 3C 7F") == 1);

    CHECK(rpc.n8Close(h));
    const rp::n8::N8Reply closed = rpc.n8Status(h);
    CHECK(closed.status == "linkDown");
    CHECK(closed.message == "no open N8 session (handle 0)");
}

TEST_CASE("a session does not open the port the streaming link holds, nor one its factory cannot open", "[n8-rpc]") {
    FakeN8        dev;
    N8Host        host(factoryFor(dev), twoPorts(), cleanDir("rp-n8-rpc-test").string());
    rp::n8::N8Rpc rpc(host, factoryFor(dev), twoPorts());
    host.connect(true);  // auto-picks /dev/ttyACM0
    REQUIRE(host.getConfig().connected);
    const rp::n8::N8Reply held = rpc.n8Open("/dev/ttyACM0");
    CHECK(held.status == "linkDown");
    CHECK(held.message == "cannot open serial port: /dev/ttyACM0");
    CHECK(rpc.n8Open("/dev/ttyS0").status == "ok");  // another port is not the link's
    host.connect(false);

    rp::n8::N8Rpc refusing(host, [](const std::string&) -> std::unique_ptr<retroplug::PortTransport> {
        throw std::runtime_error("no such port");
    }, twoPorts());
    const rp::n8::N8Reply absent = refusing.n8Open("/dev/ttyACM7");
    CHECK(absent.status == "linkDown");
    CHECK(absent.message == "cannot open serial port: /dev/ttyACM7");

    // A port another process holds says so, in the words the transport gave.
    rp::n8::N8Rpc busy(host, [](const std::string& port) -> std::unique_ptr<retroplug::PortTransport> {
        throw retroplug::PortBusyError("the serial port " + port + " is busy: another program has it open");
    }, twoPorts());
    const rp::n8::N8Reply taken = busy.n8Open("/dev/ttyACM0");
    CHECK(taken.status == "linkDown");
    CHECK(taken.message == "the serial port /dev/ttyACM0 is busy: another program has it open");
}

TEST_CASE("the N8 provider installs its cart link, restores n8.cfg and clears the link before it frees it",
          "[n8-provider]") {
    FakeN8   dev;
    Registry registry;
    registry.dir = cleanDir("rp-n8-provider-test").string();
    {
        std::ofstream cfg(registry.dir + "/n8.cfg");
        cfg << "/dev/ttyACM0\n0\n1\n";  // the port, a lookahead of 0 ms, streaming left enabled
    }
    const rp_device_host host = registry.host();
    const rp_provider*   provider = rp_n8_provider();
    REQUIRE(std::string(provider->id) == "n8");

    void* self = rp::n8::createProvider(&host, factoryFor(dev), twoPorts());
    REQUIRE(self != nullptr);
    CHECK(registry.name == rp::n8::kDeviceName);
    CHECK(registry.interfaceId == rp::n8::kCartLinkInterface);
    REQUIRE(registry.link != nullptr);
    REQUIRE(registry.acquire != nullptr);
    REQUIRE(registry.release != nullptr);
    auto* cart = static_cast<rp::n8::CartLink*>(registry.link);
    CHECK_FALSE(cart->connected());
    // The hooks reach the cart link's count of the nodes holding it: two nodes binding it (a handover), then both
    // giving it back.
    auto& counted = static_cast<retroplug::N8CartLink&>(*cart);
    CHECK(counted.holders() == 0);
    registry.acquire(registry.link);
    registry.acquire(registry.link);
    CHECK(counted.holders() == 2);
    registry.release(registry.link);
    CHECK(counted.holders() == 1);
    registry.release(registry.link);
    CHECK(counted.holders() == 0);

    // Nothing connects until restore, which reads n8.cfg.
    N8Host& n8 = rp::n8::providerHost(self);
    CHECK_FALSE(n8.getConfig().connected);
    provider->restore(self);
    CHECK(n8.getConfig().connected);
    CHECK(n8.getConfig().lookaheadMs == 0);
    CHECK(cart->connected());

    // The facet's service is the provider's, over the same host.
    auto* rpc = static_cast<rp::n8::N8Rpc*>(provider->rpc_service(self));
    REQUIRE(rpc != nullptr);
    CHECK(rpc->n8LinkConfig().selectedPort == "/dev/ttyACM0");

    void* const installed = registry.link;
    provider->destroy(self);
    CHECK(registry.cleared == std::vector<void*>{installed});
    CHECK(registry.connectedAtClear == std::vector<bool>{true});  // read through the link, still streaming
}

TEST_CASE("the N8 provider refuses another ABI, and one whose link the registry refuses clears nothing",
          "[n8-provider]") {
    FakeN8   dev;
    Registry registry;
    registry.dir        = cleanDir("rp-n8-provider-test").string();
    rp_device_host host = registry.host();
    host.abi_version    = RP_PROVIDER_ABI_VERSION + 1;
    CHECK(rp::n8::createProvider(&host, factoryFor(dev), twoPorts()) == nullptr);
    CHECK(rp::n8::createProvider(nullptr, factoryFor(dev), twoPorts()) == nullptr);

    registry.refuse = true;
    host            = registry.host();
    void* self      = rp::n8::createProvider(&host, factoryFor(dev), twoPorts());
    REQUIRE(self != nullptr);  // it runs, its node on its stub
    CHECK(registry.link == nullptr);
    rp_n8_provider()->destroy(self);
    CHECK(registry.cleared.empty());
}

TEST_CASE("one writer to an N8: the link does not connect to a session's port, by name or by the device it names",
          "[n8-rpc]") {
    FakeN8        dev;
    N8Host        host(factoryFor(dev), twoPorts(), cleanDir("rp-n8-rpc-test").string());
    auto          rpc = std::make_unique<rp::n8::N8Rpc>(host, factoryFor(dev), twoPorts());
    const auto    opened = rpc->n8Open("/dev/ttyACM0");
    REQUIRE(opened.status == "ok");
    host.connect(true);  // auto-picks /dev/ttyACM0, which the session holds
    N8ConfigDto c = host.getConfig();
    CHECK_FALSE(c.connected);
    CHECK(c.enabled);  // as a connect that failed leaves it
    CHECK(c.error == "the port /dev/ttyACM0 is open in an N8 session");
    CHECK(rpc->n8Close(static_cast<std::int32_t>(opened.value)));
    host.connect(true);
    CHECK(host.getConfig().connected);
    host.connect(false);

    // A symlink and the file it points at are one port (a /dev/serial/by-id name and its /dev/ttyACM), either way.
#if defined(_WIN32)
    return;  // Windows compares port names alone (samePort in N8Rpc.cpp): a COM name has no symlink to resolve
#endif
    const auto dir    = cleanDir("rp-n8-rpc-identity");
    const auto device = dir / "ttyFAKE0";
    const auto byId   = dir / "usb-krikzz_N8-if00";
    std::ofstream(device).put('\0');
    std::filesystem::remove(byId);
    std::error_code linked;
    std::filesystem::create_symlink(device, byId, linked);
    if (linked) return;  // no symlinks here (an unprivileged Windows): the by-name half above is what holds
    const auto session = rpc->n8Open(byId.string());
    REQUIRE(session.status == "ok");
    host.setPort(device.string());
    host.connect(true);
    CHECK_FALSE(host.getConfig().connected);
    CHECK(rpc->n8Close(static_cast<std::int32_t>(session.value)));
    host.connect(true);
    REQUIRE(host.getConfig().connected);
    CHECK(rpc->n8Open(byId.string()).message == "cannot open serial port: " + byId.string());
    host.connect(false);
    // The facet unhooks itself as it goes: the host connects freely after it.
    rpc.reset();
    host.connect(true);
    CHECK(host.getConfig().connected);
    host.connect(false);
}

TEST_CASE("a listing's names reach the session as TextDecoder decoded them", "[n8-rpc]") {
    FakeN8        dev;
    N8Host        host(factoryFor(dev), twoPorts(), cleanDir("rp-n8-rpc-test").string());
    rp::n8::N8Rpc rpc(host, factoryFor(dev), twoPorts());
    // Well-formed names as they are; each maximal ill-formed subpart one U+FFFD, as the WHATWG decoder gives it.
    const std::vector<std::pair<std::string, std::string>> names = {
        {"plain.nes", "plain.nes"},
        {"\xC3\xA9t\xC3\xA9.sav", "\xC3\xA9t\xC3\xA9.sav"},                    // valid two-byte sequences
        {"a\xF0\x9F\x8E\xB5", "a\xF0\x9F\x8E\xB5"},                            // a valid four-byte one
        {"b\xFF", "b\xEF\xBF\xBD"},                                            // a byte that never leads
        {"c\xC0\xAF", "c\xEF\xBF\xBD\xEF\xBF\xBD"},                            // an overlong lead, then a stray
        {"d\xE2\x82", "d\xEF\xBF\xBD"},                                        // a sequence cut short
        {"e\xED\xA0\x80", "e\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"},            // a surrogate's encoding
        {"f\xE2\x82x", "f\xEF\xBF\xBDx"},                                      // cut short by an ASCII byte
        {"\xEF\xBB\xBFg", "g"},                                                // a leading BOM, dropped
    };
    for (const auto& [raw, decoded] : names) dev.files["d/" + raw] = {1};
    const rp::n8::N8Reply opened = rpc.n8Open("/dev/ttyACM0");
    const auto            h      = static_cast<std::int32_t>(opened.value);
    const rp::n8::N8Reply listed = rpc.n8ListDir(h, "d");
    REQUIRE(listed.status == "ok");
    std::vector<std::string> got;
    for (const auto& e : listed.entries) got.push_back(e.name);
    std::vector<std::string> want;
    for (const auto& [raw, decoded] : dev.files) want.push_back(raw);
    for (auto& w : want) {
        w = w.substr(2);
        for (const auto& [raw, decoded] : names)
            if (w == raw) w = decoded;
    }
    CHECK(got == want);  // in the card's order (FakeN8's files map)
}

namespace {
// A port that takes every write and fails every read in the port itself, as deps/serial throwing on a gone device does
// through SerialTransport.
struct GonePort : retroplug::PortTransport {
    std::size_t write(const std::uint8_t*, std::size_t size) override { return size; }
    std::size_t read(std::uint8_t*, std::size_t, std::uint32_t) override {
        failed_ = true;
        return 0;
    }
    void        flushInput() override {}
    std::string failure() const override { return failed_ ? "read failed: Input/output error" : ""; }

private:
    bool failed_ = false;
};
}  // namespace

TEST_CASE("a read the port itself failed is linkDown in the port's words, not a timeout", "[n8-rpc]") {
    FakeN8        dev;
    N8Host        host(factoryFor(dev), twoPorts(), cleanDir("rp-n8-rpc-test").string());
    rp::n8::N8Rpc rpc(host, [](const std::string&) -> std::unique_ptr<retroplug::PortTransport> {
        return std::make_unique<GonePort>();
    }, twoPorts());
    const auto h     = static_cast<std::int32_t>(rpc.n8Open("/dev/ttyACM0").value);
    const auto reply = rpc.n8Handshake(h, 0);
    CHECK(reply.status == "linkDown");
    CHECK(reply.message == "read failed: Input/output error");
}

TEST_CASE("portSaid is empty for every refusal made before the port, and the port's words otherwise", "[n8-rpc]") {
    GonePort port;
    std::uint8_t byte = 0;
    REQUIRE(port.read(&byte, 1, 0) == 0);  // the port's last call failed, and says so still
    using rp::n8::Code;
    using rp::n8::Status;
    using rp::n8::Where;
    const auto refused = [](Where where) { return Status{Code::TooLarge, where}; };
    CHECK(retroplug::portSaid(refused(Where::Length), port).empty());
    CHECK(retroplug::portSaid(refused(Where::BatteryPath), port).empty());
    CHECK(retroplug::portSaid(refused(Where::Size), port).empty());
    CHECK(retroplug::portSaid(refused(Where::Buffer), port) == "read failed: Input/output error");
    CHECK(retroplug::portSaid(Status{Code::Timeout, Where::Read}, port) == "read failed: Input/output error");
}

TEST_CASE("a call refused before it reached the port is tooLarge in the protocol's words, whatever the port said last",
          "[n8-rpc]") {
    FakeN8                       dev;
    N8Host                       host(factoryFor(dev), twoPorts(), cleanDir("rp-n8-rpc-test").string());
    retroplug::test::FakeN8Port* port = nullptr;
    rp::n8::N8Rpc rpc(host, [&](const std::string&) -> std::unique_ptr<retroplug::PortTransport> {
        auto p = std::make_unique<retroplug::test::FakeN8Port>(dev);
        port   = p.get();
        return p;
    }, twoPorts());
    const auto h = static_cast<std::int32_t>(rpc.n8Open("/dev/ttyACM0").value);
    REQUIRE(rpc.n8Handshake(h, 0).status == "ok");
    // The port's last call failed, and says so still. A flush, not a failed call on the line: that would leave the
    // session dirty, and the refused call's resync would then answer for the port before the refusal could.
    dev.failOnFlushInput = 1;
    REQUIRE(rpc.n8FlushInput(h).status == "ok");
    REQUIRE(port->failure() == "fake port: flushInput failed");

    const auto refused = rpc.n8MenuInstall(h, std::string(0x10000, 'x'));
    CHECK(refused.status == "tooLarge");
    CHECK(refused.message.rfind("Edio: a string longer than 65535 bytes (", 0) == 0);
}

namespace {
// A cart port counting the CMD_STATUS commands written to it.
struct CountingPort : retroplug::test::FakeN8Port {
    using retroplug::test::FakeN8Port::FakeN8Port;
    int         statusWrites = 0;
    std::size_t write(const std::uint8_t* data, std::size_t size) override {
        if (size >= 4 && data[0] == 0x2B && data[2] == rp::n8::Edio::CMD_STATUS) ++statusWrites;
        return retroplug::test::FakeN8Port::write(data, size);
    }
};
}  // namespace

TEST_CASE("a call that left the line in step (refused before its first byte, a file a listing did not hold) does not "
          "resync the next",
          "[n8-rpc]") {
    FakeN8        dev;
    N8Host        host(factoryFor(dev), twoPorts(), cleanDir("rp-n8-rpc-test").string());
    CountingPort* port = nullptr;
    rp::n8::N8Rpc rpc(host, [&](const std::string&) -> std::unique_ptr<retroplug::PortTransport> {
        auto p = std::make_unique<CountingPort>(dev);
        port   = p.get();
        return p;
    }, twoPorts());
    dev.files["d/a.bin"] = {1, 2, 3};
    const auto h = static_cast<std::int32_t>(rpc.n8Open("/dev/ttyACM0").value);
    REQUIRE(rpc.n8Handshake(h, 0).status == "ok");
    REQUIRE(port->statusWrites == 1);
    const auto ssr = static_cast<std::uint32_t>(rp::n8::Edio::ADDR_SSR);

    CHECK(rpc.n8MenuInstall(h, std::string(0x10000, 'x')).status == "tooLarge");
    CHECK(rpc.n8MemRD(h, ssr, 16).status == "ok");
    CHECK(port->statusWrites == 1);  // no resync handshake went out after the refusal
    // A listing writes a status of its own, so the missing file is judged by what the call after it adds: nothing.
    CHECK(rpc.n8ReadFile(h, "d/missing.bin").status == "notFound");
    const int afterListing = port->statusWrites;
    CHECK(rpc.n8MemRD(h, ssr, 16).status == "ok");
    CHECK(port->statusWrites == afterListing);
}

namespace {
// A port that behaves as a line does, so a call that gave up part-way leaves it out of step: the N8 answers a command
// only once it is written (CMD_STATUS with <0> A5, CMD_MEM_RD with `nextMemRd`), a flush drops only what has arrived,
// and `late` bytes - the rest of a reply the caller gave up on - arrive once the line has gone quiet (the caller's own
// timeout), ahead of anything asked for after them.
struct LinePort : retroplug::PortTransport {
    std::deque<std::uint8_t>  arrived;
    std::vector<std::uint8_t> late, nextMemRd;
    bool                      wentQuiet = false;
    int                       statusWrites = 0;  // the CMD_STATUS commands written
    // The timeout of the read that answered each one.
    std::vector<std::uint32_t> statusTimeouts;
    bool                       awaitingStatus = false;

    std::size_t write(const std::uint8_t* data, std::size_t size) override {
        const int cmd = size >= 4 && data[0] == 0x2B ? data[2] : -1;
        if (cmd == rp::n8::Edio::CMD_STATUS) {
            ++statusWrites;
            awaitingStatus = true;
            arrived.insert(arrived.end(), {0x00, 0xA5});
        }
        if (cmd == rp::n8::Edio::CMD_MEM_RD) {
            arrived.insert(arrived.end(), nextMemRd.begin(), nextMemRd.end());
            nextMemRd.clear();
        }
        return size;
    }
    std::size_t read(std::uint8_t* buffer, std::size_t size, std::uint32_t timeoutMs) override {
        if (awaitingStatus) statusTimeouts.push_back(timeoutMs);
        awaitingStatus = false;
        if (wentQuiet && !late.empty()) {
            arrived.insert(arrived.begin(), late.begin(), late.end());
            late.clear();
        }
        if (arrived.empty()) {
            wentQuiet = wentQuiet || !late.empty();
            return 0;
        }
        std::size_t n = 0;
        for (; n < size && !arrived.empty(); ++n) {
            buffer[n] = arrived.front();
            arrived.pop_front();
        }
        return n;
    }
    void flushInput() override { arrived.clear(); }
};

// A port that died after its handshake: every write is taken (a write to a gone device can still appear to go out),
// every read fails in the port, in its words, and what the session wrote is kept, one command a write.
struct DeadPort : retroplug::PortTransport {
    bool                      dead = false;
    std::vector<std::uint8_t> commands;  // each write's command byte
    std::deque<std::uint8_t>  arrived;
    std::string               failed;

    std::size_t write(const std::uint8_t* data, std::size_t size) override {
        failed.clear();
        if (size >= 4 && data[0] == 0x2B) commands.push_back(data[2]);
        if (!dead && size >= 4 && data[0] == 0x2B && data[2] == rp::n8::Edio::CMD_STATUS)
            arrived.insert(arrived.end(), {0x00, 0xA5});
        return size;
    }
    std::size_t read(std::uint8_t* buffer, std::size_t size, std::uint32_t) override {
        failed = dead ? "read failed: Input/output error" : "";
        std::size_t n = 0;
        for (; !dead && n < size && !arrived.empty(); ++n) {
            buffer[n] = arrived.front();
            arrived.pop_front();
        }
        return n;
    }
    void        flushInput() override { arrived.clear(); }
    std::string failure() const override { return failed; }
};
}  // namespace

TEST_CASE("the call after one that failed part-way drains what was still arriving and handshakes before it runs",
          "[n8-rpc]") {
    FakeN8        dev;
    N8Host        host(factoryFor(dev), twoPorts(), cleanDir("rp-n8-rpc-test").string());
    LinePort*     line = nullptr;
    rp::n8::N8Rpc rpc(host, [&line](const std::string&) -> std::unique_ptr<retroplug::PortTransport> {
        auto port = std::make_unique<LinePort>();
        line      = port.get();
        return port;
    }, twoPorts());
    const auto h = static_cast<std::int32_t>(rpc.n8Open("/dev/ttyACM0").value);
    REQUIRE(rpc.n8Handshake(h, 0).status == "ok");
    const auto ssr = static_cast<std::uint32_t>(rp::n8::Edio::ADDR_SSR);

    // A sniff that gets 10 of its 512 bytes and gives up; the other 502 arrive after it did.
    line->nextMemRd.assign(10, 0x11);
    line->late.assign(502, 0x11);
    CHECK(rpc.n8MemRD(h, ssr, 512).status == "timeout");

    // The next call's own reply. With nothing in between, the late bytes come first and are taken for it.
    line->nextMemRd.assign(512, 0x22);
    const rp::n8::N8Reply next  = rpc.n8MemRD(h, ssr, 512);
    const auto            bytes = vec(next.bytes);
    CHECK(next.status == "ok");
    CHECK(std::count(bytes.begin(), bytes.end(), 0x22) == 512);
    // The resync cleared the session, so the call after it runs without one: no further CMD_STATUS went out (the
    // opening handshake and the one resync are the two).
    line->nextMemRd.assign(512, 0x22);
    CHECK(rpc.n8MemRD(h, ssr, 512).status == "ok");
    CHECK(line->statusWrites == 2);
}

TEST_CASE("a resync handshakes with the timeout the session was connected with", "[n8-rpc]") {
    FakeN8        dev;
    N8Host        host(factoryFor(dev), twoPorts(), cleanDir("rp-n8-rpc-test").string());
    LinePort*     line = nullptr;
    rp::n8::N8Rpc rpc(host, [&line](const std::string&) -> std::unique_ptr<retroplug::PortTransport> {
        auto port = std::make_unique<LinePort>();
        line      = port.get();
        return port;
    }, twoPorts());
    const auto h = static_cast<std::int32_t>(rpc.n8Open("/dev/ttyACM0").value);
    REQUIRE(rpc.n8Handshake(h, 1000).status == "ok");
    const auto ssr = static_cast<std::uint32_t>(rp::n8::Edio::ADDR_SSR);
    line->nextMemRd.assign(10, 0x11);
    line->late.assign(502, 0x11);
    CHECK(rpc.n8MemRD(h, ssr, 512).status == "timeout");
    line->nextMemRd.assign(512, 0x22);
    CHECK(rpc.n8MemRD(h, ssr, 512).status == "ok");
    CHECK(line->statusTimeouts == std::vector<std::uint32_t>{1000, 1000});
}

TEST_CASE("a handshake on a dirty session drains, then runs once with the caller's timeout and clears the session",
          "[n8-rpc]") {
    FakeN8        dev;
    N8Host        host(factoryFor(dev), twoPorts(), cleanDir("rp-n8-rpc-test").string());
    LinePort*     line = nullptr;
    rp::n8::N8Rpc rpc(host, [&line](const std::string&) -> std::unique_ptr<retroplug::PortTransport> {
        auto port = std::make_unique<LinePort>();
        line      = port.get();
        return port;
    }, twoPorts());
    const auto h = static_cast<std::int32_t>(rpc.n8Open("/dev/ttyACM0").value);
    REQUIRE(rpc.n8Handshake(h, 0).status == "ok");
    const auto ssr = static_cast<std::uint32_t>(rp::n8::Edio::ADDR_SSR);
    line->nextMemRd.assign(10, 0x11);
    line->late.assign(502, 0x11);
    CHECK(rpc.n8MemRD(h, ssr, 512).status == "timeout");

    CHECK(rpc.n8Handshake(h, 5000).status == "ok");
    CHECK(line->statusWrites == 2);  // the opening one and this: one handshake, the caller's
    CHECK(line->statusTimeouts.back() == 5000);
    line->nextMemRd.assign(512, 0x22);
    CHECK(rpc.n8MemRD(h, ssr, 512).status == "ok");
    CHECK(line->statusWrites == 2);  // and the session is clean: the call after it resyncs nothing
}

TEST_CASE("on a dead port the call after a failure answers the handshake's failure in the port's words and is not sent",
          "[n8-rpc]") {
    FakeN8        dev;
    N8Host        host(factoryFor(dev), twoPorts(), cleanDir("rp-n8-rpc-test").string());
    DeadPort*     port = nullptr;
    rp::n8::N8Rpc rpc(host, [&port](const std::string&) -> std::unique_ptr<retroplug::PortTransport> {
        auto p = std::make_unique<DeadPort>();
        port   = p.get();
        return p;
    }, twoPorts());
    const auto h = static_cast<std::int32_t>(rpc.n8Open("/dev/ttyACM0").value);
    REQUIRE(rpc.n8Handshake(h, 0).status == "ok");
    const auto ssr = static_cast<std::uint32_t>(rp::n8::Edio::ADDR_SSR);

    port->dead = true;
    port->commands.clear();
    const rp::n8::N8Reply first = rpc.n8MemRD(h, ssr, 512);
    CHECK(first.status == "linkDown");
    CHECK(first.message == "read failed: Input/output error");
    // Each call after it re-runs the handshake, which a dead port cannot answer: the call answers that failure, in the
    // port's words, and its own command never goes out onto a line that did not answer.
    for (int i = 0; i < 2; ++i) {
        const rp::n8::N8Reply again = rpc.n8MemRD(h, ssr, 512);
        CHECK(again.status == "linkDown");
        CHECK(again.message == "read failed: Input/output error");
    }
    CHECK(port->commands == std::vector<std::uint8_t>{rp::n8::Edio::CMD_MEM_RD, rp::n8::Edio::CMD_STATUS,
                                                      rp::n8::Edio::CMD_STATUS});
}
