/* rp_stage.h - a message a host hands a node outside its inputs (docs/modular-graph-phase-c5-design.md section 4).
 *
 * The one interface of the device ABI that a node presents rather than takes: the host asks for it through the node's
 * get_interface. The UI stages a settings message at one node by the document id it gives it, so it reaches the cart
 * whose menu it is whatever the project's musical routing does with channels. The host asks the node the running plan
 * places under that id, so it learns no node's type: a system node hands the message to its core's ingress, a device
 * node to its device. In the SDK rather than an engine header because a device's node presents it and includes nothing
 * of the engine's.
 *
 * [audio] - the call happens on the thread that drains the host's command ring, before the block that follows it, so a
 * staged message lands at frame 0 of the next block. */
#ifndef RP_STAGE_H
#define RP_STAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RP_STAGE_INTERFACE "rp.stage/1"
typedef struct rp_host_stage {
    void* ctx;
    /* Hand `size` staged bytes to the node, at frame 0 of the next block, the rate being the block's transport rate a
     * device needs to time a release. False when the node stages nothing (no core yet, or a message its door cannot
     * take), which is what the caller reads as "this node took none". */
    bool (*stage)(void* ctx, const uint8_t* data, size_t size, double sample_rate);
} rp_host_stage;

#ifdef __cplusplus
}
#endif

#endif /* RP_STAGE_H */
