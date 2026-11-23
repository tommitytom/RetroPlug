// The shared module sets' host half (docs/downloadable-packs-m2b-plan.md): a txiki context's contract modules, built by
// tools/build-shared-modules.mjs as named ES modules and written by retroplug-modc as one ordered table, registered
// before the context evaluates its entry so every host there (the control plane and the editor's UI, or the CLI's
// dispatcher) links to one copy of each.
#pragma once

#include <cstddef>
#include <string>

#include "quickjs.h"

#include "host/js/rp_js_module.h"

namespace rp::host {

/** Register `modules` in `ctx`, in order, each unevaluated: deserialising a module's bytecode puts it in QuickJS's
 *  loaded-module table, where a bare import of its name finds it, and resolves its own imports, so a dependency must come
 *  first (the table's order is the build's, dependencies first). A module's body runs the first time something imports
 *  it. A context is registered once, and the outcome is sticky: the first call marks the context with its result, so a
 *  second call returns that same result at once without reading (the plugin's editor, which shares the control plane's
 *  context, calls it too; a retry after a failure would append duplicates of the modules that did register). Returns
 *  "" or why a module could not be registered, naming it; the modules before it stay registered. */
std::string registerSharedModules(JSContext* ctx, const rp_js_module* modules, std::size_t count);

}  // namespace rp::host
