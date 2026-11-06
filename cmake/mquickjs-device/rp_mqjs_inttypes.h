/* What a device build of MicroQuickJS (deps/mquickjs) is compiled with first (-include, graph-runtime's device engine:
 * packages/graph-runtime/native/CMakeLists.txt): the 64-bit printf conversions mquickjs.c names (PRIu64, in a debug
 * dump). arm-none-eabi GCC's <stdint.h> is its freestanding one, which newlib's <inttypes.h> does not recognise, so
 * newlib defines none of the 64-bit ones there; the vendored source is compiled as it stands, and this supplies them
 * as newlib's own would for that target (int64_t is long long). */
#ifndef RP_MQJS_INTTYPES_H
#define RP_MQJS_INTTYPES_H

#include <inttypes.h>

#ifndef PRId64
#define PRId64 "lld"
#endif
#ifndef PRIu64
#define PRIu64 "llu"
#endif
#ifndef PRIx64
#define PRIx64 "llx"
#endif
#ifndef PRIo64
#define PRIo64 "llo"
#endif

#endif /* RP_MQJS_INTTYPES_H */
