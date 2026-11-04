#pragma once

// The N8 facet (docs/modular-graph-phase-e2-design.md section 6): what a product that starts the pack's provider mounts
// on its RPC server, through the rp_register_package_rpc cmake/packages.cmake generates for it from the pack's manifest
// (its `rpc` field names this header and registerRpc). Its streaming-link half is what the UI's Settings > N8 drives
// (packages/n8/ts's createN8Client, ui/screens/menu/n8Devices.ts): the ports, the chosen port, the link's state and
// lookahead, read and set on the provider's N8Host. A product that does not mount it has no N8 submenu, since the UI
// takes "the facet answers" for "this host has the N8 seam" (hasN8). Calls are synchronous and run on the thread
// the server answers on, the control thread, as every facet's do; N8Host serialises them with its link thread.
//
// The link half is the UI's alone. Each of its setters persists n8.cfg, which the standalone, the plugin and the CLI
// share (one config dir), so the CLI mounts it, as every product registers its packs one way, and its sessions never
// call it: a CLI script reaches the cart through the facet's session half (n8Open on a port, and the operations on the
// handle it answers), which persists nothing.

#include <cstdint>
#include <string>

#include "n8/desktop/N8Host.hpp"

namespace rp::n8 {

class N8Rpc {
public:
    explicit N8Rpc(retroplug::N8Host& host) : host_(host) {}

    // The serial ports (each flagged when it is an N8 by its USB VID:PID), the chosen port ("" for auto-detect),
    // whether the link is up, the user's streaming toggle, the lookahead, the bytes forwarded since connect and the
    // last error.
    retroplug::N8ConfigDto n8LinkConfig() { return host_.getConfig(); }
    // Choose the serial port by name; a live link reconnects on it. Persisted to n8.cfg. Always true.
    bool n8SetPort(std::string port) {
        host_.setPort(port);
        return true;
    }
    // Open (true) or close (false) the streaming link. Persisted to n8.cfg. Always true: whether the link came up is
    // n8LinkConfig's `connected` and `error`.
    bool n8Connect(bool enabled) {
        host_.connect(enabled);
        return true;
    }
    // The timed-release lookahead the link thread applies, in ms. Persisted to n8.cfg. Always true.
    bool n8SetLookahead(std::int32_t ms) {
        host_.setLookahead(ms);
        return true;
    }

private:
    retroplug::N8Host& host_;
};

// Mount the facet on `server` (any rpcpp TypedRpcServer: the products' over the QuickJS codec, the tests' over JSON).
// `service` is the N8 provider's (rp_provider.rpc_service), an N8Rpc; null (the provider did not start) mounts nothing,
// so the UI reads the host as having no N8 seam.
template <class Server>
void registerRpc(Server& server, void* service) {
    if (!service) return;
    N8Rpc& rpc = *static_cast<N8Rpc*>(service);
    server.template addMethod<&N8Rpc::n8LinkConfig>(rpc);
    server.template addMethod<&N8Rpc::n8SetPort>(rpc);
    server.template addMethod<&N8Rpc::n8Connect>(rpc);
    server.template addMethod<&N8Rpc::n8SetLookahead>(rpc);
}

}  // namespace rp::n8
