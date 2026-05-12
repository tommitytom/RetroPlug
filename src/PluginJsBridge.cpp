#include "PluginJsBridge.hpp"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

extern "C" {
    #include <quickjs.h>
}

#include "project/Project.hpp"
#include "project/ProjectSerialization.hpp"
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

// Read a file as a UTF-8 string. Returns empty string on any failure.
std::string slurpString(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const std::streamsize size = in.tellg();
    if (size <= 0) return {};
    in.seekg(0, std::ios::beg);
    std::string out(static_cast<std::size_t>(size), '\0');
    if (!in.read(out.data(), size))
        return {};
    return out;
}

// Write a string to a file (binary mode — JSON is UTF-8 ASCII anyway).
// Returns true on success.
bool spillString(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return out.good();
}

// Helper: resolve the bridge instance bound to the current LVGL display.
// Mirrors the existing pattern used by js_getFrame.
PluginJsBridge* bridgeFromContext() {
    DpfJsDisplayData* data = DpfJsDisplayData::get();
    return (data && data->bridge) ? static_cast<PluginJsBridge*>(data->bridge) : nullptr;
}

void emitRomEvent(LvglJsEngine& engine, const char* event, const std::string& payload) {
    JSContext* ctx = engine.getContext();
    if (!ctx) return;
    JSValue v = JS_NewString(ctx, payload.c_str());
    engine.emit(event, 1, &v);
    JS_FreeValue(ctx, v);
}

} // namespace

