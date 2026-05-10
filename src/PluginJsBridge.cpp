#include "PluginJsBridge.hpp"

#include <atomic>
#include <cstdio>
#include <fstream>
#include <memory>
#include <utility>
#include <vector>

extern "C" {
    #include <quickjs.h>
}

#include "project/Project.hpp"
#include "system/InputTypes.hpp"
#include "system/SystemBase.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "transport/CommandQueue.hpp"
#include "transport/EventQueue.hpp"
#include "transport/FrameBufferTriple.hpp"

namespace {

// File slurper. Runs on the UI thread (called from loadRomFromPath). Returns
// an empty vector on any failure; caller logs.
std::vector<std::uint8_t> slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const std::streamsize size = in.tellg();
    if (size <= 0) return {};
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(buf.data()), size))
        return {};
    return buf;
}

// Helper: resolve the bridge instance bound to the current LVGL display.
// Mirrors the existing pattern used by js_getFrame.
PluginJsBridge* bridgeFromContext() {
    DpfJsDisplayData* data = DpfJsDisplayData::get();
    return (data && data->bridge) ? static_cast<PluginJsBridge*>(data->bridge) : nullptr;
}

} // namespace

PluginJsBridge::PluginJsBridge(LvglJsEngine& eng,
                               Project* project,
                               CommandQueue* commands,
                               EventQueue* events,
                               std::atomic<double>* sampleRate)
    : engine(eng),
      project_(project),
      commands_(commands),
      events_(events),
      sampleRate_(sampleRate) {
    if (DpfJsDisplayData* data = DpfJsDisplayData::get())
        data->bridge = this;

    // Build globalThis[Symbol.for("plugin")] and attach the plugin's JS
    // surface: framebuffer access + ROM loading.
    JSContext* ctx = engine.getContext();
    if (!ctx) return;

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sym    = JS_NewSymbol(ctx, "plugin", true);
    JSAtom atom    = JS_ValueToAtom(ctx, sym);
    JSValue ns     = JS_NewObjectProto(ctx, JS_NULL);

    JS_DefinePropertyValue(ctx, global, atom, ns, JS_PROP_C_W_E);

    JS_SetPropertyStr(ctx, ns, "getFrame",
                      JS_NewCFunction(ctx, js_getFrame, "getFrame", 1));
    JS_SetPropertyStr(ctx, ns, "openRomBrowser",
                      JS_NewCFunction(ctx, js_openRomBrowser, "openRomBrowser", 0));
    JS_SetPropertyStr(ctx, ns, "loadRomFromPath",
                      JS_NewCFunction(ctx, js_loadRomFromPath, "loadRomFromPath", 1));
    JS_SetPropertyStr(ctx, ns, "pressButton",
                      JS_NewCFunction(ctx, js_pressButton, "pressButton", 2));

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

bool PluginJsBridge::loadRomFromPath(const std::string& path) {
    JSContext* ctx = engine.getContext();
    if (!project_ || !commands_ || !sampleRate_) {
        std::fprintf(stderr, "loadRomFromPath: shared DSP state unavailable (LV2-UI?)\n");
        return false;
    }

    std::vector<std::uint8_t> bytes = slurp(path);
    if (bytes.empty()) {
        std::fprintf(stderr, "loadRomFromPath: failed to read '%s'\n", path.c_str());
        if (ctx) {
            JSValue err = JS_NewString(ctx, path.c_str());
            engine.emit("rom-error", 1, &err);
            JS_FreeValue(ctx, err);
        }
        return false;
    }

    // Build the SystemConfig + SameBoySystem entirely on the UI thread.
    SameBoyConfig cfg;
    cfg.romPath = path;
    cfg.model   = GameboyModel::CgbC;
    cfg.fastBoot = true;

    const SystemId id = project_->nextSystemId();
    auto sys = std::make_unique<SameBoySystem>(id, cfg, std::move(bytes));

    const double sr = sampleRate_->load(std::memory_order_acquire);
    sys->onActivate(sr);

    // Hand off to the DSP. From this point the DSP owns the pointer.
    Command cmd = Command::makeLoadRom(sys.release());
    if (!commands_->tryPush(cmd)) {
        // Queue full — clean up locally rather than leaking.
        std::fprintf(stderr, "loadRomFromPath: command queue full\n");
        delete cmd.payload.loadRom.newSystem;
        if (ctx) {
            JSValue err = JS_NewString(ctx, path.c_str());
            engine.emit("rom-error", 1, &err);
            JS_FreeValue(ctx, err);
        }
        return false;
    }

    if (ctx) {
        JSValue ok = JS_NewString(ctx, path.c_str());
        engine.emit("rom-loaded", 1, &ok);
        JS_FreeValue(ctx, ok);
    }
    return true;
}

JSValue PluginJsBridge::js_getFrame(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    PluginJsBridge* self = bridgeFromContext();
    if (!self || !self->project_) return JS_NULL;

    int32_t systemIdInt = 0;
    if (argc >= 1) {
        if (JS_ToInt32(ctx, &systemIdInt, argv[0]) < 0)
            return JS_EXCEPTION;
    }

    SystemBase* sys = self->project_->findSystem(static_cast<SystemId>(systemIdInt));
    if (!sys) return JS_NULL;

    FrameBufferTriple* fb = sys->framebuffer();
    if (!fb) return JS_NULL;

    const uint32_t w      = fb->width();
    const uint32_t h      = fb->height();
    const size_t   pixels = size_t(w) * h;

    std::vector<uint32_t> staging(pixels, 0u);
    if (!fb->readInto(staging.data(), w * h))
        return JS_NULL;

    JSValue buf = JS_NewArrayBufferCopy(ctx,
        reinterpret_cast<const uint8_t*>(staging.data()),
        pixels * sizeof(uint32_t));
    if (JS_IsException(buf)) return buf;

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "width",  JS_NewUint32(ctx, w));
    JS_SetPropertyStr(ctx, obj, "height", JS_NewUint32(ctx, h));
    JS_SetPropertyStr(ctx, obj, "buffer", buf);
    return obj;
}

