#pragma once

// The midi package's provider (graph/rp_provider.h, docs/modular-graph-phase-f5b-design.md section 4.5): what a product
// starts so that the CLI's live-MIDI facet exists without the product naming a class of this package. It owns the
// facet's service (a MidiStream over a backend, opening nothing until midiOpen), and mounts nothing in the engine's
// registry: a stream is not a graph device.
//
// The manifest's `provider` is rp_midi_provider; a product names `midi` in its PROVIDERS (the CLI does) and gets it
// started in that order. A product that uses the stream itself (the SDL app) holds a MidiStream of its own and starts
// no provider.

#include "graph/rp_provider.h"
#include "midi/MidiBackend.hpp"
#include "midi/facet/MidiRpcService.hpp"

namespace rp::midi {

/** rp_midi_provider's create with the backend given: systemMidi() in production, a scripted one in the headless host,
 *  so test-native drives the same facet over a fake's ports. Null when `host` is null or of another ABI version, or
 *  when building the provider threw (said on stderr): nothing it throws leaves here, as nothing may cross the C
 *  table. */
void* createProvider(const rp_device_host* host, MidiBackend& backend);

/** The service a started provider owns (a test reads its stream's state; a product never does). */
MidiRpcService& providerService(void* self);

}  // namespace rp::midi

extern "C" const rp_provider* rp_midi_provider(void);
