#include "PluginJsBridge.hpp"

#include <cstdio>
#include <memory>
#include <span>
#include <utility>

#include "RpcEnvelope.h"

extern "C" {
    #include <quickjs.h>
}

namespace {

// Resolve the bridge instance bound to the current LVGL display.
PluginJsBridge* bridgeFromContext() {
    DpfJsDisplayData* data = DpfJsDisplayData::get();
    return (data && data->bridge) ? static_cast<PluginJsBridge*>(data->bridge) : nullptr;
}

} // namespace

PluginJsBridge::PluginJsBridge(LvglJsEngine& eng,
                               Project* project,
                               CommandQueue* commands,
                               EventQueue* events,
                               std::atomic<double>* sampleRate,
                               std::atomic<SystemId>* focusedSystemId,
                               UserConfig* userConfig)
    : engine(eng),
      project_(project) {
    if (DpfJsDisplayData* data = DpfJsDisplayData::get())
        data->bridge = this;

    // Stand up the rpcpp server stack. The service holds the shared-state
    // pointers from PluginUI; the transport buffers async/notification frames
    // for `pumpAsync` to fan out via engine.emit.
    rpcService_   = std::make_unique<PluginRpcService>(project, commands, events,
                                                       sampleRate, focusedSystemId,
                                                       userConfig);
    rpcTransport_ = std::make_unique<RpcTransport>();
    rpcServer_    = std::make_unique<RpcServer>(*rpcService_, *rpcTransport_);

    rpcServer_->addMethod<&PluginRpcService::getFrame>();
    rpcServer_->addMethod<&PluginRpcService::openRomBrowser>();
    rpcServer_->addMethod<&PluginRpcService::openSaveProjectBrowser>();
    rpcServer_->addMethod<&PluginRpcService::openLoadProjectBrowser>();
    rpcServer_->addMethod<&PluginRpcService::loadRomFromPath>();
    rpcServer_->addMethod<&PluginRpcService::addRomFromPath>();
    rpcServer_->addMethod<&PluginRpcService::replaceRomFromPath>();
    rpcServer_->addMethod<&PluginRpcService::removeSystem>();
    rpcServer_->addMethod<&PluginRpcService::listSystems>();
    rpcServer_->addMethod<&PluginRpcService::setFocus>();
    rpcServer_->addMethod<&PluginRpcService::getFocus>();
    rpcServer_->addMethod<&PluginRpcService::pressButton>();
    rpcServer_->addMethod<&PluginRpcService::setLinkGroupId>();
    rpcServer_->addMethod<&PluginRpcService::getMidiRouting>();
    rpcServer_->addMethod<&PluginRpcService::setMidiRouting>();
    rpcServer_->addMethod<&PluginRpcService::setLsdjSyncConfig>();
    rpcServer_->addMethod<&PluginRpcService::setWindowSize>();
    rpcServer_->addMethod<&PluginRpcService::isWindowSizeControlled>();
    rpcServer_->addMethod<&PluginRpcService::getUserConfig>();
    rpcServer_->addMethod<&PluginRpcService::setActiveBindings>();
    rpcServer_->addDiscoveryMethod();

    // Service emits string-payload JS events through the existing engine
    // channel mechanism (on/off in runtime/lvgljs/index.ts).
    rpcService_->setEmitEventCallback(
        [this](const std::string& channel, const std::string& payload) {
            JSContext* ctx = engine.getContext();
            if (!ctx) return;
            JSValue v = JS_NewStringLen(ctx, payload.data(), payload.size());
            engine.emit(channel.c_str(), 1, &v);
            JS_FreeValue(ctx, v);
        });

    // Expose the JS side's plugin namespace with the single sync RPC entry.
    JSContext* ctx = engine.getContext();
    if (!ctx) return;

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sym    = JS_NewSymbol(ctx, "plugin", true);
    JSAtom atom    = JS_ValueToAtom(ctx, sym);
    JSValue ns     = JS_NewObjectProto(ctx, JS_NULL);

    JS_DefinePropertyValue(ctx, global, atom, ns, JS_PROP_C_W_E);

    JS_SetPropertyStr(ctx, ns, "__rpcSend",
                      JS_NewCFunction(ctx, js_rpcSend, "__rpcSend", 1));

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
    PluginJsBridge* self = bridgeFromContext();
    if (!self || !self->rpcServer_)
        return JS_ThrowInternalError(ctx, "plugin.__rpcSend: bridge unavailable");
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "plugin.__rpcSend: expected (bytes: Uint8Array)");

    // Accept either a TypedArray view (Uint8Array, the common case from the
    // JS client) or a raw ArrayBuffer. JS_GetTypedArrayBuffer returns the
    // underlying ArrayBuffer plus the view's offset/length so we don't
    // accidentally read past the slice.
    size_t byteOffset = 0;
    size_t byteLength = 0;
    size_t arrayLen   = 0;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[0],
                                        &byteOffset, &byteLength, nullptr);
    uint8_t* data = nullptr;
    if (!JS_IsException(ab)) {
        data = JS_GetArrayBuffer(ctx, &arrayLen, ab);
    } else {
        JS_FreeValue(ctx, ab);
        data = JS_GetArrayBuffer(ctx, &arrayLen, argv[0]);
        byteOffset = 0;
        byteLength = arrayLen;
        ab = JS_DupValue(ctx, argv[0]);
    }
    if (!data) {
        JS_FreeValue(ctx, ab);
        return JS_ThrowTypeError(ctx, "plugin.__rpcSend: argument is not bytes");
    }

    std::span<const char> bytes(reinterpret_cast<const char*>(data + byteOffset),
                                byteLength);
    auto reply = self->rpcServer_->processMessage(bytes);
    JS_FreeValue(ctx, ab);

    if (!reply) return JS_NULL;

    // Surface server-side JSON-RPC error envelopes on stderr. The JS
    // client drops error replies whose id is null/undefined (which is
    // every notification reply), so without this hook a typed-handler
    // exception in C++ shows up as "nothing happens" on the UI side.
    if (auto err = rpcpp::MsgpackCodec::read<rpcpp::RpcError>(
            std::span<const char>{reply->data(), reply->size()});
        err) {
        std::fprintf(stderr, "[rpc] error %d: %s\n",
                     err->error.code, err->error.message.c_str());
    }

    return JS_NewArrayBufferCopy(ctx,
        reinterpret_cast<const uint8_t*>(reply->data()),
        reply->size());
}

void PluginJsBridge::pumpAsync() {
    if (!rpcTransport_) return;
    JSContext* ctx = engine.getContext();
    if (!ctx) return;
    while (auto frame = rpcTransport_->tryReceive()) {
        JSValue ab = JS_NewArrayBufferCopy(ctx,
            reinterpret_cast<const uint8_t*>(frame->data()),
            frame->size());
        engine.emit("rpc-message", 1, &ab);
        JS_FreeValue(ctx, ab);
    }
}
