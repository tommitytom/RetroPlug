// Regression guard for the class-id reservation in TjsHostRuntime::init (dpf.js).
//
// quickjs-ng's JS_NewClassID caches each class id in a process-global static but allocates from a
// PER-RUNTIME counter. A runtime that REUSES a static id an earlier runtime already set does not advance
// its own counter — so when a second TjsHostRuntime (a DAW constructs one to scan the plugin, frees it,
// then constructs the one it uses) reuses txiki's cached class ids and a later library registers FRESH
// native classes (lv_binding_js, when the editor attaches), those fresh ids collide with a live txiki
// class and JS_SetClassProto overwrites its prototype. The real-world symptom was TextDecoder.prototype
// .decode going missing in the plugin's editor runtime only — the config load threw, React never mounted,
// and the VST3/VST2 UI was blank while the standalone (single runtime) rendered fine.
//
// This reproduces it headlessly: a first runtime sets the statics and is freed; the second runtime then
// registers a batch of fresh native classes (standing in for lv_binding_js), and TextDecoder must still
// decode. Without the reservation, one fresh class lands on Utf8Decoder's id and check 2 fails.
//
// Run via `pnpm test:plugin`.

#include <cstdio>
#include <string>

#include "dpfjs/host/TjsHostRuntime.hpp"

extern "C" {
#include "quickjs.h"
}

namespace {

int g_index    = 0;
int g_failures = 0;

std::string decodeHi(JSContext* ctx) {
    static const char s[] = "(()=>{try{return new TextDecoder().decode(new Uint8Array([104,105]))}"
                            "catch(e){return 'ERR:'+(e&&e.message)}})()";
    JSValue r     = JS_Eval(ctx, s, sizeof(s) - 1, "<decode>", JS_EVAL_TYPE_GLOBAL);
    const char* o = JS_ToCString(ctx, r);
    std::string out = o ? o : "(null)";
    if (o) JS_FreeCString(ctx, o);
    JS_FreeValue(ctx, r);
    return out;
}

void check(bool cond, const char* what, const char* detail) {
    ++g_index;
    std::printf("%s %d - %s (%s)\n", cond ? "ok" : "not ok", g_index, what, detail);
    if (!cond) ++g_failures;
}

// Register `n` fresh native classes on ctx via the same JS_NewClassID(process-global static) idiom txiki
// and lv_binding_js use — standing in for lv_binding_js's LVGL component registration. Each gets a plain
// prototype WITHOUT a `decode` method, so any that lands on txiki's Utf8Decoder id strips TextDecoder.
JSClassID g_fresh[256] = {};
void registerFreshClasses(JSContext* ctx, int n) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    for (int i = 0; i < n; ++i) {
        JSClassDef def = {};
        def.class_name = "FreshWidget";
        JS_NewClassID(rt, &g_fresh[i]);
        JS_NewClass(rt, g_fresh[i], &def);
        JS_SetClassProto(ctx, g_fresh[i], JS_NewObject(ctx));
    }
}

} // namespace

int main() {
    std::printf("TAP version 13\n1..2\n");

    // A first runtime sets txiki's process-global class-id statics, then is freed — the DAW's scan pass.
    {
        TjsHostRuntime scan;
        if (!scan.init()) { std::printf("scan runtime init failed\n"); return 2; }
    }

    // The editor's runtime: init reuses the cached ids, then lv_binding_js-like fresh registration.
    TjsHostRuntime host;
    if (!host.init()) { std::printf("host runtime init failed\n"); return 2; }
    JSContext* ctx = host.context();

    const std::string before = decodeHi(ctx);
    check(before == "hi", "TextDecoder decodes in a 2nd runtime after init", before.c_str());

    registerFreshClasses(ctx, 128); // stand in for lv_binding_js registering its native LVGL classes

    const std::string after = decodeHi(ctx);
    check(after == "hi", "TextDecoder still decodes after fresh native classes register", after.c_str());

    std::printf("\n%s: %d/%d\n", g_failures ? "FAILED" : "all checks passed", g_index - g_failures, g_index);
    return g_failures ? 1 : 0;
}
