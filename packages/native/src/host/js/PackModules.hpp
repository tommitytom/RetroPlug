// A pack's TS halves in a txiki context (docs/downloadable-packs-m2c-plan.md): the module loader that serves them by
// name, the guard that holds a pack module to the context's contracts, and the two globals the TS loader
// (src/packs/packHost.ts) reads.
#pragma once

#include <cstddef>

#include "quickjs.h"

#include "host/js/rp_js_module.h"

namespace rp::host {

namespace packs {
class PackTable;
}

/** Install the pack loader on `ctx`'s runtime, after registerSharedModules registered `contracts` there:
 *  - a module named `pack:<id>/<half>` is the file `halves.<half>` of the pack `id` in `table`, compiled the first time
 *    something imports the name (the native half is no module, and a half the pack does not name is not found);
 *  - a module whose own name starts with `pack:` may import only a name of `contracts` that does not start with `rp:`
 *    (a contract module, or a host module's facade); anything else, relative names included, is refused, naming the
 *    pack module and the specifier, before txiki's import map is consulted. Every other module's imports go to txiki's
 *    normalizer and loader as before;
 *  - `globalThis.__rp_packList()` returns, as JSON, every pack of `table` with a half other than the native one: its
 *    id, version, half kinds, provides and requires, in load order;
 *  - `globalThis.__rp_packRefused(id, reason)` records "pack <id> is refused: <reason>" in `table`'s refusals.
 *  The table is the process's (processPacks) in every host, and must outlive the runtime. Calling it again on a
 *  context is harmless. */
void installPackLoader(JSContext* ctx, packs::PackTable& table, const rp_js_module* contracts, std::size_t count);

}  // namespace rp::host
