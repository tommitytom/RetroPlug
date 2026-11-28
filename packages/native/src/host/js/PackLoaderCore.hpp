// The part of the pack module loader that needs no txiki (docs/downloadable-packs-m2c-plan.md and
// docs/downloadable-packs-m2d-plan.md): the `pack:<id>/<half>` compile, the contract guard and the globals the TS loader
// reads, which a bare QuickJS runtime (the render worker's, in the backend library) and txiki's hosts (host/js/
// PackModules) share. QuickJS alone, so retroplug-backend can link it.
#pragma once

#include <cstddef>

#include "quickjs.h"

#include "host/js/rp_js_module.h"

namespace rp::host {

namespace packs {
class PackTable;
}

/** installPackLoader (host/js/PackModules) for a bare QuickJS runtime, which has no txiki: a render job's
 *  (host/render/RenderHost). The same `pack:<id>/<half>` loader and the same guard, after registerSharedModules
 *  registered `contracts` there; any other name is passed through as written (QuickJS finds a registered module by its
 *  name) and is not found if no module has it, since a bare runtime loads no file. Installs `__rp_packList` and no
 *  `__rp_packRefused`, `__rp_packRefusals` or `__rp_packLoadNow`: a render's refusals are its job's console lines,
 *  the context that loaded those halves first having recorded its own. The table is process-wide and must outlive the
 *  runtime. */
void installBarePackLoader(JSContext* ctx, packs::PackTable& table, const rp_js_module* contracts, std::size_t count);

// What txiki's loader borrows from the core.

/** Sets import.meta on a compiled module `val`; 0 on success. txiki's hook; a bare runtime passes none. */
using PackMetaFn = int (*)(JSContext* ctx, JSValueConst val);

/** True, with a ReferenceError thrown, when `base` is a pack module importing `name`, which is not a contract. */
bool guardRefuses(JSContext* ctx, const char* base, const char* name);

/** Whether `name` is a `pack:` module name. */
bool isPackName(const char* name);

/** A `pack:<id>/<half>` name's module, compiled and not evaluated; null with an exception thrown on a refusal. */
JSModuleDef* loadPackHalf(JSContext* ctx, const char* name, PackMetaFn setMeta);

/** Point the loader at `table` and install the contracts object and the globals (`refusals`: __rp_packRefused,
 *  __rp_packRefusals and __rp_packLoadNow). */
void installPackGlobals(JSContext* ctx, packs::PackTable& table, const rp_js_module* contracts, std::size_t count,
                        bool refusals);

}  // namespace rp::host
