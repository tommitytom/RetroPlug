// retroplug-cli — a standalone txiki/QuickJS executable over a REAL `Backend`. No Node at runtime: the
// binary embeds the txiki host + the emulator cores and returns the session's exit code.
//
//   retroplug-cli <command> [args...]     run a baked-in command (render, …) via the TS dispatcher
//   retroplug-cli <session.js> [args...]  run a JavaScript session file by path
//
// This launcher is deliberately dumb about commands: it evals the compiled-in root dispatcher bundle
// (cli/cli.ts → rp_cli_bundle) for anything that isn't a `.js` file path, passing the FULL arg vector so
// the TS side owns command routing + all help text. A `.js` argument is slurped and evaled directly (the
// dispatcher is skipped). Commands are added in TS (cli/tools.ts) — never here.
//
// It binds the same JS surface the test host (src/main.cpp) and the plugin expose — the Backend over
// globalThis[Symbol.for("plugin")].__rpcSend, console.log -> stdout, and globalThis.tjs.exit(code).
// Deliberately a near-clone of src/main.cpp's host body; a shared host-run helper can be factored later.

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include "dpfjs/host/TjsHostRuntime.hpp"  // shared txiki/QuickJS host (+ tjs.h/quickjs.h)

#ifdef RETROPLUG_N8_BRIDGE
#include "N8Bridge.hpp"  // `n8-bridge` subcommand: live MIDI -> Everdrive N8 Pro over USB (own loop)
#include "N8Sync.hpp"    // `n8-sync` subcommand: MIDI transport -> risa host sync on the N8 (own loop)
#include "host/n8/SerialRpcService.hpp"    // the serial byte-transport facet the TS N8 stack rides on
#include "host/input/MidiRpcService.hpp"   // the live-MIDI-input facet the TS bridges poll
#endif

#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/rpc/BackendRpcRegistration.hpp"
#include "system/CoreBackends.hpp"
#include "system/SystemFactory.hpp"
#include "TypedRpcServer.h"
#include "codecs/QuickJSCodec.h"
#include "transports/QuickJSTransport.h"

using BackendRpcServer = rpcpp::TypedRpcServer<rpcpp::Empty, rpcpp::QuickJSCodec>;

// The root CLI dispatcher, compiled into the binary (tjsc bytecode of cli/cli.ts, see
// packages/native/CMakeLists.txt). It owns command routing + all help; this launcher just evals it.
extern "C" {
extern const std::uint8_t  rp_cli_bundle[];
extern const std::uint32_t rp_cli_bundle_size;
}

