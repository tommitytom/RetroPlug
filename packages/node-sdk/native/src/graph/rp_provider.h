/* rp_provider.h - how a product starts a pack's device service without naming it (docs/modular-graph-phase-e2-design.md
 * section 6).
 *
 * A pack that owns hardware on the desktop (the N8's serial link, its persisted state, the cart link its node binds)
 * names a provider in its manifest: an entry of its desktop target with a `provider` (the C function returning this
 * table) and, when it has a facet, an `rpc` (the header and the function that mount it). cmake/packages.cmake generates
 * each product's rp_start_providers, rp_restore_providers, rp_stop_providers and rp_register_package_rpc from its
 * packages' manifests, so a product starts, restores, stops and mounts every provider it carries in one place and names
 * no class of any of them.
 *
 * Desktop only: a provider owns threads, ports and files, and a device image has none of those to give it (the Pico's
 * box drives its hardware itself until E3 makes it a graph host). Plain C, as the rest of the device ABI is, so a
 * provider in another language can be one. A provider is reached only through the table cmake/packages.cmake generates
 * from the manifests at build time, an extern "C" symbol the program links: no provider is loaded at run time, as a
 * script node pack is, so a downloaded pack cannot carry one until a product has a way to load code.
 *
 * Threads: every call here is [control]. A provider's device threads are its own; what it hands the engine goes
 * through rp_device_host's install and clear, whose rules are rp_device.h's. */
#ifndef RP_PROVIDER_H
#define RP_PROVIDER_H

#ifdef RP_DEVICE
#error "graph/rp_provider.h is desktop-only: a device image runs no provider"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 2: reserve_midi and suspend_hardware_inputs, the two capability calls a device provider needs from the product that
 * owns the shared MIDI stream (docs/modular-graph-phase-f3-design.md section 3), and restored, the rendezvous end. A
 * host and a provider refuse each other across a version, as they did at 1. */
#define RP_PROVIDER_ABI_VERSION 2u

/* What the product hands a provider to create it with: the engine's device registry and where persisted state lives.
 * The host and its strings outlive the provider (a product stops its providers before it destroys the engine). The rule
 * runs both ways: neither install nor clear throws (a C++ host catches what its own code throws and answers false, or
 * carries on), as no call of the provider's table does. */
typedef struct rp_device_host {
    uint32_t    abi_version; /* RP_PROVIDER_ABI_VERSION */
    void*       ctx;
    const char* config_dir;  /* where a provider keeps its persisted state (the N8's n8.cfg) */
    /* Install `link` under `name` with the interface `interface_id` (rp_device.h): a node asking for both is handed
     * `link`, and its acquire and release reach the hooks (null for none). False when the registry refuses it (a full
     * table, the name and id or the link taken, a name or id too long): the node then runs on its own stub. */
    bool (*install)(void* ctx, const char* name, const char* interface_id, void* link, void (*acquire)(void* link),
                    void (*release)(void* link));
    /* Take `link` back: a lookup after it finds nothing, and a node's later release of it reaches nothing. The provider
     * clears what it installed before it frees the link, and frees it only after the product's plans are reclaimed
     * (rp_device.h). A pointer that was never installed is cleared to nothing: the C table answers no status, and a
     * provider clears the exact pointer it installed. */
    void (*clear)(void* ctx, void* link);
    /* Keep a MIDI input/output pair OUT of the product's shared stream: a device a provider holds is that provider's, and
     * a second reader would answer its traffic and take its presses as music. Empty names undo it. True when the pair is
     * out (or was never in); a product with no shared stream answers true. The product reopens only what actually moves,
     * and does the audio stop and restart a reopen needs (a claimed pair has to be reserved before the stream opens it,
     * docs/modular-graph-phase-f3-design.md section 3). [control] */
    bool (*reserve_midi)(void* ctx, const char* input, const char* output);
    /* A scan's held edge: while it holds loose inputs, the product suspends its shared stream's hardware inputs (an
     * exclusive input on Windows could not otherwise be opened by the scan) and resumes them when the scan is done.
     * Every platform makes the call; a product with nothing to suspend ignores it. [control] */
    void (*suspend_hardware_inputs)(void* ctx, bool suspended);
    /* The control plane's half of the startup rendezvous is done (docs/modular-graph-phase-f3-design.md section 3): TS
     * read its selection, claimed the pair through the provider's facet and called the facet's restored(), so a product
     * that held MIDI and audio for the claim starts them now. A product that holds nothing ignores it, and a facet that
     * is never asked answers it itself. [control] */
    void (*restored)(void* ctx);
} rp_device_host;

typedef struct rp_provider {
    /* RP_PROVIDER_ABI_VERSION, first so that it reads the same in every layout: a host refuses a provider built
     * against another one (rp::Providers says so on stderr and carries on without it), as a provider refuses a host
     * whose rp_device_host is of another version. */
    uint32_t    abi_version;
    const char* id; /* the provider's name in a log line ("n8") */
    /* [control] After the engine, before the control plane can publish a graph. Returns the provider's state, or null
     * when it could not start (the product carries on without it, and its facet is not mounted). No call of this
     * table throws: a C++ provider catches what its own code throws and answers null, or logs and carries on. */
    void* (*create)(const rp_device_host* host);
    /* [control] Bring the persisted state back (reconnect a link left enabled), where the product restores its own. */
    void (*restore)(void* self);
    /* What the pack's rpc registration mounts (the `register` its manifest names is handed it), or null for none. */
    void* (*rpc_service)(void* self);
    /* [control] After the product's plans are reclaimed: clear what it installed and free everything. The plans come
     * first because a node holds the link it acquired and may use it in its destroy (rp_device.h): clearing the link
     * makes a later lookup answer null, but frees nothing a node already holds. */
    void (*destroy)(void* self);
    /* [control] One host frame, from the product's own frame loop, on the thread that paints: write what the audio
     * thread has queued for the device since the last one. A provider whose device write can block (WinMM waits for a
     * bulk SysEx to leave the wire) keeps a writer thread of its own, and this is what wakes it, so the frame the
     * session painted is the frame the device hears rather than the next timeout. Never blocks. Null is the same as a
     * provider that has nothing to write. */
    void (*wake)(void* self);
} rp_provider;

/* A manifest's `provider` names a function of this type, as an `entry` names one returning an rp_package. */
typedef const rp_provider* (*rp_provider_fn)(void);

#ifdef __cplusplus
}
#endif

#endif /* RP_PROVIDER_H */
