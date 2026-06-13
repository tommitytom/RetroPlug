#pragma once

extern "C" {
    #include "tjs.h"      // TJS_Initialize / TJS_NewRuntime / TJS_GetJSContext /
                          // TJS_FreeRuntime / TJS_GetLoop (+ <quickjs.h>)
    #include "private.h"  // TJS_EvalModule(Content) / tjs__execute_jobs /
                          // tjs__eval_bytecode / tjs_dump_error (+ <uv.h>)
}

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

// A reusable embedded txiki.js / QuickJS host: owns one TJSRuntime + JSContext,
// loads/evals a TS bundle, pumps the libuv job loop, and binds a generic
// in-process `__rpcSend` trampoline. Both the plugin (LvglJsEngine) and the cli
// test harness (TestHarness) build on it; each layers its own JS namespace
// extras (the plugin's lvgljs parameter/event machinery, the harness's TAP
// runner) on top, and drives pump() from its own loop.
//
// Deliberately free of rpcpp: bindRpcSend takes an opaque dispatch callable, so
// the host needs only txiki + the standard library.
class TjsHostRuntime {
public:
    // The sync dispatch the __rpcSend trampoline forwards to: msgpack request
    // bytes -> optional msgpack reply (nullopt = no reply / a notification).
    // The reply matches rpcpp's TypedRpcServer::processMessage output (the
    // codec's byte buffer). The request is a string_view (C++17, not C++20
    // std::span) so this header stays includable by the C++17 plugin engine.
    using RpcSendFn = std::function<std::optional<std::vector<char>>(std::string_view)>;

    TjsHostRuntime() = default;
    ~TjsHostRuntime();

    TjsHostRuntime(const TjsHostRuntime&)            = delete;
    TjsHostRuntime& operator=(const TjsHostRuntime&) = delete;

    // Create the runtime + context (idempotent process-global TJS_Initialize).
    // Returns false if the runtime can't be created.
    bool init();
    bool isInitialized() const { return ctx_ != nullptr; }
    void shutdown();

    TJSRuntime* runtime() const { return qrt_; }
    JSContext*  context() const { return ctx_; }

    // One event-loop iteration + drain of pending JS jobs. The consumer drives
    // this (the plugin from its LVGL idle tick, the cli from a bounded loop).
    void pump();

    // Module / script eval. evalModule reads a file; evalModuleBuffer feeds a
    // buffer (both fire the synthetic window 'load' event via is_main); bytecode
    // eval fires 'load' manually (tjs__eval_bytecode doesn't). Return 0 on
    // success, -1 on an exception (which is dumped to stderr).
    int evalModule(const char* filename);
    int evalModuleBuffer(const char* code, std::size_t len, const char* name);
    int evalModuleBytecode(const std::uint8_t* bytecode, std::size_t len);
    int evalString(const char* code);

    // Bind a generic `__rpcSend(bytes) -> ArrayBuffer | null` on `ns`,
    // dispatching to `fn`. The callable is captured in the JS function's data
    // (no process global), so multiple hosts can coexist in one process — e.g.
    // several plugin windows. `fn` is owned by the host and outlives the JS
    // function (shutdown frees the runtime before the bindings).
    void bindRpcSend(JSValue ns, RpcSendFn fn);

private:
    static JSValue rpcSendThunk(JSContext* ctx, JSValueConst thisVal,
                                int argc, JSValueConst* argv,
                                int magic, JSValue* funcData);

    TJSRuntime* qrt_ = nullptr;
    JSContext*  ctx_ = nullptr;
    std::vector<std::unique_ptr<RpcSendFn>> rpcBindings_;
};
