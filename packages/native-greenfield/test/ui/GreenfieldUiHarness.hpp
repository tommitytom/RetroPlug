#pragma once

// Greenfield headless UI test harness.
//
// Composes the backend-agnostic RenderCore (LVGL + LvglJsEngine + snapshot/queries/input) with the
// greenfield BackendFacade: it binds the facade's RPC surface as `__rpcSend` on
// globalThis[Symbol.for("plugin")] — the exact namespace realBackend.ts targets — then evals the
// greenfield UI bundle and mounts React. This is the plugin's control-plane bring-up
// (PluginGreenfieldDSP::bootControlPlane) minus the audio thread, plus a display.
//
// The bind is synchronous (the reply is materialized inline in __rpcSend) to match realBackend.ts's
// synchronous JSON-RPC client — no async "rpc-message"/pumpAsync path.

#include <cstdint>
#include <memory>

#include "RenderCore.hpp"

#include "BackendFacade.hpp"
#include "TypedRpcServer.h"
#include "codecs/QuickJSCodec.h"
#include "transports/QuickJSTransport.h"

namespace rpuigf {

class GreenfieldUiHarness {
public:
    explicit GreenfieldUiHarness(std::uint32_t width = 480, std::uint32_t height = 432);
    ~GreenfieldUiHarness();

    GreenfieldUiHarness(const GreenfieldUiHarness&) = delete;
    GreenfieldUiHarness& operator=(const GreenfieldUiHarness&) = delete;

    // Bring up the render scaffold, bind the BackendFacade RPC, eval the greenfield UI bundle, and
    // mount React. Returns false if any stage fails. Call once.
    bool boot();

    // The render scaffold — the runner drives pump/snapshot/queries/input through it.
    RenderCore& core() { return core_; }

private:
    using BackendRpcServer = rpcpp::TypedRpcServer<BackendFacade, rpcpp::QuickJSCodec>;

    RenderCore    core_;   // owns the engine + JS context; declared first so it outlives the RPC server
    BackendFacade service_;
    std::unique_ptr<rpcpp::QuickJSTransport> transport_;
    std::unique_ptr<BackendRpcServer>        server_;
    bool booted_ = false;
};

} // namespace rpuigf
