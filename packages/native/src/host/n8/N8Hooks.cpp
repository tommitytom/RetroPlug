#include "host/n8/N8Hooks.hpp"

#include <cstdint>
#include <cstring>

#include "quickjs.h"

#include "host/n8/N8Host.hpp"

namespace retroplug {
namespace {

// Recover the N8Host* from a hook's func-data. We pack the pointer's bytes directly (like ContextTargets'
// packContextTarget, but without the re-pointable Slot indirection): an N8Host's lifetime equals its
// control-plane context's, so it never comes and goes while the context lives. Each bind carries its own
// instance's pointer, so several plugin instances on separate contexts never cross-route.
N8Host* hostFromData(JSContext* ctx, JSValue* funcData) {
    std::size_t   len = 0;
    std::uint8_t* raw = JS_GetArrayBuffer(ctx, &len, funcData[0]);
    if (!raw || len != sizeof(N8Host*)) return nullptr;
    N8Host* h = nullptr;
    std::memcpy(&h, raw, sizeof(h));
    return h;
}

JSValue jsGetN8Config(JSContext* ctx, JSValueConst, int, JSValueConst*, int, JSValue* funcData) {
    JSValue o = JS_NewObject(ctx);
    N8Host* h = hostFromData(ctx, funcData);
    if (h) {
        const N8ConfigDto c = h->getConfig();
        JSValue arr = JS_NewArray(ctx);
        std::uint32_t i = 0;
        for (const N8PortDto& p : c.ports) {
            JSValue e = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, e, "port", JS_NewString(ctx, p.port.c_str()));
            JS_SetPropertyStr(ctx, e, "isN8", JS_NewBool(ctx, p.isN8));
            JS_SetPropertyUint32(ctx, arr, i++, e);
        }
        JS_SetPropertyStr(ctx, o, "ports", arr);
        JS_SetPropertyStr(ctx, o, "selectedPort", JS_NewString(ctx, c.selectedPort.c_str()));
        JS_SetPropertyStr(ctx, o, "connected", JS_NewBool(ctx, c.connected));
        JS_SetPropertyStr(ctx, o, "enabled", JS_NewBool(ctx, c.enabled));
        JS_SetPropertyStr(ctx, o, "lookaheadMs", JS_NewInt32(ctx, c.lookaheadMs));
        JS_SetPropertyStr(ctx, o, "bytes", JS_NewInt64(ctx, static_cast<std::int64_t>(c.bytes)));
        JS_SetPropertyStr(ctx, o, "error", JS_NewString(ctx, c.error.c_str()));
    }
    return o;
}

JSValue jsSetN8Port(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int, JSValue* funcData) {
    N8Host* h = hostFromData(ctx, funcData);
    if (h && argc >= 1) {
        const char* s = JS_ToCString(ctx, argv[0]);
        h->setPort(s ? s : "");
        if (s) JS_FreeCString(ctx, s);
    }
    return JS_UNDEFINED;
}

JSValue jsConnectN8(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int, JSValue* funcData) {
    N8Host* h = hostFromData(ctx, funcData);
    if (h && argc >= 1) h->connect(JS_ToBool(ctx, argv[0]) != 0);
    return JS_UNDEFINED;
}

JSValue jsSetN8Lookahead(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int, JSValue* funcData) {
    N8Host* h = hostFromData(ctx, funcData);
    if (h && argc >= 1) {
        std::int32_t ms = 0;
        JS_ToInt32(ctx, &ms, argv[0]);
        h->setLookahead(ms);
    }
    return JS_UNDEFINED;
}

bool hasGlobalFn(JSContext* ctx, const char* name) {
    JSValue    g   = JS_GetGlobalObject(ctx);
    JSValue    fn  = JS_GetPropertyStr(ctx, g, name);
    const bool has = JS_IsFunction(ctx, fn);
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, g);
    return has;
}

}  // namespace

void bindN8Hooks(JSContext* ctx, N8Host& host) {
    if (!ctx) return;
    if (hasGlobalFn(ctx, "__rp_getN8Config")) return;  // bind once per context
    JSValue g = JS_GetGlobalObject(ctx);
    N8Host* h = &host;
    auto bind = [&](const char* name, JSCFunctionData* fn, int length) {
        JSValue data = JS_NewArrayBufferCopy(ctx, reinterpret_cast<const std::uint8_t*>(&h), sizeof(h));
        JS_SetPropertyStr(ctx, g, name, JS_NewCFunctionData(ctx, fn, length, 0, 1, &data));
        JS_FreeValue(ctx, data);
    };
    bind("__rp_getN8Config", jsGetN8Config, 0);
    bind("__rp_setN8Port", jsSetN8Port, 1);
    bind("__rp_connectN8", jsConnectN8, 1);
    bind("__rp_setN8Lookahead", jsSetN8Lookahead, 1);
    JS_FreeValue(ctx, g);
}

}  // namespace retroplug
