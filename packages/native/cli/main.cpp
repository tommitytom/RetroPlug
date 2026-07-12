// retroplug-greenfield-cli — a standalone txiki/QuickJS executable that runs a greenfield session
// bundle over a REAL `Backend`. No Node at runtime: the binary embeds the txiki host + the emulator
// cores, evals one pre-bundled session `.js` (authored in TypeScript, esbuild-bundled to JS by
// tools/build-greenfield-session.js), and returns its exit code.
//
//   retroplug-greenfield-cli <session.js>
//
// It binds the same JS surface the test host (src/main.cpp) and the plugin expose — the Backend over
// globalThis[Symbol.for("plugin")].__rpcSend, console.log -> stdout, and globalThis.tjs.exit(code) —
// so a session reuses the greenfield control-plane API (createRealBackend / ProjectStore /
// createAudioDriver) unchanged. Deliberately a near-clone of src/main.cpp's host body; a shared
// host-run helper can be factored later if a third entry point appears.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "dpfjs/host/TjsHostRuntime.hpp"  // shared txiki/QuickJS host (+ tjs.h/quickjs.h)

#include "host/rpc/BackendFacade.hpp"
#include "host/rpc/BackendRpcRegistration.hpp"
#include "TypedRpcServer.h"
#include "codecs/QuickJSCodec.h"
#include "transports/QuickJSTransport.h"

using BackendRpcServer = rpcpp::TypedRpcServer<BackendFacade, rpcpp::QuickJSCodec>;

// Subcommand bundles compiled into the binary (tjsc bytecode, see packages/native/CMakeLists.txt).
// `render` runs cli/sessions/render.ts with no loose .js and no Node.
extern "C" {
extern const std::uint8_t  rp_render_bundle[];
extern const std::uint32_t rp_render_bundle_size;
}

namespace {

// A named, compiled-in session. argv[1] selecting a subcommand evals its bytecode instead of a file.
struct Subcommand {
    const char* name;
    const std::uint8_t* bytecode;
    const std::uint32_t* size;
};
const Subcommand kSubcommands[] = {
    {"render", rp_render_bundle, &rp_render_bundle_size},
};

const Subcommand* findSubcommand(const char* name) {
    for (const auto& sc : kSubcommands)
        if (std::strcmp(sc.name, name) == 0) return &sc;
    return nullptr;
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

std::string slurp(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

int main(int argc, char** argv) try {
    if (argc < 2 || argv[1][0] == '\0') {
        std::fprintf(stderr,
            "usage: retroplug-cli <command|session.js> [args...]\n"
            "  render <rom> [--sav f] [--state f] [--out f] [--ms n] [--max-ms n]\n"
            "               [--split mix|channels|pins|mono] [--bpm n] [--transport] [--no-start]\n"
            "               [--song name | --song-index n] [--list-songs]\n"
            "                       Render a ROM (+.sav/savestate) to WAV (compiled-in; no Node).\n"
            "                       LSDj: --song picks a saved song; --list-songs lists them; a loaded\n"
            "                       LSDj sav auto-detects length (renders to the HFF stop) unless --ms.\n"
            "  <session.js> [args]  Run a pre-bundled greenfield session by path (dev fallback).\n");
        return 2;
    }
    // A named subcommand (its session is compiled in) wins over a like-named file path.
    const Subcommand* sub = findSubcommand(argv[1]);

    TjsHostRuntime host;
    if (!host.init()) {
        std::fprintf(stderr, "TjsHostRuntime init failed\n");
        return 1;
    }
    JSContext* ctx = host.context();

    // rpcpp server over the QuickJS object codec (marshals request/response as live JS objects against
    // ctx — nothing serialized). The transport's async sink is unused.
    BackendFacade service;
    rpcpp::QuickJSTransport transport(ctx, [](JSContext*, JSValue) {});
    BackendRpcServer server(service, transport, rpcpp::QuickJSCodec{ctx});
    registerAllBackendRpc(server, service);

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
        for (int i = 2, j = 0; i < argc; ++i, ++j)
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

    JS_FreeValue(ctx, global);

    // A subcommand evals its compiled-in bytecode; otherwise argv[1] is a session .js path we slurp.
    int rc;
    if (sub) {
        rc = host.evalModuleBytecode(sub->bytecode, *sub->size);
    } else {
        const std::string code = slurp(argv[1]);
        rc = host.evalModuleBuffer(code.data(), code.size(), argv[1]);
    }
    if (rc != 0) {
        std::fprintf(stderr, "session eval failed\n");
        return 1;
    }

    // Drive the job loop until the session runs and calls tjs.exit (ES modules evaluate async in
    // QuickJS, so the module body itself runs here). Bounded.
    for (int i = 0; i < 20000 && !g_exit.set; ++i) host.pump();

    if (!g_exit.set) {
        std::fprintf(stderr, "session did not complete (tjs.exit never called)\n");
        return 1;
    }
    return g_exit.code;
} catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
}
