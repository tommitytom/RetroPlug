#include "transport/CommandApply.hpp"

#include <cstdint>
#include <memory>
#include <vector>

#include "system/SystemBase.hpp"
#include "transport/CommandQueue.hpp"

void applySystemCommand(SystemBase* sys, const Command& cmd, bool& projectMutated) {
    switch (cmd.kind) {
        case Command::Kind::ResetSystem:
            if (sys) sys->onReset();   // intentionally does not set projectMutated
            break;

        case Command::Kind::NewSram:
            if (sys) {
                sys->clearSram();
                projectMutated = true;
            }
            break;

        case Command::Kind::LoadSram: {
            // Ownership lands here; free even when the system is gone.
            std::unique_ptr<std::vector<std::uint8_t>> owned(cmd.payload.loadSram.bytes);
            if (sys && owned && sys->loadSramBytes(*owned)) {
                // Load battery RAM, then reset so the game boots into the loaded
                // save (it only re-reads SRAM at boot).
                sys->onReset();
                projectMutated = true;
            }
        } break;

        case Command::Kind::LoadState: {
            std::unique_ptr<std::vector<std::uint8_t>> owned(cmd.payload.loadState.bytes);
            // A savestate restores full CPU + RAM, so it takes effect
            // immediately — no reset (unlike LoadSram).
            if (sys && owned && sys->loadStateBytes(*owned)) {
                projectMutated = true;
            }
        } break;

        default:
            break;
    }
}
