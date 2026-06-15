// retroplug-ui-test: a TypeScript-bundle runner for the headless UI harness.
//
// Mirrors cli/TestHarness.cpp's txiki/QuickJS TAP runner, but exposes a `ui`
// global (Symbol.for("retroplug-ui")) backed by a UiTestHarness instead of `emu`.
// UI test bundles import from "ui-harness" (test/harness/ui.ts). The test runs
// in THIS runtime; `ui.boot()` spins up a UiTestHarness which owns its own JS
// runtime running the real UI bundle (two runtimes, validated safe). beginCase
// tears down the previous harness so each case is isolated.
//
//   retroplug-ui-test --test <bundle.js>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "UiTestHarness.hpp"

extern "C" {
    #include <quickjs.h>   // JS_* (the runtime itself is owned by the harness)
}

namespace {

// -- harness + TAP state (single runtime, single-threaded) -------------------
std::unique_ptr<rpui::UiTestHarness> g_harness;
int  g_testIndex   = 0;
int  g_failures    = 0;
bool g_donePrinted = false;

std::string slurpText(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string oneLine(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out += (c == '\n') ? ' ' : c;
    return out;
}

void tapReport(const std::string& name, bool ok, const std::string& msg) {
    ++g_testIndex;
    if (ok) {
        std::printf("ok %d - %s\n", g_testIndex, name.c_str());
    } else {
        ++g_failures;
        std::printf("not ok %d - %s\n", g_testIndex, name.c_str());
        if (!msg.empty())
            std::printf("  ---\n  message: \"%s\"\n  ...\n", oneLine(msg).c_str());
    }
    std::fflush(stdout);
}

void tapDone() {
    if (g_donePrinted) return;
    g_donePrinted = true;
    std::printf("1..%d\n", g_testIndex);
    std::fflush(stdout);
}

rpui::UiTestHarness* harnessOrThrow() {
    if (!g_harness) throw std::runtime_error("ui.boot() must be called first");
    return g_harness.get();
}

JSValue widgetInfoToJs(JSContext* ctx, const rpui::WidgetInfo& wi) {
    if (!wi.found) return JS_NULL;
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "x",          JS_NewInt32(ctx, wi.x));
    JS_SetPropertyStr(ctx, o, "y",          JS_NewInt32(ctx, wi.y));
    JS_SetPropertyStr(ctx, o, "width",      JS_NewInt32(ctx, wi.width));
    JS_SetPropertyStr(ctx, o, "height",     JS_NewInt32(ctx, wi.height));
    JS_SetPropertyStr(ctx, o, "childCount", JS_NewUint32(ctx, wi.childCount));
    JS_SetPropertyStr(ctx, o, "text",       JS_NewStringLen(ctx, wi.text.data(), wi.text.size()));
    return o;
}

// -- TAP plumbing trampolines (Symbol.for("retroplug")) ----------------------

// Single-runtime model: the test bundle runs in the engine's own runtime, so we
// can't destroy the harness between cases (we'd be tearing down the runtime the
// test is executing in). Isolation is per test FILE (each runs in its own
// process). Within a file, cases share the booted UI — keep them order-
// independent / self-cleaning. beginCase is a no-op.
JSValue jsBeginCase(JSContext*, JSValueConst, int, JSValueConst*) {
    return JS_UNDEFINED;
}

JSValue jsReport(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "report(name, ok, message?)");
    const char* name = JS_ToCString(ctx, argv[0]);
    const int   ok   = JS_ToBool(ctx, argv[1]);
    const char* msg  = (argc >= 3 && !JS_IsUndefined(argv[2])) ? JS_ToCString(ctx, argv[2]) : nullptr;
    tapReport(name ? name : "", ok == 1, msg ? msg : "");
    if (name) JS_FreeCString(ctx, name);
    if (msg)  JS_FreeCString(ctx, msg);
    return JS_UNDEFINED;
}

