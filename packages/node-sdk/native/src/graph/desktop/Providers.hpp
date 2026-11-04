#pragma once

// The providers one product instance started (graph/rp_provider.h), in the order its packages list them. The generated
// rp_start_providers (build/packages/products/<product>/rp_product_providers.hpp, cmake/packages.cmake) makes one from
// the product's table; the product keeps it beside its engine, restores it where it restores its own state, and stops
// it after its plans are reclaimed. One per instance, not one per process: a DAW holds several plugin instances, each
// with its own engine, and each starts its own providers into its own engine's registry.

#ifdef RP_DEVICE
#error "graph/desktop/Providers.hpp is desktop-only: a device image runs no provider"
#endif

#include <cstddef>
#include <cstdio>
#include <utility>
#include <vector>

#include "graph/rp_provider.h"

namespace rp {

class Providers {
public:
    Providers() = default;
    // Creates each provider of `table` in order. One built against another ABI version is refused, and one whose create
    // answers null is left out, both said on stderr: the product carries on without it, its links never installed and
    // its facet never mounted (service() answers null). A throw, which the table's calls are not to make, stops what
    // was started before it goes on: the providers created so far are destroyed, the one whose record could not be
    // kept included, so none is left running with its links installed in an engine no Providers stops.
    Providers(const rp_provider_fn* table, std::size_t count, const rp_device_host* host) {
        try {
            started_.reserve(count);
            for (std::size_t i = 0; i < count; ++i) start(i, table[i](), host);
        } catch (...) {
            stop();
            throw;
        }
    }
    Providers(const Providers&) = delete;
    Providers& operator=(const Providers&) = delete;
    Providers(Providers&& other) noexcept : started_(std::exchange(other.started_, {})) {}
    Providers& operator=(Providers&& other) noexcept {
        if (this != &other) {
            stop();
            started_ = std::exchange(other.started_, {});
        }
        return *this;
    }
    ~Providers() { stop(); }

    // Each started provider's persisted state back, in order.
    void restore() {
        for (const Started& s : started_)
            if (s.self && s.provider->restore) s.provider->restore(s.self);
    }
    // Destroys each started provider in the reverse of the order they were created, so one created later (which may
    // build on an earlier one's links) goes first. Idempotent: the destructor stops what was not stopped.
    void stop() {
        while (!started_.empty()) {
            const Started s = started_.back();
            started_.pop_back();
            if (s.self) s.provider->destroy(s.self);
        }
    }
    // What the `index`th provider of the table hands its pack's rpc registration, or null (it did not start, it has no
    // facet, or the providers were stopped).
    void* service(std::size_t index) const {
        if (index >= started_.size()) return nullptr;
        const Started& s = started_[index];
        return s.self && s.provider->rpc_service ? s.provider->rpc_service(s.self) : nullptr;
    }
    std::size_t size() const { return started_.size(); }

private:
    // The `index`th provider of the table. Of a refused one only the version is read: what follows it is laid out as
    // that provider's ABI says, so even its id is named by its place in the table.
    void start(std::size_t index, const rp_provider* provider, const rp_device_host* host) {
        void* self = nullptr;
        if (provider && provider->abi_version != RP_PROVIDER_ABI_VERSION) {
            std::fprintf(stderr, "[retroplug] provider %zu is of provider ABI %u, and this host of %u: not started\n",
                         index, static_cast<unsigned>(provider->abi_version), RP_PROVIDER_ABI_VERSION);
            provider = nullptr;
        } else {
            self = provider ? provider->create(host) : nullptr;
            if (!self)
                std::fprintf(stderr, "[retroplug] the %s provider did not start\n", provider ? provider->id : "?");
        }
        try {
            started_.push_back({provider, self});
        } catch (...) {
            if (self) provider->destroy(self);
            throw;
        }
    }

    struct Started {
        const rp_provider* provider = nullptr;
        void*              self = nullptr;
    };
    std::vector<Started> started_;
};

}  // namespace rp
