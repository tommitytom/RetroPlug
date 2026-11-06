/* The engine itself, compiled from this file in place of deps/mquickjs/mquickjs.c (cmake/mquickjs.cmake's
 * retroplug_mquickjs_engine, ENGINE), so that it is compiled once and RetroPlug's reads of the context's own
 * bookkeeping sit in the translation unit that defines struct JSContext, which no header declares: the vendored source
 * is included whole and unpatched, and what follows it is RetroPlug's (rp_mqjs.h declares it).
 *
 * One change of behaviour is RetroPlug's too, made here rather than in the vendored file: how a string is made a
 * property name (JS_MakeUniqueString). The engine keeps the names made at run time in a sorted table its collector
 * treats as weak, dropping each whose string is not marked, and a collection that runs inside the table's own growth
 * (the allocation in JS_MakeUniqueString's resize) drops entries under the insertion: the resize copies the entries
 * the table had before the collection and the insertion lands at the place it found before it, so the table comes back
 * with stale words in it, which the next lookup reads as strings (a crash, or a name that resolves to another's slot).
 * It takes a dead name in the table and a collection at that one allocation, which a tight heap makes likely (the
 * toolchain's least-heap search met it). RetroPlug's version, rp_mqjs_make_unique_string below, collects first when the
 * insertion has to grow the table and finds its place after, so the growth itself never meets a dead entry. Every call
 * to JS_MakeUniqueString in mquickjs.c passes `ctx` first, which is what the two macros below tell the definition
 * (whose first parameter is `JSContext *ctx`) from the calls by: the definition is compiled under another name and
 * left unused, and every call reaches RetroPlug's. A bump of deps/mquickjs that adds a call spelt otherwise, or
 * renames the function, fails to compile here rather than quietly reaching the upstream body.
 *
 * Five exceptions the engine loses are RetroPlug's to keep, by the same trick where it can be:
 * - String.prototype.replace with a function: js_string_replace ignores js_string_concat_subst's failure, so an
 *   exception the replacement function throws (a node's error, the heap running out, the engine refusing a C
 *   re-entry) is dropped and replace returns the string it had built. The engine's own convention is a sticky
 *   StringBuffer (a failed append sets its value to JS_EXCEPTION, which string_buffer_pop returns), which
 *   concat_subst's function path forgets: every call reaches rp_mqjs_concat_subst, which calls no function once the
 *   buffer has failed (replaceAll's later matches) and marks the buffer failed when the upstream body fails, so both
 *   of replace's paths end in string_buffer_pop returning the exception, pending as it was thrown.
 * - string_buffer_puts loses an out-of-memory the same way (its JS_NewString fails and the buffer stays good):
 *   rp_mqjs_buffer_puts marks the buffer failed.
 * - Object.prototype.hasOwnProperty does not check JS_ToPropertyKey, so a key object whose toString throws (or runs
 *   the heap out) answers false with the exception dropped: the standard library's hasOwnProperty is
 *   rp_mqjs_js_object_hasOwnProperty below, which makes the key first and returns on an exception, then hands a key
 *   that converts to itself to the upstream body, compiled under another name.
 * - Array.prototype.sort's comparison (js_array_sort_cmp) returns JS_EXCEPTION as a comparison's result when a
 *   comparator function throws, where its other failures set the sort's exception flag: the sort goes on and returns
 *   the array, the exception dropped. So a comparator's throw is ignored, and the deadline's interrupt with it (each
 *   poll of a comparator that never returns lands inside it, and is dropped there, so a sort of such a comparator
 *   never faults). js_array_sort hands its comparison to the heapsort by name, never calls it: so the definition alone
 *   is a call of the function-like macro below (its first parameter's type, size_t, tells it from anything else) and
 *   is compiled under another name, left unused, while the name js_array_sort hands on is RetroPlug's comparison
 *   below, upstream's body with the comparator's exception setting the flag, which ends the sort: every comparison
 *   after it answers 0 at once, and js_array_sort returns the exception.
 * - String.prototype.repeat returns -1, not JS_EXCEPTION, when its count's conversion throws (a valueOf's throw, the
 *   deadline's interrupt in it, a heap that runs out converting a string): a value the interpreter takes as an
 *   ordinary result (and, left on its stack, as a catch handler's offset), the exception dropped. The standard
 *   library's repeat is rp_mqjs_js_string_repeat below, which converts the count first and returns on an exception,
 *   then hands upstream's body, compiled under another name, a count that converts to itself.
 * The StringBuffer is a type mquickjs.c declares, so the wrappers take it as a void* before the include. An audit of
 * every other builtin that converts a value or calls back into JavaScript (the Array iterations and reduce, getters and
 * setters, toString and valueOf in conversions, JSON, which takes no reviver here) found the rest propagating what they
 * are handed; what is left drops an out-of-memory alone (docs/modular-graph-phase-e4-design.md section 11.8). */
