#pragma once

#include <cstdint>
#include <vector>

#include "system/SystemTypes.hpp"

class SystemBase;
class ChannelTap;

namespace rp::host {

// One system as a plan sees it: its core, plus the core's channel tap if it offers one, so a split routing arms it
// without a cast on the block thread.
struct SystemEntry {
    SystemId    id = 0;
    SystemBase* core = nullptr;
    ChannelTap* tap = nullptr;  // core->channelTap(): null unless the core offers one
};

// The systems of one plan, in project slot order (docs/modular-graph-phase-b1-design.md section 4.5), and what else
// the engine's per-block work reads off the plan. Built on the control thread with each document and adopted with
// its plan, so the audio thread's lookups always match the plan that is running. The cores belong to the plan's
// system nodes, which outlive the table. A slot whose system a node of an older plan is reused for holds that node's
// live core from here; every other is null until its node's activation builds its core and fills it, so the table is
// complete (no null core) before it is published with the plan, and nothing writes it after publication.
struct SystemTable {
    std::vector<SystemEntry> slots;

    // A linear scan in slot order: a per-system command names a system by id.
    const SystemEntry* find(SystemId id) const {
        for (const auto& e : slots)
            if (e.id == id) return &e;
        return nullptr;
    }
};

}  // namespace rp::host
