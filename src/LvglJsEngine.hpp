#pragma once

extern "C" {
    #include "tjs.h"
    #include "private.h"
    #include "lvgl.h"
}

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "host/TjsHostRuntime.hpp"
#include "native/components/window/window.hpp"

class LvglJsEngine;

// Extends lv_binding_js display data with the TJS runtime pointer.
// Stored on each LVGL display via lv_display_set_user_data().
struct DpfJsDisplayData : LvBindingJsDisplayData {
    TJSRuntime* runtime = nullptr;
    LvglJsEngine* engine = nullptr;

    static DpfJsDisplayData* get() {
        lv_display_t* disp = lv_display_get_default();
        return disp ? static_cast<DpfJsDisplayData*>(lv_display_get_user_data(disp)) : nullptr;
    }
};

class LvglJsEngine {
public:
    using ParamWriteCallback = std::function<void(uint32_t index, float value)>;

    LvglJsEngine();
    ~LvglJsEngine();

    bool init();
    void tick();
    void shutdown();
    int evalModule(const char* filename);
    int evalModuleBuffer(const char* code, size_t len, const char* name);
    int evalModuleBytecode(const uint8_t* bytecode, size_t len);
    int evalString(const char* code);
    JSContext* getContext() const;

    // Returns a *new* reference to the lvgljs Symbol-keyed JS namespace object.
    // Caller must JS_FreeValue. Used by external code (e.g. PluginJsBridge) to
    // attach further native methods without the engine knowing about them.
    JSValue lvglJsNamespace() const;

    // The embedded txiki host. PluginJsBridge uses it to bind the generic
    // __rpcSend trampoline onto its own "plugin" namespace.
    TjsHostRuntime& host() { return host_; }

    // Fire all handlers registered via lvgljs.on(channel, fn).
    // argv values are not consumed; engine handles refcounting internally.
    void emit(const char* channel, int argc, JSValueConst* argv);

    // Parameter machinery: every plugin needs a way for JS to write parameter
    // values to the host and to receive host-side parameter changes.

    // Set the callback invoked when JS calls lvgljs.setParameter(idx, val).
    void setParamWriteCallback(ParamWriteCallback cb);

    // Register a parameter name->index mapping so JS can refer to parameters
    // by name. Throws no error on conflict — last write wins.
    void registerParameter(uint32_t index, const std::string& name);

    // DSP -> JS: emit a "parameter" event with (index, value) to all handlers.
    void pushParameter(uint32_t index, float value);

private:
    static JSValue js_lvgljs_on(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_lvgljs_off(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_lvgljs_setParameter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_lvgljs_getParameterIndex(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_lvgljs_getParameterCount(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    TjsHostRuntime host_;
    JSContext* ctx = nullptr;  // cached host_.context()
    DpfJsDisplayData displayData;
    bool initialized = false;
    JSValue lvgljsObj;  // initialized to JS_UNDEFINED in ctor (not a constexpr in C++)
    std::map<std::string, std::vector<JSValue>> handlers;
    ParamWriteCallback paramWrite;
    std::map<std::string, uint32_t> paramNameToIndex;
    std::map<uint32_t, std::string> paramIndexToName;
};
