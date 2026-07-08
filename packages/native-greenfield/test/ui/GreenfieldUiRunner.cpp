// retroplug-greenfield-ui-test: a TypeScript-bundle runner for the greenfield headless UI harness.
//
// The greenfield twin of packages/native/test/ui/UiTsRunner.cpp, but wired to the GREENFIELD test
// conventions (like native-greenfield-host): the TS harness self-reports TAP via console.log and sets
// the exit code through globalThis.tjs.exit — so this runner just boots the harness, installs the
// `retroplug-ui` (`ui.*`) namespace + the tjs.exit hook, evals the test bundle, and drives the JS job
// loop until the harness exits. The harness boots the greenfield React UI on a headless software LVGL
// display (RenderCore) driven by the BackendFacade RPC (GreenfieldUiHarness). Only the render-tree
// surface is exposed; system state is driven through the stores over the bound BackendFacade RPC.
//
//   retroplug-greenfield-ui-test --test <bundle.js>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "GreenfieldUiHarness.hpp"

extern "C" {
    #include <quickjs.h>   // JS_* (the runtime itself is owned by the harness's engine)
}

namespace {

std::unique_ptr<rpuigf::GreenfieldUiHarness> g_harness;

// The exit code the harness reports through globalThis.tjs.exit(). One runner per process,
// single-threaded (mirrors native-greenfield-host's g_exit).
struct ExitState { int code = 0; bool set = false; };
ExitState g_exit;

JSValue jsExit(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int code = 0;
    if (argc >= 1) JS_ToInt32(ctx, &code, argv[0]);
    g_exit.code = code;
    g_exit.set = true;
    return JS_UNDEFINED;
}

std::string slurpText(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

rpuigf::RenderCore& coreOrThrow() {
    if (!g_harness) throw std::runtime_error("ui.boot() must be called first");
    return g_harness->core();
}

JSValue widgetInfoToJs(JSContext* ctx, const rpuigf::WidgetInfo& wi) {
    if (!wi.found) return JS_NULL;
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "x",          JS_NewInt32(ctx, wi.x));
    JS_SetPropertyStr(ctx, o, "y",          JS_NewInt32(ctx, wi.y));
    JS_SetPropertyStr(ctx, o, "width",      JS_NewInt32(ctx, wi.width));
    JS_SetPropertyStr(ctx, o, "height",     JS_NewInt32(ctx, wi.height));
    JS_SetPropertyStr(ctx, o, "childCount", JS_NewUint32(ctx, wi.childCount));
    JS_SetPropertyStr(ctx, o, "state",      JS_NewUint32(ctx, wi.state));
    JS_SetPropertyStr(ctx, o, "text",       JS_NewStringLen(ctx, wi.text.data(), wi.text.size()));
    return o;
}

// -- ui trampolines (Symbol.for("retroplug-ui")) — the render-tree surface ----
JSValue jsUiBoot(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewBool(ctx, g_harness != nullptr);  // booted by the runner before eval
}

JSValue jsUiPump(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int iterations = 30;
    if (argc >= 1 && !JS_IsUndefined(argv[0])) JS_ToInt32(ctx, &iterations, argv[0]);
    try { coreOrThrow().pump(iterations); return JS_UNDEFINED; }
    catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.pump: %s", e.what()); }
}

JSValue jsUiReopen(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    try { coreOrThrow().reopenEditor(); return JS_UNDEFINED; }
    catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.reopen: %s", e.what()); }
}

JSValue jsUiSnapshot(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    try {
        const rpuigf::Snapshot s = coreOrThrow().snapshot();
        JSValue o = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, o, "width",  JS_NewUint32(ctx, s.width));
        JS_SetPropertyStr(ctx, o, "height", JS_NewUint32(ctx, s.height));
        JS_SetPropertyStr(ctx, o, "pixels", JS_NewArrayBufferCopy(ctx, s.argb.data(), s.argb.size()));
        return o;
    } catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.snapshot: %s", e.what()); }
}

JSValue jsUiSnapshotPng(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "ui.snapshotPng(path)");
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    try {
        const bool ok = coreOrThrow().snapshotPng(path);
        JS_FreeCString(ctx, path);
        return JS_NewBool(ctx, ok);
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "ui.snapshotPng: %s", e.what());
        JS_FreeCString(ctx, path);
        return err;
    }
}

