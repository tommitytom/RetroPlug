#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "LvglJsEngine.hpp"
#include "system/SystemTypes.hpp"

extern "C" {
    #include <quickjs.h>
}

class Project;
class CommandQueue;
class EventQueue;

// Plugin-specific JS surface. Generic parameter handling (setParameter,
// name<->index lookup, "parameter" event) lives in LvglJsEngine. This class
// owns the bridges that only make sense for *this* plugin:
//
//   plugin.getFrame(systemId)        — direct read of the latest framebuffer
//   plugin.openRomBrowser()          — pop a system file dialog (UI thread)
//   plugin.loadRomFromPath(path)     — synchronous load from a known path
//
// All file IO and SameBoySystem construction happens on the UI thread inside
// `loadRomFromPath`. The fully-built system is shipped to the DSP via the
// command queue as a raw pointer; ownership transfers back to the UI for
// `delete` through the event queue when displaced. The DSP performs no
// allocation, free, or file IO.
//
// Lifetime: must be destroyed before the LvglJsEngine it references.
class PluginJsBridge {
public:
    // Any of the pointers may be nullptr in LV2-UI (separate-binary UI;
    // getPluginInstancePointer() is null, there is no shared DSP state). The
    // bridge degrades — getFrame returns null, loadRom returns an error.
    PluginJsBridge(LvglJsEngine& engine,
                   Project* project,
                   CommandQueue* commands,
                   EventQueue* events,
                   std::atomic<double>* sampleRate,
                   std::atomic<SystemId>* focusedSystemId);
    ~PluginJsBridge();

    PluginJsBridge(const PluginJsBridge&)            = delete;
    PluginJsBridge& operator=(const PluginJsBridge&) = delete;

    Project* project() const { return project_; }

    // PluginUI passes a callback that opens DPF's native file browser.
    // The bridge calls this with title/saving/defaultName so a single
    // callback covers both "Open ROM" and "Save / Load project". The UI
    // builds DPF's FileBrowserOptions from these args.
    using OpenFileBrowserFn = std::function<void(const char* title,
                                                 bool saving,
                                                 const char* defaultName)>;
    void setOpenFileBrowserCallback(OpenFileBrowserFn fn) { openFileBrowser_ = std::move(fn); }

    // Window-size plumbing. The UI binds these so JS can request a resize
    // (or query whether the WM is overriding requests). Bridge stays
    // agnostic of the DPF UI class; this is just a function pointer pair.
    using SetWindowSizeFn         = std::function<void(unsigned w, unsigned h)>;
    using IsWindowSizeControlledFn = std::function<bool()>;
    void setWindowSizeCallback(SetWindowSizeFn fn) { setWindowSize_ = std::move(fn); }
    void setIsWindowSizeControlledQuery(IsWindowSizeControlledFn fn) { isWindowSizeControlled_ = std::move(fn); }

    // Called from PluginUI::uiFileBrowserSelected. Routes to load- or
    // add- depending on the mode the JS side requested via openRomBrowser.
    void onFileBrowserSelected(const char* path);

    // Synchronous: read the file, build a SameBoySystem (calling onActivate
    // at the current sample rate), push a LoadRom (replace-focused-or-first-empty)
    // command. Used by the legacy "Load ROM" entry. Emits "rom-loaded" / "rom-error".
    bool loadRomFromPath(const std::string& path);

    // Append a brand-new system. Used by "Add instance" — pushes
    // Command::AddSystem so the DSP grows the project rather than swapping a
    // slot. Same emission contract as loadRomFromPath.
    bool addRomFromPath(const std::string& path);

    // Replace one specific system's ROM (used by per-tile "Replace ROM").
    bool replaceRomFromPath(SystemId id, const std::string& path);

    // Standalone-friendly project save/load. UI thread reads project_ for
    // save (same race rules as listSystems — accepted for debug). Load
    // ships the JSON to the DSP via Command::LoadProject and lets DSP do
    // the swap during command drain.
    bool saveProjectToPath(const std::string& path);
    bool loadProjectFromPath(const std::string& path);

private:
    // Build a fully-activated SameBoySystem from a ROM path. Returns nullptr
    // on failure (and emits a "rom-error" event so React can react).
    class SameBoySystem* buildSystemFromPath(const std::string& path);

    // JS bindings attached under globalThis[Symbol.for("plugin")].
    static JSValue js_getFrame(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_openRomBrowser(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_loadRomFromPath(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_addRomFromPath(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_replaceRomFromPath(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_removeSystem(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_listSystems(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_setFocus(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_getFocus(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_pressButton(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_setLinkGroupId(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_setWindowSize(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_isWindowSizeControlled(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_openSaveProjectBrowser(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_openLoadProjectBrowser(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    // What the next file-browser callback should do with the path.
    enum class PendingFileMode { LoadRom, AddRom, LoadProject, SaveProject };

    LvglJsEngine&            engine;
    Project*                 project_                = nullptr;
    CommandQueue*            commands_               = nullptr;
    EventQueue*              events_                 = nullptr;
    std::atomic<double>*     sampleRate_             = nullptr;
    std::atomic<SystemId>*   focusedSystemId_        = nullptr;
    OpenFileBrowserFn        openFileBrowser_;
    SetWindowSizeFn          setWindowSize_;
    IsWindowSizeControlledFn isWindowSizeControlled_;
    PendingFileMode          pendingFileMode_        = PendingFileMode::LoadRom;
    JSValue                  pluginNamespace         = JS_UNDEFINED;
};
