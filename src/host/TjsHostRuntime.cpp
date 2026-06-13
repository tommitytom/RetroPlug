#include "TjsHostRuntime.hpp"

#include <cstring>

TjsHostRuntime::~TjsHostRuntime() {
    shutdown();
}

bool TjsHostRuntime::init() {
    if (ctx_)
        return true;

    // Idempotent global init: txiki's TJS_Initialize sets process-wide state
    // (signal handlers, the module loader), so it must run exactly once even
    // when several hosts are created in one process (multiple plugin windows,
    // or the UI test runner booting a second runtime).
    static bool tjsInitialized = false;
    if (!tjsInitialized) {
        static char arg0[] = "retroplug";
        static char* argv[] = { arg0, nullptr };
        TJS_Initialize(1, argv);
        tjsInitialized = true;
    }

    qrt_ = TJS_NewRuntime();
    if (!qrt_)
        return false;
    ctx_ = TJS_GetJSContext(qrt_);
    return true;
}

void TjsHostRuntime::shutdown() {
    if (!qrt_)
        return;
    // Free the runtime (and every JS function, incl. the __rpcSend trampolines)
    // before the captured RpcSendFns it points at.
    TJS_FreeRuntime(qrt_);
    qrt_ = nullptr;
    ctx_ = nullptr;
    rpcBindings_.clear();
}

void TjsHostRuntime::pump() {
    if (!qrt_)
        return;
    uv_run(TJS_GetLoop(qrt_), UV_RUN_NOWAIT);
    tjs__execute_jobs(ctx_);
}

int TjsHostRuntime::evalModule(const char* filename) {
    if (!ctx_)
        return -1;
    JSValue result = TJS_EvalModule(ctx_, filename, true);
    if (JS_IsException(result)) {
        tjs_dump_error(ctx_);
        JS_FreeValue(ctx_, result);
        return -1;
    }
    JS_FreeValue(ctx_, result);
    return 0;
}

int TjsHostRuntime::evalModuleBuffer(const char* code, std::size_t len, const char* name) {
    if (!ctx_)
        return -1;
    // Same path as TJS_EvalModule, but fed from a buffer rather than a file.
    // Sets up import.meta.url and fires the synthetic 'load' event.
    JSValue result = TJS_EvalModuleContent(ctx_, name, true, false, code, len);
    if (JS_IsException(result)) {
        tjs_dump_error(ctx_);
        JS_FreeValue(ctx_, result);
        return -1;
    }
    JS_FreeValue(ctx_, result);
    return 0;
}

int TjsHostRuntime::evalModuleBytecode(const std::uint8_t* bytecode, std::size_t len) {
    if (!ctx_)
        return -1;
    // tjs__eval_bytecode does JS_ReadObject(JS_READ_OBJ_BYTECODE) ->
    // JS_ResolveModule -> js_module_set_import_meta(false, false) ->
    // JS_EvalFunction. It does NOT fire the 'load' event that
    // TJS_EvalModuleContent fires when is_main=true, so do it here for parity
    // with evalModuleBuffer.
    if (tjs__eval_bytecode(ctx_, bytecode, len, true) != 0)
        return -1;

    static const char kLoadEvent[] = "window.dispatchEvent(new Event('load'));";
    JSValue result = JS_Eval(ctx_, kLoadEvent, sizeof(kLoadEvent) - 1,
                             "<global>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        tjs_dump_error(ctx_);
        JS_FreeValue(ctx_, result);
        return -1;
    }
    JS_FreeValue(ctx_, result);
    return 0;
}

int TjsHostRuntime::evalString(const char* code) {
    if (!ctx_)
        return -1;
    JSValue result = JS_Eval(ctx_, code, std::strlen(code), "<eval>",
                             JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_STRICT);
    if (JS_IsException(result)) {
        tjs_dump_error(ctx_);
        JS_FreeValue(ctx_, result);
        return -1;
    }
    JS_FreeValue(ctx_, result);
    return 0;
}

// __rpcSend(bytes) -> ArrayBuffer | null. Accepts a Uint8Array view or a raw
// ArrayBuffer; forwards the bytes to the captured RpcSendFn and wraps the reply.
// The RpcSendFn* is carried in funcData[0] (an ArrayBuffer holding the pointer),
// so the trampoline needs no global to find its dispatch target.
JSValue TjsHostRuntime::rpcSendThunk(JSContext* ctx, JSValueConst, int argc,
                                     JSValueConst* argv, int, JSValue* funcData) {
    std::size_t holderLen = 0;
    std::uint8_t* holder = JS_GetArrayBuffer(ctx, &holderLen, funcData[0]);
    if (!holder || holderLen != sizeof(RpcSendFn*))
        return JS_ThrowInternalError(ctx, "__rpcSend: missing dispatch binding");
    RpcSendFn* fn = nullptr;
    std::memcpy(&fn, holder, sizeof(fn));
    if (!fn || !*fn)
        return JS_ThrowInternalError(ctx, "__rpcSend: dispatch unavailable");
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "__rpcSend: expected (bytes)");

    std::size_t byteOffset = 0, byteLength = 0, arrayLen = 0;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[0], &byteOffset, &byteLength, nullptr);
    std::uint8_t* data = nullptr;
    if (!JS_IsException(ab)) {
        data = JS_GetArrayBuffer(ctx, &arrayLen, ab);
    } else {
        JS_FreeValue(ctx, ab);
        data = JS_GetArrayBuffer(ctx, &arrayLen, argv[0]);
        byteOffset = 0; byteLength = arrayLen;
        ab = JS_DupValue(ctx, argv[0]);
    }
    if (!data) { JS_FreeValue(ctx, ab); return JS_ThrowTypeError(ctx, "__rpcSend: not bytes"); }

    std::string_view bytes(reinterpret_cast<const char*>(data + byteOffset), byteLength);
    std::optional<std::vector<char>> reply = (*fn)(bytes);
    JS_FreeValue(ctx, ab);

    if (!reply) return JS_NULL;
    return JS_NewArrayBufferCopy(ctx,
        reinterpret_cast<const std::uint8_t*>(reply->data()), reply->size());
}

void TjsHostRuntime::bindRpcSend(JSValue ns, RpcSendFn fn) {
    if (!ctx_)
        return;
    auto held = std::make_unique<RpcSendFn>(std::move(fn));
    RpcSendFn* raw = held.get();
    rpcBindings_.push_back(std::move(held));

    // Carry the RpcSendFn* through the function's data as an ArrayBuffer holding
    // the pointer bytes (QuickJS func-data are JSValues, not raw pointers).
    JSValue holder = JS_NewArrayBufferCopy(ctx_,
        reinterpret_cast<const std::uint8_t*>(&raw), sizeof(raw));
    // QuickJS-ng's JS_NewCFunctionData(ctx, func, length, magic, data_len, data)
    // has no name arg; the JS-visible name comes from JS_SetPropertyStr below.
    JSValue f = JS_NewCFunctionData(ctx_, rpcSendThunk, 1, 0, 1, &holder);
    JS_FreeValue(ctx_, holder);
    JS_SetPropertyStr(ctx_, ns, "__rpcSend", f);
}
