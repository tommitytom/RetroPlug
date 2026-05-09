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

class Project;

// Plugin-specific glue between LvglJsEngine and DPF. Generic parameter handling
// (lvgljs.setParameter, name<->index lookup, "parameter" event push) lives in
// LvglJsEngine itself; this class is the place to add JS bridges that only
// make sense for *this* plugin — the framebuffer accessor and the JSON-RPC
// bridge to plugin-specific C++ services.
//
// Lifetime: must be destroyed before the LvglJsEngine it references.
class PluginJsBridge {
public:
    // `project` may be nullptr (LV2-UI: DSP+UI live in separate binaries, so
    // getPluginInstancePointer() is null and there's no shared Project pointer).
    // The bridge handles that gracefully — plugin.getFrame returns null.
    PluginJsBridge(LvglJsEngine& engine, Project* project);
    ~PluginJsBridge();

    PluginJsBridge(const PluginJsBridge&)            = delete;
    PluginJsBridge& operator=(const PluginJsBridge&) = delete;

    Project* project() const { return project_; }

    // Hello-world RPC service — kept for now as scaffolding; remove at Step 3.
    class HelloService {
    public:
        std::string greet(std::string name);
        void greetSlow(std::string name, rpcpp::Resolver<std::string> resolver);
    };

private:
    // JS bindings attached under globalThis[Symbol.for("plugin")].
    static JSValue js_rpcSend(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_rpcPoll(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_getFrame(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    LvglJsEngine& engine;
    Project*      project_ = nullptr;

    HelloService                                          hello;
    rpcpp::QueueTransport<rpcpp::JsonCodec>               transport;
    rpcpp::TypedRpcServer<HelloService, rpcpp::JsonCodec> server;
    JSValue pluginNamespace = JS_UNDEFINED;
};
