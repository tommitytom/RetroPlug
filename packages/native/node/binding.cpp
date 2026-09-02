// retroplug.node - the Node-API host.
//
// The fourth host over the same backend service graph (after the plugin, the CLI and the render
// worker). Structurally this is cli/main.cpp's composition block with the QuickJS half swapped out:
// same services, same registerHostRpc/registerAllBackendRpc mounting, same rpcpp TypedRpcServer;
// only the codec and the way __rpcSend reaches JS differ.
//
// The whole point is that the TS layer is untouched. packages/retroplug/src/ has zero txiki
// references and talks to native through exactly one function:
//
//     globalThis[Symbol.for("plugin")].__rpcSend(request) -> reply        (SYNCHRONOUS)
//
// A native N-API callback returns synchronously, so that contract holds here as it does under
// QuickJS. (An out-of-process stdio host could not honour it without making every Backend method
// async, which is why this is an in-process addon and not rpcpp's shipped stdio client.)
//
// Scope: the WHOLE backend surface, exactly as the CLI mounts it (registerAllBackendRpc) - host,
// emulator, dsp-kernel, debug and audio-driver facets over one Engine. So a Node process can boot the
// real control plane (bootSession) and drive the emulator cores.

#include <exception>
#include <optional>

#include <node_api.h>

#include "TypedRpcServer.h"  // rpcpp - its src/ is on the include path (see cli/main.cpp)

#include "NodeCodec.hpp"
#include "NodeTransport.hpp"

#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/rpc/BackendRpcRegistration.hpp"
#include "system/CoreBackends.hpp"
#include "system/SystemFactory.hpp"

namespace {

using BackendRpcServer = rpcpp::TypedRpcServer<rpcpp::Empty, rpcpp::NodeCodec>;

// One per addon instance (one per Node context - a worker_thread gets its own, with its own Engine).
// Held through napi_set_instance_data rather than a static, so multiple contexts don't share a
// backend. Member order mirrors cli/main.cpp's composition block: the Engine and factory come first
// because the services hold references to them.
struct AddonState {
    Engine                engine;
    SystemFactory         factory;
    QueuedInvoker         invoker;
    HostRpcService        host;
    EngineRpcService      engineSvc;
    DebugRpcService       debugSvc;
    AudioDriverRpcService driver;
    rpcpp::NodeTransport  transport;
    BackendRpcServer      server;

    explicit AddonState(napi_env env)
        : invoker(engine, engine.registry()),
          engineSvc(engine, factory, invoker),
          debugSvc(engine),
          driver(engine, invoker),
          // No primary object: every facet is mounted cross-object. The async sink is unused (nothing
          // in the backend surface pushes today), matching the CLI host.
          transport([](napi_env, napi_value) {}),
          server(transport, rpcpp::NodeCodec{env}) {
        registerCoreBackends(factory);
        registerAllBackendRpc(server, host, engineSvc, debugSvc, driver);
    }
};

AddonState* stateOf(napi_env env) {
    void* data = nullptr;
    if (napi_get_instance_data(env, &data) != napi_ok) return nullptr;
    return static_cast<AddonState*>(data);
}

// globalThis[Symbol.for("plugin")].__rpcSend - takes a JSON-RPC request object, returns the reply
// object, or null for a notification. The request napi_value is handed straight to the server as the
// codec's native AST node; nothing is serialized in either direction.
napi_value RpcSend(napi_env env, napi_callback_info info) {
    size_t     argc      = 1;
    napi_value argv[1]   = {nullptr};
    if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok) {
        napi_throw_error(env, nullptr, "rpcSend: could not read arguments");
        return nullptr;
    }
    if (argc < 1) {
        napi_throw_error(env, nullptr, "rpcSend: expected a request object");
        return nullptr;
    }

    AddonState* state = stateOf(env);
    if (!state) {
        napi_throw_error(env, nullptr, "rpcSend: addon state missing");
        return nullptr;
    }

    try {
        std::optional<rpcpp::NodeOut> out = state->server.processMessage(argv[0]);
        if (!out || !out->materialize) {
            napi_value null = nullptr;
            napi_get_null(env, &null);
            return null;  // notification / no reply - realBackend maps this to undefined
        }
        return out->materialize(env);
    } catch (const std::exception& e) {
        napi_throw_error(env, nullptr, e.what());
        return nullptr;
    } catch (...) {
        napi_throw_error(env, nullptr, "rpcSend: unknown native error");
        return nullptr;
    }
}

}  // namespace

NAPI_MODULE_INIT() {
    auto* state = new AddonState(env);
    napi_set_instance_data(
        env, state,
        [](napi_env, void* data, void*) { delete static_cast<AddonState*>(data); }, nullptr);

    napi_value fn = nullptr;
    if (napi_create_function(env, "rpcSend", NAPI_AUTO_LENGTH, RpcSend, nullptr, &fn) != napi_ok) {
        napi_throw_error(env, nullptr, "could not create rpcSend");
        return nullptr;
    }
    napi_set_named_property(env, exports, "rpcSend", fn);
    return exports;
}
