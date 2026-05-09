#include "PluginJsBridge.hpp"

#include <chrono>
#include <thread>
#include <utility>

extern "C" {
    #include <quickjs.h>
}

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

PluginJsBridge::PluginJsBridge(LvglJsEngine& eng)
    : engine(eng), server(hello, transport) {
    if (DpfJsDisplayData* data = DpfJsDisplayData::get())
        data->bridge = this;

    server.addMethod<&HelloService::greet>("greet");
    server.addAsyncMethod<&HelloService::greetSlow>("greetSlow");

    // Build the plugin-specific JS namespace at globalThis[Symbol.for("plugin")]
    // and attach the rpc bindings to it. Kept separate from lvgljs (which is
    // framework-agnostic) so this plugin's surface area doesn't leak into the
    // generic engine.
    JSContext* ctx = engine.getContext();
    if (!ctx) return;

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sym = JS_NewSymbol(ctx, "plugin", true);
    JSAtom atom = JS_ValueToAtom(ctx, sym);
    JSValue ns = JS_NewObjectProto(ctx, JS_NULL);

    JS_DefinePropertyValue(ctx, global, atom, ns, JS_PROP_C_W_E);

    JS_SetPropertyStr(ctx, ns, "rpcSend",
                      JS_NewCFunction(ctx, js_rpcSend, "rpcSend", 1));
    JS_SetPropertyStr(ctx, ns, "rpcPoll",
                      JS_NewCFunction(ctx, js_rpcPoll, "rpcPoll", 0));

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

void PluginJsBridge::pushWaveform(const float* samples, uint32_t count) {
    JSContext* ctx = engine.getContext();
    if (!ctx || count == 0)
        return;
    JSValue buf = JS_NewArrayBufferCopy(ctx,
                                        reinterpret_cast<const uint8_t*>(samples),
                                        count * sizeof(float));
    if (JS_IsException(buf))
        return;
    engine.emit("waveform", 1, &buf);
    JS_FreeValue(ctx, buf);
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
