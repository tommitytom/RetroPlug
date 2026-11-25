// A pack's TS halves in a txiki context (docs/downloadable-packs-m2c-plan.md): the module loader that serves them by
// name, the guard that holds a pack module to the context's contracts, and the two globals the TS loader
// (src/packs/packHost.ts) reads.
#pragma once

#include <chrono>
#include <cstddef>

#include "dpfjs/host/TjsHostRuntime.hpp"
#include "quickjs.h"

#include "host/js/PackLoaderCore.hpp"
#include "host/js/rp_js_module.h"

namespace rp::host {

/** How long a host waits for its entry to signal ready (or finish) while a pack's half evaluates. The wait pumps with
 *  no sleep for its first pumps, so an empty pack list costs nothing; after that it sleeps ~1 ms a pump up to this
 *  deadline. It is a backstop: the TS loader gives the packs' halves one budget of 5 s overall (loadPacks.ts) and
 *  refuses what has not settled in it, so a stuck pack is named, and this deadline also covers the composition. */
inline constexpr std::chrono::seconds kHostReadyDeadline{10};

/** Install the pack loader on `ctx`'s runtime, after registerSharedModules registered `contracts` there:
 *  - a module named `pack:<id>/<half>` is the file `halves.<half>` of the pack `id` in `table`, compiled the first time
 *    something imports the name (the native, script and protocol halves are no modules, and a half the pack does not
 *    name is not found);
 *  - a module whose own name starts with `pack:` may import only a name of `contracts` that does not start with `rp:`
 *    (a contract module, or a host module's facade); anything else, relative names included, is refused, naming the
 *    pack module and the specifier, before txiki's import map is consulted. Every other module's imports go to txiki's
 *    normalizer and loader as before;
 *  - `globalThis.__rp_packList()` returns, as JSON, every pack of `table` with a half other than the native one: its
 *    id, version, half kinds, provides and requires, in load order;
 *  - `globalThis.__rp_packRefused(id, reason, skipLater)` records "pack <id> is refused: <reason>" in `table`'s
 *    refusals; with `skipLater` true the process also leaves the pack out of every later `__rp_packList` (a pack that ran
 *    past the load budget: the one refusal that costs time to repeat).
 *  The table is the process's (processPacks) in every host, and must outlive the runtime. Calling it again on a
 *  context is harmless. The table is process-wide: a later call with another table points every context's loader at
 *  that table. */
void installPackLoader(JSContext* ctx, packs::PackTable& table, const rp_js_module* contracts, std::size_t count);

/** Make `host`'s runtime safe to free while a pack's half is still pending (a top-level await parked on a timer):
 *  destroys the runtime's timers and collects, so the pending module's callbacks are freed before QuickJS frees the
 *  module structs they point at. Idempotent; ~PackHostRuntime calls it. */
void settleForFree(TjsHostRuntime& host);

/** The TjsHostRuntime every host that loads packs owns: its destructor settles the runtime before it is freed. */
class PackHostRuntime : public TjsHostRuntime {
public:
    PackHostRuntime() = default;
    ~PackHostRuntime() { settleForFree(*this); }
    PackHostRuntime(const PackHostRuntime&) = delete;
    PackHostRuntime& operator=(const PackHostRuntime&) = delete;
};

}  // namespace rp::host
