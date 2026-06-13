#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "dpfjs/LvglJsEngine.hpp"
#include "PluginRpcService.hpp"
#include "dpfjs/JsRpcBridge.hpp"
#include "system/SystemTypes.hpp"

class Project;
class CommandQueue;
class EventQueue;
class UserConfig;
class RecentFiles;

// RetroPlug's wiring of the generic dpf.js bridge to PluginRpcService. It:
//
//   - owns the PluginRpcService + a dpfjs::JsRpcBridge<PluginRpcService>
//     (which owns the rpc server/transport and the Symbol.for("plugin")
//     namespace with __rpcSend / __log)
//   - forwards the file-browser + window-size + emit callbacks from PluginUI's
//     DPF integration onto the service
//   - runs the domain pumps each uiIdle: pumpAsync (generic, delegated),
//     pumpMemorySnapshots + pumpRomWatchers (RetroPlug domain)
//
// The RPC method bodies (loadRomFromPath, listSystems, …) live in
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

    PluginJsBridge(const PluginJsBridge&)            = delete;
    PluginJsBridge& operator=(const PluginJsBridge&) = delete;

    Project* project() const { return project_; }

    // PluginUI passes a callback that opens DPF's native file browser.
    using OpenFileBrowserFn = std::function<void(const char* title,
                                                 bool saving,
                                                 const char* defaultName)>;
    void setOpenFileBrowserCallback(OpenFileBrowserFn fn) {
        service_.setOpenFileBrowserCallback(std::move(fn));
    }

    // Window-size plumbing. The UI binds these so JS can request a resize
    // (or query whether the WM is overriding requests).
    using SetWindowSizeFn          = std::function<void(unsigned w, unsigned h)>;
    using IsWindowSizeControlledFn = std::function<bool()>;
    void setWindowSizeCallback(SetWindowSizeFn fn) {
        service_.setWindowSizeCallback(std::move(fn));
    }
    void setIsWindowSizeControlledQuery(IsWindowSizeControlledFn fn) {
        service_.setIsWindowSizeControlledQuery(std::move(fn));
    }

    // Called from PluginUI::uiFileBrowserSelected. Routes to load- / add- /
    // save-project / load-project per the mode set by the most recent
    // open*Browser RPC call.
    void onFileBrowserSelected(const char* path) {
        service_.onFileBrowserSelected(path);
    }

    // Standalone-friendly project load. Used by PluginUI's
    // RETROPLUG_AUTOLOAD_PROJECT env-var path.
    bool loadProjectFromPath(const std::string& path) {
        return service_.loadProjectFromPath(path);
    }

    // Diagnostic-only ROM autoload (RETROPLUG_AUTOLOAD_ROM env var, wired
    // in PluginUI.cpp). Bypasses the file dialog so headless harnesses can
    // exercise the framebuffer + system-construction path without driving
    // the native chooser under Xvfb.
    bool loadRomFromPath(const std::string& path) {
        return service_.loadRomFromPath(path);
    }

    // Drains the rpcpp server's outgoing async/notification queue and emits
    // each frame as an ArrayBuffer through the engine's `rpc-message` channel.
    // Called from PluginUI::uiIdle.
    void pumpAsync() { rpc_.pumpAsync(); }

    // Walks the service's live-memory subscription registry, reads the
    // latest tear-free snapshot for each, hashes for dedup, and emits a
    // `"memory"` notification when the snapshot has changed since the last
    // emit. Called from PluginUI::uiIdle; cheap when no subscriptions.
    void pumpMemorySnapshots();

    // Per-uiIdle: stat the ROM paths of every system whose
    // `reloadOnRomChange` config flag is set and dispatch a reload when the
    // mtime advances.
    void pumpRomWatchers() { service_.pumpRomWatchers(); }

private:
    LvglJsEngine&    engine;
    Project*         project_ = nullptr;
    // service_ before rpc_: the generic bridge holds the server, which
    // references the service.
    PluginRpcService service_;
    dpfjs::JsRpcBridge<PluginRpcService> rpc_;
};
