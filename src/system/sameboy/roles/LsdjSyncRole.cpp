#include "system/sameboy/roles/LsdjSyncRole.hpp"

#include <cstdio>

#include "system/SystemTypes.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "util/PpqUtil.hpp"

void LsdjSyncRole::onAttach(SameBoySystem& /*system*/) {
    std::fprintf(stderr,
                 "[RetroPlug] LSDJ sync role attached (mode=%u, autoplay=%d)\n",
                 static_cast<unsigned>(cfg_.mode),
                 cfg_.autoplay ? 1 : 0);
    if (cfg_.autoplay) {
        // Step 09 will wire autoplay against the ported LSDJ offset table.
        std::fprintf(stderr, "[RetroPlug] LSDJ autoplay flag set but unimplemented in step 08\n");
    }
}

void LsdjSyncRole::onProcessBlock(SameBoySystem& system, const AudioBlockInfo& info) {
    if (cfg_.mode != LsdjSyncMode::MidiSync) {
        prevPlaying_ = info.transportPlaying;
        return;
    }

    // Inject one MIDI clock byte per 24 PPQN tick crossed this block. The
    // sample offset is currently ignored: serialIn_ is a byte deque, drained
    // by SameBoy's serial-end callback at GB hardware rate, which yields
    // ~ms-level precision — well under one clock tick (~20ms at 120 BPM).
    PpqUtil::eachTick(info, 24, [&system](std::uint32_t /*ppq*/, std::uint32_t /*offset*/) {
        system.serialIn_.push_back(0xF8);
    });

    prevPlaying_ = info.transportPlaying;
}
