// The shared module sets' host half (docs/downloadable-packs-m2b-plan.md): a txiki context's contract modules, built by
// tools/build-shared-modules.mjs as named ES modules and written by retroplug-modc as one ordered table, registered
// before the context evaluates its entry so every host there (the control plane and the editor's UI, or the CLI's
// dispatcher) links to one copy of each.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "quickjs.h"

extern "C" {
/** One module of a set, as retroplug-modc's `table` writes it. The generated C declares the same struct. */
typedef struct rp_js_module {
    const char* name;
    const std::uint8_t* bytecode;
    std::uint32_t size;
} rp_js_module;
}

namespace rp::host {

/** Register `modules` in `ctx`, in order, each unevaluated: deserialising a module's bytecode puts it in QuickJS's
 *  loaded-module table, where a bare import of its name finds it, and resolves its own imports, so a dependency must come
 *  first (the table's order is the build's, dependencies first). A module's body runs the first time something imports
 *  it. A context is registered once: a second call finds its mark and returns at once, so the plugin's editor, which
 *  shares the control plane's context, may call it too. Returns "" or why a module could not be registered, naming it;
 *  the modules before it stay registered. */
std::string registerSharedModules(JSContext* ctx, const rp_js_module* modules, std::size_t count);

}  // namespace rp::host
