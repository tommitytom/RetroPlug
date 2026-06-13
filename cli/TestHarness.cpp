#include "TestHarness.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
    #include "tjs.h"       // TJS_Initialize / TJS_NewRuntime / TJS_GetJSContext
                           // / TJS_FreeRuntime (+ <quickjs.h>)
    #include "private.h"   // TJS_EvalModuleContent / TJS_GetLoop /
                           // tjs__execute_jobs (+ <uv.h>)
}

#include "TestHarnessImpl.hpp"     // TestHarness::Impl (complete) + rpc aliases
#include "HarnessRpcRegistration.hpp"

// Guard the hand-mirrored TypeScript enums in test/harness/index.ts. The wire
// values are load-bearing; if a C++ renumber drifts from the TS Button/Mem
// objects, fail the build here with a pointed message rather than silently
// passing the wrong byte across the bridge.
static_assert(static_cast<int>(GameboyButton::Right)  == 0, "harness Button.Right out of sync");
static_assert(static_cast<int>(GameboyButton::Left)   == 1, "harness Button.Left out of sync");
static_assert(static_cast<int>(GameboyButton::Up)     == 2, "harness Button.Up out of sync");
static_assert(static_cast<int>(GameboyButton::Down)   == 3, "harness Button.Down out of sync");
static_assert(static_cast<int>(GameboyButton::A)      == 4, "harness Button.A out of sync");
static_assert(static_cast<int>(GameboyButton::B)      == 5, "harness Button.B out of sync");
static_assert(static_cast<int>(GameboyButton::Select) == 6, "harness Button.Select out of sync");
static_assert(static_cast<int>(GameboyButton::Start)  == 7, "harness Button.Start out of sync");
static_assert(static_cast<int>(rp::MemoryType::Ram)          == 0, "harness Mem.Ram out of sync");
static_assert(static_cast<int>(rp::MemoryType::Rom)          == 1, "harness Mem.Rom out of sync");
static_assert(static_cast<int>(rp::MemoryType::Sram)         == 2, "harness Mem.Sram out of sync");
static_assert(static_cast<int>(rp::MemoryType::Vram)         == 3, "harness Mem.Vram out of sync");
static_assert(static_cast<int>(rp::MemoryType::IORegisters)  == 4, "harness Mem.IORegisters out of sync");
static_assert(static_cast<int>(rp::MemoryType::HRam)         == 5, "harness Mem.HRam out of sync");
static_assert(static_cast<int>(rp::MemoryType::OAM)          == 6, "harness Mem.OAM out of sync");
static_assert(static_cast<int>(rp::MemoryType::NametableRam) == 7, "harness Mem.NametableRam out of sync");
static_assert(static_cast<int>(rp::MemoryType::ExtWorkRam)   == 8, "harness Mem.ExtWorkRam out of sync");

// The active harness for the current process. txiki occupies BOTH the QuickJS
// context- and runtime-opaque slots (vm.c stores its TJSRuntime* in each), so
// we cannot stash our Impl there. One runtime per process + single-threaded
// means a translation-unit pointer is the correct recovery mechanism for the
// static JS trampolines.
namespace { TestHarness::Impl* g_activeImpl = nullptr; }

// ---------------------------------------------------------------------------
// JS trampolines. Every body is wrapped so a C++ throw never crosses into
// QuickJS (which does not catch C++ exceptions).
// ---------------------------------------------------------------------------

namespace {

// __rpcSend(bytes) -> ArrayBuffer | null: the single sync entry the generated
// HarnessService client dispatches through (mirrors PluginJsBridge::js_rpcSend).
// Accepts a Uint8Array view or a raw ArrayBuffer; returns the msgpack reply.
JSValue jsHarnessRpcSend(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h || !h->rpcServer_)
        return JS_ThrowInternalError(ctx, "__rpcSend: harness unavailable");
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
    std::span<const char> bytes(reinterpret_cast<const char*>(data + byteOffset), byteLength);
    auto reply = h->rpcServer_->processMessage(bytes);
    JS_FreeValue(ctx, ab);
    if (!reply) return JS_NULL;
    return JS_NewArrayBufferCopy(ctx,
        reinterpret_cast<const std::uint8_t*>(reply->data()), reply->size());
}

JSValue jsBeginCase(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    h->beginCase();
    return JS_UNDEFINED;
}

JSValue jsReport(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    if (argc < 2) return JS_ThrowTypeError(ctx, "report(name, ok, message?)");
    const char* name = JS_ToCString(ctx, argv[0]);
    const int   ok   = JS_ToBool(ctx, argv[1]);
    const char* msg  = (argc >= 3 && !JS_IsUndefined(argv[2]))
                           ? JS_ToCString(ctx, argv[2]) : nullptr;
    h->report(name ? name : "", ok == 1, msg ? msg : "");
    if (name) JS_FreeCString(ctx, name);
    if (msg)  JS_FreeCString(ctx, msg);
    return JS_UNDEFINED;
}

JSValue jsDone(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    h->done();
    return JS_UNDEFINED;
}

// Console shim. txiki's built-in console writes to stdout, which would corrupt
// the TAP stream; route everything to stderr with the project's [js:<level>]
// convention instead.
JSValue jsLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int level = 0;
    if (argc >= 1) JS_ToInt32(ctx, &level, argv[0]);
    const char* msg = (argc >= 2) ? JS_ToCString(ctx, argv[1]) : nullptr;
    const char* tag = level >= 2 ? "error" : level == 1 ? "warn" : "log";
    std::fprintf(stderr, "[js:%s] %s\n", tag, msg ? msg : "");
    if (msg) JS_FreeCString(ctx, msg);
    return JS_UNDEFINED;
}

} // namespace

