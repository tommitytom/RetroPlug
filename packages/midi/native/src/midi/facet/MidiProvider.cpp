#include "midi/facet/MidiProvider.hpp"

#include <cstdio>
#include <exception>

#include "midi/RtMidiBackend.hpp"

namespace rp::midi {
namespace {

// Everything the provider owns: the backend it was handed (a reference: the product's, or the test's, outlives this)
// and the facet's service over it. Nothing is persisted here - the selection is the project's TypeScript, and a session
// opens a stream through the facet - so there is no state to restore.
struct Provider {
    Provider(MidiBackend& backend, const rp_device_host&) : rpc(backend) {}
    Provider(const Provider&)            = delete;
    Provider& operator=(const Provider&) = delete;

    MidiRpcService rpc;
};

Provider& providerOf(void* self) {
    return *static_cast<Provider*>(self);
}

// The table's entries throw nothing (graph/rp_provider.h): a create that throws (an allocation) answers null and says
// why.
void* createWithSystemMidi(const rp_device_host* host) noexcept {
    if (!host) return nullptr;
    try {
        return createProvider(host, systemMidi());
    } catch (...) {
        std::fprintf(stderr, "[retroplug] the MIDI provider did not start\n");
        return nullptr;
    }
}

// Nothing is persisted here: the selection is the project's (TypeScript), and the SDL app's own stream is its own.
void restore(void* self) noexcept {
    (void)self;
}

void* rpcService(void* self) noexcept {
    return &providerOf(self).rpc;
}

void destroy(void* self) noexcept {
    delete static_cast<Provider*>(self);
}

}  // namespace

void* createProvider(const rp_device_host* host, MidiBackend& backend) {
    if (!host || host->abi_version != RP_PROVIDER_ABI_VERSION) return nullptr;
    try {
        return new Provider(backend, *host);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[retroplug] the MIDI provider did not start: %s\n", e.what());
    } catch (...) {
        std::fprintf(stderr, "[retroplug] the MIDI provider did not start\n");
    }
    return nullptr;
}

MidiRpcService& providerService(void* self) {
    return providerOf(self).rpc;
}

}  // namespace rp::midi

extern "C" const rp_provider* rp_midi_provider(void) {
    // No `wake`: a stream has no writer thread to pump. The product's frame loop wakes what has one.
    static const rp_provider kProvider{RP_PROVIDER_ABI_VERSION, "midi", &rp::midi::createWithSystemMidi,
                                       &rp::midi::restore, &rp::midi::rpcService, &rp::midi::destroy, nullptr};
    return &kProvider;
}
