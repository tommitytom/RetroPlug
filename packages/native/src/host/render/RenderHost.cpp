#include "host/render/RenderHost.hpp"

#include "quickjs.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/rpc/BackendRpcRegistration.hpp"
#include "system/CoreBackends.hpp"
#include "system/SystemFactory.hpp"
#include "TypedRpcServer.h"
#include "codecs/QuickJSCodec.h"
#include "transports/QuickJSTransport.h"

// The render-worker bundle: worker.ts esbuilt to a global-code IIFE, tjsc'd to bytecode + embedded as a C
// array (packages/native/CMakeLists.txt → retroplug-render-worker-bundle). Loaded via JS_ReadObject +
// JS_EvalFunction (not a module — bare QuickJS has no module loader).
extern "C" {
extern const std::uint8_t  rp_render_worker_bundle[];
extern const std::uint32_t rp_render_worker_bundle_size;
}

namespace retroplug {

namespace {

using BackendRpcServer = rpcpp::TypedRpcServer<rpcpp::Empty, rpcpp::QuickJSCodec>;

// The __rpcSend dispatch closure, carried into JS func-data as a pointer (mirrors TjsHostRuntime).
using RpcSendFn = std::function<JSValue(JSContext*, JSValueConst)>;

RenderHost* hostOf(JSContext* ctx) {
    return static_cast<RenderHost*>(JS_GetContextOpaque(ctx));
}

std::string toStr(JSContext* ctx, JSValueConst v) {
    const char* s = JS_ToCString(ctx, v);
    std::string out = s ? s : "";
    if (s) JS_FreeCString(ctx, s);
    return out;
}

void dumpError(JSContext* ctx) {
    JSValue exc = JS_GetException(ctx);
    std::fprintf(stderr, "render-host: %s\n", toStr(ctx, exc).c_str());
    JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
    if (!JS_IsUndefined(stack) && !JS_IsNull(stack))
        std::fprintf(stderr, "%s\n", toStr(ctx, stack).c_str());
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exc);
}

// __rpcSend(request) -> response | null. The RpcSendFn* rides in funcData[0] (an ArrayBuffer of the pointer
// bytes), so the trampoline needs no global to find its dispatch target (mirrors TjsHostRuntime).
JSValue rpcSendThunk(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int, JSValue* funcData) {
    std::size_t len = 0;
    std::uint8_t* holder = JS_GetArrayBuffer(ctx, &len, funcData[0]);
    if (!holder || len != sizeof(RpcSendFn*))
        return JS_ThrowInternalError(ctx, "__rpcSend: missing dispatch binding");
    RpcSendFn* fn = nullptr;
    std::memcpy(&fn, holder, sizeof(fn));
    if (!fn || !*fn)
        return JS_ThrowInternalError(ctx, "__rpcSend: dispatch unavailable");
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "__rpcSend: expected (request)");
    return (*fn)(ctx, argv[0]);
}

JSValue progressThunk(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    double f = 0;
    if (argc >= 1) JS_ToFloat64(ctx, &f, argv[0]);
    if (RenderHost* h = hostOf(ctx)) h->onProgress(f);
    return JS_UNDEFINED;
}

JSValue cancelThunk(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    RenderHost* h = hostOf(ctx);
    return JS_NewBool(ctx, h && h->onCancelQuery());
}

std::vector<std::string> readStringArray(JSContext* ctx, JSValueConst arr) {
    std::vector<std::string> out;
    JSValue lenV = JS_GetPropertyStr(ctx, arr, "length"); // undefined on a non-array -> 0
    std::uint32_t len = 0;
    JS_ToUint32(ctx, &len, lenV);
    JS_FreeValue(ctx, lenV);
    for (std::uint32_t i = 0; i < len; ++i) {
        JSValue v = JS_GetPropertyUint32(ctx, arr, i);
        out.push_back(toStr(ctx, v));
        JS_FreeValue(ctx, v);
    }
    return out;
}

JSValue resultThunk(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    RenderHost* h = hostOf(ctx);
    if (!h) return JS_UNDEFINED;
    std::string status = argc >= 1 ? toStr(ctx, argv[0]) : std::string();
    std::string message = argc >= 2 ? toStr(ctx, argv[1]) : std::string();
    std::vector<std::string> outputs = argc >= 3 ? readStringArray(ctx, argv[2]) : std::vector<std::string>();
    h->onResult(std::move(status), std::move(message), std::move(outputs));
    return JS_UNDEFINED;
}

JSValue stdoutThunk(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (RenderHost* h = hostOf(ctx); h && argc >= 1) h->onStdout(toStr(ctx, argv[0]).c_str());
    return JS_UNDEFINED;
}

JSValue stderrThunk(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (RenderHost* h = hostOf(ctx); h && argc >= 1) h->onStderr(toStr(ctx, argv[0]).c_str());
    return JS_UNDEFINED;
}

