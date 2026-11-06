/* rp_mqjs.h - RetroPlug's standard library for MicroQuickJS, as the host sees it
 * (docs/modular-graph-phase-e4-design.md section 4).
 *
 * A context runs against one standard library: the tables MicroQuickJS's build tool compiles from a list of the
 * globals, classes and C functions it offers (mquickjs_build.c). RetroPlug's is rp_mqjs_stdlib.c: upstream's standard
 * classes (Object, Array, String, Number, Math, JSON, RegExp, the errors, ArrayBuffer and the typed arrays) and none of
 * its REPL's globals (print, gc, load, the timers, Date, performance, eval), plus a console whose log the host routes,
 * and the classes of the block API a script node's process sees: RpBlock (messages, send, forward, control, emit,
 * audio, frames, transport), RpMessage (frame, data, flags), RpTransport (sampleRate, tempo, ppq, playing),
 * RpControlStream (start, changes) and RpControlChange (frame, value). cmake/mquickjs.cmake (retroplug_mquickjs_stdlib)
 * builds the tables at the host's width and at the box's 32 bits, and rp_mqjs_table.c compiles them with the C
 * functions they name.
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

/* MicroQuickJS's header declares its C API with no linkage of its own, so C++ takes it inside this block; its names
 * are RetroPlug's first (rp_mqjs_names.h says why). */
#include "rp_mqjs_names.h"
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
#define RP_MQJS_FORWARD  6

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

/* The host's side of the standard library's C functions, and of the engine's one report (rp_mqjs_engine.c): ran_out,
 * called when an allocation, or a push onto the context's stack, finds no room left in the context's buffer after the
 * engine has collected, once the engine has thrown out of memory for it. What the host tells a heap that ran out
 * from by, since what is thrown cannot tell it: a script may throw null, or an error saying "out of memory", and the
 * engine throws its out-of-memory error, which a script may catch and throw again, for an array or a buffer past the
 * engine's size limits too, with room left. It runs inside the failing allocation: it must not call the engine. */
typedef struct rp_mqjs_api {
    JSValue (*call)(JSContext* ctx, JSValue* this_val, int argc, JSValue* argv, int magic);
    JSValue (*get)(JSContext* ctx, JSValue* this_val, int argc, JSValue* argv, int magic);
    void (*ran_out)(JSContext* ctx);
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

/* What a warmed block reads and writes through, in place and with no allocation (rp_mqjs_engine.c, which defines the
 * engine's objects): where JS_GetPropertyUint32 and JS_SetPropertyUint32 look an element up as a property, by a key,
 * and a typed array's bytes go through a value each. The host's arrays are its own and dense.
 *
 * rp_mqjs_array_get: element i of an array that holds it, read in place; of any other value, JS_GetPropertyUint32's.
 * rp_mqjs_array_put: element i of an array that holds it, written in place (0); for any other value, or past its
 *   length, JS_SetPropertyUint32 (which may grow it, and collect): 0, or -1 with the exception pending.
 * rp_mqjs_bytes: a Uint8Array's (or Uint8ClampedArray's) bytes and its length in place, valid until the next call that
 *   may allocate (the collector moves its buffer); null for any other value.
 * rp_mqjs_is_fixed: whether a value is an object or string outside the context's heap (a ROM atom of the standard
 *   library, a constant of the loaded bytecode), which never moves and lives as long as the context: what may be
 *   kept, by value, across collections. */
JSValue  rp_mqjs_array_get(JSContext* ctx, JSValue v, uint32_t i);
int      rp_mqjs_array_put(JSContext* ctx, JSValue v, uint32_t i, JSValue item);
uint8_t* rp_mqjs_bytes(JSContext* ctx, JSValue v, uint32_t* length);
int      rp_mqjs_is_fixed(JSContext* ctx, JSValue v);

/* How deep a context's C re-entries go: the calls into JavaScript that may be on the C stack at once, the host's own
 * call into a node (its process, a hook) the first, each native that calls back into JavaScript (Array's iterations and
 * sort, String's replace, a getter, valueOf) one more, and the host's refill of a block's arrays none
 * (rp_mqjs_new_uint8_view below). MicroQuickJS allows JS_MAX_CALL_RECURSE (8) and refuses the next with an
 * InternalError ("C stack overflow"); the host holds every call into a node's context after its load (its nodes'
 * create, hooks and blocks) to RP_MQJS_MAX_REENTRIES (rp_mqjs_limit_reentries; the load, which runs the pack's
 * definitions once at setup, the desktop's control context and the toolchain's script runs keep the engine's 8, as the
 * SDK's definitions nest deeper), so that the deepest chain of C frames a node can make fits the box's 4 KB stack with
 * room for its interrupts. Each level is about 290 bytes on the RP2350 at its largest (String's replace, a native that
 * calls back); at 4 the deepest chain (a block, three such levels, and at the last a failing JSON.parse's parser and
 * error) measured 2360 bytes of the 4096 on the RP2350, where at 8 such a chain took 3320. */
#define RP_MQJS_MAX_REENTRIES 4
void rp_mqjs_limit_reentries(JSContext* ctx, int calls);

/* The calls and branches the interpreter runs before it next polls the context's interrupt handler, set from within
 * the handler (rp_mqjs_engine.c): the engine sets the count to its own 10000 before each call of the handler, so what
 * the handler sets is the next poll's distance. Clamped to 1 to 10000. A builtin that polls in its own loop (the sort's
 * comparison, the string searches, join on what is no array) counts its work off the same count. */
void rp_mqjs_set_poll_interval(JSContext* ctx, int units);

/* A fresh ArrayBuffer of `bytes`, a Uint8Array of `length` and a Uint8Array viewing `length` bytes of `buffer` (an
 * ArrayBuffer) from `offset`, made by the engine's constructors with no call into JavaScript (rp_mqjs_engine.c), or
 * JS_EXCEPTION with the exception pending. */
JSValue rp_mqjs_new_array_buffer(JSContext* ctx, uint32_t bytes);
JSValue rp_mqjs_new_uint8_array(JSContext* ctx, uint32_t length);
JSValue rp_mqjs_new_uint8_view(JSContext* ctx, JSValue buffer, uint32_t offset, uint32_t length);

/* Marks every string of a relocated bytecode as one the collector keeps (rp_mqjs_engine.c), before JS_LoadBytecode.
 * The bytecode lies outside the context's heap, so the collector never marks its strings, and its table of the names
 * made at run time is weak: a string constant the code first uses as a property name (`g["__rp_scriptNodes"]`) is
 * marked unique where it lies and entered in that table, and the next collection drops it again while the string
 * stays marked unique. The same name made from another string after that (JS_GetPropertyStr's) is entered anew, a
 * second name of the same text, which finds nothing the first one keyed. Marked kept, the bytecode's strings stay in
 * the table for the context's life, as its own names do. */
void rp_mqjs_keep_bytecode_names(uint8_t* bytecode, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* RP_MQJS_H */
