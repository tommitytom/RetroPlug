#pragma once

// RETROPLUG_GRAPH_ENGINE=1 runs the engine on the graph executor (docs/modular-graph-phase-b1-design.md):
// the switch B1 lands behind, so today's engine stays the default until the graph one holds every parity
// case. Removed when the graph engine becomes the only one.

#include <cstdlib>
#include <cstring>

namespace rp {

// Read once per process, so every Engine in it (each background render has its own) runs the same way.
inline bool graphEngine() {
    static const bool on = [] {
        const char* v = std::getenv("RETROPLUG_GRAPH_ENGINE");
        return v != nullptr && std::strcmp(v, "1") == 0;
    }();
    return on;
}

}  // namespace rp