JSValue jsUiWidgetCount(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    try { return JS_NewInt64(ctx, (int64_t)coreOrThrow().widgetCount()); }
    catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.widgetCount: %s", e.what()); }
}

JSValue jsUiCountByType(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int t = 0;
    if (argc >= 1) JS_ToInt32(ctx, &t, argv[0]);
    try { return JS_NewInt64(ctx, (int64_t)coreOrThrow().countByType(t)); }
    catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.countByType: %s", e.what()); }
}

JSValue jsUiFindByTestId(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "ui.findByTestId(name)");
    const char* name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    try {
        auto& c = coreOrThrow();
        JSValue r = widgetInfoToJs(ctx, c.widgetInfo(c.findByTestId(name)));
        JS_FreeCString(ctx, name);
        return r;
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "ui.findByTestId: %s", e.what());
        JS_FreeCString(ctx, name);
        return err;
    }
}

JSValue jsUiFindByText(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "ui.findByText(text)");
    const char* text = JS_ToCString(ctx, argv[0]);
    if (!text) return JS_EXCEPTION;
    try {
        auto& c = coreOrThrow();
        JSValue r = widgetInfoToJs(ctx, c.widgetInfo(c.findByText(text)));
        JS_FreeCString(ctx, text);
        return r;
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "ui.findByText: %s", e.what());
        JS_FreeCString(ctx, text);
        return err;
    }
}

JSValue jsUiFindByTextContaining(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "ui.findByTextContaining(substr)");
    const char* s = JS_ToCString(ctx, argv[0]);
    if (!s) return JS_EXCEPTION;
    try {
        auto& c = coreOrThrow();
        JSValue r = widgetInfoToJs(ctx, c.widgetInfo(c.findByTextContaining(s)));
        JS_FreeCString(ctx, s);
        return r;
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "ui.findByTextContaining: %s", e.what());
        JS_FreeCString(ctx, s);
        return err;
    }
}

JSValue jsUiFindFirstByType(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int t = 0;
    if (argc >= 1) JS_ToInt32(ctx, &t, argv[0]);
    try {
        auto& c = coreOrThrow();
        return widgetInfoToJs(ctx, c.widgetInfo(c.findFirstByType(t)));
    } catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.findFirstByType: %s", e.what()); }
}

JSValue jsUiFocused(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    try {
        auto& c = coreOrThrow();
        return widgetInfoToJs(ctx, c.widgetInfo(c.focusedObject()));
    } catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.focused: %s", e.what()); }
}

JSValue jsUiTapKey(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int key = 0;
    if (argc < 1 || JS_ToInt32(ctx, &key, argv[0]) < 0) return JS_EXCEPTION;
    try { coreOrThrow().tapKey(static_cast<std::uint32_t>(key)); return JS_UNDEFINED; }
    catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.tapKey: %s", e.what()); }
}

JSValue jsUiClickAt(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int x = 0, y = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "ui.clickAt(x, y)");
    if (JS_ToInt32(ctx, &x, argv[0]) < 0 || JS_ToInt32(ctx, &y, argv[1]) < 0) return JS_EXCEPTION;
    try { coreOrThrow().clickAt(x, y); return JS_UNDEFINED; }
    catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.clickAt: %s", e.what()); }
}

JSValue jsUiMoveMouse(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int x = 0, y = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "ui.moveMouse(x, y)");
    if (JS_ToInt32(ctx, &x, argv[0]) < 0 || JS_ToInt32(ctx, &y, argv[1]) < 0) return JS_EXCEPTION;
    try { coreOrThrow().moveMouse(x, y); return JS_UNDEFINED; }
    catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.moveMouse: %s", e.what()); }
}

// Advance the emulator so tiles get live frames (the plugin's audio thread does this; pump() only ticks
// LVGL). Drives the BackendFacade the UI reads over RPC.
JSValue jsUiAdvance(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    double ms = 0;
    if (argc >= 1) JS_ToFloat64(ctx, &ms, argv[0]);
    if (!g_harness) return JS_ThrowTypeError(ctx, "ui.advance: boot first");
    try { g_harness->advance(ms); return JS_UNDEFINED; }
    catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.advance: %s", e.what()); }
}

