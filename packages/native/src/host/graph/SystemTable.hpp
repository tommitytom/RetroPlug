#pragma once

#include <cstdint>
#include <vector>

#include "system/SystemTypes.hpp"

class SystemBase;
class MesenNesSystem;

namespace rp::host {

// One system as a plan sees it: its core, plus the concrete pointer the engine's NES tap needs, so the tap needs no
// dynamic_cast on the block thread.
struct SystemEntry {
    SystemId        id = 0;
    SystemBase*     core = nullptr;
    MesenNesSystem* nes = nullptr;  // null unless a NES
};

// The systems of one plan, in project slot order (docs/modular-graph-phase-b1-design.md section 4.5), and what else
// the engine's per-block work reads off the plan. Built on the control thread with each document and adopted with
// its plan, so the audio thread's lookups always match the plan that is running. The cores belong to the plan's
// system nodes, which outlive the table.
struct SystemTable {
    std::vector<SystemEntry> slots;
    // The system the plan's rp.n8.cart-mirror names (0 = none): one of the plan's NES systems.
    SystemId                 cartMirror = 0;

    // A linear scan in slot order: a per-system command names a system by id.
    const SystemEntry* find(SystemId id) const {
        for (const auto& e : slots)
            if (e.id == id) return &e;
        return nullptr;
    }
};

}  // namespace rp::host
