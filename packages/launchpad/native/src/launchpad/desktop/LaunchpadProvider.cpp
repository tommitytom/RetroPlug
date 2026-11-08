#include "launchpad/desktop/LaunchpadProvider.hpp"

#include <cstdio>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "launchpad/SurfaceLink.hpp"
#include "launchpad/desktop/RtMidiPort.hpp"

namespace rp::launchpad {
namespace {

// Everything the provider owns, in the order it is built and the reverse of the order it goes: the host it was handed
// (copied, so the product's strings are read once and outlive the provider), the link, the pool and the facet over both.
// The link is cleared from the registry before any of them is freed, so a lookup after the stop finds nothing: an
// acquire answers null and the node that asks runs on its stub. That does not make it safe to stop the provider under a
// plan, as a node holds the link it acquired (rp_device.h): a product reclaims its plans before it stops its providers.
struct Provider {
    Provider(const rp_device_host& given, MidiPortOpener portOpener, PortLister lister)
        : host(given),
          opener(std::move(portOpener)),
          link(opener),
          pool(opener),
          rpc(link, pool, &host, std::move(lister)) {
        // One claim per machine (PortClaim): two standalones restoring one config.json must not both take one device.
        // The config dir is the product's, copied above, so the locks live where its other state does.
        const std::string configDir = host.config_dir ? host.config_dir : "";
        rpc.setClaim([configDir](const std::string& port) { return claimPort(configDir, port); });
        // A scan's held edge: while the pool holds any input, the product suspends its shared stream's hardware inputs
        // (a Windows input is exclusive, so a scan could not otherwise hear the interface the user picked). A product
        // with nothing to suspend ignores the call.
        pool.setOnInputsHeld([this](bool held) {
            if (host.suspend_hardware_inputs) host.suspend_hardware_inputs(host.ctx, held);
        });
        // A refusal (a full table, the name taken) leaves the surface nodes on NoSurface, so it is said, not swallowed.
        installed = host.install && host.install(host.ctx, kDeviceName, kSurfaceLinkInterface,
                                                 static_cast<SurfaceLink*>(&link), nullptr, nullptr);
        if (!installed) std::fprintf(stderr, "[retroplug] the Launchpad surface link was not installed\n");
    }
    ~Provider() {
        if (installed && host.clear) host.clear(host.ctx, static_cast<SurfaceLink*>(&link));
        // The goodbye goes out while the cross-process claim is still held: the facet owns those tokens and is destroyed
        // before the link is, so without this another process could take the lock and open the port this one is still
        // writing its close bytes to.
        link.disconnect();
    }
    Provider(const Provider&)            = delete;
    Provider& operator=(const Provider&) = delete;

    rp_device_host host;
    MidiPortOpener opener;  // the link's and the pool's: a test's fake is therefore the one a scan gets too
    ControllerLink link;
    MidiPortPool   pool;
    LaunchpadRpc   rpc;
    bool           installed = false;
};

Provider& providerOf(void* self) {
    return *static_cast<Provider*>(self);
}

// The table's entries throw nothing (graph/rp_provider.h): a create that throws (an allocation) answers null and says
// why.
void* createWithRtMidi(const rp_device_host* host) noexcept {
    try {
        return createProvider(host, rtMidiPortOpener("RetroPlug Launchpad"),
                              [] {
                                  const PortNames names = listMidiPorts();
                                  return LaunchpadPortsDto{names.inputs, names.outputs};
                              });
    } catch (...) {
        std::fprintf(stderr, "[retroplug] the Launchpad provider did not start\n");
        return nullptr;
    }
}

// Nothing is persisted here: config.json's `controlSurface` is TS's, and the control plane claims the pair through the
// facet once it has read it. A product that starts the provider and never hears from TS holds no port.
void restore(void* self) noexcept {
    (void)self;
}

void* rpcService(void* self) noexcept {
    return &providerOf(self).rpc;
}

void destroy(void* self) noexcept {
    delete static_cast<Provider*>(self);
}

// The product's frame: what the audio thread queued for the device goes out now, rather than at the writer thread's
// next timeout. Without a writer thread pump() writes on this thread, which is what a test asserts deterministically.
void wake(void* self) noexcept {
    providerOf(self).link.pump();
}

}  // namespace

void* createProvider(const rp_device_host* host, MidiPortOpener opener, PortLister lister) {
    if (!host || host->abi_version != RP_PROVIDER_ABI_VERSION) return nullptr;
    try {
        return new Provider(*host, std::move(opener), std::move(lister));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[retroplug] the Launchpad provider did not start: %s\n", e.what());
    } catch (...) {
        std::fprintf(stderr, "[retroplug] the Launchpad provider did not start\n");
    }
    return nullptr;
}

ControllerLink& providerLink(void* self) {
    return providerOf(self).link;
}

}  // namespace rp::launchpad

extern "C" const rp_provider* rp_launchpad_provider(void) {
    static const rp_provider kProvider{RP_PROVIDER_ABI_VERSION, "launchpad", &rp::launchpad::createWithRtMidi,
                                       &rp::launchpad::restore, &rp::launchpad::rpcService, &rp::launchpad::destroy,
                                       &rp::launchpad::wake};
    return &kProvider;
}