// ---------------------------------------------------------------------------
// TestHarness lifecycle.
// ---------------------------------------------------------------------------

TestHarness::TestHarness() : impl_(std::make_unique<Impl>()) {
    // Idempotent global init (mirrors src/LvglJsEngine.cpp:130-136).
    static bool tjsInitialized = false;
    if (!tjsInitialized) {
        static char arg0[] = "retroplug-cli";
        static char* argv[] = { arg0, nullptr };
        TJS_Initialize(1, argv);
        tjsInitialized = true;
    }

    impl_->qrt = TJS_NewRuntime();
    if (!impl_->qrt) throw std::runtime_error("TJS_NewRuntime() failed");
    impl_->ctx = TJS_GetJSContext(impl_->qrt);

    // Recover *this Impl inside the static C trampolines. NOT via the context/
    // runtime opaque slots — txiki owns both for its TJSRuntime*.
    g_activeImpl = impl_.get();

    // Stand up the rpcpp server stack. The generated HarnessService client
    // dispatches through __rpcSend -> processMessage.
    impl_->rpcService_   = std::make_unique<HarnessRpcService>(impl_.get());
    impl_->rpcTransport_ = std::make_unique<HarnessRpcTransport>();
    impl_->rpcServer_    = std::make_unique<HarnessRpcServer>(*impl_->rpcService_,
                                                              *impl_->rpcTransport_);
    registerHarnessRpcMethods(*impl_->rpcServer_);

    JSContext* ctx = impl_->ctx;

    // Build the Symbol.for("retroplug") namespace and attach the native
    // functions before defining it (DefinePropertyValue consumes the ref).
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sym    = JS_NewSymbol(ctx, "retroplug", /*is_global*/ true);
    JSAtom atom    = JS_ValueToAtom(ctx, sym);
    JSValue ns     = JS_NewObjectProto(ctx, JS_NULL);

    auto bind = [&](const char* name, JSCFunction* fn, int argc) {
        JS_SetPropertyStr(ctx, ns, name, JS_NewCFunction(ctx, fn, name, argc));
    };
    bind("beginCase",    jsBeginCase,    0);
    bind("report",       jsReport,       3);
    bind("done",         jsDone,         0);
    bind("log",          jsLog,          2);
    // The emulator surface: the generated HarnessService client dispatches
    // through this single sync RPC entry (the per-method trampolines are gone).
    bind("__rpcSend",    jsHarnessRpcSend, 1);

    JS_DefinePropertyValue(ctx, global, atom, ns, JS_PROP_C_W_E);
    JS_FreeAtom(ctx, atom);
    JS_FreeValue(ctx, sym);
    JS_FreeValue(ctx, global);

    // Redirect console.* to stderr (keep stdout TAP-clean).
    static const char kConsoleShim[] =
        "(() => {"
        "  const rp = globalThis[Symbol.for('retroplug')];"
        "  const mk = (lvl) => (...a) => rp.log(lvl, a.map("
        "    x => typeof x === 'string' ? x : "
        "         (() => { try { return JSON.stringify(x); }"
        "                  catch (e) { return String(x); } })()"
        "  ).join(' '));"
        "  globalThis.console = { log: mk(0), info: mk(0), debug: mk(0),"
        "                         warn: mk(1), error: mk(2) };"
        "})();";
    JSValue r = JS_Eval(ctx, kConsoleShim, std::strlen(kConsoleShim),
                        "<console-shim>", JS_EVAL_TYPE_GLOBAL);
    JS_FreeValue(ctx, r);
}

TestHarness::~TestHarness() {
    if (impl_ && impl_->qrt) {
        TJS_FreeRuntime(impl_->qrt);
        impl_->qrt = nullptr;
        impl_->ctx = nullptr;
    }
    g_activeImpl = nullptr;
}

int TestHarness::runFile(const std::string& jsPath) {
    std::printf("TAP version 13\n");
    std::fflush(stdout);

    std::string code;
    try {
        code = rpcli::slurpText(jsPath);
    } catch (const std::exception& e) {
        std::printf("Bail out! %s\n", e.what());
        std::fflush(stdout);
        return 1;
    }

    JSContext* ctx = impl_->ctx;
    // is_main=true fires the synthetic window 'load' event the runner hooks.
    JSValue res = TJS_EvalModuleContent(ctx, jsPath.c_str(), /*is_main*/ true,
                                        /*use_realpath*/ false, code.data(),
                                        code.size());
    const bool threw = JS_IsException(res);
    if (threw) tjs_dump_error(ctx);
    JS_FreeValue(ctx, res);

    // Drain any async work the tests scheduled (timers / promises). v1 tests
    // are synchronous, so a bounded pump is sufficient.
    for (int i = 0; i < 64; ++i) {
        uv_run(TJS_GetLoop(impl_->qrt), UV_RUN_NOWAIT);
        tjs__execute_jobs(ctx);
    }

    if (threw) {
        std::printf("Bail out! test module evaluation failed\n");
        std::fflush(stdout);
        return 1;
    }

    impl_->done();  // ensure a plan line even if the test forgot
    return impl_->failures > 0 ? 1 : 0;
}
