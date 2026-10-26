/* rp_node.h - the RetroPlug node ABI.
 *
 * Every node reaches the planner through these structures, whether it is statically linked C++, a
 * downloaded native pack, or a TypeScript node the host wraps. Plain C so a dynamic library built by any
 * compiler can fill it in; nothing here may change shape without bumping RP_ABI_VERSION. Optional
 * capabilities are looked up by versioned string id through get_interface, so they can grow without
 * touching this core table (docs/modular-graph-design.md section 4).
 *
 * Threads: calls marked [control] run on the control thread and may allocate; calls marked [audio] run
 * on whichever thread drives the block and must not allocate, block, or throw. [control] and [audio]
 * calls on ONE instance may run at the same time, so a node shares no mutable state between them except
 * through the settings swap (prepare_settings / commit_settings). The exceptions are activate,
 * deactivate and destroy: a host never runs them while an [audio] call on that instance is in flight.
 *
 * One process may hold several hosts, each with its own control thread (every background render runs
 * its own engine). So the package- and type-level calls - the entry point, descriptor, layout_for,
 * query_memory, create, destroy - may run concurrently for different instances, and any lazy one-time
 * setup a pack does there must be thread-safe (std::call_once, a function-local static).
 *
 * Every type-level call but destroy is handed the rp_node_type it was made through, so one set of functions
 * can serve several types, each telling itself apart by its ctx (the host's script types are served so: one
 * type per definition a bundle registers, all behind the same functions).
 *
 * Every rp_node_vtable entry is required: a node with nothing to do supplies a no-op. get_interface may
 * return null for every id.
 *
 * Result codes are fixed-width integers rather than enum types: an enum's size is implementation-defined
 * (-fshort-enums makes it one byte on some embedded toolchains), and every value here crosses the ABI.
 */
#ifndef RP_NODE_H
#define RP_NODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Still 1 though rp_node_type has changed shape (its type-level calls take the type, and it gained ctx): ABI v1
 * has never shipped, so no pack was built against the old one. */
#define RP_ABI_VERSION 1u

/* The symbol a downloaded pack exports (an rp_package_entry_fn), and the visibility it needs. */
#define RP_PACKAGE_ENTRY_SYMBOL "rp_package_entry"
#ifndef RP_EXPORT
#if defined(_WIN32)
#define RP_EXPORT __declspec(dllexport)
#else
#define RP_EXPORT __attribute__((visibility("default")))
#endif
#endif

typedef struct rp_node rp_node;
typedef struct rp_host rp_host;

typedef uint32_t rp_status;
enum { RP_OK = 0u, RP_FAULT = 1u };

enum { RP_LOG_ERROR = 0, RP_LOG_WARNING = 1, RP_LOG_INFO = 2, RP_LOG_DEBUG = 3 };

/* A control-thread call that fails writes a NUL-terminated reason here. */
typedef struct rp_error { char message[256]; } rp_error;

enum { RP_SETTINGS_JSON = 0u };
typedef struct rp_settings {
    const char* data;     /* bytes, not NUL-terminated; UTF-8 text when the encoding is JSON */
    size_t      size;
    uint32_t    encoding; /* RP_SETTINGS_JSON on the desktop */
} rp_settings;

typedef struct rp_memory_req { size_t size; size_t align; } rp_memory_req;

/* Services the host offers a node. alloc/free are for nodes that declare the heap capability, and are
 * null on a target with no heap (where such a node is never created). get_interface is never null. */
struct rp_host {
    uint32_t abi_version;
    void*    ctx;
    void* (*alloc)(void* ctx, size_t size, size_t align);   /* [control] align <= alignof(max_align_t) */
    void  (*free)(void* ctx, void* ptr);                     /* [control] */
    void  (*log)(void* ctx, int level, const char* message); /* [control] level: RP_LOG_* */
    /* [any] An optional host service by versioned id, or null. This is how host services grow without
     * bumping RP_ABI_VERSION, the host-side twin of rp_node_vtable.get_interface. */
    const void* (*get_interface)(const rp_host* host, const char* id);
};

