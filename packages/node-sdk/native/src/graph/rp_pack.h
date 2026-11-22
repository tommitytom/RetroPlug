/* rp_pack.h - what a pack's native library exports, and the API level it is held to.
 *
 * A pack (docs/downloadable-packs-loading-design.md section 2) is a folder a RetroPlug loads at run time; its native
 * half is one dynamic library exporting rp_pack_entry and nothing else. The entry answers the API level the library
 * was built against and the same rp_package list a built-in package's generated table holds (graph/Packages.hpp).
 * The loader reads the level first and uses nothing else from a library built at any other level, so this header's
 * shape is the one thing that may never change: a new field is a new header, not an edit to this one.
 */
#ifndef RP_PACK_H
#define RP_PACK_H

#include <stddef.h>
#include <stdint.h>

#include "graph/rp_node.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The API level: one integer, bumped by hand in any commit that breaks packs (the C ABI in rp_node.h, SystemBase and
 * the contract headers, the TS contract modules). A pack records the level it was built against, in its pack.json and
 * from its entry, and is refused at any other. cmake/packs.cmake reads this line to stamp a pack's pack.json, and M2's
 * TS halves read it the same way (the generator, packages/retroplug/scripts/lib/packages.mjs, writing it into a
 * generated module), so it stays a plain decimal on a line of its own and there is one source for both languages. */
#define RP_API_LEVEL 1u

/* The symbol a pack's library exports (an rp_pack_entry_fn). */
#define RP_PACK_ENTRY_SYMBOL "rp_pack_entry"

/* [control] Writes the level the library was built against to *api_level and the package count to *count, and returns
 * the packages, valid for the life of the process (a pack's library is never unloaded). It must not throw: no
 * exception crosses this entry. */
typedef const rp_package* const* (*rp_pack_entry_fn)(uint32_t* api_level, size_t* count);
RP_EXPORT const rp_package* const* rp_pack_entry(uint32_t* api_level, size_t* count);

#ifdef __cplusplus
}
#endif

#endif /* RP_PACK_H */
