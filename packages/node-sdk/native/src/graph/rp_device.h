/* rp_device.h - the device links a node binds by name (docs/modular-graph-phase-e2-design.md section 5).
 *
 * A host owns the hardware - the serial port, the MIDI ports, reconnecting, the farewell a device needs - and installs
 * a link for each device under a name and the id of the interface the link implements. A node that drives the device
 * asks for both and is handed the link, or null when the host has none: the interface between the node and its link is
 * the device's own contract (a pack's header, rp::n8::CartLink for the N8's cart), and the host knows nothing of it but
 * its id. Plain C so a C node can bind one; the link it is handed is whatever that interface says it is.
 *
 * Threads: acquire and release are [control]. A node acquires in its activate and releases in its destroy, and both
 * run on the control thread (a plan's reclaim included). A reactivate runs deactivate and activate again on one
 * instance (a sample-rate change, a block growth), so a node that already holds a link keeps it and does not acquire a
 * second: the host forwards each acquire a node makes to the link's own hook, and the balance is the node's to keep.
 * The rule for freeing a link, once: a host frees a link only after every plan that could hold it is destroyed. A node
 * holds the link it acquired for the life of its plan and uses it in its destroy, not only to give it back (an N8 cart
 * node hands its held notes to the link there), so a host tears its plans down, then takes its links back, then frees
 * them. Taking a link back (the host's clear) only stops new acquires; it does not make freeing safe.
 *
 * A link pointer identifies one installation: release is handed the pointer alone, so a host installs each link under
 * one name and one interface id, and refuses the same pointer a second time. A device that offers two interfaces (or
 * two versions of one) installs a distinct object for each, never one context struct under two ids. */
#ifndef RP_DEVICE_H
#define RP_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The longest name and interface id a host installs a link under, in bytes, the NUL not counted: a host refuses an
 * install past either, and an acquire with a longer string finds nothing. A pack picks its names and ids within them.
 * Both are UTF-8. */
#define RP_DEVICE_NAME_MAX 31
#define RP_DEVICE_INTERFACE_ID_MAX 63

/* The host's device links, through rp_host.get_interface (a node takes the interface in bind_host). /2: links by name
 * and interface id, handed out as the interface's own pointer; /1 handed out C++ classes of the engine's, one accessor
 * per device. */
#define RP_HOST_DEVICES_INTERFACE "rp.host.devices/2"
typedef struct rp_host_devices {
    void* ctx;
    /* [control] The link the host installed under `name` with the interface `interface_id`, or null: a name the host
     * has no device for, or a device whose link implements another interface (another version of it included). A
     * node that is answered null runs on a disconnected stub of its own, so a document plans and runs the same in
     * every host. The host forwards the acquire to the link's own hook (a cart link counts the nodes holding it). */
    void* (*acquire)(void* ctx, const char* name, const char* interface_id);
    /* [control] A node is done with a link it acquired here. The link is told only while it is still the one the host
     * installed. That identity check guards a release that arrives out of order, after the host took the link back; it
     * is not a licence to free a link a plan still holds (see the rule above). Null is ignored. */
    void (*release)(void* ctx, void* link);
} rp_host_devices;

#ifdef __cplusplus
}
#endif

#endif /* RP_DEVICE_H */
