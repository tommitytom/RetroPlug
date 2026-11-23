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

}  // namespace

std::string registerSharedModules(JSContext* ctx, const rp_js_module* modules, std::size_t count) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue mark = JS_GetPropertyStr(ctx, global, kMark);
    const bool registered = JS_ToBool(ctx, mark) > 0;
    JS_FreeValue(ctx, mark);
    if (registered) {
        JS_FreeValue(ctx, global);
        return {};
    }
    for (std::size_t i = 0; i < count; ++i) {
        const rp_js_module& m = modules[i];
        JSValue mod = JS_ReadObject(ctx, m.bytecode, m.size, JS_READ_OBJ_BYTECODE);
        if (JS_IsException(mod)) {
            JS_FreeValue(ctx, global);
            return std::string("the shared module ") + m.name + " could not be registered: " + pendingMessage(ctx);
        }
        if (JS_VALUE_GET_TAG(mod) != JS_TAG_MODULE) {
            JS_FreeValue(ctx, mod);
            JS_FreeValue(ctx, global);
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
            JS_FreeValue(ctx, global);
            return std::string("the shared module ") + m.name + " could not be resolved: " + pendingMessage(ctx);
        }
    }
    JS_DefinePropertyValueStr(ctx, global, kMark, JS_TRUE, 0);
    JS_FreeValue(ctx, global);
    return {};
}

}  // namespace rp::host