/* ---- block data ------------------------------------------------------------------------------- */

enum { RP_TRANSPORT_PLAYING = 1u << 0 };

typedef struct rp_transport {
    double   sample_rate;
    double   tempo; /* bpm */
    double   ppq;   /* at block start */
    uint32_t flags; /* RP_TRANSPORT_*; room to grow (recording, loop) without an ABI bump */
} rp_transport;

/* On a bytes port: everything up to and including this message must reach the device before what follows. */
enum { RP_MESSAGE_FLUSH = 1u << 0 };

/* One timestamped item on an events or bytes port. The payload lives in the queue's byte arena at
 * [offset, offset + size). flags: RP_MESSAGE_*. */
typedef struct rp_message {
    uint32_t frame;
    uint32_t size;
    uint32_t offset;
    uint32_t flags;
} rp_message;

/* A fixed-capacity, frame-ordered queue. Never grows: a push that does not fit is dropped and counted in
 * `dropped`, which is written on the audio thread; a host reads it there (or while quiescent) and
 * publishes it, never directly from the control thread. A wired output's queue holds what its port declares in
 * the node's layout ("capacity"), and its count is published and started again after every block. An output with
 * no wire writes the scratch queue every unconnected output shares, at the default 256 messages and 4096 bytes
 * whatever its port declares; nothing reads it, and its drops go uncounted. */
typedef struct rp_queue {
    rp_message* items;
    uint32_t    count;
    uint32_t    capacity;
    uint8_t*    bytes;
    uint32_t    bytes_used;
    uint32_t    bytes_capacity;
    uint32_t    dropped;
} rp_queue;

typedef struct rp_control_change { uint32_t frame; uint32_t reserved; double value; } rp_control_change;

/* A control stream: its value at block start plus this block's frame-ordered changes. */
typedef struct rp_control_queue {
    double             start;
    rp_control_change* changes;
    uint32_t           count;
    uint32_t           capacity;
    uint32_t           dropped;
} rp_control_queue;

/* A resource: a typed block of data created and freed only on the control thread. type is a
 * registered resource type id ("audio-buffer", "bytes", "string", ...); payload is interpreted by it. */
typedef struct rp_resource {
    const char* type;
    void*       payload;
} rp_resource;

/* The payload of an "audio-buffer" resource. */
typedef struct rp_audio_buffer {
    uint32_t channels;
    uint32_t frames;
    double   sample_rate;
    float**  data;      /* data[channel][frame] */
} rp_audio_buffer;

/* The payload of "bytes" and "string" resources. */
typedef struct rp_bytes { const uint8_t* data; size_t size; } rp_bytes;

typedef enum rp_port_kind {
    RP_PORT_AUDIO    = 0,
    RP_PORT_EVENTS   = 1,
    RP_PORT_BYTES    = 2,
    RP_PORT_CONTROL  = 3,
    RP_PORT_LINK     = 4,
    RP_PORT_RESOURCE = 5
} rp_port_kind;

typedef struct rp_audio_view {
    float* const* channels;  /* channels[c][frame]; never null: unconnected ports get silence / scratch */
    uint32_t      channel_count;
} rp_audio_view;

/* One port as the node sees it this block. Input views are read-only by contract. */
typedef struct rp_port_view {
    uint32_t kind; /* rp_port_kind */
    union {
        rp_audio_view     audio;
        rp_queue*         queue;    /* events, bytes */
        rp_control_queue* control;
        rp_resource**     resource; /* the slot holding the current handle (may hold null) */
    } u;
} rp_port_view;
/* A resource OUTPUT slot outlives plans, so a node rewrites each of its resource outputs every block, with
 * a handle this block's plan holds (one bound to the node, or a resource it was handed) or null. A handle
 * left over from an earlier block may belong to a plan that has since been freed. */

typedef struct rp_process {
    uint32_t                 frames;
    const rp_transport*      transport;
    const rp_port_view*      inputs;
    uint32_t                 input_count;
    const rp_port_view*      outputs;
    uint32_t                 output_count;
    rp_control_queue* const* params;  /* one per param, in layout order */
    uint32_t                 param_count;
} rp_process;

