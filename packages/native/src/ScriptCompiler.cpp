#include "ScriptCompiler.hpp"

extern "C" {
#include "quickjs.h"
}

namespace dsp {

std::optional<std::vector<std::uint8_t>> compileToBytecode(const std::string& source) {
    JSRuntime* rt = JS_NewRuntime();
    if (!rt) return std::nullopt;
    JSContext* ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return std::nullopt;
    }

    std::optional<std::vector<std::uint8_t>> result;

    // ES5 global code, compile-only → a JS_TAG_FUNCTION_BYTECODE object.
    JSValue fn = JS_Eval(ctx, source.c_str(), source.size(), "<dsp-script>",
                         JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    if (!JS_IsException(fn)) {
        std::size_t len = 0;
        std::uint8_t* buf = JS_WriteObject(ctx, &len, fn, JS_WRITE_OBJ_BYTECODE);
        if (buf) {
            result = std::vector<std::uint8_t>(buf, buf + len);
            js_free(ctx, buf);
        }
    }
    JS_FreeValue(ctx, fn);

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return result;
}

} // namespace dsp
