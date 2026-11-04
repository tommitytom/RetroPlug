#pragma once

// The rp_device_host a product hands its providers (graph/rp_provider.h, docs/modular-graph-phase-e2-design.md section
// 6): install and clear forward to the engine's device registry (Engine::installDevice and clearDevice, whose rules are
// DeviceRegistry's), and the config dir is the product's. A product declares it after its engine and before the
// rp::Providers it starts with it, so it outlives every provider.

#include <string>
#include <utility>

#include "graph/rp_provider.h"
#include "host/engine/Engine.hpp"

namespace rp::host {

class ProviderHost {
public:
    ProviderHost(Engine& engine, std::string configDir) : configDir_(std::move(configDir)) {
        host_ = {RP_PROVIDER_ABI_VERSION, &engine, configDir_.c_str(), &install, &clear};
    }
    ProviderHost(const ProviderHost&) = delete;
    ProviderHost& operator=(const ProviderHost&) = delete;

    const rp_device_host* get() const { return &host_; }

private:
    static bool install(void* ctx, const char* name, const char* interfaceId, void* link, void (*acquire)(void*),
                        void (*release)(void*)) {
        return static_cast<Engine*>(ctx)->installDevice(name, interfaceId, link, acquire, release);
    }
    static void clear(void* ctx, void* link) { static_cast<Engine*>(ctx)->clearDevice(link); }

    std::string    configDir_;
    rp_device_host host_{};
};

}  // namespace rp::host
