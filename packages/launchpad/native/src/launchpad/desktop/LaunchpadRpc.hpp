#pragma once

// The Launchpad facet (docs/modular-graph-phase-f3-design.md section 3): what a product that starts the pack's provider
// mounts on its RPC server, through the rp_register_package_rpc cmake/packages.cmake generates for it from the pack's
// manifest (its `rpc` field names this header and registerRpc). Both halves are answered on the thread the server
// answers on, the control thread, and both are synchronous: a scan steps a few calls per frame, and a claim opens two
// ports.
//
// It is TS's only door to ports. The claimed-pair half is what the control plane drives once a Launchpad is chosen: the
// port names, the claim (with the opaque close bytes the device is let go with), the release, the status a menu line
// reads, and a raw send for a message that is not the session's. The pool half is what a SCAN is built from: single
// port handles, opened in a batch, written to and read from, closed. The probe's bytes, the timing, the meaning of an
// answer and which device answered are all TS's (packages/launchpad/ts/portScan.ts), so native never learns the
// protocol and a second surface is a TS change.
//
// A product that does not mount it has no Launchpad submenu, since the UI takes "the facet answers" for "this host has
// the control-surface seam" (hasLaunchpad). The provider owns the link, the pool and the host's reserve calls; this
// class is the table over them.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "graph/rp_provider.h"
#include "launchpad/desktop/ControllerLink.hpp"
#include "launchpad/desktop/MidiPortPool.hpp"
#include "launchpad/desktop/PortClaim.hpp"

namespace rp::launchpad {

// One entry of the port pickers: the system's MIDI inputs, then its outputs, by the names the backend reports.
struct LaunchpadPortsDto {
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
};

// What the menu's status line and the TS client read: whether the pair is held, its generation (what the session
// compares), the bytes sent and lost since the claim, the pair's names and the last failure's words.
struct LaunchpadStatusDto {
    bool          connected = false;
    std::uint32_t generation = 0;
    std::uint64_t sent = 0;
    std::uint64_t dropped = 0;
    std::string   input;
    std::string   output;
    std::string   error;
};

// What a scan's read answers: the messages that arrived since the last read, and how many the queue lost.
struct LaunchpadReadDto {
    std::vector<rfl::Bytestring> messages;
    std::uint64_t                dropped = 0;
};

// The port names the picker and a scan's "open every port" need: the provider's lister (the real RtMidi enumeration, or
// a test host's scripted one), so a test opens nothing.
using PortLister = std::function<LaunchpadPortsDto()>;

// A claim on one port against other PROCESSES (PortClaim's claimPort in production; a null token = held elsewhere).
// Injected so a test needs no filesystem, and unset means no cross-process coordination.
using ClaimFn = std::function<PortClaim(const std::string& port)>;

class LaunchpadRpc {
public:
    // `host` may be null (a test): it is only read to keep the claimed pair out of the product's shared MIDI stream
    // (rp_device_host::reserve_midi, docs/modular-graph-phase-f3-design.md section 3), which a host with no shared
    // stream has no need of.
    LaunchpadRpc(ControllerLink& link, MidiPortPool& pool, const rp_device_host* host, PortLister lister);
    // The cross-process claim the provider installs (claimPort under the product's config dir). Unset: this process is
    // the only one that might hold a port.
    void setClaim(ClaimFn fn) { claim_ = std::move(fn); }
    ~LaunchpadRpc();
    LaunchpadRpc(const LaunchpadRpc&)            = delete;
    LaunchpadRpc& operator=(const LaunchpadRpc&) = delete;

    // --- the claimed pair (the control plane's) ---

