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
        host_ = {RP_PROVIDER_ABI_VERSION, &engine, configDir_.c_str(), &install, &clear, &reserveMidi,
                 &suspendHardwareInputs, &restored};
    }
    ProviderHost(const ProviderHost&) = delete;
    ProviderHost& operator=(const ProviderHost&) = delete;

    const rp_device_host* get() const { return &host_; }

private:
    // Neither throws across the C table (rp_provider.h): the registry is a fixed table, and noexcept says so here, so a
    // registry that one day could would end the process at this boundary rather than unwind through a provider.
    static bool install(void* ctx, const char* name, const char* interfaceId, void* link, void (*acquire)(void*),
                        void (*release)(void*)) noexcept {
        return static_cast<Engine*>(ctx)->installDevice(name, interfaceId, link, acquire, release);
    }
    static void clear(void* ctx, void* link) noexcept { static_cast<Engine*>(ctx)->clearDevice(link); }

    // A product implements these when it owns a shared MIDI stream to keep a claimed pair out of, hardware inputs a scan
    // has to step aside for, and MIDI and audio it held until the control plane said it had restored its selection
    // (the SDL standalone's MidiIo: docs/modular-graph-phase-f3-design.md section 3). A host with no such stream (the
    // headless host, the CLI) records NOTHING: a pair is not reserved, so it was never in, which is what reserving
    // means for a stream that does not exist; nothing is suspended, since there is nothing to suspend; and nothing was
    // held, so there is nothing to resume.
    static bool reserveMidi(void*, const char*, const char*) noexcept { return true; }
    static void suspendHardwareInputs(void*, bool) noexcept {}
    // The rendezvous's product half: the SDL holds MIDI and audio for the claim and resumes them here (stage 4's
    // MidiIo). A host that holds nothing has nothing to resume, so it records nothing.
    static void restored(void*) noexcept {}

    std::string    configDir_;
    rp_device_host host_{};
};

}  // namespace rp::host
