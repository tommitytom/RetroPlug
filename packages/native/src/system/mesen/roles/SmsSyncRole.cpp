#include "system/mesen/roles/SmsSyncRole.hpp"

#include <algorithm>

#include "Core/SMS/SmsControlManager.h"
#include "Core/SMS/SmsMemoryManager.h"

// The controller port the sync lines live on. smsggdj reads $DD, which is port 1 (TR/TL/TH);
// port 0 is $DC, the player-1 pad. Not a knob: the ROM's sync_read hardcodes it. Game Gear does not
// use this at all - see onAttach.
namespace { constexpr std::uint8_t kSyncPort = 1; }

void SmsSyncRole::pushBytes(std::uint32_t offset, const std::uint8_t* data, std::size_t count, bool /*flush*/) {
    if (data == nullptr || count == 0) return;
    for (std::size_t i = 0; i < count; ++i) {
        pending_.push_back({ offset, data[i] });
    }
    // A second feed could enqueue after this one with an earlier offset; pumpUntil re-sorts once
    // before draining rather than keeping the deque ordered on every push.
    needsSort_ = true;
}

void SmsSyncRole::pumpUntil(std::uint32_t sampleOffset) {
    if (pending_.empty()) return;

    if (needsSort_) {
        // Stable so levels sharing an offset keep enqueue order - the last one queued for a given
        // sample is the one that ends up on the line. Runs once per block (all enqueuing precedes
        // the step loop), after which this is a plain front-drain and the loop below is one compare.
        std::stable_sort(pending_.begin(), pending_.end(),
                         [](const PendingLevel& a, const PendingLevel& b) { return a.offset < b.offset; });
        needsSort_ = false;
    }

    while (!pending_.empty() && pending_.front().offset <= sampleOffset) {
        lastApplied_ = pending_.front().levels;
        if (gameGear_) {
            if (memoryManager_) memoryManager_->SetGgExternalInput(lastApplied_);
        } else if (controlManager_) {
            controlManager_->SetExternalInput(kSyncPort, lastApplied_);
        }
        pending_.pop_front();
    }
}

void SmsSyncRole::rebase(std::uint32_t frames) {
    for (auto& e : pending_) {
        e.offset = (e.offset > frames) ? e.offset - frames : 0;
    }
}