    // The system's MIDI inputs and outputs.
    LaunchpadPortsDto launchpadPorts();
    // The pair's state: held or not, its generation, its counts, the names and the last error.
    LaunchpadStatusDto launchpadStatus();
    // Claim the named pair, with the bytes that let the device go (an opaque blob, replayed on release and at
    // shutdown), in this order: the pair already held is closed (its close bytes) and its claims given back, both ports
    // are checked to be present, both are claimed against other processes (one claim when the input and the output are
    // one name, as an interface's ports commonly are), the pair is kept out of the product's shared MIDI stream BEFORE
    // the ports are opened (a Windows input the stream still held could not be opened at all), and then it is opened.
    // A claim that fails leaves nothing held, keeps the pair out of the stream, and answers its error in `error`.
    LaunchpadStatusDto launchpadClaim(std::string input, std::string output, rfl::Bytestring closeBytes);
    // Give the device back (its close bytes first) and let the shared stream have the pair again.
    LaunchpadStatusDto launchpadRelease();
    // Write `data` to the claimed pair now, after what the session queued. For letting the device go while the pair
    // stays claimed (the app switched off); false when nothing is held.
    bool launchpadSend(rfl::Bytestring data);
    // The startup rendezvous's end (design section 3): a product that held MIDI and audio for the claim resumes them
    // here, through the provider's rp_device_host::restored (the product names no class of this pack). Always true, and
    // a product that holds nothing answers at once.
    bool launchpadRestored();

    // --- the pool (a scan's) ---

    // Open every named input as ONE held edge (a Windows input is exclusive, so the shared stream's inputs step aside
    // for the batch): a handle per name, 0 for each that would not open.
    std::vector<std::int32_t> launchpadOpenInputs(std::vector<std::string> names);
    // Open one output: a handle, or 0.
    std::int32_t launchpadOpenOutput(std::string name);
    bool         launchpadPortSend(std::int32_t handle, rfl::Bytestring data);
    LaunchpadReadDto launchpadPortRead(std::int32_t handle);
    bool         launchpadPortClose(std::int32_t handle);
    // The pool's last failure's words (the first port that refused in a batch, else empty).
    std::string launchpadPortError();

private:
    void reserve(const std::string& input, const std::string& output);

    ControllerLink&       link_;
    MidiPortPool&         pool_;
    const rp_device_host* host_;
    PortLister            lister_;
    ClaimFn               claim_;
    // The last claim's own failure (a port held by another process, a refused open): the link's lastError is its own,
    // and a claim refused before anything opens leaves the link with nothing to say.
    std::string           error_;
    PortClaim             claimIn_;
    PortClaim             claimOut_;
};

// Mount the facet on `server`: an rpcpp TypedRpcServer whose codec carries rfl::Bytestring, which the products' QuickJS
// codec does as a Uint8Array (the tests mount it on the same) and the JSON codec cannot. `service` is the provider's
// (rp_provider.rpc_service), a LaunchpadRpc; null (the provider did not start) mounts nothing, so the UI reads the host
// as having no Launchpad seam. The one template of the facet: it adds the methods to a server, and the methods are the
// pack's library's.
template <class Server>
void registerRpc(Server& server, void* service) {
    if (!service) return;
    LaunchpadRpc& rpc = *static_cast<LaunchpadRpc*>(service);
    server.template addMethod<&LaunchpadRpc::launchpadPorts>(rpc);
    server.template addMethod<&LaunchpadRpc::launchpadStatus>(rpc);
    server.template addMethod<&LaunchpadRpc::launchpadClaim>(rpc);
    server.template addMethod<&LaunchpadRpc::launchpadRelease>(rpc);
    server.template addMethod<&LaunchpadRpc::launchpadSend>(rpc);
    server.template addMethod<&LaunchpadRpc::launchpadRestored>(rpc);
    server.template addMethod<&LaunchpadRpc::launchpadOpenInputs>(rpc);
    server.template addMethod<&LaunchpadRpc::launchpadOpenOutput>(rpc);
    server.template addMethod<&LaunchpadRpc::launchpadPortSend>(rpc);
    server.template addMethod<&LaunchpadRpc::launchpadPortRead>(rpc);
    server.template addMethod<&LaunchpadRpc::launchpadPortClose>(rpc);
    server.template addMethod<&LaunchpadRpc::launchpadPortError>(rpc);
}

}  // namespace rp::launchpad