JSValue jsDone(JSContext*, JSValueConst, int, JSValueConst*) { tapDone(); return JS_UNDEFINED; }

JSValue jsLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int level = 0;
    if (argc >= 1) JS_ToInt32(ctx, &level, argv[0]);
    const char* msg = (argc >= 2) ? JS_ToCString(ctx, argv[1]) : nullptr;
    const char* tag = level >= 2 ? "error" : level == 1 ? "warn" : "log";
    std::fprintf(stderr, "[js:%s] %s\n", tag, msg ? msg : "");
    if (msg) JS_FreeCString(ctx, msg);
    return JS_UNDEFINED;
}

// -- ui trampolines (Symbol.for("retroplug-ui")) -----------------------------

// The harness is booted once by the runner (before the test bundle is eval'd, at
// a shallow C-stack/single-runtime point). ui.boot() is therefore idempotent.
JSValue jsUiBoot(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewBool(ctx, g_harness != nullptr);
}

JSValue jsUiLoadRom(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "ui.loadRom(path, savPath?)");
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    std::string sav;
    if (argc >= 2 && JS_IsString(argv[1])) {
        if (const char* s = JS_ToCString(ctx, argv[1])) { sav = s; JS_FreeCString(ctx, s); }
    }
    try {
        const std::uint32_t id = harnessOrThrow()->loadRom(path, sav);
        JS_FreeCString(ctx, path);
        return JS_NewInt32(ctx, static_cast<int32_t>(id));
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "ui.loadRom: %s", e.what());
        JS_FreeCString(ctx, path);
        return err;
    }
}

JSValue jsUiLoadProject(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "ui.loadProject(path)");
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    try {
        const bool ok = harnessOrThrow()->loadProject(path);
        JS_FreeCString(ctx, path);
        return JS_NewBool(ctx, ok);
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "ui.loadProject: %s", e.what());
        JS_FreeCString(ctx, path);
        return err;
    }
}

JSValue jsUiSelectFile(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "ui.selectFile(path)");
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    try {
        harnessOrThrow()->selectFile(path);
        JS_FreeCString(ctx, path);
        return JS_UNDEFINED;
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "ui.selectFile: %s", e.what());
        JS_FreeCString(ctx, path);
        return err;
    }
}

JSValue jsUiWriteFile(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "ui.writeFile(path, bytes)");
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    size_t len = 0;
    uint8_t* data = JS_GetArrayBuffer(ctx, &len, argv[1]);
    if (!data) {
        JS_FreeCString(ctx, path);
        return JS_ThrowTypeError(ctx, "ui.writeFile: bytes must be an ArrayBuffer");
    }
    try {
        harnessOrThrow()->writeFile(path, std::vector<std::uint8_t>(data, data + len));
        JS_FreeCString(ctx, path);
        return JS_UNDEFINED;
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "ui.writeFile: %s", e.what());
        JS_FreeCString(ctx, path);
        return err;
    }
}

JSValue jsUiWriteProjectJson(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "ui.writeProjectJson(path, romPath)");
    const char* path = JS_ToCString(ctx, argv[0]);
    const char* rom  = JS_ToCString(ctx, argv[1]);
    if (!path || !rom) { if (path) JS_FreeCString(ctx, path); if (rom) JS_FreeCString(ctx, rom); return JS_EXCEPTION; }
    JSValue ret;
    try { harnessOrThrow()->writeProjectJson(path, rom); ret = JS_UNDEFINED; }
    catch (const std::exception& e) { ret = JS_ThrowTypeError(ctx, "ui.writeProjectJson: %s", e.what()); }
    JS_FreeCString(ctx, path);
    JS_FreeCString(ctx, rom);
    return ret;
}

JSValue jsUiPump(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int iterations = 30;
    if (argc >= 1 && !JS_IsUndefined(argv[0])) JS_ToInt32(ctx, &iterations, argv[0]);
    try { harnessOrThrow()->pump(iterations); return JS_UNDEFINED; }
    catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.pump: %s", e.what()); }
}

