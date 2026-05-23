#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "LvglJsEngine.hpp"
#include "PluginRpcService.hpp"
#include "system/SystemTypes.hpp"
#include "TypedRpcServer.h"
#include "codecs/MsgpackCodec.h"
#include "transports/QueueTransport.h"

extern "C" {
    #include <quickjs.h>
}

class Project;
class CommandQueue;
class EventQueue;
class UserConfig;
class RecentFiles;

// Thin shim between the QuickJS runtime and the rpcpp-typed
// PluginRpcService. The bridge:
//
//   - owns the TypedRpcServer<PluginRpcService, MsgpackCodec> stack
//   - exposes one C function on globalThis[Symbol.for("plugin")]:
//     __rpcSend(Uint8Array) -> Uint8Array | null
//   - forwards the file-browser + window-size + emit callbacks from
//     PluginUI's DPF integration onto the service
//   - drains the rpcpp transport queue once per uiIdle (pumpAsync)
//
// The actual RPC method bodies (loadRomFromPath, listSystems, …) live in
// PluginRpcService — see src/PluginRpcService.{hpp,cpp}.
//
// Lifetime: must be destroyed before the LvglJsEngine it references.
class PluginJsBridge {
public:
    // Any of the pointers may be nullptr in LV2-UI (separate-binary UI;
    // getPluginInstancePointer() is null, there is no shared DSP state). The
    // service degrades — getFrame returns null, loadRom returns false.
    PluginJsBridge(LvglJsEngine& engine,
                   Project* project,
                   CommandQueue* commands,
                   EventQueue* events,
                   std::atomic<double>* sampleRate,
                   std::atomic<SystemId>* focusedSystemId,
                   UserConfig* userConfig = nullptr,
                   RecentFiles* recentFiles = nullptr);
    ~PluginJsBridge();

    PluginJsBridge(const PluginJsBridge&)            = delete;
    PluginJsBridge& operator=(const PluginJsBridge&) = delete;

    Project* project() const { return project_; }

    // PluginUI passes a callback that opens DPF's native file browser.
    using OpenFileBrowserFn = std::function<void(const char* title,
                                                 bool saving,
                                                 const char* defaultName)>;
    void setOpenFileBrowserCallback(OpenFileBrowserFn fn) {
        if (rpcService_) rpcService_->setOpenFileBrowserCallback(std::move(fn));
    }

    // Window-size plumbing. The UI binds these so JS can request a resize
    // (or query whether the WM is overriding requests).
    using SetWindowSizeFn          = std::function<void(unsigned w, unsigned h)>;
    using IsWindowSizeControlledFn = std::function<bool()>;
    void setWindowSizeCallback(SetWindowSizeFn fn) {
        if (rpcService_) rpcService_->setWindowSizeCallback(std::move(fn));
    }
    void setIsWindowSizeControlledQuery(IsWindowSizeControlledFn fn) {
        if (rpcService_) rpcService_->setIsWindowSizeControlledQuery(std::move(fn));
    }

    // Called from PluginUI::uiFileBrowserSelected. Routes to load- / add- /
    // save-project / load-project per the mode set by the most recent
    // open*Browser RPC call.
    void onFileBrowserSelected(const char* path) {
        if (rpcService_) rpcService_->onFileBrowserSelected(path);
    }

    // Standalone-friendly project load. Used by PluginUI's
    // RETROPLUG_AUTOLOAD_PROJECT env-var path.
    bool loadProjectFromPath(const std::string& path) {
        return rpcService_ ? rpcService_->loadProjectFromPath(path) : false;
    }

    // Diagnostic-only ROM autoload (RETROPLUG_AUTOLOAD_ROM env var, wired
    // in PluginUI.cpp). Bypasses the file dialog so headless harnesses can
    // exercise the framebuffer + system-construction path without driving
    // the native chooser under Xvfb.
    bool loadRomFromPath(const std::string& path) {
        return rpcService_ ? rpcService_->loadRomFromPath(path) : false;
    }

    // Drains the rpcpp server's outgoing async/notification queue and emits
    // each frame as an ArrayBuffer through the engine's `rpc-message`
    // channel. Called from PluginUI::uiIdle. No-op while only sync handlers
    // are registered, but required if any future method goes async.
    void pumpAsync();

    // Walks the service's live-memory subscription registry, reads the
    // latest tear-free snapshot for each, hashes for dedup, and emits a
    // `"memory"` event with (systemId, type, ArrayBuffer, version) when
    // the snapshot has changed since the last emit. Called from
    // PluginUI::uiIdle; cheap when there are no active subscriptions.
    void pumpMemorySnapshots();

    // Per-uiIdle: stat the ROM paths of every system whose
    // `reloadOnRomChange` config flag is set and dispatch a reload when the
    // mtime advances.
    void pumpRomWatchers() { if (rpcService_) rpcService_->pumpRomWatchers(); }

private:
    // rpcpp transport — single sync entry point exposed to QuickJS. Body
    // calls rpcServer_->processMessage and returns either the inline
    // response (as ArrayBuffer) or null for notifications.
    static JSValue js_rpcSend(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    using RpcTransport = rpcpp::QueueTransport<rpcpp::MsgpackCodec>;
    using RpcServer    = rpcpp::TypedRpcServer<PluginRpcService, rpcpp::MsgpackCodec>;

    LvglJsEngine&            engine;
    Project*                 project_                = nullptr;
    JSValue                  pluginNamespace         = JS_UNDEFINED;

    // Order matters: transport must outlive server.
    std::unique_ptr<PluginRpcService> rpcService_;
    std::unique_ptr<RpcTransport>     rpcTransport_;
    std::unique_ptr<RpcServer>        rpcServer_;
};
