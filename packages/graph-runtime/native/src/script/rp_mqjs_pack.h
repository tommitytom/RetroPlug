/* rp_mqjs_pack.h - a device script pack as a binary carries it (docs/modular-graph-phase-e4-design.md sections 3
 * and 4).
 *
 * The device-pack toolchain (packages/retroplug/scripts/build-device-pack.mjs) writes one C source per width for each
 * pack it compiles, <pack>.pack64.c and <pack>.pack32.c, which define the pack's bytecode and the node type ids its
 * definitions register, and nothing else; cmake/packages.cmake builds the one of the binary's width into a library of
 * its own, and a product that lists the pack registers it from that (script/MqjsHost.hpp, rp::mqjs::package). Plain C
 * with no other header, so any compiler a product uses builds it.
 *
 * A pack is bundled twice (build-device-pack.mjs): whole, and as the device's runtime of the pack, over the device's
 * runtime of the node SDK, which keeps what running a node takes (a definition's id, create, layout and idle flag)
 * and drops the definitions' checks, settings parse and descriptors. Every node of the pack, on every host, runs from
 * the runtime: `bytecode` is it, at the source's width, so the desktop's nodes, the exporter's trial load and the
 * tests run the bytes the box runs, at another width. The whole pack is `control_bytecode`, in the 64-bit source alone:
 * the desktop's control context runs it to register the types, read their descriptors and check settings.
 *
 * The bytecode is const, as mqjs -o wrote it, and is never run where it lies: each context the host makes runs a copy
 * of its own, in the pack's memory beside the context. MicroQuickJS relocates bytecode in place (it swaps the
 * bytecode's strings for the atoms of the standard library it runs against, which only a context over that library can
 * do), and a context that runs it writes into it as well: a string constant the code first uses as a property name is
 * marked unique where it lies, in that context's atom table alone, so a second context loading the same bytes would
 * look the name up in vain. */
#ifndef RP_MQJS_PACK_H
#define RP_MQJS_PACK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Where a binary keeps a pack's bytecode. Nowhere in particular by default; an image the Pico SDK copies into RAM at
 * boot (copy_to_ram: the box) defines RP_MQJS_BYTECODE_IN_FLASH, which keeps it in the SDK's .flashdata, the one
 * section that copy leaves in flash: setup copies the bytecode into the pack's memory, so a second copy in RAM would
 * be RAM spent for nothing. */
#if defined(RP_MQJS_BYTECODE_IN_FLASH)
#define RP_MQJS_BYTECODE_PLACEMENT __attribute__((section(".flashdata.rp_mqjs_bytecode")))
#else
#define RP_MQJS_BYTECODE_PLACEMENT
#endif

typedef struct rp_mqjs_pack_source {
    const char*        name;          /* the package's name, as its manifest's directory is ("devtest") */
    const uint8_t*     bytecode;      /* mqjs -o's output for the device's runtime of the pack, at the width this
                                       * binary runs: what every node context loads */
    uint32_t           bytecode_size;
    uint32_t           heap_bytes;    /* the manifest's heapBytes: the box's context, in bytes (MqjsHost.hpp) */
    uint32_t           type_count;
    const char* const* type_ids;      /* the ids its definitions register, in their order */
    /* The size of the pack's 32-bit bytecode, the box's (bytecode_size itself in the 32-bit source): what a 64-bit
     * build sizes the box's package memory with (MqjsHost.hpp's boxPackageBytes). */
    uint32_t           box_bytecode_size;
    /* The whole pack, at this width, which the desktop's control context loads; null in the 32-bit source, whose
     * binary (the box) has no control context. */
    const uint8_t*     control_bytecode;
    uint32_t           control_bytecode_size;
} rp_mqjs_pack_source;

#ifdef __cplusplus
}
#endif

#endif /* RP_MQJS_PACK_H */
