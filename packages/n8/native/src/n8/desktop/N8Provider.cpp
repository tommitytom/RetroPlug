#include "n8/desktop/N8Provider.hpp"

#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "n8/CartLink.hpp"
#include "n8/desktop/N8CartLink.hpp"
#include "n8/desktop/N8Rpc.hpp"
#include "n8/desktop/WjwwoodSerialPort.hpp"

namespace rp::n8 {
namespace {

// Everything the provider owns, in the order it is built and the reverse of the order it goes: the host and the
// config dir it was handed (copied, so the provider never reads the product's strings after create), the N8Host, the
// cart link over it and the facet over it. The cart link is cleared from the registry before any of them is freed, so
// a lookup after the stop finds nothing: an acquire answers null, and the node that asks runs on its stub. That does
// not make it safe to stop the provider under a plan, as a node holds the link it acquired and may use it in its
// destroy (n8/CartLink.hpp): a product reclaims its plans before it stops its providers (graph/rp_provider.h).
struct Provider {
    Provider(const rp_device_host& given, retroplug::N8Link::PortFactory factory, retroplug::N8Host::PortLister lister)
        : host(given),
          configDir(given.config_dir ? given.config_dir : ""),
          n8(std::move(factory), std::move(lister), configDir),
          cart(n8),
          rpc(n8) {
        host.config_dir = configDir.c_str();
        // A refusal (a full table, the name taken) leaves rp.n8.nes on its stub, so it is said, not swallowed.
        installed = host.install && host.install(host.ctx, kDeviceName, kCartLinkInterface,
                                                 static_cast<CartLink*>(&cart), &acquireCart, &releaseCart);
        if (!installed) std::fprintf(stderr, "[retroplug] the N8 cart link was not installed\n");
    }
    ~Provider() {
        if (installed && host.clear) host.clear(host.ctx, static_cast<CartLink*>(&cart));
    }
    Provider(const Provider&) = delete;
    Provider& operator=(const Provider&) = delete;

    // The registry's hooks: a node's acquire and release reach the cart link, which counts the nodes holding it.
    static retroplug::N8CartLink& cartOf(void* link) {
        return static_cast<retroplug::N8CartLink&>(*static_cast<CartLink*>(link));
    }
    static void acquireCart(void* link) { cartOf(link).acquire(); }
    static void releaseCart(void* link) { cartOf(link).release(); }

    rp_device_host         host;
    std::string            configDir;
    retroplug::N8Host      n8;
    retroplug::N8CartLink  cart;
    N8Rpc                  rpc;
    bool                   installed = false;
};

Provider& providerOf(void* self) { return *static_cast<Provider*>(self); }

// The table's entries throw nothing (graph/rp_provider.h): a create that throws (an allocation, n8.cfg's directory)
// answers null and says why, and a restore that throws says why and leaves the link down.
void* createWithSerial(const rp_device_host* host) noexcept {
    try {
        return createProvider(
            host, [](const std::string& port) -> std::unique_ptr<retroplug::ISerialPort> {
                return std::make_unique<retroplug::WjwwoodSerialPort>(port);
            },
            [] {
                std::vector<retroplug::N8PortDto> ports;
                for (const auto& p : retroplug::listSerialPorts()) ports.push_back({p.port, p.isN8});
                return ports;
            });
    } catch (...) {
        std::fprintf(stderr, "[retroplug] the N8 provider did not start\n");
        return nullptr;
    }
}

// Load n8.cfg and reconnect the link if it was left enabled.
void restore(void* self) noexcept {
    try {
        retroplug::N8Host& n8 = providerOf(self).n8;
        n8.restore();
        if (const auto c = n8.getConfig(); c.enabled)
            std::fprintf(stderr, "[retroplug] N8 restore: port '%s'%s\n", c.selectedPort.c_str(),
                         c.connected ? "" : " - not connected");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[retroplug] N8 restore failed: %s\n", e.what());
    } catch (...) {
        std::fprintf(stderr, "[retroplug] N8 restore failed\n");
    }
}

void* rpcService(void* self) noexcept { return &providerOf(self).rpc; }

void destroy(void* self) noexcept { delete static_cast<Provider*>(self); }

}  // namespace

void* createProvider(const rp_device_host* host, retroplug::N8Link::PortFactory factory,
                     retroplug::N8Host::PortLister lister) {
    if (!host || host->abi_version != RP_PROVIDER_ABI_VERSION) return nullptr;
    try {
        return new Provider(*host, std::move(factory), std::move(lister));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[retroplug] the N8 provider did not start: %s\n", e.what());
    } catch (...) {
        std::fprintf(stderr, "[retroplug] the N8 provider did not start\n");
    }
    return nullptr;
}

retroplug::N8Host& providerHost(void* self) { return providerOf(self).n8; }

}  // namespace rp::n8

extern "C" const rp_provider* rp_n8_provider(void) {
    static const rp_provider kProvider{RP_PROVIDER_ABI_VERSION, "n8", &rp::n8::createWithSerial, &rp::n8::restore,
                                       &rp::n8::rpcService, &rp::n8::destroy};
    return &kProvider;
}
