#include "PluginJsBridge.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "PluginRpcRegistration.hpp"
#include "project/Project.hpp"
#include "system/SystemBase.hpp"
#include "transport/MemorySnapshotTriple.hpp"
#include "util/Hash.hpp"

extern "C" {
    #include <quickjs.h>
}

namespace {

// Wire shape of a `"memory"` JSON-RPC notification. Mirrored on the JS side
// by the useMemory hook in ui/plugin/memory.ts. The struct is reflected by
// rpcpp's TypedRpcServer::writeNotification<T> path so bytes ride msgpack BIN
// without going through rfl::Generic.
struct MemoryNotificationPayload {
    std::uint32_t   systemId;
    std::uint32_t   type;
    rfl::Bytestring bytes;
    std::uint32_t   version;
};

} // namespace

PluginJsBridge::PluginJsBridge(LvglJsEngine& eng,
                               Project* project,
                               CommandQueue* commands,
                               EventQueue* events,
                               std::atomic<double>* sampleRate,
                               std::atomic<SystemId>* focusedSystemId,
                               UserConfig* userConfig,
                               RecentFiles* recentFiles)
    : engine(eng),
      project_(project),
      service_(project, commands, events, sampleRate, focusedSystemId,
               userConfig, recentFiles),
      // The generic bridge owns the rpc server/transport + the "plugin"
      // namespace (__rpcSend / __log). It references service_ — declared first.
      rpc_(eng, service_, "plugin") {

    registerPluginRpcMethods(rpc_.server());

    // Service emits string-payload JS events through the engine channel
    // mechanism (on/off in runtime/lvgljs/index.ts).
    service_.setEmitEventCallback(
        [this](const std::string& channel, const std::string& payload) {
            JSContext* ctx = engine.getContext();
            if (!ctx) return;
            JSValue v = JS_NewStringLen(ctx, payload.data(), payload.size());
            engine.emit(channel.c_str(), 1, &v);
            JS_FreeValue(ctx, v);
        });

    // RetroPlug-specific JS prop on the bridge namespace: a debug-overlay
    // toggle read from the (domain) RETROPLUG_DEBUG_OVERLAY env var.
    if (JSContext* ctx = engine.getContext();
        ctx && !JS_IsUndefined(rpc_.jsNamespace())) {
        JS_SetPropertyStr(ctx, rpc_.jsNamespace(), "debugOverlay",
                          JS_NewBool(ctx, std::getenv("RETROPLUG_DEBUG_OVERLAY") != nullptr));
    }
}

void PluginJsBridge::pumpMemorySnapshots() {
    if (!project_) return;
    auto& subs = service_.memorySubs();
    if (subs.empty()) return;

    const auto nowNs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    std::vector<std::uint8_t> buf;

    for (auto& [key, state] : subs) {
        SystemBase* sys = project_->findSystem(key.systemId);
        if (!sys) continue;
        MemorySnapshotTriple* triple = sys->memorySnapshot(key.type);
        if (!triple) continue;

        // Per-sub hz cap. 0 means no cap (run at uiIdle rate).
        if (state.hz > 0 && state.lastEmitNs != 0) {
            const std::uint64_t periodNs = 1000000000ULL / state.hz;
            if (nowNs - state.lastEmitNs < periodNs) continue;
        }

        if (!triple->readInto(buf)) continue;

        const std::uint64_t hash = rp::hash::fnv1a64(buf.data(), buf.size());
        // Skip when the snapshot hasn't changed AND at least one prior emit
        // has happened (so the very first sample always lands even if its
        // hash matches the zero-initialized lastHash).
        if (hash == state.lastHash && state.version != 0) continue;

        state.lastHash   = hash;
        state.lastEmitNs = nowNs;
        ++state.version;

        // Push a JSON-RPC notification through the rpcpp transport. pumpAsync
        // (called immediately after this method in PluginUI::uiIdle) drains
        // the transport into engine.emit("rpc-message", ab); the JS-side
        // rpcpp client decodes the frame, sees an isNotification envelope,
        // and dispatches to plugin.$on("memory", ...) subscribers. Keeps
        // this entire path off the QuickJS-direct API so the web port can
        // swap QueueTransport for a postMessage transport with no further
        // changes.
        MemoryNotificationPayload payload;
        payload.systemId = key.systemId;
        payload.type     = static_cast<std::uint32_t>(key.type);
        payload.bytes.resize(buf.size());
        if (!buf.empty()) {
            std::memcpy(payload.bytes.data(), buf.data(), buf.size());
        }
        payload.version  = state.version;
        rpc_.server().writeNotification("memory", payload);
    }
}