PluginJsBridge::PluginJsBridge(LvglJsEngine& eng,
                               Project* project,
                               CommandQueue* commands,
                               EventQueue* events,
                               std::atomic<double>* sampleRate,
                               std::atomic<SystemId>* focusedSystemId)
    : engine(eng),
      project_(project),
      commands_(commands),
      events_(events),
      sampleRate_(sampleRate),
      focusedSystemId_(focusedSystemId) {
    if (DpfJsDisplayData* data = DpfJsDisplayData::get())
        data->bridge = this;

    // Build globalThis[Symbol.for("plugin")] and attach the plugin's JS
    // surface: framebuffer access + ROM loading + multi-instance plumbing.
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
    JS_SetPropertyStr(ctx, ns, "openSaveProjectBrowser",
                      JS_NewCFunction(ctx, js_openSaveProjectBrowser, "openSaveProjectBrowser", 0));
    JS_SetPropertyStr(ctx, ns, "openLoadProjectBrowser",
                      JS_NewCFunction(ctx, js_openLoadProjectBrowser, "openLoadProjectBrowser", 0));
    JS_SetPropertyStr(ctx, ns, "loadRomFromPath",
                      JS_NewCFunction(ctx, js_loadRomFromPath, "loadRomFromPath", 1));
    JS_SetPropertyStr(ctx, ns, "addRomFromPath",
                      JS_NewCFunction(ctx, js_addRomFromPath, "addRomFromPath", 1));
    JS_SetPropertyStr(ctx, ns, "replaceRomFromPath",
                      JS_NewCFunction(ctx, js_replaceRomFromPath, "replaceRomFromPath", 2));
    JS_SetPropertyStr(ctx, ns, "removeSystem",
                      JS_NewCFunction(ctx, js_removeSystem, "removeSystem", 1));
    JS_SetPropertyStr(ctx, ns, "listSystems",
                      JS_NewCFunction(ctx, js_listSystems, "listSystems", 0));
    JS_SetPropertyStr(ctx, ns, "setFocus",
                      JS_NewCFunction(ctx, js_setFocus, "setFocus", 1));
    JS_SetPropertyStr(ctx, ns, "getFocus",
                      JS_NewCFunction(ctx, js_getFocus, "getFocus", 0));
    JS_SetPropertyStr(ctx, ns, "pressButton",
                      JS_NewCFunction(ctx, js_pressButton, "pressButton", 3));
    JS_SetPropertyStr(ctx, ns, "setLinkGroupId",
                      JS_NewCFunction(ctx, js_setLinkGroupId, "setLinkGroupId", 2));
    JS_SetPropertyStr(ctx, ns, "getMidiRouting",
                      JS_NewCFunction(ctx, js_getMidiRouting, "getMidiRouting", 0));
    JS_SetPropertyStr(ctx, ns, "setMidiRouting",
                      JS_NewCFunction(ctx, js_setMidiRouting, "setMidiRouting", 1));
    JS_SetPropertyStr(ctx, ns, "setWindowSize",
                      JS_NewCFunction(ctx, js_setWindowSize, "setWindowSize", 2));
    JS_SetPropertyStr(ctx, ns, "isWindowSizeControlled",
                      JS_NewCFunction(ctx, js_isWindowSizeControlled, "isWindowSizeControlled", 0));

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

SameBoySystem* PluginJsBridge::buildSystemFromPath(const std::string& path) {
    if (!project_ || !sampleRate_) {
        std::fprintf(stderr, "buildSystemFromPath: shared DSP state unavailable (LV2-UI?)\n");
        return nullptr;
    }
    std::vector<std::uint8_t> bytes = slurp(path);
    if (bytes.empty()) {
        std::fprintf(stderr, "buildSystemFromPath: failed to read '%s'\n", path.c_str());
        emitRomEvent(engine, "rom-error", path);
        return nullptr;
    }

    SameBoyConfig cfg;
    cfg.romPath  = path;
    cfg.model    = GameboyModel::CgbC;
    cfg.fastBoot = true;

    // Optional sibling .sav (cartridge battery RAM). Slurp once on path-
    // based load — from here on the SRAM lives in cfg.sram and rides with
    // the project state. Missing file is not an error; the cart just starts
    // with blank battery, like in any standalone emulator.
    {
        std::filesystem::path sav = std::filesystem::path(path);
        sav.replace_extension(".sav");
        std::vector<std::uint8_t> sramBytes = slurp(sav.string());
        if (!sramBytes.empty())
            cfg.sram = Base64Bytes(std::move(sramBytes));
    }

    const SystemId id = project_->nextSystemId();
    auto sys = std::make_unique<SameBoySystem>(id, cfg, std::move(bytes));
    const double sr = sampleRate_->load(std::memory_order_acquire);
    sys->onActivate(sr);
    return sys.release();
}

bool PluginJsBridge::loadRomFromPath(const std::string& path) {
    if (!commands_) {
        emitRomEvent(engine, "rom-error", path);
        return false;
    }
    SameBoySystem* sys = buildSystemFromPath(path);
    if (!sys) return false;

    if (!commands_->tryPush(Command::makeLoadRom(sys))) {
        std::fprintf(stderr, "loadRomFromPath: command queue full\n");
        delete sys;
        emitRomEvent(engine, "rom-error", path);
        return false;
    }
    emitRomEvent(engine, "rom-loaded", path);
    return true;
}

bool PluginJsBridge::addRomFromPath(const std::string& path) {
    if (!commands_) {
        emitRomEvent(engine, "rom-error", path);
        return false;
    }
    SameBoySystem* sys = buildSystemFromPath(path);
    if (!sys) return false;

    if (!commands_->tryPush(Command::makeAddSystem(sys))) {
        std::fprintf(stderr, "addRomFromPath: command queue full\n");
        delete sys;
        emitRomEvent(engine, "rom-error", path);
        return false;
    }
    emitRomEvent(engine, "rom-loaded", path);
    return true;
}

bool PluginJsBridge::saveProjectToPath(const std::string& path) {
    if (!project_) {
        emitRomEvent(engine, "project-error", path);
        return false;
    }
    // UI thread reads project_; same accepted race as listSystems / getFrame.
    // Snapshotting the entire project (including each system's GB savestate)
    // while audio runs could in principle pick up a torn frame, but in
    // practice the data is consistent enough for debug round-trips. If this
    // ever bites, swap to a command-based snapshot.
    std::string json;
    try {
        json = projectConfigToJson(project_->snapshotConfig());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "saveProjectToPath: serialize failed: %s\n", e.what());
        emitRomEvent(engine, "project-error", path);
        return false;
    }
    if (!spillString(path, json)) {
        std::fprintf(stderr, "saveProjectToPath: write failed for '%s'\n", path.c_str());
        emitRomEvent(engine, "project-error", path);
        return false;
    }
    emitRomEvent(engine, "project-saved", path);
    return true;
}

