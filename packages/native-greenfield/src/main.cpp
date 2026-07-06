// native-greenfield-host — a minimal txiki/QuickJS host that runs a greenfield TS test
// bundle over a real `Backend`. It binds `globalThis[Symbol.for("plugin")].__rpcSend` to a
// BackendRpcService (fs/config/codec) — the same namespace the future plugin host uses, so
// one TS adapter (realBackend.ts) serves both. Emulator-free: no Project/SystemBase.
//
// The greenfield harness (packages/retroplug-greenfield/testing/harness.ts) self-reports
// TAP via console.log (txiki's console -> stdout, left untouched) and sets the process exit
// code via globalThis.tjs.exit(code). We provide that exit hook (recording the code rather
// than terminating) and drive the job loop until it fires.
//
//   native-greenfield-host [--test] <bundle.js>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "dpfjs/host/TjsHostRuntime.hpp"  // shared txiki/QuickJS host (+ tjs.h/quickjs.h)

#include "BackendFacade.hpp"
#include "BackendRpcRegistration.hpp"
#include "TypedRpcServer.h"
#include "codecs/QuickJSCodec.h"
#include "transports/QuickJSTransport.h"

using BackendRpcServer = rpcpp::TypedRpcServer<BackendFacade, rpcpp::QuickJSCodec>;

namespace {

// The exit code the harness reports through globalThis.tjs.exit(). One host per process,
// single-threaded, so a TU-local is the right mechanism (mirrors TestHarness.cpp's
// g_activeImpl pattern).
struct ExitState { int code = 0; bool set = false; };
ExitState g_exit;

JSValue jsExit(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int code = 0;
    if (argc >= 1) JS_ToInt32(ctx, &code, argv[0]);
    g_exit.code = code;
    g_exit.set = true;
    return JS_UNDEFINED;
}

std::string slurp(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

int main(int argc, char** argv) try {
    std::string bundlePath;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--test") == 0) continue;
        bundlePath = argv[i];
    }
    if (bundlePath.empty()) {
        std::fprintf(stderr, "usage: native-greenfield-host [--test] <bundle.js>\n");
        return 2;
    }

    TjsHostRuntime host;
    if (!host.init()) {
        std::fprintf(stderr, "TjsHostRuntime init failed\n");
        return 1;
    }
    JSContext* ctx = host.context();

    // rpcpp server over the QuickJS object codec (marshals request/response as live JS
    // objects against ctx — nothing serialized). The transport's async sink is unused.
    BackendFacade service;
    rpcpp::QuickJSTransport transport(ctx, [](JSContext*, JSValue) {});
    BackendRpcServer server(service, transport, rpcpp::QuickJSCodec{ctx});
    registerBackendRpcMethods(server);

    JSValue global = JS_GetGlobalObject(ctx);

    // globalThis[Symbol.for("plugin")] = { __rpcSend }
    {
        JSValue sym  = JS_NewSymbol(ctx, "plugin", /*is_global*/ 1);
        JSAtom atom  = JS_ValueToAtom(ctx, sym);
        JSValue ns   = JS_NewObjectProto(ctx, JS_NULL);
        host.bindRpcSend(ns, [&server](JSContext* sctx, JSValueConst req) -> JSValue {
            auto out = server.processMessage(req);
            if (!out) return JS_NULL;        // notification / no reply
            return out->materialize(sctx);   // owned; handed back to JS
        });
        JS_DefinePropertyValue(ctx, global, atom, ns, JS_PROP_C_W_E);
        JS_FreeAtom(ctx, atom);
        JS_FreeValue(ctx, sym);
    }

    // globalThis.tjs.exit — the greenfield harness sets the exit code through it. Override
    // any txiki-provided exit so we record the code and return it (rather than terminating
    // mid-pump and losing it).
    {
        JSValue tjsObj = JS_GetPropertyStr(ctx, global, "tjs");
        if (JS_IsUndefined(tjsObj) || JS_IsNull(tjsObj)) {
            JS_FreeValue(ctx, tjsObj);
            tjsObj = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, global, "tjs", JS_DupValue(ctx, tjsObj));
        }
        JS_SetPropertyStr(ctx, tjsObj, "exit", JS_NewCFunction(ctx, jsExit, "exit", 1));
        JS_FreeValue(ctx, tjsObj);
    }

    JS_FreeValue(ctx, global);

    const std::string code = slurp(bundlePath);
    const int rc = host.evalModuleBuffer(code.data(), code.size(), bundlePath.c_str());
    if (rc != 0) {
        std::fprintf(stderr, "module eval failed\n");
        return 1;
    }

    // Drive the job loop until the harness's microtask + tests run and call tjs.exit
    // (ES modules evaluate async in QuickJS, so the module body itself runs here). Bounded.
    for (int i = 0; i < 20000 && !g_exit.set; ++i) host.pump();

    if (!g_exit.set) {
        std::fprintf(stderr, "tests did not complete (tjs.exit never called)\n");
        return 1;
    }
    return g_exit.code;
} catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
}
