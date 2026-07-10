// Guards the per-context routing behind PluginUI's __rp_* window hooks (ContextTargets.hpp): two
// JSContexts, each with its own registered owner, and an identically-bound C-function must reach ITS
// context's owner — never a shared process-global (the bug this replaced, where a DAW's second editor
// stole the first's window calls). Also checks that re-pointing / clearing one context leaves the other
// untouched. A bare quickjs runtime per context is enough — no txiki/LVGL/PluginUI needed, which is the
// whole point of extracting the mechanism into a header.
//
// Exercised by `pnpm test:plugin` (builds + runs this binary; exit code is pass/fail).

#include <cstdio>
#include <cstring>

#include <quickjs.h>

#include "ContextTargets.hpp"

namespace {

struct Owner {
    int id = 0;
};

retroplug::ContextTargetTable<Owner> g_table;

int g_index    = 0;
int g_failures = 0;

void check(bool cond, const char* what) {
    ++g_index;
    std::printf("%s %d - %s\n", cond ? "ok" : "not ok", g_index, what);
    if (!cond) ++g_failures;
}

// globalThis.probe(): return the id of the Owner routed to THIS context via func-data, or -1 if none.
JSValue probe(JSContext* ctx, JSValueConst, int, JSValueConst*, int, JSValue* funcData) {
    Owner* owner = retroplug::contextTargetFromData<Owner>(ctx, funcData);
    return JS_NewInt32(ctx, owner ? owner->id : -1);
}

// Bind probe() on `ctx`, carrying ctx's own slot — exactly how installWindowSizeHooks binds the real hooks.
void installProbe(JSContext* ctx) {
    JSValue g    = JS_GetGlobalObject(ctx);
    JSValue data = retroplug::packContextTarget(ctx, g_table.slotFor(ctx));
    JS_SetPropertyStr(ctx, g, "probe", JS_NewCFunctionData(ctx, probe, 0, 0, 1, &data));
    JS_FreeValue(ctx, data);
    JS_FreeValue(ctx, g);
}

int callProbe(JSContext* ctx) {
    static const char* kSrc = "probe()";
    JSValue r               = JS_Eval(ctx, kSrc, std::strlen(kSrc), "<probe>", JS_EVAL_TYPE_GLOBAL);
    int32_t v               = -999;
    JS_ToInt32(ctx, &v, r);
    JS_FreeValue(ctx, r);
    return v;
}

} // namespace

int main() {
    std::printf("TAP version 13\n1..6\n");

    JSRuntime* rt = JS_NewRuntime();
    JSContext* a  = JS_NewContext(rt);
    JSContext* b  = JS_NewContext(rt);

    Owner ownerA{101};
    Owner ownerB{202};

    // Register each context's owner, then bind the identical probe on both.
    g_table.slotFor(a)->ptr = &ownerA;
    g_table.slotFor(b)->ptr = &ownerB;
    installProbe(a);
    installProbe(b);

    // The crux: the same-named function on a different context reaches its own owner (no cross-talk).
    check(callProbe(a) == 101, "context A routes to owner A");
    check(callProbe(b) == 202, "context B routes to owner B");

    // Re-pointing one context's slot (e.g. its editor was swapped) reroutes only that context.
    g_table.slotFor(a)->ptr = &ownerB;
    check(callProbe(a) == 202, "re-pointing A's slot reroutes A only");
    check(callProbe(b) == 202, "B is unaffected by A re-pointing");

    // Clearing a context (owner gone) makes its hook inert without touching the other's.
    g_table.clear(a, &ownerB);
    check(callProbe(a) == -1, "clearing A makes A's hook inert (null)");
    check(callProbe(b) == 202, "B still routes to owner B after A is cleared");

    JS_FreeContext(a);
    JS_FreeContext(b);
    JS_FreeRuntime(rt);

    if (g_failures) std::printf("\n# FAILED %d of %d checks\n", g_failures, g_index);
    else std::printf("\n# all %d checks passed\n", g_index);
    return g_failures ? 1 : 0;
}