void installUiNamespace(JSContext* ctx) {
    const std::vector<std::pair<const char*, std::pair<JSCFunction*, int>>> fns = {
        { "boot",                 { jsUiBoot,                 0 } },
        { "pump",                 { jsUiPump,                 1 } },
        { "reopen",               { jsUiReopen,               0 } },
        { "snapshot",             { jsUiSnapshot,             0 } },
        { "snapshotPng",          { jsUiSnapshotPng,          1 } },
        { "widgetCount",          { jsUiWidgetCount,          0 } },
        { "countByType",          { jsUiCountByType,          1 } },
        { "findByTestId",         { jsUiFindByTestId,         1 } },
        { "findByText",           { jsUiFindByText,           1 } },
        { "findByTextContaining", { jsUiFindByTextContaining, 1 } },
        { "findFirstByType",      { jsUiFindFirstByType,      1 } },
        { "focused",              { jsUiFocused,              0 } },
        { "tapKey",               { jsUiTapKey,               1 } },
        { "clickAt",              { jsUiClickAt,              2 } },
        { "moveMouse",            { jsUiMoveMouse,            2 } },
        { "advance",              { jsUiAdvance,              1 } },
    };
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sym    = JS_NewSymbol(ctx, "retroplug-ui", /*is_global*/ true);
    JSAtom atom    = JS_ValueToAtom(ctx, sym);
    JSValue ns     = JS_NewObjectProto(ctx, JS_NULL);
    for (const auto& [name, fnArgc] : fns)
        JS_SetPropertyStr(ctx, ns, name, JS_NewCFunction(ctx, fnArgc.first, name, fnArgc.second));
    JS_DefinePropertyValue(ctx, global, atom, ns, JS_PROP_C_W_E);
    JS_FreeAtom(ctx, atom);
    JS_FreeValue(ctx, sym);
    JS_FreeValue(ctx, global);
}

// globalThis.tjs.exit — the greenfield harness sets the exit code through it (mirrors
// native-greenfield-host). Override any txiki-provided exit so we record the code and return it.
void installExitHook(JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue tjsObj = JS_GetPropertyStr(ctx, global, "tjs");
    if (JS_IsUndefined(tjsObj) || JS_IsNull(tjsObj)) {
        JS_FreeValue(ctx, tjsObj);
        tjsObj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, global, "tjs", JS_DupValue(ctx, tjsObj));
    }
    JS_SetPropertyStr(ctx, tjsObj, "exit", JS_NewCFunction(ctx, jsExit, "exit", 1));
    JS_FreeValue(ctx, tjsObj);
    JS_FreeValue(ctx, global);
}

int runUiTestFile(const std::string& jsPath) {
    // Boot the harness FIRST: it owns the single JS runtime (LvglJsEngine) + the headless display +
    // the BackendFacade RPC bridge + the greenfield UI bundle. The test bundle runs IN this runtime.
    try {
        g_harness = std::make_unique<rpuigf::GreenfieldUiHarness>();
        if (!g_harness->boot()) { std::fprintf(stderr, "greenfield UI harness boot failed\n"); return 1; }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "greenfield UI harness boot: %s\n", e.what());
        return 1;
    }

    JSContext* ctx = g_harness->core().engineContext();
    if (!ctx) { std::fprintf(stderr, "no engine context\n"); return 1; }

    installUiNamespace(ctx);
    installExitHook(ctx);

    std::string code;
    try { code = slurpText(jsPath); }
    catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }

    const int rc = g_harness->core().evalTestModule(code.data(), code.size(), jsPath.c_str());
    if (rc != 0) { std::fprintf(stderr, "test module evaluation failed\n"); return 1; }

    // Drive the JS job loop until the harness's microtask + tests run and call tjs.exit (ES modules
    // evaluate async in QuickJS, so the module body runs here). The tests render via ui.pump() (which
    // drives LVGL). Bounded.
    for (int i = 0; i < 20000 && !g_exit.set; ++i) g_harness->core().engine().host().pump();

    g_harness.reset(); // tear down display + engine + UI bundle cleanly

    if (!g_exit.set) {
        std::fprintf(stderr, "tests did not complete (tjs.exit never called)\n");
        return 1;
    }
    return g_exit.code;
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--test") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--test requires a JS file argument\n");
                return 2;
            }
            return runUiTestFile(argv[i + 1]);
        }
    }
    std::fprintf(stderr, "usage: %s --test <bundle.js>\n", argv[0]);
    return 2;
}