bool PluginJsBridge::loadProjectFromPath(const std::string& path) {
    if (!commands_) {
        emitRomEvent(engine, "project-error", path);
        return false;
    }
    std::string json = slurpString(path);
    if (json.empty()) {
        std::fprintf(stderr, "loadProjectFromPath: empty / unreadable '%s'\n", path.c_str());
        emitRomEvent(engine, "project-error", path);
        return false;
    }
    // Heap-allocate the JSON string, transfer ownership to the DSP via the
    // command queue. The DSP frees it after parsing.
    auto* heap = new std::string(std::move(json));
    if (!commands_->tryPush(Command::makeLoadProject(heap))) {
        std::fprintf(stderr, "loadProjectFromPath: command queue full\n");
        delete heap;
        emitRomEvent(engine, "project-error", path);
        return false;
    }
    emitRomEvent(engine, "project-loaded", path);
    return true;
}

bool PluginJsBridge::replaceRomFromPath(SystemId id, const std::string& path) {
    if (!commands_) {
        emitRomEvent(engine, "rom-error", path);
        return false;
    }
    SameBoySystem* sys = buildSystemFromPath(path);
    if (!sys) return false;

    if (!commands_->tryPush(Command::makeReplaceSystem(id, sys))) {
        std::fprintf(stderr, "replaceRomFromPath: command queue full\n");
        delete sys;
        emitRomEvent(engine, "rom-error", path);
        return false;
    }
    emitRomEvent(engine, "rom-loaded", path);
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

JSValue PluginJsBridge::js_openRomBrowser(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    PluginJsBridge* self = bridgeFromContext();
    if (!self || !self->openFileBrowser_) {
        std::fprintf(stderr, "plugin.openRomBrowser: no open-browser callback registered\n");
        return JS_FALSE;
    }
    // Optional first arg: { mode: "add" | "replace" }. Defaults to replace.
    self->pendingFileMode_ = PendingFileMode::LoadRom;
    if (argc >= 1 && JS_IsObject(argv[0])) {
        JSValue modeVal = JS_GetPropertyStr(ctx, argv[0], "mode");
        if (JS_IsString(modeVal)) {
            const char* s = JS_ToCString(ctx, modeVal);
            if (s) {
                if (std::string_view(s) == "add")
                    self->pendingFileMode_ = PendingFileMode::AddRom;
                JS_FreeCString(ctx, s);
            }
        }
        JS_FreeValue(ctx, modeVal);
    }
    self->openFileBrowser_("Open Game Boy ROM", false, nullptr);
    return JS_TRUE;
}

JSValue PluginJsBridge::js_openSaveProjectBrowser(JSContext*, JSValueConst, int, JSValueConst*) {
    PluginJsBridge* self = bridgeFromContext();
    if (!self || !self->openFileBrowser_) return JS_FALSE;
    self->pendingFileMode_ = PendingFileMode::SaveProject;
    self->openFileBrowser_("Save RetroPlug project", true, "project.rplg");
    return JS_TRUE;
}

JSValue PluginJsBridge::js_openLoadProjectBrowser(JSContext*, JSValueConst, int, JSValueConst*) {
    PluginJsBridge* self = bridgeFromContext();
    if (!self || !self->openFileBrowser_) return JS_FALSE;
    self->pendingFileMode_ = PendingFileMode::LoadProject;
    self->openFileBrowser_("Load RetroPlug project", false, nullptr);
    return JS_TRUE;
}

void PluginJsBridge::onFileBrowserSelected(const char* path) {
    if (!path || !*path) return;
    switch (pendingFileMode_) {
        case PendingFileMode::AddRom:      addRomFromPath(path);     break;
        case PendingFileMode::LoadProject: loadProjectFromPath(path); break;
        case PendingFileMode::SaveProject: saveProjectToPath(path);   break;
        case PendingFileMode::LoadRom:
        default:                           loadRomFromPath(path);    break;
    }
    pendingFileMode_ = PendingFileMode::LoadRom;
}

JSValue PluginJsBridge::js_pressButton(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    PluginJsBridge* self = bridgeFromContext();
    if (!self || !self->commands_ || !self->project_) return JS_FALSE;
    if (argc < 2) return JS_ThrowTypeError(ctx, "plugin.pressButton: expected (button, down [, systemId])");

    int32_t buttonInt = 0;
    if (JS_ToInt32(ctx, &buttonInt, argv[0]) < 0) return JS_EXCEPTION;
    const bool down = JS_ToBool(ctx, argv[1]) != 0;

    // Resolve target system: explicit arg wins, then focused, then first.
    SystemId target = 0;
    if (argc >= 3 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2])) {
        int32_t idInt = 0;
        if (JS_ToInt32(ctx, &idInt, argv[2]) < 0) return JS_EXCEPTION;
        target = static_cast<SystemId>(idInt);
    }
    if (target == 0 && self->focusedSystemId_)
        target = self->focusedSystemId_->load(std::memory_order_acquire);
    if (target == 0) {
        const auto& systems = self->project_->systems();
        if (systems.empty() || !systems.front()) return JS_FALSE;
        target = systems.front()->id();
    }

    Command cmd = Command::makeButtonPress(target,
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

JSValue PluginJsBridge::js_addRomFromPath(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "plugin.addRomFromPath: expected path string");
    PluginJsBridge* self = bridgeFromContext();
    if (!self) return JS_FALSE;

    size_t len = 0;
    const char* cs = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!cs) return JS_EXCEPTION;
    std::string path(cs, len);
    JS_FreeCString(ctx, cs);

    return self->addRomFromPath(path) ? JS_TRUE : JS_FALSE;
}