void bindRpcSend(JSContext* ctx, JSValue ns, RpcSendFn* dispatch) {
    JSValue holder = JS_NewArrayBufferCopy(ctx,
        reinterpret_cast<const std::uint8_t*>(&dispatch), sizeof(dispatch));
    JSValue f = JS_NewCFunctionData(ctx, rpcSendThunk, 1, 0, 1, &holder);
    JS_FreeValue(ctx, holder);
    JS_SetPropertyStr(ctx, ns, "__rpcSend", f);
}

// Bare QuickJS gives us the ES standard library (JSON, TypedArrays, DataView, Array, Math) but no console
// and no TextEncoder/TextDecoder — the two Web globals the control plane touches (console throughout;
// TextEncoder/TextDecoder constructed at module load by projectStore/recentStore). Shim both; console routes
// to the bound stdout/stderr thunks, TextEncoder/TextDecoder are minimal UTF-8 (guarded so a runtime that
// already provides them wins).
const char kBootstrap[] = R"JS(
(function () {
  var out = function () { __rp_writeStdout(Array.prototype.join.call(arguments, ' ') + '\n'); };
  var err = function () { __rp_writeStderr(Array.prototype.join.call(arguments, ' ') + '\n'); };
  globalThis.console = { log: out, info: out, debug: out, warn: err, error: err, trace: err };
  if (typeof globalThis.TextEncoder === 'undefined') {
    globalThis.TextEncoder = class TextEncoder {
      get encoding() { return 'utf-8'; }
      encode(str) {
        str = String(str === undefined ? '' : str);
        var out = [];
        for (var i = 0; i < str.length; i++) {
          var c = str.charCodeAt(i);
          if (c < 0x80) out.push(c);
          else if (c < 0x800) out.push(0xc0 | (c >> 6), 0x80 | (c & 0x3f));
          else if (c >= 0xd800 && c <= 0xdbff) {
            var c2 = str.charCodeAt(i + 1);
            if (c2 >= 0xdc00 && c2 <= 0xdfff) {
              c = 0x10000 + ((c & 0x3ff) << 10) + (c2 & 0x3ff); i++;
              out.push(0xf0 | (c >> 18), 0x80 | ((c >> 12) & 0x3f), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f));
            } else out.push(0xef, 0xbf, 0xbd);
          } else if (c >= 0xdc00 && c <= 0xdfff) out.push(0xef, 0xbf, 0xbd);
          else out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f));
        }
        return new Uint8Array(out);
      }
    };
  }
  if (typeof globalThis.TextDecoder === 'undefined') {
    globalThis.TextDecoder = class TextDecoder {
      constructor(label) { this._label = label || 'utf-8'; }
      get encoding() { return 'utf-8'; }
      decode(buf) {
        if (buf === undefined) return '';
        var b = buf instanceof Uint8Array ? buf : new Uint8Array(buf.buffer || buf);
        var out = '';
        for (var i = 0; i < b.length;) {
          var c = b[i++];
          if (c < 0x80) out += String.fromCharCode(c);
          else if (c < 0xe0) out += String.fromCharCode(((c & 0x1f) << 6) | (b[i++] & 0x3f));
          else if (c < 0xf0) out += String.fromCharCode(((c & 0x0f) << 12) | ((b[i++] & 0x3f) << 6) | (b[i++] & 0x3f));
          else {
            var cp = ((c & 0x07) << 18) | ((b[i++] & 0x3f) << 12) | ((b[i++] & 0x3f) << 6) | (b[i++] & 0x3f);
            cp -= 0x10000;
            out += String.fromCharCode(0xd800 + (cp >> 10), 0xdc00 + (cp & 0x3ff));
          }
        }
        return out;
      }
    };
  }
})();
)JS";

} // namespace

