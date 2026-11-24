#include "host/render/RenderHost.hpp"

#include "quickjs.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/rpc/BackendRpcRegistration.hpp"
#include "TypedRpcServer.h"
#include "codecs/QuickJSCodec.h"
#include "transports/QuickJSTransport.h"
#include "host/QuickJsGlobals.hpp"
#include "host/js/SharedModules.hpp"

// The render worker's shared module set (packages/native/cmake/product-bundles.cmake, retroplug-render-modules): the
// shell contracts as a table of named modules, registered before the entry, and the entry, src/render/worker.ts, as
// module bytecode (rp:render) importing them by name. Bare QuickJS has no module loader of txiki's: a registered module
// is found by its name, and a pack's half through the bare pack loader (host/js/PackModules).
extern "C" {
extern const rp_js_module  rp_render_modules[];
extern const std::uint32_t rp_render_modules_count;
extern const std::uint8_t  rp_render_bundle[];
extern const std::uint32_t rp_render_bundle_size;
}

namespace retroplug {

namespace {

using BackendRpcServer = rpcpp::TypedRpcServer<rpcpp::Empty, rpcpp::QuickJSCodec>;

// Counts the runs in the process, every RenderHost's: each run's is its renderSerial (one per engine job, as each job
// builds a host of its own and runs it once).
std::atomic<std::uint64_t> g_renderSerials{0};

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

// A script exception's report goes where the worker's console.error would: the host's console sink when it has one.
void dumpError(JSContext* ctx) {
    JSValue exc = JS_GetException(ctx);
    std::string report = "render-host: " + toStr(ctx, exc) + "\n";
    JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
    if (!JS_IsUndefined(stack) && !JS_IsNull(stack)) report += toStr(ctx, stack) + "\n";
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exc);
    if (RenderHost* h = hostOf(ctx)) h->onStderr(report.c_str());
    else std::fputs(report.c_str(), stderr);
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

JSValue renderedThunk(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    double ms = 0;
    if (argc >= 1) JS_ToFloat64(ctx, &ms, argv[0]);
    if (RenderHost* h = hostOf(ctx)) h->onRendered(ms);
    return JS_UNDEFINED;
}

// The runtime's interrupt handler, which QuickJS polls while JS runs: true stops the script.
int interruptThunk(JSRuntime*, void* opaque) {
    return static_cast<RenderHost*>(opaque)->onInterruptPoll() ? 1 : 0;
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

// TODO: Should be in TS and compiled in rather than inline
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
    JS_SetMaxStackSize(rt_, kStackBytes);
    JS_SetInterruptHandler(rt_, interruptThunk, this);
    ctx_ = JS_NewContext(rt_);
    JS_SetContextOpaque(ctx_, this); // the opaque-routed thunks recover the host from ctx

    JSValue global = JS_GetGlobalObject(ctx_);
    JS_SetPropertyStr(ctx_, global, "__rp_writeStdout", JS_NewCFunction(ctx_, stdoutThunk, "__rp_writeStdout", 1));
    JS_SetPropertyStr(ctx_, global, "__rp_writeStderr", JS_NewCFunction(ctx_, stderrThunk, "__rp_writeStderr", 1));
    JS_SetPropertyStr(ctx_, global, "__rp_reportRenderedMs", JS_NewCFunction(ctx_, renderedThunk, "__rp_reportRenderedMs", 1));
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

RenderHost::Result RenderHost::run(const std::string& jobJson, RenderedFn onRendered, CancelFn isCancelled,
                                   ConsoleFn onConsole) {
    return runWith(nullptr, jobJson, std::move(onRendered), std::move(isCancelled), std::move(onConsole));
}

RenderHost::Result RenderHost::runScript(const std::string& source, const std::string& jobJson, RenderedFn onRendered,
                                         CancelFn isCancelled, ConsoleFn onConsole) {
    return runWith(&source, jobJson, std::move(onRendered), std::move(isCancelled), std::move(onConsole));
}

RenderHost::Result RenderHost::runWith(const std::string* source, const std::string& jobJson, RenderedFn onRendered,
                                       CancelFn isCancelled, ConsoleFn onConsole) {
    renderedFn_ = std::move(onRendered);
    cancelFn_ = std::move(isCancelled);
    consoleFn_ = std::move(onConsole);
    result_ = Result{};
    cancelSince_ = {};
    interrupted_ = false;

    // The Engine service graph — one per host (mirrors cli/main.cpp), minus TjsHostRuntime. No audio thread
    // is started, so the QueuedInvoker flushes inline on this thread. registerAllBackendRpc mounts the full
    // surface the worker's control plane calls (host fs, emulator, harness render, debug readCpu).
    Engine engine;
    QueuedInvoker invoker{engine};
    HostRpcService hostSvc;
    EngineRpcService engineSvc{engine, invoker};
    engineSvc.setContinuousBlocks(true);
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
    // and driven on the same (worker) thread, but re-anchor before entering JS to be safe (as ScriptHost does): the
    // kStackBytes limit counts down from here.
    JS_UpdateStackTop(rt_);

    // globalThis[Symbol.for("plugin")] = { __rpcSend, args: [jobJson], renderSerial }: the worker reads args[0], and
    // folds renderSerial, a number no other run in the process has, into the names of the files it streams to.
    // bindRpcSend here is the bare-runtime one (this host has no TjsHostRuntime), which is why
    // installPluginNamespace takes a callback rather than a runtime.
    const std::uint64_t serial = g_renderSerials.fetch_add(1, std::memory_order_relaxed) + 1;
    installPluginNamespace(ctx_, [&](JSValue ns) {
        bindRpcSend(ctx_, ns, &dispatch);
        JSValue args = JS_NewArray(ctx_);
        JS_SetPropertyUint32(ctx_, args, 0, JS_NewStringLen(ctx_, jobJson.data(), jobJson.size()));
        JS_SetPropertyStr(ctx_, ns, "args", args);
        JS_SetPropertyStr(ctx_, ns, "renderSerial", JS_NewInt64(ctx_, static_cast<std::int64_t>(serial)));
    });

    // Run the worker's module (or a test's global script). The worker's top-level main() reads the spec, renders, and
    // reports back through the result thunk, synchronously on this thread.
    JSValue res = JS_UNDEFINED;
    JSValue entry = JS_UNDEFINED;
    // The interrupt's InternalError is reported here, as the cancel it is, rather than as a script error (dumpError).
    const auto failed = [this](JSValue v) {
        if (!JS_IsException(v)) return false;
        if (!interrupted_) return true;
        JS_FreeValue(ctx_, JS_GetException(ctx_));
        return false;
    };
    if (source) {
        res = JS_Eval(ctx_, source->c_str(), source->size(), "<render-host-script>", JS_EVAL_TYPE_GLOBAL);
    } else {
        // The set's contract modules first, each unevaluated, dependencies first: the entry's imports resolve to them.
        const std::string why = rp::host::registerSharedModules(ctx_, rp_render_modules, rp_render_modules_count);
        if (!why.empty()) {
            result_.status = "error";
            result_.message = "render worker: " + why;
            return result_;
        }
        JSValue mod = JS_ReadObject(ctx_, rp_render_bundle, rp_render_bundle_size, JS_READ_OBJ_BYTECODE);
        if (JS_IsException(mod)) {
            dumpError(ctx_);
            result_.status = "error";
            result_.message = "render worker bytecode read failed";
            return result_;
        }
        if (JS_ResolveModule(ctx_, mod) < 0) {
            dumpError(ctx_);
            JS_FreeValue(ctx_, mod);
            result_.status = "error";
            result_.message = "render worker's imports did not resolve";
            return result_;
        }
        // A module's evaluation answers a promise, which the job loop below settles: a throw in the module's body
        // rejects it rather than returning an exception here.
        res = JS_EvalFunction(ctx_, mod);
        if (!JS_IsException(res)) entry = JS_DupValue(ctx_, res);
    }
    if (failed(res)) {
        dumpError(ctx_);
        if (result_.status.empty()) {
            result_.status = "error";
            result_.message = "render worker eval failed";
        }
    }
    JS_FreeValue(ctx_, res);

    // Drain any pending promise jobs. The render path is synchronous, but keep the runtime quiescent. An interrupted
    // script's are left to the runtime's teardown, which frees them: the handler would stop each one again.
    JSContext* jc = nullptr;
    while (!interrupted_) {
        const int r = JS_ExecutePendingJob(rt_, &jc);
        if (r == 0) break;
        if (r < 0) {
            if (interrupted_) JS_FreeValue(jc ? jc : ctx_, JS_GetException(jc ? jc : ctx_));
            else dumpError(jc ? jc : ctx_);
            break;
        }
    }

    // A throw in the entry module's own body (the worker's failure: main catches the render's) rejects its evaluation.
    if (!interrupted_ && JS_IsObject(entry) && JS_PromiseState(ctx_, entry) == JS_PROMISE_REJECTED) {
        JS_Throw(ctx_, JS_PromiseResult(ctx_, entry));
        dumpError(ctx_);
        if (result_.status.empty()) {
            result_.status = "error";
            result_.message = "render worker eval failed";
        }
    }
    JS_FreeValue(ctx_, entry);

    if (interrupted_) {
        result_.status = "cancelled";
        result_.message.clear();
    }

    if (result_.status.empty()) {
        result_.status = "error";
        result_.message = "render worker did not report a result (it may be waiting on something that never settled)";
    }
    return result_;
}

void RenderHost::onRendered(double ms) {
    if (renderedFn_) renderedFn_(ms);
}

bool RenderHost::onCancelQuery() {
    return cancelFn_ ? cancelFn_() : false;
}

bool RenderHost::onInterruptPoll() {
    if (interrupted_) return true;
    if (!cancelFn_ || !cancelFn_()) return false;
    const auto now = std::chrono::steady_clock::now();
    if (cancelSince_ == std::chrono::steady_clock::time_point{}) cancelSince_ = now;
    if (now - cancelSince_ < kCancelGrace) return false;
    interrupted_ = true;
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(kCancelGrace).count();
    onStderr(("render-host: the render did not see its cancel within " + std::to_string(seconds) +
              " s and was interrupted; a .part file it was writing may be left\n").c_str());
    return true;
}

void RenderHost::onResult(std::string status, std::string message, std::vector<std::string> outputs) {
    result_.status = std::move(status);
    result_.message = std::move(message);
    result_.outputs = std::move(outputs);
}

void RenderHost::onStdout(const char* text) {
    if (consoleFn_) consoleFn_(false, text);
    else std::fputs(text, stdout);
}

void RenderHost::onStderr(const char* text) {
    if (consoleFn_) consoleFn_(true, text);
    else std::fputs(text, stderr);
}

} // namespace retroplug
