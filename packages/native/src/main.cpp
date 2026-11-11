// retroplug-host — a minimal txiki/QuickJS host that runs a TS test
// bundle over a real `Backend`. It binds `globalThis[Symbol.for("plugin")].__rpcSend` to the
// whole backend RPC surface (every facet, over HostServices' Engine and cores) - the same
// namespace the plugin host uses, so one TS adapter (realBackend.ts) serves both.
//
// The harness (@retroplug/testing's harness.ts) self-reports
// TAP via console.log (txiki's console -> stdout, left untouched) and sets the process exit
// code via globalThis.tjs.exit(code). We provide that exit hook (recording the code rather
// than terminating) and drive the job loop until it fires.
//
//   retroplug-host [--test] <bundle.js>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "dpfjs/host/TjsHostRuntime.hpp"  // shared txiki/QuickJS host (+ tjs.h/quickjs.h)

#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/rpc/BackendRpcRegistration.hpp"
#include "TypedRpcServer.h"
#include "codecs/QuickJSCodec.h"
#include "transports/QuickJSTransport.h"
#include "host/HostServices.hpp"
#include "launchpad/harness/HarnessSurfaceLink.hpp"
#include "host/QuickJsGlobals.hpp"
#include "host/devices/ProviderHost.hpp"
#include "launchpad/desktop/LaunchpadProvider.hpp"
#include "launchpad/desktop/LaunchpadRpc.hpp"
#include "n8/desktop/N8Provider.hpp"
#include "n8/desktop/N8Rpc.hpp"
#include "FakeN8.hpp"     // packages/n8/native/test: the cart this host's N8 provider opens its ports on
#include "FakePorts.hpp"  // packages/launchpad/native/test: the scripted device its Launchpad facet serves
#include "midi/FakeMidiBackend.hpp"     // packages/midi: the scripted MIDI system its facet serves
#include "midi/facet/MidiProvider.hpp"  // its provider and facet (the midi package's, mounted over the fake)

using BackendRpcServer = rpcpp::TypedRpcServer<rpcpp::Empty, rpcpp::QuickJSCodec>;

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

// Set by globalThis.__rp_keepAlive(): the test awaits work that outlasts the bounded spin in main().
bool g_keepAlive = false;

JSValue jsKeepAlive(JSContext*, JSValueConst, int, JSValueConst*) {
    g_keepAlive = true;
    return JS_UNDEFINED;
}

