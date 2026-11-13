#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rfl/Literal.hpp"

namespace rp::blastem {

// Which machine BlastEm boots (genesis.c's set_region): the header's own region, or one forced. Construct-time: a
// region fixes the master clock (53,693,175 NTSC against 53,203,395 PAL), the video standard and the frame height
// (224 against 240), so a change is a rebuild, exactly as the NES's region is a reset.
//
// The enumerators ARE the settings' labels: knobs::Label<E> is built from their names with the first letter
// lowercased (SystemNode.hpp), so `NtscUs` reads and writes as "ntscUs" and a label array beside it would be a second
// spelling of one fact (and would go stale). packages/blastem/ts/enums.ts carries the same four words.
enum class MdRegion : std::uint32_t { Auto = 0, NtscUs = 1, NtscJp = 2, Pal = 3 };

// Plain-data config for a Mega Drive system slot, in the shape of MesenSmsConfig.
struct BlastemMdConfig {
    using Tag = rfl::Literal<"md">;

    std::uint32_t region  = 0;     // an MdRegion ordinal
    float         gainDb  = 0.0f;
    // The file stem a core built outside a plan names its ROM by (BlastemMdSystem stages nothing: BlastEm takes its
    // bytes, so this only names the image in messages and in `event_system_start`).
    std::string               romName;
    std::vector<std::uint8_t> sram;       // battery seed (may be empty)
    std::vector<std::uint8_t> savestate;  // savestate seed (may be empty; wins over the battery)
};

}  // namespace rp::blastem
