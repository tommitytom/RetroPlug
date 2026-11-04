#pragma once

// The N8 pack's provider (graph/rp_provider.h, docs/modular-graph-phase-e2-design.md section 6): what a product starts
// so that the N8 runs in it without the product naming a class of it. It owns the N8Host (the streaming link's
// serial thread, n8.cfg in the host's config dir, the cart jobs) over a port factory and lister, the N8CartLink over
// that host, installed in the engine's registry as `n8` / rp.n8.cart-link/1 (n8/CartLink.hpp) for rp.n8.nes to bind,
// and the facet's service (n8/desktop/N8Rpc.hpp). rp_n8_provider is the manifest's `provider`: its ports are
// deps/serial's.

#include "graph/rp_provider.h"
#include "n8/desktop/N8Host.hpp"
#include "n8/desktop/N8Link.hpp"

namespace rp::n8 {

// rp_n8_provider's create with the port factory and lister given, for a host whose ports are not deps/serial's: the
// tests hand FakeN8's, and the provider is otherwise the one every product starts (its restore, rpc_service and
// destroy are rp_n8_provider()'s, given what this returns). Null when `host` is null or of another ABI version, or when
// building the provider threw (said on stderr): nothing it throws leaves here, as nothing may cross the C table.
void* createProvider(const rp_device_host* host, retroplug::N8Link::PortFactory factory,
                     retroplug::N8Host::PortLister lister);

// The N8Host a started provider owns (a test reads its state; a product never does).
retroplug::N8Host& providerHost(void* self);

}  // namespace rp::n8

extern "C" const rp_provider* rp_n8_provider(void);
