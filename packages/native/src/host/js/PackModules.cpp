#include "host/js/PackModules.hpp"

#include "tjs.h"

#include "host/packs/PackTable.hpp"

namespace rp::host {

namespace {

char* normalize(JSContext* ctx, const char* base, const char* name, void* opaque) {
    if (guardRefuses(ctx, base, name)) return nullptr;
    return tjs_module_normalizer(ctx, base, name, opaque);
}

// A pack module's import.meta takes its name as written: it is no path.
int setImportMeta(JSContext* ctx, JSValueConst val) { return js_module_set_import_meta(ctx, val, false, false); }

JSModuleDef* load(JSContext* ctx, const char* name, void* opaque, JSValueConst attributes) {
    if (!isPackName(name)) return tjs_module_loader(ctx, name, opaque, attributes);
    return loadPackHalf(ctx, name, setImportMeta);
}

}  // namespace

void installPackLoader(JSContext* ctx, packs::PackTable& table, const rp_js_module* contracts, std::size_t count) {
    // txiki's opaque is its TJSRuntime; the wrappers hand it on unchanged.
    JS_SetModuleLoaderFunc2(JS_GetRuntime(ctx), normalize, load, tjs_module_attr_checker, TJS_GetRuntime(ctx));
    installPackGlobals(ctx, table, contracts, count, true);
}

void settleForFree(TjsHostRuntime& host) {
    TJSRuntime* qrt = host.runtime();
    if (!qrt || !host.context()) return;
    // QuickJS-ng frees a context's module structs while a callback of a pending async module (a pack half parked on a
    // timer) still holds a counted reference to one: the callback is garbage only once its timer is destroyed, and no
    // collection runs before the context is freed (the same defect as bellard/quickjs#280). Destroying the timers
    // here, then collecting, frees the callback while the modules are alive. txiki destroys timers again when it
    // frees the runtime; that walk finds none left.
    tjs__destroy_timers(qrt);
    JS_RunGC(JS_GetRuntime(host.context()));
}

}  // namespace rp::host