#include <stddef.h>

#include "rp_mqjs_names.h"
#include "mquickjs.h"

static JSValue rp_mqjs_make_unique_string(JSContext* ctx, JSValue val);
#define JS_MakeUniqueString(first, second) RP_MQJS_UNIQUE_##first, second)
#define RP_MQJS_UNIQUE_JSContext rp_mqjs_upstream_make_unique_string(JSContext
#define RP_MQJS_UNIQUE_ctx rp_mqjs_make_unique_string(ctx

static int rp_mqjs_concat_subst(JSContext* ctx, void* buffer, JSValue* str, JSValue* rep, uint32_t pos,
                                uint32_t end_of_match, JSValue* capture_buf, uint32_t captures_len, JSValue* needle);
#define js_string_concat_subst(first, ...) RP_MQJS_SUBST_##first, __VA_ARGS__)
#define RP_MQJS_SUBST_JSContext rp_mqjs_upstream_concat_subst(JSContext
#define RP_MQJS_SUBST_ctx rp_mqjs_concat_subst(ctx

static int rp_mqjs_buffer_puts(JSContext* ctx, void* buffer, const char* str);
#define string_buffer_puts(first, ...) RP_MQJS_PUTS_##first, __VA_ARGS__)
#define RP_MQJS_PUTS_JSContext rp_mqjs_upstream_buffer_puts(JSContext
#define RP_MQJS_PUTS_ctx rp_mqjs_buffer_puts(ctx

#undef js_object_hasOwnProperty
#define js_object_hasOwnProperty rp_mqjs_upstream_object_hasOwnProperty

#undef js_string_repeat
#define js_string_repeat rp_mqjs_upstream_string_repeat

static int js_array_sort_cmp(size_t i1, size_t i2, void* opaque);
#define js_array_sort_cmp(first, ...) RP_MQJS_SORT_CMP_##first, __VA_ARGS__)
#define RP_MQJS_SORT_CMP_size_t rp_mqjs_upstream_array_sort_cmp(size_t

#include "mquickjs.c"

#undef JS_MakeUniqueString
#undef js_string_concat_subst
#undef string_buffer_puts
#undef js_object_hasOwnProperty
#undef js_array_sort_cmp
#undef js_string_repeat

#include "rp_mqjs.h"