JSValue jsUiSnapshot(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    try {
        const rpui::Snapshot s = harnessOrThrow()->snapshot();
        JSValue o = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, o, "width",  JS_NewUint32(ctx, s.width));
        JS_SetPropertyStr(ctx, o, "height", JS_NewUint32(ctx, s.height));
        JS_SetPropertyStr(ctx, o, "pixels",
            JS_NewArrayBufferCopy(ctx, s.argb.data(), s.argb.size()));
        return o;
    } catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.snapshot: %s", e.what()); }
}

JSValue jsUiSnapshotPng(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "ui.snapshotPng(path)");
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    try {
        const bool ok = harnessOrThrow()->snapshotPng(path);
        JS_FreeCString(ctx, path);
        return JS_NewBool(ctx, ok);
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "ui.snapshotPng: %s", e.what());
        JS_FreeCString(ctx, path);
        return err;
    }
}

JSValue jsUiReadMemory(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int id = 0, type = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "ui.readMemory(sys, type)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &type, argv[1]) < 0) return JS_EXCEPTION;
    try {
        const auto mem = harnessOrThrow()->readMemory(static_cast<std::uint32_t>(id), type);
        return JS_NewArrayBufferCopy(ctx, mem.data(), mem.size());
    } catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.readMemory: %s", e.what()); }
}

JSValue jsUiWidgetCount(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    try { return JS_NewInt64(ctx, (int64_t)harnessOrThrow()->widgetCount()); }
    catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.widgetCount: %s", e.what()); }
}

JSValue jsUiCountByType(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int t = 0;
    if (argc >= 1) JS_ToInt32(ctx, &t, argv[0]);
    try { return JS_NewInt64(ctx, (int64_t)harnessOrThrow()->countByType(t)); }
    catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.countByType: %s", e.what()); }
}

JSValue jsUiFindByTestId(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "ui.findByTestId(name)");
    const char* name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    try {
        auto* h = harnessOrThrow();
        JSValue r = widgetInfoToJs(ctx, h->widgetInfo(h->findByTestId(name)));
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
        auto* h = harnessOrThrow();
        JSValue r = widgetInfoToJs(ctx, h->widgetInfo(h->findByText(text)));
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
        auto* h = harnessOrThrow();
        JSValue r = widgetInfoToJs(ctx, h->widgetInfo(h->findByTextContaining(s)));
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
        auto* h = harnessOrThrow();
        return widgetInfoToJs(ctx, h->widgetInfo(h->findFirstByType(t)));
    } catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.findFirstByType: %s", e.what()); }
}

JSValue jsUiFocused(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    try {
        auto* h = harnessOrThrow();
        return widgetInfoToJs(ctx, h->widgetInfo(h->focusedObject()));
    } catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.focused: %s", e.what()); }
}

JSValue jsUiTapKey(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int key = 0;
    if (argc < 1 || JS_ToInt32(ctx, &key, argv[0]) < 0) return JS_EXCEPTION;
    try { harnessOrThrow()->tapKey(static_cast<std::uint32_t>(key)); return JS_UNDEFINED; }
    catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.tapKey: %s", e.what()); }
}

JSValue jsUiClickAt(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int x = 0, y = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "ui.clickAt(x, y)");
    if (JS_ToInt32(ctx, &x, argv[0]) < 0 || JS_ToInt32(ctx, &y, argv[1]) < 0) return JS_EXCEPTION;
    try { harnessOrThrow()->clickAt(x, y); return JS_UNDEFINED; }
    catch (const std::exception& e) { return JS_ThrowTypeError(ctx, "ui.clickAt: %s", e.what()); }
}

