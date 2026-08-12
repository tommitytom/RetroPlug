#include "host/launchpad/LaunchpadHooks.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "quickjs.h"

#include "host/launchpad/LaunchpadHost.hpp"

namespace retroplug {
namespace {

// Recover the LaunchpadHost* from a hook's func-data - the N8Hooks idiom: the pointer's bytes are packed
// directly, since a host's lifetime equals its control-plane context's.
LaunchpadHost* hostFromData(JSContext* ctx, JSValue* funcData) {
    std::size_t    len = 0;
    std::uint8_t*  raw = JS_GetArrayBuffer(ctx, &len, funcData[0]);
    if (!raw || len != sizeof(LaunchpadHost*)) return nullptr;
    LaunchpadHost* h = nullptr;
    std::memcpy(&h, raw, sizeof(h));
    return h;
}

std::string toStr(JSContext* ctx, JSValueConst v) {
    const char* s = JS_ToCString(ctx, v);
    std::string out = s ? s : "";
    if (s) JS_FreeCString(ctx, s);
    return out;
}

JSValue newStringArray(JSContext* ctx, const std::vector<std::string>& v) {
    JSValue arr = JS_NewArray(ctx);
    for (std::uint32_t i = 0; i < v.size(); ++i)
        JS_SetPropertyUint32(ctx, arr, i, JS_NewString(ctx, v[i].c_str()));
    return arr;
}

JSValue jsGetLaunchpadConfig(JSContext* ctx, JSValueConst, int, JSValueConst*, int, JSValue* funcData) {
    JSValue        o = JS_NewObject(ctx);
    LaunchpadHost* h = hostFromData(ctx, funcData);
    if (h) {
        const LaunchpadConfigDto c = h->getConfig();
        JS_SetPropertyStr(ctx, o, "inputs", newStringArray(ctx, c.inputs));
        JS_SetPropertyStr(ctx, o, "outputs", newStringArray(ctx, c.outputs));
        JS_SetPropertyStr(ctx, o, "selectedInput", JS_NewString(ctx, c.selectedInput.c_str()));
        JS_SetPropertyStr(ctx, o, "selectedOutput", JS_NewString(ctx, c.selectedOutput.c_str()));
        JS_SetPropertyStr(ctx, o, "connected", JS_NewBool(ctx, c.connected));
        JS_SetPropertyStr(ctx, o, "enabled", JS_NewBool(ctx, c.enabled));
        JS_SetPropertyStr(ctx, o, "sent", JS_NewInt64(ctx, static_cast<std::int64_t>(c.sent)));
        JS_SetPropertyStr(ctx, o, "dropped", JS_NewInt64(ctx, static_cast<std::int64_t>(c.dropped)));
        JS_SetPropertyStr(ctx, o, "error", JS_NewString(ctx, c.error.c_str()));
    }
    return o;
}

JSValue jsSetLaunchpadPorts(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int, JSValue* funcData) {
    LaunchpadHost* h = hostFromData(ctx, funcData);
    if (h && argc >= 2) h->setPorts(toStr(ctx, argv[0]), toStr(ctx, argv[1]));
    return JS_UNDEFINED;
}

JSValue jsConnectLaunchpad(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int, JSValue* funcData) {
    LaunchpadHost* h = hostFromData(ctx, funcData);
    if (h && argc >= 1) h->connect(JS_ToBool(ctx, argv[0]) != 0);
    return JS_UNDEFINED;
}

// The farewell blob: an array of bytes TS builds (exitToLiveMode) and native replays verbatim when the
// device is given back. Deliberately opaque - the whole point is that native never learns the protocol.
JSValue jsSetLaunchpadFarewell(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int, JSValue* funcData) {
    LaunchpadHost* h = hostFromData(ctx, funcData);
    if (!h || argc < 1 || !JS_IsArray(argv[0])) return JS_UNDEFINED;
    std::uint32_t len = 0;
    JSValue       lenv = JS_GetPropertyStr(ctx, argv[0], "length");
    JS_ToUint32(ctx, &len, lenv);
    JS_FreeValue(ctx, lenv);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(len);
    for (std::uint32_t i = 0; i < len; ++i) {
        JSValue      v = JS_GetPropertyUint32(ctx, argv[0], i);
        std::int32_t b = 0;
        JS_ToInt32(ctx, &b, v);
        JS_FreeValue(ctx, v);
        bytes.push_back(static_cast<std::uint8_t>(b & 0xFF));
    }
    h->setFarewell(std::move(bytes));
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

void bindLaunchpadHooks(JSContext* ctx, LaunchpadHost& host) {
    if (!ctx) return;
    if (hasGlobalFn(ctx, "__rp_getLaunchpadConfig")) return;  // bind once per context
    JSValue        g = JS_GetGlobalObject(ctx);
    LaunchpadHost* h = &host;
    auto bind = [&](const char* name, JSCFunctionData* fn, int length) {
        JSValue data = JS_NewArrayBufferCopy(ctx, reinterpret_cast<const std::uint8_t*>(&h), sizeof(h));
        JS_SetPropertyStr(ctx, g, name, JS_NewCFunctionData(ctx, fn, length, 0, 1, &data));
        JS_FreeValue(ctx, data);
    };
    bind("__rp_getLaunchpadConfig", jsGetLaunchpadConfig, 0);
    bind("__rp_setLaunchpadPorts", jsSetLaunchpadPorts, 2);
    bind("__rp_connectLaunchpad", jsConnectLaunchpad, 1);
    bind("__rp_setLaunchpadFarewell", jsSetLaunchpadFarewell, 1);
    JS_FreeValue(ctx, g);
}

}  // namespace retroplug