static JSValue rp_mqjs_make_unique_string(JSContext* ctx, JSValue val) {
    JSString* p;
    int a, is_numeric, i;
    JSValueArray* arr;
    const JSValueArray* arr1;
    JSValue val1, new_tab;
    JSGCRef val_ref;

    (void)rp_mqjs_upstream_make_unique_string;
    if (!JS_IsPtr(val)) return val;
    p = JS_VALUE_TO_PTR(val);
    if (p->mtag != JS_MTAG_STRING || p->is_unique) return val;

    /* Found in the ROM tables (the standard library's, a loaded bytecode's) or in the table of names made so far. */
    for (i = 0; i < ctx->n_rom_atom_tables; i++) {
        arr1 = ctx->rom_atom_tables[i];
        if (arr1) {
            val1 = find_atom(ctx, &a, arr1, arr1->size, val);
            if (!JS_IsNull(val1)) return val1;
        }
    }
    arr = JS_IsNull(ctx->unique_strings) ? NULL : JS_VALUE_TO_PTR(ctx->unique_strings);
    val1 = find_atom(ctx, &a, arr, ctx->unique_strings_len, val);
    if (!JS_IsNull(val1)) return val1;

    JS_PUSH_VALUE(ctx, val);
    is_numeric = js_is_numeric_string(ctx, val);
    JS_POP_VALUE(ctx, val);
    if (is_numeric < 0) return JS_EXCEPTION;

    /* Not found: inserted. When the table is full its growth allocates, and a collection there would drop the names
     * that died under the insertion; so collect first, which drops them now, and the growth's own collection, if it
     * comes, finds every entry alive. The place is found after, in the table as the collection left it. The sweep
     * shrinks the table to its live length after every collection, so the first new name after any collection finds
     * it full: that name pays one more full collection. A node that makes names at event rate (a message's own
     * property name) pays nothing measurable; one that makes a new name every block pays a collection a block. */
    if (JS_IsNull(ctx->unique_strings) ||
        ((JSValueArray*)JS_VALUE_TO_PTR(ctx->unique_strings))->size <= ctx->unique_strings_len) {
        JS_PUSH_VALUE(ctx, val);
        JS_GC(ctx);
        JS_POP_VALUE(ctx, val);
    }
    arr = JS_IsNull(ctx->unique_strings) ? NULL : JS_VALUE_TO_PTR(ctx->unique_strings);
    find_atom(ctx, &a, arr, ctx->unique_strings_len, val);

    JS_PUSH_VALUE(ctx, val);
    new_tab = js_resize_value_array(ctx, ctx->unique_strings, ctx->unique_strings_len + 1);
    JS_POP_VALUE(ctx, val);
    if (JS_IsException(new_tab)) return JS_EXCEPTION;
    ctx->unique_strings = new_tab;
    arr = JS_VALUE_TO_PTR(ctx->unique_strings);
    memmove(&arr->arr[a + 1], &arr->arr[a], sizeof(arr->arr[0]) * (ctx->unique_strings_len - a));
    arr->arr[a] = val;
    p = JS_VALUE_TO_PTR(val);
    p->is_unique = TRUE;
    p->is_numeric = is_numeric;
    ctx->unique_strings_len++;
    return val;
}

void rp_mqjs_memory_of(JSContext* ctx, rp_mqjs_memory* out) {
    out->fixed = (size_t)(ctx->heap_base - (uint8_t*)ctx);
    out->heap = (size_t)(ctx->heap_free - ctx->heap_base);
    out->stack = (size_t)(ctx->stack_top - (uint8_t*)ctx->sp);
    out->free_bytes = (size_t)((uint8_t*)ctx->sp - ctx->heap_free);
}

void rp_mqjs_keep_bytecode_names(uint8_t* bytecode, uint32_t size) {
    uint8_t* ptr = bytecode + sizeof(JSBytecodeHeader);
    uint8_t* end = bytecode + size;
    while (ptr < end) {
        JSMemBlockHeader* mb = (JSMemBlockHeader*)ptr;
        if (mb->mtag == JS_MTAG_STRING) mb->gc_mark = 1;
        ptr += get_mblock_size(ptr);
    }
}

/* The host's reads and writes of its own arrays and byte views, in place: what a warmed block runs a message through
 * (rp_mqjs.h says why each exists). None allocates, so none collects. */

JSValue rp_mqjs_array_get(JSContext* ctx, JSValue v, uint32_t i) {
    if (JS_IsPtr(v)) {
        JSObject* p = JS_VALUE_TO_PTR(v);
        if (p->mtag == JS_MTAG_OBJECT && p->class_id == JS_CLASS_ARRAY && i < p->u.array.len) {
            const JSValueArray* tab = JS_VALUE_TO_PTR(p->u.array.tab);
            return tab->arr[i];
        }
    }
    return JS_GetPropertyUint32(ctx, v, i);
}