RenderHost::RenderHost() {
    rt_ = JS_NewRuntime();
    ctx_ = JS_NewContext(rt_);
    JS_SetContextOpaque(ctx_, this); // the opaque-routed thunks recover the host from ctx

    JSValue global = JS_GetGlobalObject(ctx_);
    JS_SetPropertyStr(ctx_, global, "__rp_writeStdout", JS_NewCFunction(ctx_, stdoutThunk, "__rp_writeStdout", 1));
    JS_SetPropertyStr(ctx_, global, "__rp_writeStderr", JS_NewCFunction(ctx_, stderrThunk, "__rp_writeStderr", 1));
    JS_SetPropertyStr(ctx_, global, "__rp_reportRenderProgress", JS_NewCFunction(ctx_, progressThunk, "__rp_reportRenderProgress", 1));
    JS_SetPropertyStr(ctx_, global, "__rp_isRenderCancelled", JS_NewCFunction(ctx_, cancelThunk, "__rp_isRenderCancelled", 0));
    JS_SetPropertyStr(ctx_, global, "__rp_renderResult", JS_NewCFunction(ctx_, resultThunk, "__rp_renderResult", 3));
    JS_FreeValue(ctx_, global);

    JSValue r = JS_Eval(ctx_, kBootstrap, std::strlen(kBootstrap), "<render-host-bootstrap>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) dumpError(ctx_);
    JS_FreeValue(ctx_, r);
}

RenderHost::~RenderHost() {
    if (rt_) {
        JS_FreeContext(ctx_);
        JS_FreeRuntime(rt_);
    }
}

RenderHost::Result RenderHost::run(const std::string& jobJson, ProgressFn onProgress, CancelFn isCancelled) {
    progressFn_ = std::move(onProgress);
    cancelFn_ = std::move(isCancelled);
    result_ = Result{};

    // The Engine service graph — one per host (mirrors cli/main.cpp), minus TjsHostRuntime. No audio thread
    // is started, so the QueuedInvoker flushes inline on this thread. registerAllBackendRpc mounts the full
    // surface the worker's control plane calls (host fs, emulator, dsp-kernel, harness render, debug readCpu).
    Engine engine;
    SystemFactory factory;
    registerCoreBackends(factory);
    QueuedInvoker invoker{engine, engine.registry()};
    HostRpcService hostSvc;
    EngineRpcService engineSvc{engine, factory, invoker};
    DebugRpcService debugSvc{engine};
    AudioDriverRpcService driver{engine, invoker};

    rpcpp::QuickJSTransport transport(ctx_, [](JSContext*, JSValue) {});
    BackendRpcServer server(transport, rpcpp::QuickJSCodec{ctx_});
    registerAllBackendRpc(server, hostSvc, engineSvc, debugSvc, driver);

    RpcSendFn dispatch = [&server](JSContext* sctx, JSValueConst req) -> JSValue {
        auto out = server.processMessage(req);
        if (!out) return JS_NULL;      // notification / no reply
        return out->materialize(sctx); // owned; handed back to JS
    };

    // QuickJS's stack-overflow guard is calibrated to the runtime's creating thread. RenderHost is created
    // and driven on the same (worker) thread, but re-anchor before entering JS to be safe (see DspRuntime).
    JS_UpdateStackTop(rt_);

    JSValue global = JS_GetGlobalObject(ctx_);
    // globalThis[Symbol.for("plugin")] = { __rpcSend, args: [jobJson] } — the worker reads args[0].
    {
        JSValue sym = JS_NewSymbol(ctx_, "plugin", /*is_global*/ 1);
        JSAtom atom = JS_ValueToAtom(ctx_, sym);
        JSValue ns = JS_NewObjectProto(ctx_, JS_NULL);
        bindRpcSend(ctx_, ns, &dispatch);
        JSValue args = JS_NewArray(ctx_);
        JS_SetPropertyUint32(ctx_, args, 0, JS_NewStringLen(ctx_, jobJson.data(), jobJson.size()));
        JS_SetPropertyStr(ctx_, ns, "args", args);
        JS_DefinePropertyValue(ctx_, global, atom, ns, JS_PROP_C_W_E);
        JS_FreeAtom(ctx_, atom);
        JS_FreeValue(ctx_, sym);
    }
    JS_FreeValue(ctx_, global);

    // Run the worker bundle as global-code bytecode. Its top-level main() reads the spec, renders, and
    // reports back through the result thunk — all synchronously on this thread.
    JSValue obj = JS_ReadObject(ctx_, rp_render_worker_bundle, rp_render_worker_bundle_size, JS_READ_OBJ_BYTECODE);
    if (JS_IsException(obj)) {
        dumpError(ctx_);
        JS_FreeValue(ctx_, obj);
        result_.status = "error";
        result_.message = "render worker bytecode read failed";
        return result_;
    }
    JSValue res = JS_EvalFunction(ctx_, obj);
    if (JS_IsException(res)) {
        dumpError(ctx_);
        if (result_.status.empty()) {
            result_.status = "error";
            result_.message = "render worker eval failed";
        }
    }
    JS_FreeValue(ctx_, res);

    // Drain any pending promise jobs. The render path is synchronous, but keep the runtime quiescent.
    JSContext* jc = nullptr;
    for (;;) {
        int r = JS_ExecutePendingJob(rt_, &jc);
        if (r == 0) break;
        if (r < 0) { dumpError(jc ? jc : ctx_); break; }
    }

    if (result_.status.empty()) {
        result_.status = "error";
        result_.message = "render worker did not report a result";
    }
    return result_;
}

void RenderHost::onProgress(double fraction) {
    if (progressFn_) progressFn_(fraction);
}

bool RenderHost::onCancelQuery() {
    return cancelFn_ ? cancelFn_() : false;
}

void RenderHost::onResult(std::string status, std::string message, std::vector<std::string> outputs) {
    result_.status = std::move(status);
    result_.message = std::move(message);
    result_.outputs = std::move(outputs);
}

void RenderHost::onStdout(const char* text) {
    std::fputs(text, stdout);
}

void RenderHost::onStderr(const char* text) {
    std::fputs(text, stderr);
}

} // namespace retroplug
