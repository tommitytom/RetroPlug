/* RetroPlug's standard library for MicroQuickJS at run time (rp_mqjs.h): the tables the build tool generated from
 * rp_mqjs_stdlib.c for this width, compiled with the C functions they name. Upstream's are the engine's own (declared
 * in mquickjs_priv.h, which the generated header includes); RetroPlug's are the three below, which hand every call to
 * the host that made the context, and the accessors rp_mqjs.h declares. Compiled into the engine's library
 * (cmake/mquickjs.cmake), once per binary. */
#include <stddef.h>

#include "rp_mqjs.h"

/* The host's functions, through the context's opaque: a struct whose first member is the host's rp_mqjs_api. */
static const rp_mqjs_api* rp_mqjs_api_of(JSContext* ctx) {
    return *(const rp_mqjs_api* const*)JS_GetContextOpaque(ctx);
}

static JSValue js_rp_call(JSContext* ctx, JSValue* this_val, int argc, JSValue* argv, int magic) {
    return rp_mqjs_api_of(ctx)->call(ctx, this_val, argc, argv, magic);
}

static JSValue js_rp_get(JSContext* ctx, JSValue* this_val, int argc, JSValue* argv, int magic) {
    return rp_mqjs_api_of(ctx)->get(ctx, this_val, argc, argv, magic);
}

/* The block API's objects are the host's to make: their classes' constructors only refuse. */
static JSValue js_rp_construct(JSContext* ctx, JSValue* this_val, int argc, JSValue* argv, int magic) {
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)magic;
    return JS_ThrowTypeError(ctx, "the host makes the block's objects");
}

#define JS_CLASS_COUNT RP_MQJS_CLASS_COUNT
#include "rp_mqjs_stdlib.h"

uint32_t rp_mqjs_length(JSContext* ctx, JSValue v) {
    const int id = JS_GetClassID(ctx, v);
    JSValue n;
    if (id == JS_CLASS_ARRAY)
        n = js_array_get_length(ctx, &v, 0, NULL);
    else if (id >= JS_CLASS_UINT8C_ARRAY && id <= JS_CLASS_FLOAT64_ARRAY)
        n = js_typed_array_get_length(ctx, &v, 0, NULL, 0);
    else
        return UINT32_MAX;
    return JS_IsInt(n) ? (uint32_t)JS_VALUE_GET_INT(n) : UINT32_MAX;
}

int rp_mqjs_set_length(JSContext* ctx, JSValue* array, uint32_t n) {
    JSValue length = JS_NewInt32(ctx, (int32_t)n);
    return JS_IsException(js_array_set_length(ctx, array, 1, &length)) ? -1 : 0;
}

JSValue rp_mqjs_error_field(JSContext* ctx, JSValue error, int stack) {
    if (!JS_IsError(ctx, error)) return JS_UNDEFINED;
    return js_error_get_message(ctx, &error, 0, NULL, stack);
}