JSValue PluginJsBridge::js_openRomBrowser(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    PluginJsBridge* self = bridgeFromContext();
    if (!self || !self->openRomBrowser_) {
        std::fprintf(stderr, "plugin.openRomBrowser: no open-browser callback registered\n");
        return JS_FALSE;
    }
    self->openRomBrowser_();
    return JS_TRUE;
}

JSValue PluginJsBridge::js_pressButton(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    PluginJsBridge* self = bridgeFromContext();
    if (!self || !self->commands_ || !self->project_) return JS_FALSE;
    if (argc < 2) return JS_ThrowTypeError(ctx, "plugin.pressButton: expected (button, down)");

    int32_t buttonInt = 0;
    if (JS_ToInt32(ctx, &buttonInt, argv[0]) < 0) return JS_EXCEPTION;
    bool down = JS_ToBool(ctx, argv[1]) != 0;

    // Single-instance MVP: always route to slot 0. The system's id is
    // unstable across ROM swaps (each swap allocates a fresh id), so we
    // resolve at send time. Multi-instance focus tracking arrives at step 5.
    const auto& systems = self->project_->systems();
    if (systems.empty() || !systems.front()) return JS_FALSE;
    const SystemId id = systems.front()->id();

    Command cmd = Command::makeButtonPress(id,
                                           static_cast<GameboyButton>(buttonInt),
                                           down);
    return self->commands_->tryPush(cmd) ? JS_TRUE : JS_FALSE;
}

JSValue PluginJsBridge::js_loadRomFromPath(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "plugin.loadRomFromPath: expected path string");
    PluginJsBridge* self = bridgeFromContext();
    if (!self) return JS_FALSE;

    size_t len = 0;
    const char* cs = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!cs) return JS_EXCEPTION;
    std::string path(cs, len);
    JS_FreeCString(ctx, cs);

    return self->loadRomFromPath(path) ? JS_TRUE : JS_FALSE;
}
