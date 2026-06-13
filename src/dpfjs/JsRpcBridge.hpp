#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "LvglJsEngine.hpp"
#include "RpcEnvelope.h"
#include "TypedRpcServer.h"
#include "codecs/MsgpackCodec.h"
#include "transports/QueueTransport.h"

extern "C" {
    #include <quickjs.h>
}

namespace dpfjs {

// Generic QuickJS <-> rpcpp bridge for a concrete Service. Owns the rpc
// server + transport, builds a Symbol.for(<namespace>) JS object with the single
// sync `__rpcSend` entry (dispatched through the shared txiki host) plus a
// `__log` stderr shim, and drains async/notification frames to
// engine.emit("rpc-message"). The consumer registers the service's methods on
// server() and may attach further native props to jsNamespace().
//
// A template (not a type-erased base) because TypedRpcServer reflects the
// concrete Service's method set at compile time — `processMessage` and
// `writeNotification<T>` are concrete-Service members. Keeping the server
// concrete here means the domain bridge calls writeNotification directly (no
// indirection) and __rpcSend has no vcall.
template <class Service, class Codec = rpcpp::MsgpackCodec>
class JsRpcBridge {
public:
    using RpcTransport = rpcpp::QueueTransport<Codec>;
    using RpcServer    = rpcpp::TypedRpcServer<Service, Codec>;

    JsRpcBridge(LvglJsEngine& engine, Service& service, const char* namespaceSymbol)
        : engine_(engine) {
        // transport must outlive server (member order below).
        transport_ = std::make_unique<RpcTransport>();
        server_    = std::make_unique<RpcServer>(service, *transport_);

        JSContext* ctx = engine_.getContext();
        if (!ctx) return;

        JSValue global = JS_GetGlobalObject(ctx);
        JSValue sym    = JS_NewSymbol(ctx, namespaceSymbol, /*is_global*/ true);
        JSAtom atom    = JS_ValueToAtom(ctx, sym);
        JSValue ns     = JS_NewObjectProto(ctx, JS_NULL);

        JS_DefinePropertyValue(ctx, global, atom, ns, JS_PROP_C_W_E);

        // __rpcSend: the host's generic trampoline; we supply the dispatch
        // callable, capturing this (one bridge per engine; the host frees the
        // binding at shutdown, after the bridge). Echo server-side JSON-RPC
        // error envelopes to stderr — the JS client drops error replies with a
        // null id (every notification reply), so without this a typed-handler
        // exception in C++ would read as "nothing happened" on the UI side.
        engine_.host().bindRpcSend(ns,
            [this](std::string_view bytes) -> std::optional<std::vector<char>> {
                auto reply = server_->processMessage(
                    std::span<const char>(bytes.data(), bytes.size()));
                if (reply) {
                    if (auto err = Codec::template read<rpcpp::RpcError>(
                            std::span<const char>{reply->data(), reply->size()});
                        err) {
                        std::fprintf(stderr, "[rpc] error %d: %s\n",
                                     err->error.code, err->error.message.c_str());
                    }
                }
                return reply;
            });
        JS_SetPropertyStr(ctx, ns, "__log",
                          JS_NewCFunction(ctx, js_log, "__log", 2));

        ns_ = JS_DupValue(ctx, ns);

        JS_FreeAtom(ctx, atom);
        JS_FreeValue(ctx, sym);
        JS_FreeValue(ctx, global);
    }

    ~JsRpcBridge() {
        if (JSContext* ctx = engine_.getContext(); ctx && !JS_IsUndefined(ns_)) {
            JS_FreeValue(ctx, ns_);
            ns_ = JS_UNDEFINED;
        }
    }

    JsRpcBridge(const JsRpcBridge&)            = delete;
    JsRpcBridge& operator=(const JsRpcBridge&) = delete;

    RpcServer&    server()      { return *server_; }
    RpcTransport& transport()   { return *transport_; }
    JSValue       jsNamespace() const { return ns_; }  // borrowed; consumer adds props

    // Drain the rpc transport's outgoing queue (async resolver responses +
    // notification frames) into engine.emit("rpc-message", ArrayBuffer).
    void pumpAsync() {
        if (!transport_) return;
        JSContext* ctx = engine_.getContext();
        if (!ctx) return;
        while (auto frame = transport_->tryReceive()) {
            JSValue ab = JS_NewArrayBufferCopy(ctx,
                reinterpret_cast<const std::uint8_t*>(frame->data()), frame->size());
            engine_.emit("rpc-message", 1, &ab);
            JS_FreeValue(ctx, ab);
        }
    }

private:
    // Console-stderr shim: `__log(level, msg)`. tjs has no native console, so
    // the JS-side polyfill (ui/runtime/console.ts) routes through this.
    static JSValue js_log(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        const char* level = (argc >= 1) ? JS_ToCString(ctx, argv[0]) : nullptr;
        const char* msg   = (argc >= 2) ? JS_ToCString(ctx, argv[1]) : nullptr;
        std::fprintf(stderr, "[js:%s] %s\n", level ? level : "log", msg ? msg : "");
        if (level) JS_FreeCString(ctx, level);
        if (msg)   JS_FreeCString(ctx, msg);
        return JS_UNDEFINED;
    }

    LvglJsEngine& engine_;
    JSValue       ns_ = JS_UNDEFINED;
    std::unique_ptr<RpcTransport> transport_; // before server_
    std::unique_ptr<RpcServer>    server_;
};

} // namespace dpfjs
