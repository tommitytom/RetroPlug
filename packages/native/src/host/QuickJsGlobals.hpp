#pragma once

#include <functional>
#include <string>

#include "dpfjs/host/TjsHostRuntime.hpp" // brings tjs.h/quickjs.h, as both hosts do

/** Calling into the control plane's JS globals, which the plugin and the SDL standalone each had their
 *  own copy of.
 *
 *  The two had diverged in one behavioural way and one diagnostic way, and the behavioural one is the
 *  reason this is the PLUGIN's body rather than SDL's:
 *
 *    * The plugin refuses to call anything unless the host initialised AND the bundle signalled ready.
 *      SDL had no such guard - it does not need one, because its boot returns false on either failure
 *      and main exits 1, so an unready SDL host is a dead process. A DAW cannot be treated that way:
 *      a failed plugin scan must degrade, not exit, which is what commit 48c8a895 fixed. Taking SDL's
 *      body here would have silently reverted that and put the scan crash back.
 *
 *    * The plugin prints the JS stack on an exception and SDL printed only the message. That is pure
 *      diagnostics, so SDL gains it.
 *
 *  `ready` is how the difference survives unification: the plugin passes its jsReady_ latch, SDL passes
 *  true, and neither host's behaviour moves. */
struct JsGlobals {
    JSContext*                                   ctx   = nullptr;
    bool                                         ready = false;
    std::function<void(const char*, const char*)> log;  // (name, detail) — d_stderr / fprintf per host

    explicit operator bool() const { return ctx != nullptr && ready; }
};

/** Whether the control plane has set its `__rp_ready` flag. False on an uninitialised host. */
inline bool jsReadReady(JSContext* ctx) {
    if (!ctx) return false;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue v      = JS_GetPropertyStr(ctx, global, "__rp_ready");
    const bool r   = JS_ToBool(ctx, v) > 0;
    JS_FreeValue(ctx, v);
    JS_FreeValue(ctx, global);
    return r;
}

/** Whether a global of that name exists and is callable. */
inline bool jsHasGlobalFunction(JSContext* ctx, const char* name) {
    if (!ctx) return false;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue fn     = JS_GetPropertyStr(ctx, global, name);
    const bool ok  = JS_IsFunction(ctx, fn);
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, global);
    return ok;
}

/** Call a global with an optional single string argument, returning its string result.
 *
 *  A bool result is stringified to "true"/"false" so callers that return one (__rp_loadProjectPath)
 *  get something unambiguous in diagnostics rather than the empty "false/void" default. Anything else
 *  returns empty. An exception is logged through `g.log` and yields empty - never a throw across the
 *  host boundary. */
inline std::string jsCallGlobal(const JsGlobals& g, const char* name, const char* arg) {
    if (!g) return {};
    JSContext* ctx = g.ctx;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue fn     = JS_GetPropertyStr(ctx, global, name);
    std::string out;
    if (JS_IsFunction(ctx, fn)) {
        JSValue argv[1];
        int argc = 0;
        if (arg != nullptr) { argv[0] = JS_NewString(ctx, arg); argc = 1; }
        JSValue ret = JS_Call(ctx, fn, global, argc, argc ? argv : nullptr);
        if (argc) JS_FreeValue(ctx, argv[0]);
        if (JS_IsException(ret)) {
            JSValue     exc = JS_GetException(ctx);
            const char* s   = JS_ToCString(ctx, exc);
            JSValue     stk = JS_GetPropertyStr(ctx, exc, "stack");
            const char* st  = JS_IsUndefined(stk) ? nullptr : JS_ToCString(ctx, stk);
            std::string detail = s ? s : "?";
            if (st) { detail += "\n"; detail += st; }
            if (g.log) g.log(name, detail.c_str());
            if (st) JS_FreeCString(ctx, st);
            JS_FreeValue(ctx, stk);
            if (s) JS_FreeCString(ctx, s);
            JS_FreeValue(ctx, exc);
        } else if (JS_IsString(ret)) {
            const char* s = JS_ToCString(ctx, ret);
            if (s) { out = s; JS_FreeCString(ctx, s); }
        } else if (JS_IsBool(ret)) {
            out = JS_ToBool(ctx, ret) > 0 ? "true" : "false";
        }
        JS_FreeValue(ctx, ret);
    }
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, global);
    return out;
}

/** Ask the control plane whether a close should be VETOED (the unsaved-changes guard).
 *
 *  Identical in both hosts; only the wrapper differs, and each wrapper is correct for its host. The
 *  plugin latches allowClose_ because its own requestQuit() calls getWindow().close(), which re-enters
 *  onClose; SDL's jsQuitWindow just clears a flag, so it needs no latch. A null context allows the
 *  close in both (the plugin returns true, SDL leaves veto false) - there is no UI left to ask. */
inline bool jsCloseVetoed(JSContext* ctx) {
    if (!ctx) return false;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue fn     = JS_GetPropertyStr(ctx, global, "__rp_onCloseRequested");
    bool veto = false;
    if (JS_IsFunction(ctx, fn)) {
        JSValue ret = JS_Call(ctx, fn, JS_UNDEFINED, 0, nullptr);
        veto = JS_ToBool(ctx, ret) == 1;
        JS_FreeValue(ctx, ret);
    }
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, global);
    return veto;
}

/** Deliver a file dialog's pick into JS by calling the resolver the real Backend registered. An empty
 *  path is a CANCEL and crosses as null, which is what the awaiting Promise expects.
 *
 *  Safe as a direct call because both hosts run it on the same thread that pumps the JS loop - the
 *  Promise settles on the next tick. The plugin calls lvglMakeCurrent() first, which stays at its call
 *  site: that is about which editor's LVGL instance is current when several share one process, not
 *  about delivering the pick. */
inline void jsDeliverFileBrowserResult(JSContext* ctx, const char* path) {
    if (!ctx) return;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue fn     = JS_GetPropertyStr(ctx, global, "__rp_onFileBrowserResult");
    if (JS_IsFunction(ctx, fn)) {
        JSValue arg = (path && *path) ? JS_NewString(ctx, path) : JS_NULL;
        JSValue ret = JS_Call(ctx, fn, JS_UNDEFINED, 1, &arg);
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, arg);
    }
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, global);
}
