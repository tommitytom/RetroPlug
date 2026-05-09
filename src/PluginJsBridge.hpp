#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "LvglJsEngine.hpp"

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
                   std::atomic<double>* sampleRate);
    ~PluginJsBridge();

    PluginJsBridge(const PluginJsBridge&)            = delete;
    PluginJsBridge& operator=(const PluginJsBridge&) = delete;

    Project* project() const { return project_; }

    // PluginUI passes a callback that opens DPF's native file browser.
    // The bridge's openRomBrowser JS function calls this; the UI's
    // uiFileBrowserSelected then calls back into loadRomFromPath.
    using OpenRomBrowserFn = std::function<void()>;
    void setOpenRomBrowserCallback(OpenRomBrowserFn fn) { openRomBrowser_ = std::move(fn); }

    // PluginUI sets this to a callback that flips its "UI captures keyboard"
    // flag. When true, PluginUI::onKeyboard stops mapping keys to GameboyButton
    // and returns false for everything except Esc, letting LVGL route arrows
    // / Enter / etc. to the focused React widget. The React MenuOverlay
    // raises this flag on mount and lowers it on unmount.
    using SetUiCapturesKeyboardFn = std::function<void(bool)>;
    void setUiCapturesKeyboardCallback(SetUiCapturesKeyboardFn fn) { uiCapturesKeyboard_ = std::move(fn); }

    // Synchronous: read the file, build a SameBoySystem (calling onActivate
    // at the current sample rate), push a LoadRom command. Returns true on
    // success. Emits a "rom-loaded" or "rom-error" JS event for the React UI.
    // Safe to call on the UI thread; not safe from any other thread.
    bool loadRomFromPath(const std::string& path);

private:
    // JS bindings attached under globalThis[Symbol.for("plugin")].
    static JSValue js_getFrame(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_openRomBrowser(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_loadRomFromPath(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_setUiCapturesKeyboard(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    LvglJsEngine&            engine;
    Project*                 project_    = nullptr;
    CommandQueue*            commands_   = nullptr;
    EventQueue*              events_     = nullptr;
    std::atomic<double>*     sampleRate_ = nullptr;
    OpenRomBrowserFn         openRomBrowser_;
    SetUiCapturesKeyboardFn  uiCapturesKeyboard_;
    JSValue                  pluginNamespace = JS_UNDEFINED;
};