void installNamespace(JSContext* ctx, const char* symbolName,
                      const std::vector<std::pair<const char*, std::pair<JSCFunction*, int>>>& fns) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sym    = JS_NewSymbol(ctx, symbolName, /*is_global*/ true);
    JSAtom atom    = JS_ValueToAtom(ctx, sym);
    JSValue ns     = JS_NewObjectProto(ctx, JS_NULL);
    for (const auto& [name, fnArgc] : fns)
        JS_SetPropertyStr(ctx, ns, name, JS_NewCFunction(ctx, fnArgc.first, name, fnArgc.second));
    JS_DefinePropertyValue(ctx, global, atom, ns, JS_PROP_C_W_E);
    JS_FreeAtom(ctx, atom);
    JS_FreeValue(ctx, sym);
    JS_FreeValue(ctx, global);
}

int runUiTestFile(const std::string& jsPath) {
    // Boot the harness FIRST: it owns the single JS runtime (LvglJsEngine, which
    // calls TJS_Initialize once) + the headless display + the real UI bundle. The
    // test bundle then runs IN this runtime, alongside the UI.
    try {
        g_harness = std::make_unique<rpui::UiTestHarness>();
        if (!g_harness->boot()) { std::printf("Bail out! UI harness boot failed\n"); return 1; }
    } catch (const std::exception& e) {
        std::printf("Bail out! UI harness boot: %s\n", e.what());
        return 1;
    }

    JSContext* ctx = g_harness->engineContext();
    if (!ctx) { std::printf("Bail out! no engine context\n"); return 1; }

    // TAP plumbing the harness front door (test/harness/index.ts) calls.
    installNamespace(ctx, "retroplug", {
        { "beginCase", { jsBeginCase, 0 } },
        { "report",    { jsReport,    3 } },
        { "done",      { jsDone,      0 } },
        { "log",       { jsLog,       2 } },
    });
    // The ui API.
    installNamespace(ctx, "retroplug-ui", {
        { "boot",            { jsUiBoot,            0 } },
        { "loadRom",         { jsUiLoadRom,         1 } },
        { "loadProject",     { jsUiLoadProject,     1 } },
        { "selectFile",      { jsUiSelectFile,      1 } },
        { "writeFile",       { jsUiWriteFile,       2 } },
        { "writeProjectJson",{ jsUiWriteProjectJson, 2 } },
        { "pump",            { jsUiPump,            1 } },
        { "readMemory",      { jsUiReadMemory,      2 } },
        { "snapshot",        { jsUiSnapshot,        0 } },
        { "snapshotPng",     { jsUiSnapshotPng,     1 } },
        { "widgetCount",     { jsUiWidgetCount,     0 } },
        { "countByType",     { jsUiCountByType,     1 } },
        { "findByTestId",    { jsUiFindByTestId,    1 } },
        { "findByText",           { jsUiFindByText,           1 } },
        { "findByTextContaining", { jsUiFindByTextContaining, 1 } },
        { "findFirstByType",      { jsUiFindFirstByType,      1 } },
        { "focused",         { jsUiFocused,         0 } },
        { "tapKey",          { jsUiTapKey,          1 } },
        { "clickAt",         { jsUiClickAt,         2 } },
    });

    std::printf("TAP version 13\n");
    std::fflush(stdout);

    std::string code;
    try { code = slurpText(jsPath); }
    catch (const std::exception& e) { std::printf("Bail out! %s\n", e.what()); return 1; }

    // is_main=true fires the synthetic window 'load' event index.ts's runAll hooks
    // -> the test cases run synchronously here. (The UI bundle has no 'load'
    // listener, so re-firing 'load' only triggers the test runner.)
    const int rc = g_harness->evalTestModule(code.data(), code.size(), jsPath.c_str());

    // Drain any async work the tests scheduled (timers / promises).
    for (int i = 0; i < 64; ++i) g_harness->drainJs();

    tapDone();
    g_harness.reset(); // tear down display + engine + UI bundle cleanly

    if (rc != 0) { std::printf("Bail out! test module evaluation failed\n"); return 1; }
    return g_failures > 0 ? 1 : 0;
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
