#pragma once

// A server of the products' own type for the tests that mount the N8 facet: rpcpp's TypedRpcServer over the QuickJS
// codec, in a bare QuickJS context of its own. The facet's byte-carrying methods take and answer rfl::Bytestring,
// which the QuickJS codec carries as a Uint8Array and the JSON codec cannot carry at all, so a facet a JSON server
// could mount would be one no product mounts. ask() builds a request in JS from a params expression and answers the
// reply as JSON text, a Uint8Array in it written as the array of its bytes.
//
// Shared by retroplug-n8-test (N8Provider.test.cpp) and retroplug-engine-graph-test (Providers.test.cpp).

#include <string>

#include <quickjs.h>

#include "TypedRpcServer.h"
#include "codecs/QuickJSCodec.h"
#include "transports/QuickJSTransport.h"

namespace rp::n8::test {

class QjsRpc {
public:
    using Server = rpcpp::TypedRpcServer<rpcpp::Empty, rpcpp::QuickJSCodec>;

    QjsRpc() : transport_(js_.ctx, [](JSContext*, JSValue) {}), server_(transport_, rpcpp::QuickJSCodec{js_.ctx}) {}

    Server& server() { return server_; }

    // `method` called with `params`, a JS expression for its params array ("[]", `[0, new Uint8Array([1, 2])]`): the
    // reply (result or error) as JSON, or "" for none.
    std::string ask(const std::string& method, const std::string& params = "[]") {
        const std::string source = "({jsonrpc: \"2.0\", id: 1, method: \"" + method + "\", params: " + params + "})";
        JSValue           req    = JS_Eval(js_.ctx, source.c_str(), source.size(), "<ask>", JS_EVAL_TYPE_GLOBAL);
        auto              out    = server_.processMessage(req);
        JS_FreeValue(js_.ctx, req);
        if (!out) return {};
        JSValue reply = out->materialize(js_.ctx);
        JSValue show  = JS_Eval(js_.ctx, kShow, sizeof kShow - 1, "<show>", JS_EVAL_TYPE_GLOBAL);
        JSValue text  = JS_Call(js_.ctx, show, JS_UNDEFINED, 1, &reply);
        std::string result;
        if (const char* s = JS_ToCString(js_.ctx, text)) {
            result = s;
            JS_FreeCString(js_.ctx, s);
        }
        JS_FreeValue(js_.ctx, text);
        JS_FreeValue(js_.ctx, show);
        JS_FreeValue(js_.ctx, reply);
        return result;
    }

private:
    static constexpr char kShow[] =
        "(v) => JSON.stringify(v, (k, x) => (x instanceof Uint8Array ? Array.from(x) : typeof x === 'bigint' ? "
        "Number(x) : x))";

    // The runtime and its context, first in the object so they go last, after the server and the transport.
    struct Js {
        Js() : rt(JS_NewRuntime()), ctx(JS_NewContext(rt)) {}
        ~Js() {
            JS_FreeContext(ctx);
            JS_FreeRuntime(rt);
        }
        JSRuntime* rt;
        JSContext* ctx;
    };

    Js                      js_;
    rpcpp::QuickJSTransport transport_;
    Server                  server_;
};

}  // namespace rp::n8::test
