#include "TestHarness.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "dpfjs/host/TjsHostRuntime.hpp"  // shared txiki/QuickJS host (+ tjs.h/quickjs.h)

#include "TestHarnessImpl.hpp"      // TestHarness::Impl (complete) + rpc aliases
#include "HarnessRpcRegistration.hpp"

// Guard the hand-mirrored TypeScript enums in packages/retroplug/src/emu.ts. The
// wire values are load-bearing; if a C++ renumber drifts from the TS
// Button/Mem/Routing objects, fail the build here with a pointed message rather
// than silently passing the wrong byte across the bridge.
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
static_assert(static_cast<int>(MidiRouting::SendToAll)               == 0, "harness Routing.SendToAll out of sync");
static_assert(static_cast<int>(MidiRouting::FourChannelsPerInstance) == 1, "harness Routing.FourChannelsPerInstance out of sync");
static_assert(static_cast<int>(MidiRouting::OneChannelPerInstance)   == 2, "harness Routing.OneChannelPerInstance out of sync");
static_assert(static_cast<int>(MidiRouting::MidiChannelToInstance)   == 3, "harness Routing.MidiChannelToInstance out of sync");

// The active harness for the current process. The __rpcSend dispatch is carried
// by the host's trampoline (no global needed), but the TAP-runner trampolines
// below still recover their Impl through this translation-unit pointer — one
// runtime per process + single-threaded makes that the correct mechanism.
namespace { TestHarness::Impl* g_activeImpl = nullptr; }

// ---------------------------------------------------------------------------
// JS trampolines for the native TAP runner. Every body is wrapped so a C++
// throw never crosses into QuickJS (which does not catch C++ exceptions).
// ---------------------------------------------------------------------------

namespace {

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

// getArgv() -> string[]: the end-user CLI bundle's argument vector (set by
// runBundle before eval). Empty under --test.
JSValue jsGetArgv(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    JSValue arr = JS_NewArray(ctx);
    auto* h = g_activeImpl;
    if (!h) return arr;
    for (std::size_t i = 0; i < h->cliArgs.size(); ++i) {
        const std::string& a = h->cliArgs[i];
        JS_SetPropertyUint32(ctx, arr, static_cast<std::uint32_t>(i),
                             JS_NewStringLen(ctx, a.data(), a.size()));
    }
    return arr;
}

// exit(code): the CLI bundle reports its process exit code (returned by runBundle).
JSValue jsExit(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    int code = 0;
    if (argc >= 1) JS_ToInt32(ctx, &code, argv[0]);
    if (h) h->cliExitCode = code;
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

TestHarness::TestHarness()
    : impl_(std::make_unique<Impl>()),
      host_(std::make_unique<TjsHostRuntime>()) {
    if (!host_->init())
        throw std::runtime_error("TjsHostRuntime init failed");

    // Recover *this Impl inside the static TAP trampolines.
    g_activeImpl = impl_.get();

    // Stand up the rpcpp server stack. The generated HarnessService client
    // dispatches through __rpcSend -> processMessage.
    impl_->rpcService_   = std::make_unique<HarnessRpcService>(impl_.get());
    impl_->rpcTransport_ = std::make_unique<HarnessRpcTransport>();
    impl_->rpcServer_    = std::make_unique<HarnessRpcServer>(*impl_->rpcService_,
                                                              *impl_->rpcTransport_);
    registerHarnessRpcMethods(*impl_->rpcServer_);

    JSContext* ctx = host_->context();

    // Build the Symbol.for("retroplug") namespace and attach the native
    // functions before defining it (DefinePropertyValue consumes the ref).
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sym    = JS_NewSymbol(ctx, "retroplug", /*is_global*/ true);
    JSAtom atom    = JS_ValueToAtom(ctx, sym);
    JSValue ns     = JS_NewObjectProto(ctx, JS_NULL);

    auto bind = [&](const char* name, JSCFunction* fn, int argc) {
        JS_SetPropertyStr(ctx, ns, name, JS_NewCFunction(ctx, fn, name, argc));
    };
    bind("beginCase", jsBeginCase, 0);
    bind("report",    jsReport,    3);
    bind("done",      jsDone,      0);
    bind("log",       jsLog,       2);
    bind("getArgv",   jsGetArgv,   0);
    bind("exit",      jsExit,      1);
    // The emulator surface: the generated HarnessService client dispatches
    // through the host's single sync RPC entry.
    host_->bindRpcSend(ns,
        [impl = impl_.get()](std::string_view bytes) {
            return impl->rpcServer_->processMessage(
                std::span<const char>(bytes.data(), bytes.size()));
        });

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
    // Free the runtime (and the __rpcSend trampoline whose captured lambda
    // points at impl_) while impl_ is still alive, then drop the recovery
    // pointer. impl_ outlives host_ either way (declared first).
    host_.reset();
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

    // is_main=true fires the synthetic window 'load' event the runner hooks.
    const int rc = host_->evalModuleBuffer(code.data(), code.size(), jsPath.c_str());

    // Drain any async work the tests scheduled (timers / promises). v1 tests
    // are synchronous, so a bounded pump is sufficient.
    for (int i = 0; i < 64; ++i)
        host_->pump();

    if (rc != 0) {
        std::printf("Bail out! test module evaluation failed\n");
        std::fflush(stdout);
        return 1;
    }

    impl_->done();  // ensure a plan line even if the test forgot
    return impl_->failures > 0 ? 1 : 0;
}

namespace {
// Drain async work the bundle scheduled (timers / promises). The CLI bundle is
// effectively synchronous, so a bounded pump suffices.
void drainJobs(TjsHostRuntime& host) {
    for (int i = 0; i < 64; ++i) host.pump();
}
} // namespace

int TestHarness::runBundle(const std::uint8_t* bytecode, std::size_t len,
                           const std::vector<std::string>& argv) {
    impl_->cliArgs     = argv;
    impl_->cliExitCode = 0;
    const int rc = host_->evalModuleBytecode(bytecode, len);
    drainJobs(*host_);
    if (rc != 0) return 1;
    return impl_->cliExitCode;
}

int TestHarness::runBundleFromFile(const std::string& path,
                                   const std::vector<std::string>& argv) {
    std::string code;
    try {
        code = rpcli::slurpText(path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    impl_->cliArgs     = argv;
    impl_->cliExitCode = 0;
    const int rc = host_->evalModuleBuffer(code.data(), code.size(), path.c_str());
    drainJobs(*host_);
    if (rc != 0) return 1;
    return impl_->cliExitCode;
}
