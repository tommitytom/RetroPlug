/* rp_mqjs.h - RetroPlug's standard library for MicroQuickJS, as the host sees it
 * (docs/modular-graph-phase-e4-design.md section 4).
 *
 * A context runs against one standard library: the tables MicroQuickJS's build tool compiles from a list of the
 * globals, classes and C functions it offers (mquickjs_build.c). RetroPlug's is rp_mqjs_stdlib.c: upstream's standard
 * classes (Object, Array, String, Number, Math, JSON, RegExp, the errors, ArrayBuffer and the typed arrays) and none of
 * its REPL's globals (print, gc, load, the timers, Date, performance, eval), plus a console whose log the host routes,
 * and the classes of the block API a script node's process sees: RpBlock (messages, send, control, emit, audio,
 * frames, transport), RpMessage (frame, data, flags), RpTransport (sampleRate, tempo, ppq, playing), RpControlStream
 * (start, changes) and RpControlChange (frame, value). cmake/mquickjs.cmake (retroplug_mquickjs_stdlib) builds the
 * tables at the host's width and at the box's 32 bits, and rp_mqjs_table.c compiles them with the C functions they
 * name.
 *
 * Every one of those C functions is one of two dispatchers, which hand the call to the host through the context's
 * opaque: a context's opaque points to a struct whose first member is a `const rp_mqjs_api*`. So one table, linked
 * once, serves every build of the host a binary holds (the desktop's and the device's, side by side in the device-mode
 * test), each reaching its own functions through the contexts it made. */
#ifndef RP_MQJS_H
#define RP_MQJS_H

#ifdef __cplusplus
extern "C" {
#endif

/* MicroQuickJS's header declares its C API with no linkage of its own, so C++ takes it inside this block. */
#include "mquickjs.h"

/* The block API's classes, after MicroQuickJS's own. */
#define RP_MQJS_CLASS_BLOCK          (JS_CLASS_USER + 0)
#define RP_MQJS_CLASS_MESSAGE        (JS_CLASS_USER + 1)
#define RP_MQJS_CLASS_TRANSPORT      (JS_CLASS_USER + 2)
#define RP_MQJS_CLASS_CONTROL_STREAM (JS_CLASS_USER + 3)
#define RP_MQJS_CLASS_CONTROL_CHANGE (JS_CLASS_USER + 4)
#define RP_MQJS_CLASS_COUNT          (JS_CLASS_USER + 5)

/* What a method call asks for (rp_mqjs_api.call's `magic`). */
#define RP_MQJS_MESSAGES 0
#define RP_MQJS_SEND     1
#define RP_MQJS_CONTROL  2
#define RP_MQJS_EMIT     3
#define RP_MQJS_AUDIO    4
#define RP_MQJS_LOG      5 /* console.log */

/* What a getter reads (rp_mqjs_api.get's `magic`). */
#define RP_MQJS_FRAMES         0  /* RpBlock */
#define RP_MQJS_TRANSPORT      1
#define RP_MQJS_MESSAGE_FRAME  2  /* RpMessage */
#define RP_MQJS_MESSAGE_DATA   3
#define RP_MQJS_MESSAGE_FLAGS  4
#define RP_MQJS_SAMPLE_RATE    5  /* RpTransport */
#define RP_MQJS_TEMPO          6
#define RP_MQJS_PPQ            7
#define RP_MQJS_PLAYING        8
#define RP_MQJS_STREAM_START   9  /* RpControlStream */
#define RP_MQJS_STREAM_CHANGES 10
#define RP_MQJS_CHANGE_FRAME   11 /* RpControlChange */
#define RP_MQJS_CHANGE_VALUE   12

/* The host's side of the standard library's C functions. */
typedef struct rp_mqjs_api {
    JSValue (*call)(JSContext* ctx, JSValue* this_val, int argc, JSValue* argv, int magic);
    JSValue (*get)(JSContext* ctx, JSValue* this_val, int argc, JSValue* argv, int magic);
} rp_mqjs_api;

/* The standard library's tables, at the width this is compiled for (rp_mqjs_table.c). */
extern const JSSTDLibraryDef rp_mqjs_stdlib;

/* What the public API reaches only by a property's name, which costs an allocation each time (JS_GetPropertyStr and
 * JS_SetPropertyStr make the name a string first), through the engine's own accessors instead, allocating nothing:
 * what a warmed block must not do. rp_mqjs_table.c, which compiles against the engine's private header. */
/* An array's or a typed array's length; UINT32_MAX for any other value. */
uint32_t rp_mqjs_length(JSContext* ctx, JSValue v);
/* An array's length set to n: 0, or -1 with the exception pending. Within the array's capacity and at least half of
 * it, the engine neither grows nor shrinks the array's storage; past it the engine grows it, which may collect, and
 * writes the array back through `array` after: so `array` must point at a rooted slot (a JSGCRef's), never at a copy
 * of the value on the C stack, which the collector cannot see. */
int rp_mqjs_set_length(JSContext* ctx, JSValue* array, uint32_t n);
/* An error's message (stack 0) or its stack (stack 1), as the error holds it; undefined for a value that is not an
 * error. */
JSValue rp_mqjs_error_field(JSContext* ctx, JSValue error, int stack);

/* A context's buffer as the engine has it at the call, read from its own JSContext (rp_mqjs_engine.c, which compiles
 * the engine): `fixed` what the engine takes of the buffer before its heap (its JSContext and the class prototypes),
 * `heap` how far the heap has grown (the live objects and, until the next collection, the garbage), `stack` the
 * stack's depth and `free_bytes` what lies between the two. */
typedef struct rp_mqjs_memory {
    size_t fixed;
    size_t heap;
    size_t stack;
    size_t free_bytes;
} rp_mqjs_memory;
void rp_mqjs_memory_of(JSContext* ctx, rp_mqjs_memory* out);

#ifdef __cplusplus
}
#endif

#endif /* RP_MQJS_H */