JSValue PluginJsBridge::js_replaceRomFromPath(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "plugin.replaceRomFromPath: expected (id, path)");
    PluginJsBridge* self = bridgeFromContext();
    if (!self) return JS_FALSE;

    int32_t idInt = 0;
    if (JS_ToInt32(ctx, &idInt, argv[0]) < 0) return JS_EXCEPTION;

    size_t len = 0;
    const char* cs = JS_ToCStringLen(ctx, &len, argv[1]);
    if (!cs) return JS_EXCEPTION;
    std::string path(cs, len);
    JS_FreeCString(ctx, cs);

    return self->replaceRomFromPath(static_cast<SystemId>(idInt), path) ? JS_TRUE : JS_FALSE;
}

JSValue PluginJsBridge::js_removeSystem(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "plugin.removeSystem: expected (id)");
    PluginJsBridge* self = bridgeFromContext();
    if (!self || !self->commands_) return JS_FALSE;

    int32_t idInt = 0;
    if (JS_ToInt32(ctx, &idInt, argv[0]) < 0) return JS_EXCEPTION;

    return self->commands_->tryPush(Command::makeRemoveSystem(static_cast<SystemId>(idInt)))
        ? JS_TRUE
        : JS_FALSE;
}

JSValue PluginJsBridge::js_listSystems(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    PluginJsBridge* self = bridgeFromContext();
    if (!self || !self->project_) return JS_NewArray(ctx);

    JSValue arr = JS_NewArray(ctx);
    uint32_t i = 0;
    for (const auto& sys : self->project_->systems()) {
        if (!sys) continue;
        JSValue entry = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, entry, "id", JS_NewUint32(ctx, sys->id()));

        // Pull config fields when available. Other system kinds (future Mesen)
        // get id-only.
        if (auto* sb = dynamic_cast<const SameBoySystem*>(sys.get())) {
            JS_SetPropertyStr(ctx, entry, "kind",        JS_NewString(ctx, "sameboy"));
            JS_SetPropertyStr(ctx, entry, "gainDb",      JS_NewFloat64(ctx, sb->config_.gainDb));
            JS_SetPropertyStr(ctx, entry, "linkGroupId", JS_NewUint32(ctx, sb->config_.linkGroupId));
        }
        JS_SetPropertyUint32(ctx, arr, i++, entry);
    }
    return arr;
}