/* ---- the node --------------------------------------------------------------------------------- */

typedef uint32_t rp_prepare_result;
enum {
    RP_PREPARE_SWAP    = 0u, /* *prepared is ready to commit on the audio thread; the layout is unchanged */
    RP_PREPARE_REBUILD = 1u, /* the change needs a fresh instance (a new ROM, a layout it cannot reshape to) */
    RP_PREPARE_INVALID = 2u, /* rejected; err holds the reason */
    /* Keep this instance, with the layout layout_for gives the new settings: the host binds the new plan's
     * ports to it by id, and commits *prepared at the swap, so the first block on the new ports runs with
     * the new settings. What a param, a control output or a resource output slot holds carries over when
     * its id persists and it means the same: a param of the same type, range and enum labels (its name,
     * unit or default may change), an output of the same kind and resource type. The rest start afresh: a
     * param at its default, with its document value (if the document gives one) sent again, a control
     * output at 0, a resource slot empty. commit_settings runs on the audio thread and cannot allocate, so
     * anything the node sizes by its layout (per-port state, say) is built here, in *prepared. With the
     * layout unchanged it is a swap, so a prepare that cannot see the current layout may answer it for
     * every change it can keep. */
    RP_PREPARE_RESHAPE = 3u
};

typedef struct rp_node_vtable {
    /* [control] Must preserve state: a sample-rate or block-size change re-activates, never resets. */
    bool (*activate)(rp_node* node, double sample_rate, uint32_t max_frames, rp_error* err);
    /* [control] */
    void (*deactivate)(rp_node* node);
    /* [audio] The connected-output mask changed at a plan swap; connected[i] is 0 or 1 per output. The
     * array is valid only during the call: copy what you keep. Not called again until the mask changes,
     * or a reshape changes the node's layout: that always brings one, after its commit_settings, sized by
     * the new layout. */
    void (*ports_changed)(rp_node* node, const uint8_t* connected, uint32_t output_count);
    /* [audio] One block. Must write every frame of every CONNECTED audio output (unconnected ones get a
     * shared scratch buffer, so skipping them is safe, and is what the connected mask is for). Return
     * RP_FAULT to be bypassed from then on. */
    rp_status (*process)(rp_node* node, const rp_process* block);
    /* [control] Validate new settings and decide how this instance takes them: a swap (its layout
     * unchanged), a reshape or a rebuild. Asked for every settings change, whether or not the layout
     * changed. Unless the type declares RP_TYPE_STATELESS_PREPARE, a host never calls this while a
     * commit_settings for the same node may still be in flight, so the node may read its current settings
     * here (to compare against). */
    rp_prepare_result (*prepare_settings)(rp_node* node, const rp_settings* settings, void** prepared,
                                          rp_error* err);
    /* [audio] Swap in a prepared settings object; hand the previous one back through *old. */
    void (*commit_settings)(rp_node* node, void* prepared, void** old);
    /* [control] Free a settings object handed back by commit_settings (or a prepared one never committed). */
    void (*release_settings)(rp_node* node, void* old);
    /* [any] An optional interface by versioned id ("rp.step/1"), or null. */
    const void* (*get_interface)(rp_node* node, const char* id);
} rp_node_vtable;

struct rp_node {
    const rp_node_vtable* vt;
};

/* rp_node_type.flags: promises a type makes the host, fixed for the life of the package. Every bit is a
 * relaxation: it lets the host skip a precaution, and a host that ignores it is only more cautious. So a
 * host ignores the bits it does not know (NodeRegistry registers a type whatever its flags hold), and a
 * pack built against a newer header still loads on an older host. A promise no host could safely ignore
 * needs a new RP_ABI_VERSION, not a bit. */