// This process's id, which names its scratch directory.
long processId() {
#if defined(_WIN32)
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
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
        std::fprintf(stderr, "usage: retroplug-host [--test] <bundle.js>\n");
        return 2;
    }

    TjsHostRuntime host;
    if (!host.init()) {
        std::fprintf(stderr, "TjsHostRuntime init failed\n");
        return 1;
    }
    JSContext* ctx = host.context();

    // The harness's control surface: the graph's rp.launchpad.surface-in / -out nodes bind it by name, so a headless
    // test's staged pad presses reach the controller session a document places and its answers have a device-side
    // count. It is the test host's only control surface (it opens no MIDI port); the panel it stands in for has no
    // tests of its own here, so the link is the harness's, not a fake of the Launchpad protocol. Declared before the
    // services, so it outlives the engine whose plans' nodes hold a handle on it.
    rp::launchpad::HarnessSurfaceLink harnessSurface;
    // The N8 this host's sessions reach (a test:native CLI-style session, through the real facet): FakeN8's model of
    // the cart, which the pack's own tests drive too, behind the real N8 provider. This host starts the provider
    // itself, with FakeN8's ports, rather than through a generated rp_start_providers with deps/serial's, so no
    // production provider carries a fake cart or a switch to one. Declared before the services, so it outlives every
    // port the provider opens on it.
    retroplug::test::FakeN8 fakeCart;

    // The backend service graph (see host/HostServices.hpp). This host exposes the whole surface, so it
    // mounts every facet.
    HostServices svc;
    // The N8 provider over the fake cart, its link installed in the engine's registry as a product's is; its facet is
    // mounted below beside the host's own. Its n8.cfg, which the link half's setters write (test-native/n8-session
    // connects the link), goes to a scratch directory of this process's own, never the user's (the standalone and the
    // plugin read that one) and never another host's (test:native runs its files in parallel, and the authoring tools
    // run this host too), removed at exit: a static, because the harness ends the process through txiki's own exit
    // (tjs:os), which runs static destructors and not this frame's. At a return from main the plans go first, while
    // the link is alive (a node may use its handle in destroy), then the provider: declared before `driver`, so
    // ~AudioDriverRpcService has stopped any audio thread by then.
    struct ScratchDir {
        std::filesystem::path path;
        // Only a directory this host MADE is removed: RETROPLUG_LAUNCHPAD_CLAIM_DIR names a directory a caller owns, and a
        // host that deleted it would take that caller's lock files with it (a second host's claim would then succeed on an
        // unlinked inode). The runner removes its per-file directories itself.
        bool                  owns = true;
        ~ScratchDir() {
            if (!owns) return;
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    };
    static ScratchDir n8ConfigDir{std::filesystem::temp_directory_path() /
                                  ("retroplug-host-n8-" + std::to_string(processId()))};
    std::filesystem::create_directories(n8ConfigDir.path);
    rp::host::ProviderHost deviceHost{svc.engine, n8ConfigDir.path.string()};
    struct CartProvider {
        Engine& engine;
        void*   self;
        ~CartProvider() {
            engine.clearGraph();
            if (self) rp_n8_provider()->destroy(self);
        }
    } cartProvider{svc.engine,
                   rp::n8::createProvider(deviceHost.get(), retroplug::test::factoryFor(fakeCart),
                                          [] { return std::vector<retroplug::N8PortDto>{{"fake-n8", true}}; })};
    // The Launchpad pack's provider over a SCRIPTED device (packages/launchpad/native/test/FakePorts.hpp): a Pro MK3 on
    // "LPProMK3 MIDI" both ways, which answers the Universal Device Inquiry and its own layout query, and an interface's
    // port that answers nothing. The facet is the pack's real one - the provider, the claim path, the pool and the
    // cross-process claim - so test-native drives the SAME code the standalone does, over this device instead of
    // rtmidi's ports, and no hardware is touched. The provider is started here rather than by rp_start_providers so no
    // production provider carries a fake; the generated header still links its desktop half, which is what this call is.
    //
    // The graph's own control surface stays the harness link above, and the device host is told so (installLinks false):
    // a test stages pad presses into THAT through stageControllerIn, which no port can do, and the facet needs no
    // registry at all. Its config dir is a scratch of this process's, so its cross-process claim is this process's alone
    // and never another host's (test:native runs its files in parallel); RETROPLUG_LAUNCHPAD_CLAIM_DIR fixes it instead,
    // which is how a case runs a SECOND host on the same directory and sees the refusal (test-native/
    // launchpad-devices.test.ts). A fixed directory is the caller's and is left as it is found.
    static ScratchDir launchpadConfigDir{[] {
        const char* fixed = std::getenv("RETROPLUG_LAUNCHPAD_CLAIM_DIR");
        if (fixed && *fixed) return ScratchDir{std::filesystem::path(fixed), /*owns=*/false};
        return ScratchDir{std::filesystem::temp_directory_path() /
                          ("retroplug-host-launchpad-" + std::to_string(processId()))};
    }()};
    std::filesystem::create_directories(launchpadConfigDir.path);
    rp::launchpad::test::Rig launchpadRig;
    // What a real machine lists BESIDES the device: RetroPlug's own virtual ports, a second instance's `RetroPlug
    // Launchpad` link ports, and ALSA's Midi Through. The facet drops them (midi/PortFilter.hpp's isHardwareMidiPort), so
    // the pickers and a scan never see them, and test-native holds that through the production facet.
    launchpadRig.inputs  = {"LPProMK3 MIDI", "MIDISPORT 2x2 Port A", "Midi Through:Midi Through Port-0 14:0",
                            "RetroPlug:RetroPlug In 128:0", "RetroPlug Launchpad:LPProMK3 MIDI 129:0"};
    launchpadRig.outputs = {"LPProMK3 MIDI", "MIDISPORT 2x2 Port A", "Midi Through:Midi Through Port-0 14:1",
                            "RetroPlug:RetroPlug Out 128:1", "RetroPlug Launchpad:LPProMK3 MIDI 129:1"};
    launchpadRig.answering = {"LPProMK3 MIDI"};  // the interface's port is listed and answers nothing
    rp::host::ProviderHost launchpadHost{svc.engine, launchpadConfigDir.path.string(),
                                         rp::host::ProviderHost::Calls{}, /*installLinks=*/false};
    struct SurfaceProvider {
        Engine& engine;
        void*   self;
        ~SurfaceProvider() {
            engine.clearGraph();
            if (self) rp_launchpad_provider()->destroy(self);
        }
    } surfaceProvider{svc.engine,
                      rp::launchpad::createProvider(launchpadHost.get(), launchpadRig,
                                                    rp::launchpad::test::listerFor(launchpadRig))};
    // The midi package's facet over a SCRIPTED MIDI system, so test-native drives the production facet the CLI mounts -
    // which is the one door a live-MIDI client has (both of the CLI's own facets and this one are the same seven
    // methods). A machine with the same two controllers the Launchpad rig lists, so a case sees the same names through
    // either facet, and nothing real is opened: the backend is the midi package's fake, never RtMidi.
    rp::midi::FakeMidiBackend midiRig;
    midiRig.inputs  = {"LPProMK3 MIDI", "MIDISPORT 2x2 Port A", "Midi Through:Midi Through Port-0 14:0",
                       "RetroPlug:RetroPlug In 128:0", "RetroPlug Launchpad:LPProMK3 MIDI 129:0"};
    midiRig.outputs = {"LPProMK3 MIDI", "MIDISPORT 2x2 Port A", "Midi Through:Midi Through Port-0 14:1",
                       "RetroPlug:RetroPlug Out 128:1", "RetroPlug Launchpad:LPProMK3 MIDI 129:1"};
    // A scripted machine: every write gets one reply on EVERY open input, so a case can see which inputs a stream
    // actually opened (each open input contributes one message to the next poll).
    midiRig.onSend = [](rp::midi::FakeMidiBackend& backend, const std::string&, const std::uint8_t*, std::size_t) {
        const std::vector<std::uint8_t> reply{0xB0, 0x01, 0x02};
        for (const auto& entry : backend.listening) backend.deliver(entry.first, reply);
    };
    struct MidiProvider {
        void* self;
        ~MidiProvider() {
            if (self) rp_midi_provider()->destroy(self);
        }
    } midiProvider{rp::midi::createProvider(launchpadHost.get(), midiRig)};
    // The two facets the plugin and the SDL standalone deliberately do NOT mount, so they are not
    // in HostServices: ~AudioDriverRpcService has teardown side effects a host that never uses the
    // facet should not inherit.
    DebugRpcService       debugSvc{svc.engine};
    AudioDriverRpcService driver{svc.engine, svc.invoker};
    // The link before the first document, and the RPC service's handle on it for the harness's staging seam. A refusal
    // (a full table, a name taken) would leave the surface nodes on their stubs, so it is said, not swallowed.
    if (!svc.engine.installLink<rp::launchpad::SurfaceLink>(rp::launchpad::kDeviceName,
                                                            rp::launchpad::kSurfaceLinkInterface, harnessSurface))
        std::fprintf(stderr, "[retroplug-host] the harness surface link was not installed\n");
    svc.engineSvc.setControllerStaging(
        [&harnessSurface](std::span<const std::uint8_t> bytes) { return harnessSurface.queueInput(bytes); });

    // rpcpp server over the QuickJS object codec (marshals request/response as live JS objects against
    // ctx — nothing serialized). No primary object: every facet is mounted cross-object. Async sink unused.
    rpcpp::QuickJSTransport transport(ctx, [](JSContext*, JSValue) {});
    BackendRpcServer server(transport, rpcpp::QuickJSCodec{ctx});
    registerAllBackendRpc(server, svc.host, svc.engineSvc, debugSvc, driver);
    if (cartProvider.self) rp::n8::registerRpc(server, rp_n8_provider()->rpc_service(cartProvider.self));
    else std::fprintf(stderr, "[retroplug-host] the N8 provider did not start\n");
    // The Launchpad facet beside the N8's: a test's door to the scripted device, and to what the facet asked this product
    // for (svc.engineSvc.drainProductCalls reads launchpadHost's record: the claim's reserve_midi, a scan's held edge
    // and the startup rendezvous).
    svc.engineSvc.setProductCalls(&launchpadHost.productCalls());
    if (surfaceProvider.self) rp::launchpad::registerRpc(server, rp_launchpad_provider()->rpc_service(surfaceProvider.self));
    else std::fprintf(stderr, "[retroplug-host] the Launchpad provider did not start\n");
    // The MIDI facet beside them: the live-MIDI seam a CLI session opens a stream through, over the scripted system
    // above (test-native/midi-facet.test.ts holds what one does with it).
    if (midiProvider.self) rp::midi::registerRpc(server, rp_midi_provider()->rpc_service(midiProvider.self));
    else std::fprintf(stderr, "[retroplug-host] the MIDI provider did not start\n");

    JSValue global = JS_GetGlobalObject(ctx);

    // globalThis[Symbol.for("plugin")] = { __rpcSend } — the namespace realBackend.ts targets.
    installPluginNamespace(ctx, [&](JSValue ns) {
        host.bindRpcSend(ns, [&server](JSContext* sctx, JSValueConst req) -> JSValue {
            auto out = server.processMessage(req);
            if (!out) return JS_NULL;        // notification / no reply
            return out->materialize(sctx);   // owned; handed back to JS
        });
    });

    // globalThis.tjs.exit — the harness sets the exit code through it. Override
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
    JS_SetPropertyStr(ctx, global, "__rp_keepAlive", JS_NewCFunction(ctx, jsKeepAlive, "__rp_keepAlive", 0));

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

    // A test that awaits real work - a parity recording runs each case alone in a child process - says so
    // with __rp_keepAlive(), and is pumped until it exits, bounded by the wall clock. Asking is required:
    // txiki keeps a handle of its own alive, so "the loop still has work" would also hold a hung test.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(30);
    while (g_keepAlive && !g_exit.set && std::chrono::steady_clock::now() < deadline) {
        host.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!g_exit.set) {
        std::fprintf(stderr, "tests did not complete (tjs.exit never called)\n");
        return 1;
    }
    return g_exit.code;
} catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
}
