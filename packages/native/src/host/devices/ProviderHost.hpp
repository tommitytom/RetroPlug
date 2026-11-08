#pragma once

// The rp_device_host a product hands its providers (graph/rp_provider.h, docs/modular-graph-phase-e2-design.md section
// 6): install and clear forward to the engine's device registry (Engine::installDevice and clearDevice, whose rules are
// DeviceRegistry's), and the config dir is the product's. A product declares it after its engine and before the
// rp::Providers it starts with it, so it outlives every provider.

#include <string>
#include <utility>
#include <vector>

#include "graph/rp_provider.h"
#include "host/engine/Engine.hpp"

namespace rp::host {

class ProviderHost {
public:
    // What a product does about the control-surface calls a provider makes (docs/modular-graph-phase-f3-design.md
    // section 3). Three kinds of product, and only the first implements anything:
    //
    //   - the SDL standalone owns a shared MIDI stream and holds MIDI and audio for the startup rendezvous, so it
    //     fills all three (its own calls over MidiIo);
    //   - the headless host and the CLI have no stream to keep a pair out of and nothing to hold, so they fill none
    //     and this host RECORDS what it was told (productCalls), which is what a test reads;
    //   - a product that holds nothing resumes nothing, so a null `restored` is "done" and not "unsupported".
    //
    // ctx is the product's, and every member is optional on its own.
    struct Calls {
        void* ctx = nullptr;
        bool (*reserveMidi)(void* ctx, const std::string& input, const std::string& output) = nullptr;
        void (*suspendHardwareInputs)(void* ctx, bool suspended) = nullptr;
        void (*restored)(void* ctx) = nullptr;
    };

    // `installLinks` false: this product installs the engine's device links itself, so a provider's install gets a
    // registry seat and nothing more (answered true, cleared as if it had been there). The headless host is the one
    // product that needs it: its graph's control surface is the harness link (a TEST stages pad presses into it through
    // the engine facet, which no port can), and it still starts the Launchpad pack's provider to exercise its FACET over
    // fake ports. A facet serves ports, not plans, so that one needs no registry at all.
    ProviderHost(Engine& engine, std::string configDir)
        : ProviderHost(engine, std::move(configDir), Calls{}, true) {}
    ProviderHost(Engine& engine, std::string configDir, Calls calls, bool installLinks = true)
        : engine_(&engine), configDir_(std::move(configDir)), calls_(calls), installLinks_(installLinks) {
        host_ = {RP_PROVIDER_ABI_VERSION, this, configDir_.c_str(), &install, &clear, &reserveMidi,
                 &suspendHardwareInputs, &restored};
    }
    ProviderHost(const ProviderHost&) = delete;
    ProviderHost& operator=(const ProviderHost&) = delete;

    const rp_device_host* get() const { return &host_; }

    /** Every control-surface call the providers made, in order, as "reserve_midi:<input>|<output>",
     *  "suspend_hardware_inputs:1" / ":0" or "restored" - the record a host with no stream of its own keeps for its
     *  tests (the headless host hands it to its engine facet, which drains it). A product that implements the calls
     *  itself records nothing here. */
    std::vector<std::string>&       productCalls() { return productCalls_; }
    const std::vector<std::string>& productCalls() const { return productCalls_; }

private:
    // Neither throws across the C table (rp_provider.h): the registry is a fixed table, and noexcept says so here, so a
    // registry that one day could would end the process at this boundary rather than unwind through a provider.
    static bool install(void* self, const char* name, const char* interfaceId, void* link, void (*acquire)(void*),
                        void (*release)(void*)) noexcept {
        auto& host = *static_cast<ProviderHost*>(self);
        if (!host.installLinks_) return true;  // a seat and nothing more; see the constructor
        return host.engine_->installDevice(name, interfaceId, link, acquire, release);
    }
    static void clear(void* self, void* link) noexcept {
        auto& host = *static_cast<ProviderHost*>(self);
        if (host.installLinks_) host.engine_->clearDevice(link);
    }

    // The table's ctx is this host, because the three calls below need the record as well as the product's own.
    static bool reserveMidi(void* self, const char* input, const char* output) noexcept {
        auto& host = *static_cast<ProviderHost*>(self);
        const std::string in  = input ? input : "";
        const std::string out = output ? output : "";
        if (host.calls_.reserveMidi)
            return host.calls_.reserveMidi(host.calls_.ctx, in, out);
        // A product with no shared stream holds nothing, so nothing can be kept out of one and a reservation is always
        // satisfied; the record is what a test reads.
        host.record("reserve_midi:" + in + "|" + out);
        return true;
    }
    static void suspendHardwareInputs(void* self, bool suspended) noexcept {
        auto& host = *static_cast<ProviderHost*>(self);
        if (host.calls_.suspendHardwareInputs) {
            host.calls_.suspendHardwareInputs(host.calls_.ctx, suspended);
            return;
        }
        host.record(std::string("suspend_hardware_inputs:") + (suspended ? "1" : "0"));
    }
    static void restored(void* self) noexcept {
        auto& host = *static_cast<ProviderHost*>(self);
        if (host.calls_.restored) {
            host.calls_.restored(host.calls_.ctx);
            return;
        }
        host.record("restored");
    }

    void record(std::string call) { productCalls_.push_back(std::move(call)); }

    // Declared in the order the constructor initialises them (the engine, the config dir, the product's calls, then
    // whether links are this host's): the other way round is a -Wreorder warning, and Windows builds with /W4 /WX.
    Engine*                  engine_;
    std::string              configDir_;
    Calls                    calls_;
    bool                     installLinks_;
    std::vector<std::string> productCalls_;
    rp_device_host           host_{};
};

}  // namespace rp::host
