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

#include "dpfjs/host/ClassIdSpace.hpp"
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
    std::printf("TAP version 13\n1..5\n");

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

    // Check 3 (hole-proof): the sync advances past the MAX registered id, not the
    // first unregistered one. Register a sentinel class at a high id so the ids
    // just above txiki's block are unregistered holes; the next fresh id must clear
    // the sentinel. A naive stop-at-first-gap sync would land in the hole and fail.
    {
        TjsHostRuntime holey;
        if (!holey.init()) { std::printf("holey init failed\n"); return 2; }
        JSRuntime* r = JS_GetRuntime(holey.context());

        const JSClassID sentinel = 4096; // far above txiki's ~67..94 block
        JSClassDef sdef  = {};
        sdef.class_name  = "HoleSentinel";
        JS_NewClass(r, sentinel, &sdef); // ids between txiki's high-water and 4096 are holes
        check(JS_IsRegisteredClass(r, sentinel), "sentinel class registered at a high id", "4096");

        dpfjs::syncClassIdAllocator(r);
        JSClassID fresh = 0;
        JS_NewClassID(r, &fresh);
        char det[64];
        std::snprintf(det, sizeof det, "fresh=%u sentinel=%u", fresh, sentinel);
        check(fresh > sentinel, "fresh id clears the max registered id (hole-proof)", det);
    }

    // Check 4 (reuse-then-fresh / second-editor case): a first runtime fresh-allocates
    // a class block (caching statics + advancing its counter); a second runtime
    // reuse-registers those same statics (growing class_count without advancing its
    // counter), models lv_binding_js on a 2nd editor. After the post-registration
    // sync, a later fresh id must clear the reused block.
    static JSClassID s_libA[48] = {}; // process-global statics, like lv_binding_js's
    {
        // Runtime #1 fresh-allocates libA: caches the statics AND advances r1's counter.
        TjsHostRuntime r1;
        if (!r1.init()) { std::printf("r1 init failed\n"); return 2; }
        JSRuntime* r = JS_GetRuntime(r1.context());
        for (JSClassID& id : s_libA) {
            JSClassDef d = {};
            d.class_name = "LibA";
            JS_NewClassID(r, &id);
            JS_NewClass(r, id, &d);
        }
    }
    {
        // Runtime #2 reuses libA's cached ids: JS_NewClass grows r2's class_count,
        // but the reused JS_NewClassID does NOT advance r2's counter.
        TjsHostRuntime r2;
        if (!r2.init()) { std::printf("r2 init failed\n"); return 2; }
        JSRuntime* r = JS_GetRuntime(r2.context());
        JSClassID maxLibA = 0;
        for (JSClassID id : s_libA) {
            JSClassDef d = {};
            d.class_name = "LibA";
            JS_NewClass(r, id, &d);
            if (id > maxLibA) maxLibA = id;
        }

        dpfjs::syncClassIdAllocator(r); // the "after NativeRenderInit" sync
        JSClassID later = 0;
        JS_NewClassID(r, &later);
        char det[64];
        std::snprintf(det, sizeof det, "later=%u maxLibA=%u", later, maxLibA);
        check(later > maxLibA, "later fresh id clears the reused block (2nd-editor case)", det);
    }

    std::printf("\n%s: %d/%d\n", g_failures ? "FAILED" : "all checks passed", g_index - g_failures, g_index);
    return g_failures ? 1 : 0;
}
