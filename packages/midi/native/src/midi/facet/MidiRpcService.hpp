#pragma once

// The MIDI facet (docs/modular-graph-phase-f5b-design.md sections 4.4 and 4.5): the CLI's live-MIDI-input transport,
// the thin native seam the TS N8 bridges and probes open a stream through. A product that names `midi` in its PROVIDERS
// mounts it from the generated rp_register_package_rpc, which this header's registerRpc is the manifest's `rpc` entry
// for. The SDL app does not use it: it holds a MidiStream of its own.
//
// The stream behind it is this package's, so a product mounts the facet by naming the package in its PROVIDERS, the
// same way it starts any pack's, and no build option decides whether the CLI has one.

#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "midi/MidiBackend.hpp"
#include "midi/MidiStream.hpp"

namespace rp::midi {

/** One incoming MIDI message for the TS layer. The stream carries no per-message timing (a backend's timestamp is
 *  discarded), so this is just the raw bytes - the MIDI-in twin of the engine's host MIDI out. */
struct RpcMidiIn {
    rfl::Bytestring bytes;
};

/** The thin native live-MIDI-input transport exposed to the TS control plane over the Backend RPC channel, beside the
 *  N8 pack's facet the same sessions open the cart through. Owns one MidiStream; a TS session enumerates inputs, opens
 *  (a virtual port plus the selected hardware inputs), and drains messages by polling. The backend's callback threads
 *  and the receive ring stay native behind it. */
class MidiRpcService {
public:
    /** Constructed by the provider over the product's backend (systemMidi in production, a fake in a host that scripts
     *  one). Nothing opens until midiOpen. */
    explicit MidiRpcService(MidiBackend& backend)
        : base_(backend), stream_(std::make_unique<MidiStream>(backend)), clientName_(backend.clientName()) {
        scratch_.reserve(MidiStream::kCapacity);
    }

    /** Hardware MIDI input port names (skips our own virtual port, ALSA "Through" and anything the test-only allow list
     *  hides). */
    std::vector<std::string> midiListInputs();

    /** The same for outputs - the other half of talking to a control surface, which has to be LIT as well as read. */
    std::vector<std::string> midiListOutputs();

    /** Open MIDI: create the virtual "<client> In/Out" ports and open the hardware inputs.
     *
     * `input` is a selection in the shape the contract documents: "" (or the older "*") opens EVERY hardware input, and
     * any other string opens exactly the port it names (a name that is not present opens none, and is opened by a later
     * midiOpen once it is). "" is what `n8-bridge` with no `--midi-in` sends; a session that wants none opens a name
     * that matches nothing, as the Launchpad probe's "(no hardware input)" does.
     *
     * `clientName` is the name the session's virtual ports are published under ("RetroPlug N8", "RetroPlug Loopback"),
     * which is what core/ts/host.ts documents and what a DAW routes to. A backend owns its client name, so another name
     * is another backend: this asks the product's for one (MidiBackend::withClientName) and, where it cannot be
     * renamed, keeps the one it was given. Empty means the backend's own name. Returns false when no MIDI system is
     * available. */
    bool midiOpen(std::string clientName, std::string input);

    /** Choose the hardware output to mirror sends to ("" = the virtual port only). Applied immediately. */
    void midiSelectOutput(std::string output);

    /** Drain the input messages queued since the last poll (each = raw bytes). Empty when nothing arrived. */
    std::vector<RpcMidiIn> midiPoll();

    /** Send one raw message. Any length - a control surface's bulk-LED sysex runs to hundreds of bytes, and the stream
     *  imposes no cap. */
    void midiSend(rfl::Bytestring bytes);

    /** Close all MIDI ports. */
    void midiClose();

private:
    MidiBackend&                 base_;        // the product's system: what a rename starts from
    std::unique_ptr<MidiBackend> renamed_;     // ...and the instance a session's own client name made, when one did
    std::unique_ptr<MidiStream>  stream_;      // over base_, or over renamed_
    std::string                  clientName_;  // the name stream_ opens its ports under
    std::vector<MidiStream::Message> scratch_;  // reused poll buffer, reserved to the ring
};

/** Mount the facet on `server`, an rpcpp TypedRpcServer whose codec carries rfl::Bytestring (the products' QuickJS
 *  codec does, as a Uint8Array). `service` is the provider's (rp_provider.rpc_service), a MidiRpcService; null (the
 *  provider did not start) mounts nothing. Template + header-only, as the Launchpad's facet is, so the pack itself
 *  needs neither rpcpp nor QuickJS. */
template <class Server>
void registerRpc(Server& server, void* service) {
    if (!service) return;
    MidiRpcService& svc = *static_cast<MidiRpcService*>(service);
    server.template addMethod<&MidiRpcService::midiListInputs>(svc);
    server.template addMethod<&MidiRpcService::midiListOutputs>(svc);
    server.template addMethod<&MidiRpcService::midiOpen>(svc);
    server.template addMethod<&MidiRpcService::midiSelectOutput>(svc);
    server.template addMethod<&MidiRpcService::midiPoll>(svc);
    server.template addMethod<&MidiRpcService::midiSend>(svc);
    server.template addMethod<&MidiRpcService::midiClose>(svc);
}

}  // namespace rp::midi