namespace {

// True when `s` ends with `.js` — the launcher treats such an argv[1] as a session file path to eval
// directly (skipping the dispatcher). Everything else is a command for the dispatcher.
bool endsWithJs(const char* s) {
    const std::size_t n = std::strlen(s);
    return n >= 3 && std::strcmp(s + n - 3, ".js") == 0;
}

// The exit code the session reports through globalThis.tjs.exit(). One host per process,
// single-threaded, so a TU-local is the right mechanism (mirrors src/main.cpp).
struct ExitState { int code = 0; bool set = false; };
ExitState g_exit;

JSValue jsExit(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int code = 0;
    if (argc >= 1) JS_ToInt32(ctx, &code, argv[0]);
    g_exit.code = code;
    g_exit.set = true;
    return JS_UNDEFINED;
}

// A long-running session (a live MIDI bridge) calls globalThis.__rp_keepAlive() during setup to opt out of
// the bounded batch pump: the launcher then pumps until tjs.exit or Ctrl-C, so the session can react to MIDI
// indefinitely. Set on the single JS thread; read by the pump loop.
bool g_keepAlive = false;
JSValue jsKeepAlive(JSContext*, JSValueConst, int, JSValueConst*) {
    g_keepAlive = true;
    return JS_UNDEFINED;
}

// Ctrl-C stops a long-running session (and any batch command). The handler must be async-signal-safe, so it
// only flips a sig_atomic_t; the pump loop checks it and exits 0.
volatile std::sig_atomic_t g_sigint = 0;
void onSigint(int) { g_sigint = 1; }

std::string slurp(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

int main(int argc, char** argv) try {
    // `n8-bridge` is a live-hardware subcommand: it opens real MIDI + serial ports and runs its own
    // unbounded loop, so it can't ride the bounded QuickJS pump the TS dispatcher uses. Handle it here,
    // before the `.js` / dispatcher fallthrough. Compiled in only when the bridge deps are linked.
    if (argc >= 2 && std::strcmp(argv[1], "n8-bridge") == 0) {
#ifdef RETROPLUG_N8_BRIDGE
        return retroplug::runN8Bridge(argc, argv);
#else
        std::fprintf(stderr, "n8-bridge: this build was compiled without N8 bridge support "
                             "(-DRETROPLUG_N8_BRIDGE=OFF)\n");
        return 1;
#endif
    }
    // `n8-load` is NOT intercepted here: it's a TS tool now (cli/sessions/n8-load.ts), a linear script that
    // rides the bounded QuickJS pump + the serial byte-transport facet. It routes through the dispatcher below.
    if (argc >= 2 && std::strcmp(argv[1], "n8-sync") == 0) {
#ifdef RETROPLUG_N8_BRIDGE
        return retroplug::runN8Sync(argc, argv);
#else
        std::fprintf(stderr, "n8-sync: this build was compiled without N8 bridge support "
                             "(-DRETROPLUG_N8_BRIDGE=OFF)\n");
        return 1;
#endif
    }

    // A `.js` argv[1] is a session file we eval directly; anything else (incl. no args) goes to the
    // compiled-in dispatcher, which prints help and routes commands. The args exposed to JS start after
    // the file path (argv[2..]) for a file, or include the command (argv[1..]) for the dispatcher.
    const bool runFile = argc >= 2 && endsWithJs(argv[1]);
    const int  argStart = runFile ? 2 : 1;

    TjsHostRuntime host;
    if (!host.init()) {
        std::fprintf(stderr, "TjsHostRuntime init failed\n");
        return 1;
    }
    JSContext* ctx = host.context();

    // The backend service graph: one Engine + factory + the ONE invoker, and the four concern services
    // over them. The CLI exposes the whole surface (incl. the debug facet), so it mounts every facet.
    Engine engine;
    SystemFactory factory;
    registerCoreBackends(factory);
    QueuedInvoker invoker{engine, engine.registry()};
    HostRpcService        hostSvc;
    EngineRpcService      engineSvc{engine, factory, invoker};
    DebugRpcService       debugSvc{engine};
    AudioDriverRpcService driver{engine, invoker};

    // rpcpp server over the QuickJS object codec (marshals request/response as live JS objects against
    // ctx — nothing serialized). No primary object: every facet is mounted cross-object. Async sink unused.
    rpcpp::QuickJSTransport transport(ctx, [](JSContext*, JSValue) {});
    BackendRpcServer server(transport, rpcpp::QuickJSCodec{ctx});
    registerAllBackendRpc(server, hostSvc, engineSvc, debugSvc, driver);

#ifdef RETROPLUG_N8_BRIDGE
    // The serial + MIDI-input transport facets (CLI-only): the thin native seams the TS N8 stack (Edio
    // framing, menu, ROM/save orchestration + the live MIDI bridges in packages/retroplug/src/n8) rides on.
    // Deliberately kept out of registerAllBackendRpc so the plugin/test hosts don't drag in serial/rtmidi;
    // mounted here alongside the other N8 subcommands.
    retroplug::SerialRpcService serialSvc;
    retroplug::registerSerialRpc(server, serialSvc);
    retroplug::MidiRpcService midiSvc;
    retroplug::registerMidiRpc(server, midiSvc);
#endif

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
        // .args — the session's argument vector (argv[2..]; argv[1] is the session .js). Read by
        // cli/session.ts's hostArgs(). Hung off our own namespace object because txiki already defines
        // tjs.args as a read-only accessor.
        JSValue args = JS_NewArray(ctx);
        for (int i = argStart, j = 0; i < argc; ++i, ++j)
            JS_SetPropertyUint32(ctx, args, static_cast<uint32_t>(j), JS_NewString(ctx, argv[i]));
        JS_SetPropertyStr(ctx, ns, "args", args);
        JS_DefinePropertyValue(ctx, global, atom, ns, JS_PROP_C_W_E);
        JS_FreeAtom(ctx, atom);
        JS_FreeValue(ctx, sym);
    }

    // globalThis.tjs.exit — the session sets the process exit code through it. Override any
    // txiki-provided exit so we record the code and return it (rather than terminating mid-pump).
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

    // globalThis.__rp_keepAlive — a long-running session (MIDI bridge) calls it to switch the pump from the
    // bounded batch backstop to run-until-exit (see the pump loop below).
    JS_SetPropertyStr(ctx, global, "__rp_keepAlive", JS_NewCFunction(ctx, jsKeepAlive, "__rp_keepAlive", 0));

    JS_FreeValue(ctx, global);

    // A `.js` path is slurped + evaled; otherwise run the compiled-in dispatcher (it reads the command
    // from the args we exposed above and prints help / routes it).
    int rc;
    if (runFile) {
        const std::string code = slurp(argv[1]);
        rc = host.evalModuleBuffer(code.data(), code.size(), argv[1]);
    } else {
        rc = host.evalModuleBytecode(rp_cli_bundle, rp_cli_bundle_size);
    }
    if (rc != 0) {
        std::fprintf(stderr, "session eval failed\n");
        return 1;
    }

    std::signal(SIGINT, onSigint);

    // Drive the job loop. ES modules evaluate async in QuickJS, so the module body (the dispatcher + the tool)
    // runs during these pumps. A normal command runs to completion + calls tjs.exit within the bounded
    // backstop; a long-running session calls __rp_keepAlive() during setup, which drops us into the unbounded
    // loop below so it can react to MIDI until tjs.exit or Ctrl-C.
    for (int i = 0; i < 20000 && !g_exit.set && !g_sigint && !g_keepAlive; ++i) host.pump();

    if (g_keepAlive) {
        // Long-running session (e.g. a live MIDI bridge): pump until it exits or Ctrl-C. The 250us cadence
        // matches the native bridge loop; the session's own setInterval poll fires as the loop pumps.
        while (!g_exit.set && !g_sigint) {
            host.pump();
            std::this_thread::sleep_for(std::chrono::microseconds(250));
        }
    } else if (!g_exit.set && !g_sigint) {
        std::fprintf(stderr, "session did not complete (tjs.exit never called)\n");
        return 1;
    }
    return g_exit.set ? g_exit.code : 0;  // a Ctrl-C (g_sigint) without tjs.exit is a clean exit 0
} catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
}