enum {
    /* Its prepare_settings reads nothing commit_settings writes (the instance's current settings among
     * them) and writes nothing commit_settings reads (what a commit needs travels in *prepared), and its
     * answer depends only on the new settings and on what create fixed. The host may then prepare again
     * while an earlier commit for the same instance is in flight, so setGraph need not wait for the audio
     * thread to adopt it. The host judges each change against the last PUBLISHED plan, which the audio
     * thread may not have committed yet, so an answer that read the committed settings would compare with
     * the wrong ones. */
    RP_TYPE_STATELESS_PREPARE = 1u << 0
};

typedef struct rp_node_type rp_node_type;
struct rp_node_type {
    const char* id;
    /* [control] The type descriptor JSON; valid for the life of the package. */
    const char* (*descriptor)(const rp_node_type* type);
    /* [control] The resolved layout JSON (ports, params, exclusive groups) a node of this type has with
     * these settings: a function of the settings alone, so the host can validate a whole document before
     * it creates anything. It is an instance's layout until a settings change alters it, which the
     * instance then takes as a rebuild or a reshape (prepare_settings), never a swap. Writes it
     * NUL-terminated into out[0..capacity) when it fits and returns its length without the NUL, so a
     * caller whose buffer is too small learns the size and asks again. Returns 0, with err filled, for
     * settings it refuses. */
    size_t (*layout_for)(const rp_node_type* type, const rp_settings* settings, char* out, size_t capacity,
                         rp_error* err);
    /* [control] Size and alignment of one instance for these settings. */
    bool (*query_memory)(const rp_node_type* type, const rp_settings* settings, rp_memory_req* out, rp_error* err);
    /* [control] Construct into host-supplied memory. Returns null (and fills err) on failure. */
    rp_node* (*create)(const rp_node_type* type, const rp_host* host, void* memory, const rp_settings* settings,
                       rp_error* err);
    /* [control] Destruct. The host frees the memory afterwards. */
    void (*destroy)(rp_node* node);
    uint32_t flags; /* RP_TYPE_*; 0 promises nothing */
    /* The type's own data, for functions that serve several types; null when a type has functions of its own.
     * The host never reads it. */
    void* ctx;
};

typedef struct rp_package {
    uint32_t                   abi_version;
    const char*                manifest_json;
    uint32_t                   node_type_count;
    const rp_node_type* const* node_types;
} rp_package;

/* A downloaded pack exports this symbol; statically linked packages hand the same struct to the
 * generated registration list. Declared here, inside the header's C linkage, so a C++ pack that defines
 * it cannot forget extern "C". */
typedef const rp_package* (*rp_package_entry_fn)(void);
RP_EXPORT const rp_package* rp_package_entry(void);

/* ---- optional interfaces ------------------------------------------------------------------------ */

/* "rp.step/1": the block triad coupled groups run in lockstep. All three are [audio]. */
#define RP_STEP_INTERFACE "rp.step/1"
typedef uint32_t rp_step_result;
enum { RP_STEP_BELOW = 0u, RP_STEP_DONE = 1u, RP_STEP_FAULT = 2u };
typedef struct rp_step_interface {
    void (*prepare)(rp_node* node, const rp_process* block);
    /* Advance toward target_frames by a bounded amount (an instruction, a scanline). Return RP_STEP_DONE
     * once this node has produced target_frames. It may be called again after that, and must then do
     * nothing and return RP_STEP_DONE. RP_STEP_FAULT, or any value that is not BELOW or DONE, bypasses the
     * node as a faulting process() would. */
    rp_step_result (*step)(rp_node* node, uint32_t target_frames);
    void (*finish)(rp_node* node, const rp_process* block);
} rp_step_interface;

/* "rp.link/1": told its peers whenever the coupled group it belongs to changes. [audio] The peers array
 * is valid only during the call (the node pointers in it stay valid until the next set_peers): copy what
 * you keep. A peer may be bypassed later (it faulted), after which it is no longer stepped; a node must
 * tolerate a peer that has stopped advancing rather than wait on it. */
#define RP_LINK_INTERFACE "rp.link/1"
typedef struct rp_link_interface {
    void (*set_peers)(rp_node* node, rp_node* const* peers, uint32_t peer_count);
} rp_link_interface;

#ifdef __cplusplus
}
#endif

#endif /* RP_NODE_H */
