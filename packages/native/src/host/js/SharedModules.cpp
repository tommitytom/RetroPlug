#include "host/js/SharedModules.hpp"

namespace rp::host {

namespace {

/** The global property a registered context is marked with. A string key, not a symbol: nothing in JS reads it, and a
 *  name under the shell's `__rp_` prefix says whose it is. */
constexpr const char* kMark = "__rp_sharedModulesRegistered";

std::string pendingMessage(JSContext* ctx) {
    JSValue e = JS_GetException(ctx);
    const char* s = JS_ToCString(ctx, e);
    std::string out = "an exception with no message";
    if (s) {
        out = s;
        JS_FreeCString(ctx, s);
    } else {
        // The conversion threw its own exception, which is pending now and would leak past this call.
        JS_FreeValue(ctx, JS_GetException(ctx));
    }
    JS_FreeValue(ctx, e);
    return out;
}

std::string registerAll(JSContext* ctx, const rp_js_module* modules, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        const rp_js_module& m = modules[i];
        JSValue mod = JS_ReadObject(ctx, m.bytecode, m.size, JS_READ_OBJ_BYTECODE);
        if (JS_IsException(mod)) {
            return std::string("the shared module ") + m.name + " could not be registered: " + pendingMessage(ctx);
        }
        if (JS_VALUE_GET_TAG(mod) != JS_TAG_MODULE) {
            JS_FreeValue(ctx, mod);
            return std::string("the shared module ") + m.name + " is not module bytecode";
        }
        // Resolved now, as registerSdkModule resolves the SDK: QuickJS frees every unresolved module in the context when a
        // module it has found fails to resolve (JS_ResolveModule, or a dynamic import), so a set left unresolved would
        // lose the modules no host had reached yet (the UI's, until the first editor opens) to an unrelated failure.
        // Marking the set resolved keeps it out of that sweep. A failed read or compile frees only its own module. The
        // sweep is not reachable from a test short of injecting an allocation failure, so no test covers it. Cheap: the
        // imports were bound when the bytecode was read.
        const int resolved = JS_ResolveModule(ctx, mod);
        JS_FreeValue(ctx, mod);
        if (resolved < 0) {
            return std::string("the shared module ") + m.name + " could not be resolved: " + pendingMessage(ctx);
        }
    }
    return {};
}

}  // namespace

std::string registerSharedModules(JSContext* ctx, const rp_js_module* modules, std::size_t count) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue mark = JS_GetPropertyStr(ctx, global, kMark);
    if (JS_IsString(mark)) {
        // An earlier call's outcome, success or failure: a retry would read from index 0 and append duplicates of the
        // modules that did register, so the same answer comes back without reading anything.
        const char* s = JS_ToCString(ctx, mark);
        std::string out = s ? s : "";
        JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, mark);
        JS_FreeValue(ctx, global);
        return out;
    }
    JS_FreeValue(ctx, mark);
    const std::string why = registerAll(ctx, modules, count);
    JSValue value = JS_NewString(ctx, why.c_str());
    JS_DefinePropertyValueStr(ctx, global, kMark, value, 0);
    JS_FreeValue(ctx, global);
    return why;
}

}  // namespace rp::host
