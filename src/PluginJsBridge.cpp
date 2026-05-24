#include "PluginJsBridge.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "RpcEnvelope.h"
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

namespace {

// Resolve the bridge instance bound to the current LVGL display.
PluginJsBridge* bridgeFromContext() {
    DpfJsDisplayData* data = DpfJsDisplayData::get();
    return (data && data->bridge) ? static_cast<PluginJsBridge*>(data->bridge) : nullptr;
}

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
      project_(project) {
    if (DpfJsDisplayData* data = DpfJsDisplayData::get())
        data->bridge = this;

    // Stand up the rpcpp server stack. The service holds the shared-state
    // pointers from PluginUI; the transport buffers async/notification frames
    // for `pumpAsync` to fan out via engine.emit.
    rpcService_   = std::make_unique<PluginRpcService>(project, commands, events,
                                                       sampleRate, focusedSystemId,
                                                       userConfig, recentFiles);
    rpcTransport_ = std::make_unique<RpcTransport>();
    rpcServer_    = std::make_unique<RpcServer>(*rpcService_, *rpcTransport_);

    rpcServer_->addMethod<&PluginRpcService::getFrame>();
    rpcServer_->addMethod<&PluginRpcService::openRomBrowser>();
    rpcServer_->addMethod<&PluginRpcService::openSaveProjectBrowser>();
    rpcServer_->addMethod<&PluginRpcService::openLoadProjectBrowser>();
    rpcServer_->addMethod<&PluginRpcService::loadRomFromPath>();
    rpcServer_->addMethod<&PluginRpcService::addRomFromPath>();
    rpcServer_->addMethod<&PluginRpcService::replaceRomFromPath>();
    rpcServer_->addMethod<&PluginRpcService::removeSystem>();
    rpcServer_->addMethod<&PluginRpcService::duplicateSystem>();
    rpcServer_->addMethod<&PluginRpcService::clearCurrentProjectPath>();
    rpcServer_->addMethod<&PluginRpcService::listSystems>();
    rpcServer_->addMethod<&PluginRpcService::setFocus>();
    rpcServer_->addMethod<&PluginRpcService::getFocus>();
    rpcServer_->addMethod<&PluginRpcService::pressButton>();
    rpcServer_->addMethod<&PluginRpcService::setLinkGroupId>();
    rpcServer_->addMethod<&PluginRpcService::getMidiRouting>();
    rpcServer_->addMethod<&PluginRpcService::setMidiRouting>();
    rpcServer_->addMethod<&PluginRpcService::getAudioRouting>();
    rpcServer_->addMethod<&PluginRpcService::setAudioRouting>();
    rpcServer_->addMethod<&PluginRpcService::getZoom>();
    rpcServer_->addMethod<&PluginRpcService::setZoom>();
    rpcServer_->addMethod<&PluginRpcService::getLayout>();
    rpcServer_->addMethod<&PluginRpcService::setLayout>();
    rpcServer_->addMethod<&PluginRpcService::resetSystem>();
    rpcServer_->addMethod<&PluginRpcService::newSram>();
    rpcServer_->addMethod<&PluginRpcService::setFastBoot>();
    rpcServer_->addMethod<&PluginRpcService::setModel>();
    rpcServer_->addMethod<&PluginRpcService::setHighpass>();
    rpcServer_->addMethod<&PluginRpcService::setReloadOnRomChange>();
    rpcServer_->addMethod<&PluginRpcService::setLsdjSyncConfig>();
    rpcServer_->addMethod<&PluginRpcService::setWindowSize>();
    rpcServer_->addMethod<&PluginRpcService::isWindowSizeControlled>();
    rpcServer_->addMethod<&PluginRpcService::getKitsConfig>();
    rpcServer_->addMethod<&PluginRpcService::compileAndPatchKit>();
    rpcServer_->addMethod<&PluginRpcService::auditionSample>();
    rpcServer_->addMethod<&PluginRpcService::eraseKit>();
    rpcServer_->addMethod<&PluginRpcService::openSampleBrowser>();
    rpcServer_->addMethod<&PluginRpcService::getUserConfig>();
    rpcServer_->addMethod<&PluginRpcService::setActiveKeyboardBindings>();
    rpcServer_->addMethod<&PluginRpcService::setActiveGamepadBindings>();
    rpcServer_->addMethod<&PluginRpcService::openSettingsFolder>();
    rpcServer_->addMethod<&PluginRpcService::saveSram>();
    rpcServer_->addMethod<&PluginRpcService::openSaveSramBrowser>();
    rpcServer_->addMethod<&PluginRpcService::saveState>();
    rpcServer_->addMethod<&PluginRpcService::openSaveStateBrowser>();
    rpcServer_->addMethod<&PluginRpcService::openLoadStateBrowser>();
    rpcServer_->addMethod<&PluginRpcService::getRecentFiles>();
    rpcServer_->addMethod<&PluginRpcService::getMemory>();
    rpcServer_->addMethod<&PluginRpcService::subscribeMemory>();
    rpcServer_->addMethod<&PluginRpcService::unsubscribeMemory>();
    rpcServer_->addDiscoveryMethod();

    // Service emits string-payload JS events through the existing engine
    // channel mechanism (on/off in runtime/lvgljs/index.ts).
    rpcService_->setEmitEventCallback(
        [this](const std::string& channel, const std::string& payload) {
            JSContext* ctx = engine.getContext();
            if (!ctx) return;
            JSValue v = JS_NewStringLen(ctx, payload.data(), payload.size());
            engine.emit(channel.c_str(), 1, &v);
            JS_FreeValue(ctx, v);
        });

    // Expose the JS side's plugin namespace with the single sync RPC entry.
    JSContext* ctx = engine.getContext();
    if (!ctx) return;

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sym    = JS_NewSymbol(ctx, "plugin", true);
    JSAtom atom    = JS_ValueToAtom(ctx, sym);
    JSValue ns     = JS_NewObjectProto(ctx, JS_NULL);

    JS_DefinePropertyValue(ctx, global, atom, ns, JS_PROP_C_W_E);

    JS_SetPropertyStr(ctx, ns, "__rpcSend",
                      JS_NewCFunction(ctx, js_rpcSend, "__rpcSend", 1));
    JS_SetPropertyStr(ctx, ns, "__log",
                      JS_NewCFunction(ctx, js_log, "__log", 2));
    JS_SetPropertyStr(ctx, ns, "debugOverlay",
                      JS_NewBool(ctx, std::getenv("RETROPLUG_DEBUG_OVERLAY") != nullptr));

    pluginNamespace = JS_DupValue(ctx, ns);

    JS_FreeAtom(ctx, atom);
    JS_FreeValue(ctx, sym);
    JS_FreeValue(ctx, global);
}

PluginJsBridge::~PluginJsBridge() {
    if (JSContext* ctx = engine.getContext(); ctx && !JS_IsUndefined(pluginNamespace)) {
        JS_FreeValue(ctx, pluginNamespace);
        pluginNamespace = JS_UNDEFINED;
    }
    if (DpfJsDisplayData* data = DpfJsDisplayData::get()) {
        if (data->bridge == this)
            data->bridge = nullptr;
    }
}

JSValue PluginJsBridge::js_rpcSend(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    PluginJsBridge* self = bridgeFromContext();
    if (!self || !self->rpcServer_)
        return JS_ThrowInternalError(ctx, "plugin.__rpcSend: bridge unavailable");
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "plugin.__rpcSend: expected (bytes: Uint8Array)");