int rp_mqjs_array_put(JSContext* ctx, JSValue v, uint32_t i, JSValue item) {
    if (JS_IsPtr(v)) {
        JSObject* p = JS_VALUE_TO_PTR(v);
        if (p->mtag == JS_MTAG_OBJECT && p->class_id == JS_CLASS_ARRAY && i < p->u.array.len) {
            JSValueArray* tab = JS_VALUE_TO_PTR(p->u.array.tab);
            tab->arr[i] = item;
            return 0;
        }
    }
    return JS_IsException(JS_SetPropertyUint32(ctx, v, i, item)) ? -1 : 0;
}

uint8_t* rp_mqjs_bytes(JSContext* ctx, JSValue v, uint32_t* length) {
    (void)ctx;
    if (!JS_IsPtr(v)) return NULL;
    JSObject* p = JS_VALUE_TO_PTR(v);
    if (p->mtag != JS_MTAG_OBJECT || (p->class_id != JS_CLASS_UINT8_ARRAY && p->class_id != JS_CLASS_UINT8C_ARRAY))
        return NULL;
    JSObject*    buffer = JS_VALUE_TO_PTR(p->u.typed_array.buffer);
    JSByteArray* bytes = JS_VALUE_TO_PTR(buffer->u.array_buffer.byte_buffer);
    *length = p->u.typed_array.len;
    return bytes->buf + p->u.typed_array.offset;
}

int rp_mqjs_is_fixed(JSContext* ctx, JSValue v) { return JS_IsPtr(v) && JS_IS_ROM_PTR(ctx, JS_VALUE_TO_PTR(v)); }

/* The five wrappers the header comment describes. */

static int rp_mqjs_concat_subst(JSContext* ctx, void* buffer, JSValue* str, JSValue* rep, uint32_t pos,
                                uint32_t end_of_match, JSValue* capture_buf, uint32_t captures_len, JSValue* needle) {
    StringBuffer* b = buffer;
    if (JS_IsException(b->buffer_ref.val)) return -1;
    if (rp_mqjs_upstream_concat_subst(ctx, b, str, rep, pos, end_of_match, capture_buf, captures_len, needle) < 0) {
        b->buffer_ref.val = JS_EXCEPTION;
        return -1;
    }
    return 0;
}

static int rp_mqjs_buffer_puts(JSContext* ctx, void* buffer, const char* str) {
    StringBuffer* b = buffer;
    if (rp_mqjs_upstream_buffer_puts(ctx, b, str) < 0) {
        b->buffer_ref.val = JS_EXCEPTION;
        return -1;
    }
    return 0;
}

JSValue rp_mqjs_js_object_hasOwnProperty(JSContext* ctx, JSValue* this_val, int argc, JSValue* argv) {
    if (JS_IsObject(ctx, *this_val)) {
        const JSValue key = JS_ToPropertyKey(ctx, argv[0]);
        if (JS_IsException(key)) return JS_EXCEPTION;
        argv[0] = key;
    }
    return rp_mqjs_upstream_object_hasOwnProperty(ctx, this_val, argc, argv);
}

JSValue rp_mqjs_js_string_repeat(JSContext* ctx, JSValue* this_val, int argc, JSValue* argv) {
    /* Upstream's order: a this that is no string is refused before the count is read. */
    if (JS_IsString(ctx, *this_val)) {
        int n;
        if (JS_ToInt32Sat(ctx, &n, argv[0])) return JS_EXCEPTION;
        const JSValue count = JS_NewInt32(ctx, n);
        if (JS_IsException(count)) return JS_EXCEPTION;
        argv[0] = count;
    }
    return rp_mqjs_upstream_string_repeat(ctx, this_val, argc, argv);
}

