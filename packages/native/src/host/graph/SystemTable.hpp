#pragma once

#include <cstdint>
#include <vector>

#include "system/SystemTypes.hpp"

class SystemBase;
class SameBoySystem;
class MesenNesSystem;

namespace rp::host {

// One system as a plan sees it: its core, plus the concrete pointers the engine's per-block work needs
// (the MI.OUT collection, the NES tap), so that work needs no dynamic_cast.
struct SystemEntry {
    SystemId        id = 0;
    SystemBase*     core = nullptr;
    SameBoySystem*  sameboy = nullptr;  // null unless a Game Boy
    MesenNesSystem* nes = nullptr;      // null unless a NES
};

// The systems of one plan, in project slot order (docs/modular-graph-phase-b1-design.md section 4.5), and what else
// the engine's per-block work reads off the plan. Built on the control thread with each document and adopted with
// its plan, so the audio thread's lookups always match the plan that is running. The cores belong to the plan's
// system nodes, which outlive the table.
struct SystemTable {
    std::vector<SystemEntry> slots;
    // The plan holds an rp.host.midi-in (an rp.host.controller-in), which takes the staged host (control-surface)
    // MIDI at its block, so the kernel stage gets none of it, whichever of the two runs first.
    bool                     takesHostMidi = false;
    bool                     takesControllerMidi = false;
    // The system the plan's rp.n8.cart-mirror names (0 = none).
    SystemId                 cartMirror = 0;

    // A linear scan in slot order: the kernel's outputs name a system by id.
    const SystemEntry* find(SystemId id) const {
        for (const auto& e : slots)
            if (e.id == id) return &e;
        return nullptr;
    }
};

}  // namespace rp::host
