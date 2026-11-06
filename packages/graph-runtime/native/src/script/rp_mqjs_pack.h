/* rp_mqjs_pack.h - a device script pack as a binary carries it (docs/modular-graph-phase-e4-design.md sections 3
 * and 4).
 *
 * The device-pack toolchain (packages/retroplug/scripts/build-device-pack.mjs) writes one C source per width for each
 * pack it compiles, <pack>.pack64.c and <pack>.pack32.c, which define the pack's bytecode and the node type ids its
 * definitions register, and nothing else; cmake/packages.cmake builds the one of the binary's width into a library of
 * its own, and a product that lists the pack registers it from that (script/MqjsHost.hpp, rp::mqjs::package). Plain C
 * with no other header, so any compiler a product uses builds it.
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

typedef struct rp_mqjs_pack_source {
    const char*        name;          /* the package's name, as its manifest's directory is ("devtest") */
    const uint8_t*     bytecode;      /* mqjs -o's output at the width this binary runs: a JSBytecodeHeader, then it */
    uint32_t           bytecode_size;
    uint32_t           heap_bytes;    /* the manifest's heapBytes: the whole of the pack's context, in bytes */
    uint32_t           type_count;
    const char* const* type_ids;      /* the ids its definitions register, in their order */
} rp_mqjs_pack_source;

#ifdef __cplusplus
}
#endif

#endif /* RP_MQJS_PACK_H */
