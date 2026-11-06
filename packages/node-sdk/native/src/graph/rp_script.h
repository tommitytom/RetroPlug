/* rp_script.h - what a host and a script node say to each other beyond the node ABI (graph/rp_node.h), for the
 * MicroQuickJS script host (graph-runtime's script/MqjsHost.hpp, docs/modular-graph-phase-e4-design.md section 4).
 * Plain C, so a device's host (the box) answers the host's half without the script host's header, and the exporter
 * reads a script node's fault with no more than this.
 *
 * Every id is asked by rp_get_interface as any optional interface is: a host's through rp_host.get_interface, a node's
 * through its vtable's. A host that offers neither host interface is still a host a script node runs under. */
#ifndef RP_SCRIPT_H
#define RP_SCRIPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "graph/rp_node.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The clock a host offers its script nodes' deadline [audio]: microseconds from any start, on the thread that runs
 * blocks, 0 when it cannot read one (a call it could not time runs untimed, never at once past its deadline).
 * deadline_micros is each call's deadline; 0 asks the script host's default for a host's clock (the box's figure,
 * script/MqjsHost.hpp's kHostClockDeadlineMicros). */
#define RP_HOST_CLOCK_INTERFACE "rp.host.clock/1"
typedef struct rp_host_clock {
    void*    ctx;
    uint64_t (*micros)(void* ctx);
    uint32_t deadline_micros;
} rp_host_clock;

/* The deadline a desktop times a script node's call by, by the calling thread's CPU time, when its host offers no
 * clock, and the one the exporter's trial load offers (script/MqjsHost.hpp says why it is this long). */
#define RP_SCRIPT_DESKTOP_DEADLINE_MICROS 500000u

/* Where a script pack's console.log goes [audio]: the bytes it prints, its newline included, in pieces of any size. A
 * host that offers none drops what a pack logs. A string argument is written as it is; any other is MicroQuickJS's own
 * print of the value (JS_PrintValueF, which mquickjs_priv.h's JS_DUMP builds in: an engine built without it writes
 * nothing for one). */
#define RP_HOST_SCRIPT_LOG_INTERFACE "rp.host.script-log/1"
typedef struct rp_host_script_log {
    void* ctx;
    void (*write)(void* ctx, const char* text, size_t size);
} rp_host_script_log;

/* A script node's fault, through the node's get_interface [any, once the node has faulted, or while no call into it
 * runs]: the words of its first fault, NUL-terminated, or null while it has none; and its kind (RP_SCRIPT_FAULT_*, 0
 * for none). A script node's instance is made at its first call on the thread that runs blocks, so a fault create
 * or its first block causes is there after one block: the exporter's trial load runs one, then reads every node's. */
#define RP_SCRIPT_FAULT_INTERFACE "rp.script.fault/1"
enum {
    RP_SCRIPT_FAULT_THREW = 1,    /* a call threw (create, layout, process or a hook) */
    RP_SCRIPT_FAULT_DEADLINE = 2, /* a call ran past its deadline */
    RP_SCRIPT_FAULT_HEAP = 3,     /* its pack's heap ran out, in its call or another node's */
    RP_SCRIPT_FAULT_REFUSED = 4   /* what a call returned, or what it was handed, breaks the contract */
};
typedef struct rp_script_fault {
    const char* (*reason)(rp_node* node);
    uint32_t (*kind)(rp_node* node);
} rp_script_fault;

/* A script node's pack's context as it stands, through the node's get_interface [the thread that runs blocks, or
 * while none runs], read from the engine's own JSContext. `collect` runs a collection first, so `heap` is what the
 * live objects take; without, it is how far the heap has grown since the last collection, which a block that allocates
 * nothing leaves where it was. `bytes` is the context's buffer (the manifest's heapBytes), `fixed` what MicroQuickJS
 * takes of it before its heap (its JSContext and the class prototypes) and `stack` the stack's depth at the call.
 * False while the node has no instance (before its first call on that thread). */
#define RP_SCRIPT_HEAP_INTERFACE "rp.script.heap/1"
typedef struct rp_script_heap_use {
    size_t bytes;
    size_t fixed;
    size_t heap;
    size_t stack;
} rp_script_heap_use;
typedef struct rp_script_heap {
    bool (*use)(rp_node* node, bool collect, rp_script_heap_use* out);
} rp_script_heap;

#ifdef __cplusplus
}
#endif

#endif /* RP_SCRIPT_H */