static int js_array_sort_cmp(size_t i1, size_t i2, void* opaque) {
    JSArraySortContext* s = opaque;
    JSContext*          ctx = s->ctx;
    JSValueArray*       arr;
    int                 cmp, j1, j2;

    (void)rp_mqjs_upstream_array_sort_cmp;
    if (s->exception) return 0;

    arr = JS_VALUE_TO_PTR(*s->parr);
    if (s->pfunc) {
        JSValue res;
        /* A comparator is held to answer 0 for the same value: it is not called. */
        if (arr->arr[2 * i1] == arr->arr[2 * i2]) goto cmp_same;
        if (JS_StackCheck(ctx, 4)) goto exception;
        arr = JS_VALUE_TO_PTR(*s->parr);

        JS_PushArg(ctx, arr->arr[2 * i2]);
        JS_PushArg(ctx, arr->arr[2 * i1]); /* arg0 */
        JS_PushArg(ctx, *s->pfunc);        /* func */
        JS_PushArg(ctx, JS_UNDEFINED);     /* this */
        res = JS_Call(ctx, 2);
        if (JS_IsException(res)) goto exception;
        if (JS_IsInt(res)) {
            int val = JS_VALUE_GET_INT(res);
            cmp = (val > 0) - (val < 0);
        } else {
            double val;
            if (JS_ToNumber(ctx, &val, res)) goto exception;
            cmp = (val > 0) - (val < 0);
        }
    } else {
        JSValue  str1, str2;
        JSGCRef  str1_ref;

        str1 = arr->arr[2 * i1];
        if (!JS_IsString(ctx, str1)) {
            str1 = JS_ToString(ctx, str1);
            if (JS_IsException(str1)) goto exception;
            arr = JS_VALUE_TO_PTR(*s->parr);
        }
        str2 = arr->arr[2 * i2];
        if (!JS_IsString(ctx, str2)) {
            JS_PUSH_VALUE(ctx, str1);
            str2 = JS_ToString(ctx, str2);
            JS_POP_VALUE(ctx, str1);
            if (JS_IsException(str2)) goto exception;
        }
        cmp = js_string_compare(ctx, str1, str2);
    }
    if (cmp != 0) return cmp;
cmp_same:
    /* The sort is stable: equal values keep their order. */
    arr = JS_VALUE_TO_PTR(*s->parr);
    j1 = JS_VALUE_GET_INT(arr->arr[2 * i1 + 1]);
    j2 = JS_VALUE_GET_INT(arr->arr[2 * i2 + 1]);
    return (j1 > j2) - (j1 < j2);

exception:
    s->exception = TRUE;
    return 0;
}

void rp_mqjs_limit_reentries(JSContext* ctx, int calls) {
    if (calls < 1) calls = 1;
    if (calls > JS_MAX_CALL_RECURSE) calls = JS_MAX_CALL_RECURSE;
    ctx->js_call_rec_count = JS_MAX_CALL_RECURSE - calls;
}

/* The host's buffers and views, made by the engine's own constructors (rp_mqjs.h): with no JS_Call, so at any depth
 * of the C re-entries, and with no global looked up by name. The typed array's constructor reads its arguments before
 * it allocates, and roots the buffer it keeps across its allocation, so arguments in a C array are safe: given as short
 * integers, which make nothing (a number past JS_SHORTINT_MAX, 2^30 - 1, would be a boxed float, made on the heap,
 * which could move the buffer held in the array before the constructor reads it). A length or an offset past that is
 * refused; the host's are a queue's, of 4096 bytes and 2^20 messages at most. */

JSValue rp_mqjs_new_array_buffer(JSContext* ctx, uint32_t bytes) { return js_array_buffer_alloc(ctx, bytes); }

JSValue rp_mqjs_new_uint8_array(JSContext* ctx, uint32_t length) {
    if (length > JS_SHORTINT_MAX) return JS_ThrowRangeError(ctx, "invalid length");
    JSValue arg = JS_NewShortInt((int32_t)length);
    return js_typed_array_constructor(ctx, NULL, 1 | FRAME_CF_CTOR, &arg, JS_CLASS_UINT8_ARRAY);
}

JSValue rp_mqjs_new_uint8_view(JSContext* ctx, JSValue buffer, uint32_t offset, uint32_t length) {
    if (offset > JS_SHORTINT_MAX || length > JS_SHORTINT_MAX) return JS_ThrowRangeError(ctx, "invalid length");
    JSValue args[3] = {buffer, JS_NewShortInt((int32_t)offset), JS_NewShortInt((int32_t)length)};
    return js_typed_array_constructor(ctx, NULL, 3 | FRAME_CF_CTOR, args, JS_CLASS_UINT8_ARRAY);
}
