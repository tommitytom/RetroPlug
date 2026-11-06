/* The engine itself, compiled from this file in place of deps/mquickjs/mquickjs.c (cmake/mquickjs.cmake's
 * retroplug_mquickjs_engine, ENGINE), so that it is compiled once and RetroPlug's reads of the context's own
 * bookkeeping sit in the translation unit that defines struct JSContext, which no header declares: the vendored source
 * is included whole and unpatched, and what follows it is RetroPlug's (rp_mqjs.h declares it). */
#include "mquickjs.c"

#include "rp_mqjs.h"

void rp_mqjs_memory_of(JSContext* ctx, rp_mqjs_memory* out) {
    out->fixed = (size_t)(ctx->heap_base - (uint8_t*)ctx);
    out->heap = (size_t)(ctx->heap_free - ctx->heap_base);
    out->stack = (size_t)(ctx->stack_top - (uint8_t*)ctx->sp);
    out->free_bytes = (size_t)((uint8_t*)ctx->sp - ctx->heap_free);
}