JSValue PluginJsBridge::js_setFocus(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "plugin.setFocus: expected (id)");
    PluginJsBridge* self = bridgeFromContext();
    if (!self || !self->focusedSystemId_) return JS_FALSE;

    int32_t idInt = 0;
    if (JS_ToInt32(ctx, &idInt, argv[0]) < 0) return JS_EXCEPTION;
    self->focusedSystemId_->store(static_cast<SystemId>(idInt), std::memory_order_release);
    return JS_TRUE;
}

JSValue PluginJsBridge::js_getFocus(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    PluginJsBridge* self = bridgeFromContext();
    if (!self || !self->focusedSystemId_) return JS_NewUint32(ctx, 0);
    return JS_NewUint32(ctx, self->focusedSystemId_->load(std::memory_order_acquire));
}

JSValue PluginJsBridge::js_setLinkGroupId(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "plugin.setLinkGroupId: expected (id, groupId)");
    PluginJsBridge* self = bridgeFromContext();
    if (!self || !self->commands_) return JS_FALSE;

    int32_t id = 0, groupId = 0;
    if (JS_ToInt32(ctx, &id,      argv[0]) < 0) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &groupId, argv[1]) < 0) return JS_EXCEPTION;
    if (groupId < 0 || groupId > 255) return JS_FALSE;

    return self->commands_->tryPush(
        Command::makeSetLinkGroup(static_cast<SystemId>(id),
                                  static_cast<std::uint8_t>(groupId)))
        ? JS_TRUE : JS_FALSE;
}

JSValue PluginJsBridge::js_getMidiRouting(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    PluginJsBridge* self = bridgeFromContext();
    if (!self || !self->project_)
        return JS_NewUint32(ctx, static_cast<std::uint32_t>(MidiRouting::SendToAll));
    return JS_NewUint32(ctx,
        static_cast<std::uint32_t>(self->project_->config().settings.midiRouting));
}

JSValue PluginJsBridge::js_setMidiRouting(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "plugin.setMidiRouting: expected (routing)");
    PluginJsBridge* self = bridgeFromContext();
    if (!self || !self->commands_) return JS_FALSE;

    int32_t r = 0;
    if (JS_ToInt32(ctx, &r, argv[0]) < 0) return JS_EXCEPTION;
    // Reject out-of-range values rather than narrowing into a meaningful enum
    // by accident — the JS side needs to stay in sync with the C++ enum.
    if (r < 0 || r > static_cast<int32_t>(MidiRouting::MidiChannelToInstance))
        return JS_FALSE;

    return self->commands_->tryPush(
        Command::makeSetMidiRouting(static_cast<MidiRouting>(r)))
        ? JS_TRUE : JS_FALSE;
}

JSValue PluginJsBridge::js_setWindowSize(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "plugin.setWindowSize: expected (w, h)");
    PluginJsBridge* self = bridgeFromContext();
    if (!self || !self->setWindowSize_) return JS_FALSE;

    int32_t w = 0, h = 0;
    if (JS_ToInt32(ctx, &w, argv[0]) < 0) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &h, argv[1]) < 0) return JS_EXCEPTION;
    if (w <= 0 || h <= 0) return JS_FALSE;

    self->setWindowSize_(static_cast<unsigned>(w), static_cast<unsigned>(h));
    return JS_TRUE;
}

JSValue PluginJsBridge::js_isWindowSizeControlled(JSContext*, JSValueConst, int, JSValueConst*) {
    PluginJsBridge* self = bridgeFromContext();
    if (!self || !self->isWindowSizeControlled_) return JS_FALSE;
    return self->isWindowSizeControlled_() ? JS_TRUE : JS_FALSE;
}
