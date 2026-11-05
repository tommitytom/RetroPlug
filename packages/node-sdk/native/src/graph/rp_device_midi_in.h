/* rp_device_midi_in.h - a device host's MIDI input, as rp.device.midi-in reads it
 * (docs/modular-graph-phase-e3-design.md section 5).
 *
 * A device host (the Pico box's firmware) parses the MIDI it receives into whole messages, each with its status byte,
 * and stages each at the frame of the next block its arrival falls on; rp.device.midi-in (packages/device-io) takes
 * what is staged for its block into its output queue. The host keeps the messages and their times; the node keeps the
 * queue's rules (frame order, a full queue's drops counted as its own). In the SDK rather than the package because the
 * hosts that offer it (the box's library, a desktop test's fake, the plan exporter's stub, which has nothing to give)
 * include nothing of a pack's. Plain C so a C host can implement it.
 *
 * [audio] - next is called on the thread that runs the block, from the node's process, so a host stages from its own
 * loop between blocks, never during one. */
#ifndef RP_DEVICE_MIDI_IN_H
#define RP_DEVICE_MIDI_IN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What rp.device.midi-in's output carries in one block, the box's UART ring's size: a host stages no more than this,
 * and the node drops (and counts) what passes it. */
#define RP_DEVICE_MIDI_IN_MESSAGES 256
#define RP_DEVICE_MIDI_IN_BYTES 4096

/* One staged message: its frame within the block, and its bytes, which stay valid until the next call to next. */
typedef struct rp_device_midi_message {
    uint32_t       frame;
    uint32_t       size;
    const uint8_t* data;
} rp_device_midi_message;

#define RP_DEVICE_MIDI_IN_INTERFACE "rp.device.midi-in/1"
typedef struct rp_device_midi_in {
    void* ctx;
    /* [audio] The next message staged for this block of `frames` frames into *out, in the order the host staged them,
     * and true; false once none is left, which leaves the staging empty for the next block. A frame should be below
     * `frames` and no earlier than the one before it: the node clamps one that is not (to the block's last frame, and
     * up to the last frame it took), as any queue keeps its order. */
    bool (*next)(void* ctx, uint32_t frames, rp_device_midi_message* out);
} rp_device_midi_in;

#ifdef __cplusplus
}
#endif

#endif /* RP_DEVICE_MIDI_IN_H */
