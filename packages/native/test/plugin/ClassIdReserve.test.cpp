// Regression guard for the class-id counter sync in TjsHostRuntime::init / LvglJsEngine (dpf.js).
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
// dpfjs::syncClassIdAllocator (ClassIdSpace.hpp) closes it by advancing the runtime's counter past the
// true registered high-water via the public JS_IsRegisteredClass — self-sizing (no magic constant) and
// hole-proof (past the MAX registered id, not the first gap).
//
// Run via `pnpm test:plugin`.

#include <string>

// Include the txiki-bearing host header first: its utils.h defines a CHECK(expr) macro
// (abort-on-false) that would otherwise shadow Catch2's CHECK. Undef it before pulling in
// Catch2 so the Catch2 assertion macros win in this translation unit.
#include "dpfjs/host/ClassIdSpace.hpp"
#include "dpfjs/host/TjsHostRuntime.hpp"

extern "C" {
#include "quickjs.h"
}

#ifdef CHECK
#undef CHECK
#endif

#include <catch2/catch_test_macros.hpp>

namespace {

std::string decodeHi(JSContext* ctx) {
    static const char s[] = "(()=>{try{return new TextDecoder().decode(new Uint8Array([104,105]))}"
                            "catch(e){return 'ERR:'+(e&&e.message)}})()";
    JSValue r       = JS_Eval(ctx, s, sizeof(s) - 1, "<decode>", JS_EVAL_TYPE_GLOBAL);
    const char* o   = JS_ToCString(ctx, r);
    std::string out = o ? o : "(null)";
    if (o) JS_FreeCString(ctx, o);
    JS_FreeValue(ctx, r);
    return out;
}

// Register `n` fresh native classes on ctx via the same JS_NewClassID(process-global static) idiom txiki
// and lv_binding_js use — standing in for lv_binding_js's LVGL component registration. Each gets a plain
// prototype WITHOUT a `decode` method, so any that lands on txiki's Utf8Decoder id strips TextDecoder.
void registerFreshClasses(JSContext* ctx, int n, JSClassID* ids) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    for (int i = 0; i < n; ++i) {
        JSClassDef def  = {};
        def.class_name  = "FreshWidget";
        JS_NewClassID(rt, &ids[i]);
        JS_NewClass(rt, ids[i], &def);
        JS_SetClassProto(ctx, ids[i], JS_NewObject(ctx));
    }
}

} // namespace

TEST_CASE("TextDecoder survives a fresh native-class batch in a reused runtime", "[classid]") {
    // A first runtime sets txiki's process-global class-id statics, then is freed — the DAW's scan pass.
    {
        TjsHostRuntime scan;
        REQUIRE(scan.init());
    }

    // The editor's runtime: init reuses the cached ids (counter left low without the sync), then
    // lv_binding_js-like fresh registration would collide with a live txiki class.
    TjsHostRuntime host;
    REQUIRE(host.init());
    JSContext* ctx = host.context();

    CHECK(decodeHi(ctx) == "hi"); // decodes after init

    static JSClassID s_fresh[128] = {};
    registerFreshClasses(ctx, 128, s_fresh); // stand in for lv_binding_js registering its LVGL classes

    CHECK(decodeHi(ctx) == "hi"); // still decodes after fresh native classes register
}

TEST_CASE("syncClassIdAllocator advances past the max registered id (hole-proof)", "[classid]") {
    // The sync must clear the MAX registered id, not the first unregistered one. Register a sentinel at a
    // high id so the ids just above txiki's block are unregistered holes; the next fresh id must clear the
    // sentinel. A naive stop-at-first-gap sync would land in the hole and hand out a colliding id.
    TjsHostRuntime holey;
    REQUIRE(holey.init());
    JSRuntime* r = JS_GetRuntime(holey.context());

    const JSClassID sentinel = 4096; // far above txiki's ~67..94 block
    JSClassDef sdef  = {};
    sdef.class_name  = "HoleSentinel";
    JS_NewClass(r, sentinel, &sdef); // ids between txiki's high-water and 4096 are holes
    REQUIRE(JS_IsRegisteredClass(r, sentinel));

    dpfjs::syncClassIdAllocator(r);
    JSClassID fresh = 0;
    JS_NewClassID(r, &fresh);
    CHECK(fresh > sentinel);
}

TEST_CASE("syncClassIdAllocator clears a reused class block (second-editor case)", "[classid]") {
    // A first runtime fresh-allocates a class block (caching statics + advancing its counter); a second
    // runtime reuse-registers those same statics (growing class_count without advancing its counter) —
    // modelling lv_binding_js on a 2nd editor. After the post-registration sync, a later fresh id must
    // clear the reused block.
    static JSClassID s_libA[48] = {}; // process-global statics, like lv_binding_js's

    {
        // Runtime #1 fresh-allocates libA: caches the statics AND advances r1's counter.
        TjsHostRuntime r1;
        REQUIRE(r1.init());
        JSRuntime* r = JS_GetRuntime(r1.context());
        for (JSClassID& id : s_libA) {
            JSClassDef d = {};
            d.class_name = "LibA";
            JS_NewClassID(r, &id);
            JS_NewClass(r, id, &d);
        }
    }

    // Runtime #2 reuses libA's cached ids: JS_NewClass grows r2's class_count, but the reused
    // JS_NewClassID does NOT advance r2's counter.
    TjsHostRuntime r2;
    REQUIRE(r2.init());
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
    CHECK(later > maxLibA);
}
