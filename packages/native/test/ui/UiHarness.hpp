#pragma once

// Headless UI test harness.
//
// Composes the backend-agnostic RenderCore (LVGL + LvglJsEngine + snapshot/queries/input) with the
// backend service graph: it mounts the host + emulator facets as `__rpcSend` on
// globalThis[Symbol.for("plugin")] — the exact namespace realBackend.ts targets — then evals the
// UI bundle and mounts React. This is the plugin's control-plane bring-up
// (PluginDSP::bootControlPlane) minus the audio thread + DSP kernel, plus a display.
//
// The bind is synchronous (the reply is materialized inline in __rpcSend) to match realBackend.ts's
// synchronous JSON-RPC client — no async "rpc-message"/pumpAsync path.

#include <cstdint>
#include <memory>

#include "RenderCore.hpp"

#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/rpc/EngineRpcService.hpp"
#include "host/rpc/HostRpcService.hpp"
#include "system/SystemFactory.hpp"
#include "TypedRpcServer.h"
#include "codecs/QuickJSCodec.h"
#include "transports/QuickJSTransport.h"

namespace rpuigf {

class UiHarness {
public:
    explicit UiHarness(std::uint32_t width = 480, std::uint32_t height = 432);
    ~UiHarness();

    UiHarness(const UiHarness&) = delete;
    UiHarness& operator=(const UiHarness&) = delete;

    // Bring up the render scaffold, bind the backend RPC surface, eval the UI bundle, and
    // mount React. Returns false if any stage fails. Call once.
    bool boot();

    // The render scaffold — the runner drives pump/snapshot/queries/input through it.
    RenderCore& core() { return core_; }

    // Advance the emulator by `ms` (discarding the rendered audio) so tiles receive live frames. The
    // plugin's audio thread does this per block; RenderCore::pump only ticks LVGL, so a headless UI test
    // must drive it explicitly. Same Engine the UI's getFrame reads over the bound RPC.
    void advance(double ms) { engineSvc_.renderAudio(ms); }

private:
    using BackendRpcServer = rpcpp::TypedRpcServer<rpcpp::Empty, rpcpp::QuickJSCodec>;

    RenderCore core_;   // owns the LVGL engine + JS context; first so it outlives the RPC server

    // The backend service graph (host + emulator only — the UI never drives the DSP kernel, audio-render
    // harness, debugger, or background thread). Declaration order is load-bearing.
    Engine           engine_;
    SystemFactory    factory_;
    QueuedInvoker    invoker_{engine_, engine_.registry()};
    HostRpcService   host_;
    EngineRpcService engineSvc_{engine_, factory_, invoker_};

    std::unique_ptr<rpcpp::QuickJSTransport> transport_;
    std::unique_ptr<BackendRpcServer>        server_;
    bool booted_ = false;
};

} // namespace rpuigf
