#pragma once

// The Launchpad pack's provider (graph/rp_provider.h, docs/modular-graph-phase-f3-design.md section 3): what a product
// starts so that a Launchpad runs in it without the product naming a class of the pack. It owns the ControllerLink (the
// claimed pair, its rings and its writer thread), the MidiPortPool (the ports a TS scan opens) and the facet's service
// (launchpad/desktop/LaunchpadRpc.hpp), and installs the link in the engine's registry as `launchpad` /
// rp.launchpad.surface-link/2 (launchpad/SurfaceLink.hpp) for the surface nodes to bind. rp_launchpad_provider is the
// manifest's `provider`: its ports are deps/rtmidi's.
//
// TS owns every decision - which ports, the close bytes, the probe and the timing - so this holds no protocol and no
// persisted state: config.json's `controlSurface` is the control plane's, and a claim arrives through the facet.

#include "graph/rp_provider.h"
#include "launchpad/desktop/ControllerLink.hpp"
#include "launchpad/desktop/LaunchpadRpc.hpp"
#include "launchpad/desktop/MidiPortPool.hpp"
#include "midi/MidiBackend.hpp"

namespace rp::launchpad {

// rp_launchpad_provider's create with the backend and lister given, for a host whose ports are not deps/rtmidi's (the
// tests and the headless host hand a scripted fake's, and open nothing). Null when `host` is null or of another ABI
// version, or when building the provider threw (said on stderr): nothing it throws leaves here, as nothing may cross the
// C table.
void* createProvider(const rp_device_host* host, rp::midi::MidiBackend& backend, PortLister lister);

// The port list a picker or a scan reads from `backend`: every port the system names, through the one filter
// (midi/PortFilter.hpp), so RetroPlug's own virtual ports, another instance's link ports and ALSA's Through are not
// ports, and nothing the test-only allow list hides is listed. A scan writes a Device Inquiry out of every output it is
// handed, so this list is its guard.
PortLister launchpadPortLister(rp::midi::MidiBackend& backend);

// The ControllerLink a started provider owns (a test reads its state; a product never does).
ControllerLink& providerLink(void* self);

}  // namespace rp::launchpad

extern "C" const rp_provider* rp_launchpad_provider(void);
