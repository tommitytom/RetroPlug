#include "host/dsp/ScriptCompiler.hpp"

#include <cstdlib>
#include <string_view>

extern "C" {
#include "quickjs.h"
}

namespace dsp {

namespace {

// The name the compiled code goes by: what a syntax error's position names first in its stack ("    at <script>:3:1").
constexpr std::string_view kName = "<script>";

// `v` as text ("" when it has none), its exception cleared.
std::string text(JSContext* ctx, JSValueConst v) {
    std::size_t n = 0;
    const char* s = JS_ToCStringLen(ctx, &n, v);
    if (!s) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return {};
    }
    std::string out(s, n);
    JS_FreeCString(ctx, s);
    return out;
}

// The pending compile error, as the header says: "Name: message", after "line L: " when the error's stack names a place
// in the source. QuickJS puts no position on the error object itself (no lineNumber, no columnNumber): the parser
// writes it as the stack's first frame, so it is read from there.
std::string compileError(JSContext* ctx) {
    JSValue thrown = JS_GetException(ctx);
    std::string out = text(ctx, thrown);
    if (out.empty()) out = "the parser threw something with no text";
    JSValue stack = JS_IsError(thrown) ? JS_GetPropertyStr(ctx, thrown, "stack") : JS_UNDEFINED;
    const std::string frames = JS_IsString(stack) ? text(ctx, stack) : std::string();
    const std::string at = "at " + std::string(kName) + ":";
    if (const auto p = frames.find(at); p != std::string::npos) {
        const char* digits = frames.c_str() + p + at.size();
        char* end = nullptr;
        const long line = std::strtol(digits, &end, 10);
        if (end != digits && line > 0) out = "line " + std::to_string(line) + ": " + out;
    }
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, thrown);
    return out;
}

}  // namespace

std::optional<std::vector<std::uint8_t>> compileToBytecode(const std::string& source, std::size_t stackBytes,
                                                           std::string* error) {
    const auto fail = [&](std::string why) -> std::optional<std::vector<std::uint8_t>> {
        if (error) *error = std::move(why);
        return std::nullopt;
    };
    JSRuntime* rt = JS_NewRuntime();
    if (!rt) return fail("no QuickJS runtime could be made");
    // The parser recurses on the calling thread, which may be the Engine's control thread: held to the stack the caller
    // gives (the script host's, under the smallest thread that may call it), so deeply nested source is a compile error
    // rather than an overflowed stack.
    JS_SetMaxStackSize(rt, stackBytes);
    JSContext* ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return fail("no QuickJS context could be made");
    }

    std::optional<std::vector<std::uint8_t>> result;
    // Global code, compile-only: a JS_TAG_FUNCTION_BYTECODE object.
    JSValue fn =
        JS_Eval(ctx, source.c_str(), source.size(), kName.data(), JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(fn)) {
        fail("the source does not compile: " + compileError(ctx));
    } else {
        std::size_t len = 0;
        std::uint8_t* buf = JS_WriteObject(ctx, &len, fn, JS_WRITE_OBJ_BYTECODE);
        if (buf) {
            result = std::vector<std::uint8_t>(buf, buf + len);
            js_free(ctx, buf);
        } else {
            JS_FreeValue(ctx, JS_GetException(ctx));
            fail("the bytecode could not be written (out of memory)");
        }
    }
    JS_FreeValue(ctx, fn);

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return result;
}

} // namespace dsp
