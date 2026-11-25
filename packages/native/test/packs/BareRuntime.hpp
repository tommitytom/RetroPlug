// A bare QuickJS runtime in a test, as a render job's (host/render/RenderHost): no txiki, a job queue run here.
#pragma once

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "quickjs.h"

namespace rp::host::packs::test {

struct BareRuntime {
    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);
    ~BareRuntime() {
        JS_RunGC(rt);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
    }
    void drain() {
        JSContext* jc = nullptr;
        while (JS_ExecutePendingJob(rt, &jc) > 0) {}
    }
    std::string eval(const std::string& code, int flags) {
        JSValue v = JS_Eval(ctx, code.c_str(), code.size(), "<bare-test>", flags);
        REQUIRE_FALSE(JS_IsException(v));
        JS_FreeValue(ctx, v);
        drain();
        JSValue g = JS_GetGlobalObject(ctx);
        JSValue s = JS_GetPropertyStr(ctx, g, "__pt");
        const char* c = JS_ToCString(ctx, s);
        std::string out = c ? c : "<none>";
        JS_FreeCString(ctx, c);
        JS_FreeValue(ctx, s);
        JS_SetPropertyStr(ctx, g, "__pt", JS_UNDEFINED);
        JS_FreeValue(ctx, g);
        return out;
    }
    std::string importResult(const std::string& name) {
        return eval("import('" + name + "').then(() => { globalThis.__pt = 'ok'; }, (e) => { globalThis.__pt = 'error ' + "
                    "e.message; });",
                    JS_EVAL_TYPE_MODULE);
    }
    std::string packList() { return eval("globalThis.__pt = globalThis.__rp_packList();", JS_EVAL_TYPE_GLOBAL); }
};

}  // namespace rp::host::packs::test
