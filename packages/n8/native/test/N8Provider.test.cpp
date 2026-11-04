// The N8 pack's provider and facet (n8/desktop/N8Provider.hpp, N8Rpc.hpp; docs/modular-graph-phase-e2-design.md section
// 6), over FakeN8: the facet's four link methods read and set an N8Host's state, through the class and through a server
// they are mounted on (rpcpp's JSON codec here; the products mount them over QuickJS's), and a null service mounts
// nothing; the provider installs its cart link under the N8's name and interface, hands the registry hooks that reach
// that link, brings n8.cfg back at restore, and clears the link before it frees it. Run by `pnpm test:n8`.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "TypedRpcServer.h"
#include "codecs/JsonCodec.h"
#include "transports/QueueTransport.h"

#include "FakeN8.hpp"
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

using JsonServer = rpcpp::TypedRpcServer<rpcpp::Empty, rpcpp::JsonCodec>;

std::string ask(JsonServer& server, const std::string& method, const std::string& params = "[]") {
    const auto out =
        server.processMessage(R"({"jsonrpc":"2.0","id":1,"method":")" + method + R"(","params":)" + params + "}");
    return out ? *out : std::string();
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
    rp::n8::N8Rpc   rpc(host);

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

TEST_CASE("registerRpc mounts the four methods on a server, and a null service mounts none", "[n8-rpc]") {
    FakeN8        dev;
    N8Host        host(factoryFor(dev), twoPorts(), cleanDir("rp-n8-rpc-test").string());
    rp::n8::N8Rpc rpc(host);

    rpcpp::QueueTransport<rpcpp::JsonCodec> transport;
    JsonServer                              server(transport);
    rp::n8::registerRpc(server, &rpc);
    std::string reply = ask(server, "n8LinkConfig");
    INFO(reply);
    CHECK(has(reply, R"({"port":"/dev/ttyS0","isN8":false})"));
    CHECK(has(reply, R"("connected":false)"));
    CHECK(has(ask(server, "n8Connect", "[true]"), R"("result":true)"));
    CHECK(has(ask(server, "n8SetLookahead", "[30]"), R"("result":true)"));
    CHECK(has(ask(server, "n8SetPort", R"(["/dev/ttyACM0"])"), R"("result":true)"));
    reply = ask(server, "n8LinkConfig");
    CHECK(has(reply, R"("connected":true)"));
    CHECK(has(reply, R"("lookaheadMs":30)"));
    CHECK(has(reply, R"("selectedPort":"/dev/ttyACM0")"));

    rpcpp::QueueTransport<rpcpp::JsonCodec> otherTransport;
    JsonServer                              other(otherTransport);
    rp::n8::registerRpc(other, nullptr);
    CHECK(has(ask(other, "n8LinkConfig"), "-32601"));  // Method not found
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
