#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "LvglJsEngine.hpp"

#include "TypedRpcServer.h"
#include "codecs/JsonCodec.h"
#include "transports/QueueTransport.h"

extern "C" {
    #include <quickjs.h>
}

// Plugin-specific glue between LvglJsEngine and DPF. Generic parameter handling
// (lvgljs.setParameter, name<->index lookup, "parameter" event push) lives in
// LvglJsEngine itself; this class is the place to add JS bridges that only
// make sense for *this* plugin — the waveform display and the JSON-RPC bridge
// to plugin-specific C++ services.
//
// Lifetime: must be destroyed before the LvglJsEngine it references.
class PluginJsBridge {
public:
    explicit PluginJsBridge(LvglJsEngine& engine);
    ~PluginJsBridge();

    PluginJsBridge(const PluginJsBridge&) = delete;
    PluginJsBridge& operator=(const PluginJsBridge&) = delete;

    void pushWaveform(const float* samples, uint32_t count);

    // Hello-world RPC service. Sync greet() is registered with addMethod and
    // returns inline; async greetSlow() resolves on a worker thread after a
    // short delay, exercising the polling path.
    class HelloService {
    public:
        std::string greet(std::string name);
        void greetSlow(std::string name, rpcpp::Resolver<std::string> resolver);
    };

private:
    // JS bindings attached to the plugin namespace (separate from lvgljs).
    static JSValue js_rpcSend(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_rpcPoll(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    LvglJsEngine& engine;

    HelloService hello;
    rpcpp::QueueTransport<rpcpp::JsonCodec> transport;
    rpcpp::TypedRpcServer<HelloService, rpcpp::JsonCodec> server;
    JSValue pluginNamespace = JS_UNDEFINED;
};
