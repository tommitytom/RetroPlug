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

JSValue newByteArray(JSContext* ctx, const std::vector<std::uint8_t>& v) {
    JSValue arr = JS_NewArray(ctx);
    for (std::uint32_t i = 0; i < v.size(); ++i)
        JS_SetPropertyUint32(ctx, arr, i, JS_NewInt32(ctx, v[i]));
    return arr;
}

/** A JS array of numbers as bytes. Shared by the two blobs TS hands down - the farewell and the scan probe -
 *  both of which native forwards verbatim without parsing. */
std::vector<std::uint8_t> toBytes(JSContext* ctx, JSValueConst v) {
    std::vector<std::uint8_t> bytes;
    if (!JS_IsArray(v)) return bytes;
    std::uint32_t len  = 0;
    JSValue       lenv = JS_GetPropertyStr(ctx, v, "length");
    JS_ToUint32(ctx, &len, lenv);
    JS_FreeValue(ctx, lenv);
    bytes.reserve(len);
    for (std::uint32_t i = 0; i < len; ++i) {
        JSValue      e = JS_GetPropertyUint32(ctx, v, i);
        std::int32_t b = 0;
        JS_ToInt32(ctx, &b, e);
        JS_FreeValue(ctx, e);
        bytes.push_back(static_cast<std::uint8_t>(b & 0xFF));
    }
    return bytes;
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
        JS_SetPropertyStr(ctx, o, "deviceLabel", JS_NewString(ctx, c.deviceLabel.c_str()));
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
    if (!h || argc < 1) return JS_UNDEFINED;
    h->setFarewell(toBytes(ctx, argv[0]));
    return JS_UNDEFINED;
}

// Start a port scan. The probe is another opaque blob (TS builds a Universal Device Inquiry); native writes
// it out of each port and reports what comes back, and TS is what knows a Novation reply when it sees one.
// Returns whether a scan actually started - it will not while one is running, or while the link holds a pair.
JSValue jsScanLaunchpad(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int, JSValue* funcData) {
    LaunchpadHost* h = hostFromData(ctx, funcData);
    if (!h || argc < 1) return JS_NewBool(ctx, false);
    std::int32_t windowMs = 0;
    if (argc >= 2) JS_ToInt32(ctx, &windowMs, argv[1]);
    if (windowMs <= 0) windowMs = 300;
    return JS_NewBool(ctx, h->scan(toBytes(ctx, argv[0]), static_cast<unsigned>(windowMs)));
}

JSValue jsGetLaunchpadScan(JSContext* ctx, JSValueConst, int, JSValueConst*, int, JSValue* funcData) {
    JSValue        o = JS_NewObject(ctx);
    LaunchpadHost* h = hostFromData(ctx, funcData);
    if (h) {
        const LaunchpadScanStatusDto s = h->scanStatus();  // also fires the scan's finishing edge (UI thread)
        JS_SetPropertyStr(ctx, o, "busy", JS_NewBool(ctx, s.busy));
        JS_SetPropertyStr(ctx, o, "done", JS_NewBool(ctx, s.done));
        JS_SetPropertyStr(ctx, o, "phase", JS_NewString(ctx, s.phase.c_str()));
        JS_SetPropertyStr(ctx, o, "error", JS_NewString(ctx, s.error.c_str()));
        JS_SetPropertyStr(ctx, o, "skipped", JS_NewInt64(ctx, s.skipped));
        JS_SetPropertyStr(ctx, o, "version", JS_NewInt64(ctx, static_cast<std::int64_t>(s.version)));
        JSValue replies = JS_NewArray(ctx);
        for (std::uint32_t i = 0; i < s.replies.size(); ++i) {
            JSValue r = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, r, "output", JS_NewString(ctx, s.replies[i].output.c_str()));
            JS_SetPropertyStr(ctx, r, "input", JS_NewString(ctx, s.replies[i].input.c_str()));
            JS_SetPropertyStr(ctx, r, "bytes", newByteArray(ctx, s.replies[i].bytes));
            JS_SetPropertyUint32(ctx, replies, i, r);
        }
        JS_SetPropertyStr(ctx, o, "replies", replies);
    }
    return o;
}

// What the scan identified, in TS's words. Native stores the string and hands it back; it never parses it,
// for the same reason it never parses the farewell.
JSValue jsSetLaunchpadDevice(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int, JSValue* funcData) {
    LaunchpadHost* h = hostFromData(ctx, funcData);
    if (h && argc >= 1) h->setDeviceLabel(toStr(ctx, argv[0]));
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
    bind("__rp_scanLaunchpad", jsScanLaunchpad, 2);
    bind("__rp_getLaunchpadScan", jsGetLaunchpadScan, 0);
    bind("__rp_setLaunchpadDevice", jsSetLaunchpadDevice, 1);
    JS_FreeValue(ctx, g);
}

}  // namespace retroplug
