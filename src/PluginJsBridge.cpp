#include "PluginJsBridge.hpp"

#include <chrono>
#include <thread>
#include <utility>

extern "C" {
    #include <quickjs.h>
}

#include "project/Project.hpp"
#include "system/SystemBase.hpp"
#include "transport/FrameBufferTriple.hpp"

std::string PluginJsBridge::HelloService::greet(std::string name) {
    return "Hello, " + name + "!";
}

void PluginJsBridge::HelloService::greetSlow(std::string name,
                                             rpcpp::Resolver<std::string> resolver) {
    std::thread([name = std::move(name), resolver = std::move(resolver)]() mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        resolver.resolve("Hello (slow), " + name + "!");
    }).detach();
}

PluginJsBridge::PluginJsBridge(LvglJsEngine& eng, Project* project)
    : engine(eng), project_(project), server(hello, transport) {
    if (DpfJsDisplayData* data = DpfJsDisplayData::get())
        data->bridge = this;

    server.addMethod<&HelloService::greet>("greet");
    server.addAsyncMethod<&HelloService::greetSlow>("greetSlow");

    // Build globalThis[Symbol.for("plugin")] and attach the plugin's JS surface:
    // rpcSend/rpcPoll for the RPC bridge, getFrame for direct framebuffer access.
    JSContext* ctx = engine.getContext();
    if (!ctx) return;

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sym    = JS_NewSymbol(ctx, "plugin", true);
    JSAtom atom    = JS_ValueToAtom(ctx, sym);
    JSValue ns     = JS_NewObjectProto(ctx, JS_NULL);

    JS_DefinePropertyValue(ctx, global, atom, ns, JS_PROP_C_W_E);

    JS_SetPropertyStr(ctx, ns, "rpcSend",
                      JS_NewCFunction(ctx, js_rpcSend, "rpcSend", 1));
    JS_SetPropertyStr(ctx, ns, "rpcPoll",
                      JS_NewCFunction(ctx, js_rpcPoll, "rpcPoll", 0));
    JS_SetPropertyStr(ctx, ns, "getFrame",
                      JS_NewCFunction(ctx, js_getFrame, "getFrame", 1));

    pluginNamespace = JS_DupValue(ctx, ns);

    JS_FreeAtom(ctx, atom);
    JS_FreeValue(ctx, sym);
    JS_FreeValue(ctx, global);
}

PluginJsBridge::~PluginJsBridge() {
    if (JSContext* ctx = engine.getContext(); ctx && !JS_IsUndefined(pluginNamespace)) {
        JS_FreeValue(ctx, pluginNamespace);
        pluginNamespace = JS_UNDEFINED;
    }
    if (DpfJsDisplayData* data = DpfJsDisplayData::get()) {
        if (data->bridge == this)
            data->bridge = nullptr;
    }
}

JSValue PluginJsBridge::js_rpcSend(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "plugin.rpcSend: expected message string");
    DpfJsDisplayData* data = DpfJsDisplayData::get();
    if (!data || !data->bridge)
        return JS_NULL;

    size_t len = 0;
    const char* msg = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!msg)
        return JS_EXCEPTION;

    auto resp = data->bridge->server.processMessage(std::string_view{msg, len});
    JS_FreeCString(ctx, msg);

    if (!resp.has_value())
        return JS_NULL;
    return JS_NewStringLen(ctx, resp->data(), resp->size());
}

JSValue PluginJsBridge::js_rpcPoll(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    DpfJsDisplayData* data = DpfJsDisplayData::get();
    if (!data || !data->bridge)
        return JS_NULL;

    auto resp = data->bridge->transport.tryReceive();
    if (!resp.has_value())
        return JS_NULL;
    return JS_NewStringLen(ctx, resp->data(), resp->size());
}

JSValue PluginJsBridge::js_getFrame(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    DpfJsDisplayData* data = DpfJsDisplayData::get();
    if (!data || !data->bridge) return JS_NULL;

    Project* proj = data->bridge->project();
    if (!proj) return JS_NULL;

    int32_t systemIdInt = 0;
    if (argc >= 1) {
        if (JS_ToInt32(ctx, &systemIdInt, argv[0]) < 0)
            return JS_EXCEPTION;
    }

    SystemBase* sys = proj->findSystem(static_cast<SystemId>(systemIdInt));
    if (!sys) return JS_NULL;

    FrameBufferTriple* fb = sys->framebuffer();
    if (!fb) return JS_NULL;

    const uint32_t w = fb->width();
    const uint32_t h = fb->height();
    const size_t   pixels = size_t(w) * h;

    // Read into a stack/heap staging buffer, then JS_NewArrayBufferCopy into JS.
    // 90 KiB (SameBoy) / 240 KiB (NES); a few-KB stack threshold is fine, but
    // play it safe with a heap allocation.
    std::vector<uint32_t> staging(pixels, 0u);
    if (!fb->readInto(staging.data(), w * h))
        return JS_NULL;  // no frame published yet

    JSValue buf = JS_NewArrayBufferCopy(ctx,
        reinterpret_cast<const uint8_t*>(staging.data()),
        pixels * sizeof(uint32_t));
    if (JS_IsException(buf)) return buf;

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "width",  JS_NewUint32(ctx, w));
    JS_SetPropertyStr(ctx, obj, "height", JS_NewUint32(ctx, h));
    JS_SetPropertyStr(ctx, obj, "buffer", buf); // takes ownership
    return obj;
}