    // Accept either a TypedArray view (Uint8Array, the common case from the
    // JS client) or a raw ArrayBuffer. JS_GetTypedArrayBuffer returns the
    // underlying ArrayBuffer plus the view's offset/length so we don't
    // accidentally read past the slice.
    size_t byteOffset = 0;
    size_t byteLength = 0;
    size_t arrayLen   = 0;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[0],
                                        &byteOffset, &byteLength, nullptr);
    uint8_t* data = nullptr;
    if (!JS_IsException(ab)) {
        data = JS_GetArrayBuffer(ctx, &arrayLen, ab);
    } else {
        JS_FreeValue(ctx, ab);
        data = JS_GetArrayBuffer(ctx, &arrayLen, argv[0]);
        byteOffset = 0;
        byteLength = arrayLen;
        ab = JS_DupValue(ctx, argv[0]);
    }
    if (!data) {
        JS_FreeValue(ctx, ab);
        return JS_ThrowTypeError(ctx, "plugin.__rpcSend: argument is not bytes");
    }

    std::span<const char> bytes(reinterpret_cast<const char*>(data + byteOffset),
                                byteLength);
    auto reply = self->rpcServer_->processMessage(bytes);
    JS_FreeValue(ctx, ab);

    if (!reply) return JS_NULL;

    // Surface server-side JSON-RPC error envelopes on stderr. The JS
    // client drops error replies whose id is null/undefined (which is
    // every notification reply), so without this hook a typed-handler
    // exception in C++ shows up as "nothing happens" on the UI side.
    if (auto err = rpcpp::MsgpackCodec::read<rpcpp::RpcError>(
            std::span<const char>{reply->data(), reply->size()});
        err) {
        std::fprintf(stderr, "[rpc] error %d: %s\n",
                     err->error.code, err->error.message.c_str());
    }

    return JS_NewArrayBufferCopy(ctx,
        reinterpret_cast<const uint8_t*>(reply->data()),
        reply->size());
}

JSValue PluginJsBridge::js_log(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const char* level = (argc >= 1) ? JS_ToCString(ctx, argv[0]) : nullptr;
    const char* msg   = (argc >= 2) ? JS_ToCString(ctx, argv[1]) : nullptr;
    std::fprintf(stderr, "[js:%s] %s\n",
                 level ? level : "log",
                 msg   ? msg   : "");
    if (level) JS_FreeCString(ctx, level);
    if (msg)   JS_FreeCString(ctx, msg);
    return JS_UNDEFINED;
}

void PluginJsBridge::pumpAsync() {
    if (!rpcTransport_) return;
    JSContext* ctx = engine.getContext();
    if (!ctx) return;
    while (auto frame = rpcTransport_->tryReceive()) {
        JSValue ab = JS_NewArrayBufferCopy(ctx,
            reinterpret_cast<const uint8_t*>(frame->data()),
            frame->size());
        engine.emit("rpc-message", 1, &ab);
        JS_FreeValue(ctx, ab);
    }
}

void PluginJsBridge::pumpMemorySnapshots() {
    if (!rpcService_ || !rpcServer_ || !project_) return;
    auto& subs = rpcService_->memorySubs();
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
        rpcServer_->writeNotification("memory", payload);
    }
}
