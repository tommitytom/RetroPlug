#include "system/SystemBase.hpp"

#include <cstdio>
#include <cstring>

void SystemBase::onProcess(const AudioBlockInfo& info, float* const* outs) {
    // Drive the triad to the block target, then finish, with the two lanes this entry drains: the mix. It says so to
    // the core the way a node's prepare does, so a core that gates on a ring of its own fills the one finishBlock
    // reads (a console package's node file).
    setBlockLanes(2);
    prepareForBlock(info);
    while (stepIfBelowTarget(info.frames)) {}
    finishBlock(info, outs, 2);
}

bool SystemBase::runUntilPc(std::uint32_t target, std::uint64_t maxCycles) {
    std::optional<std::uint32_t> pc = getProgramCounter();
    if (!pc) return false;            // backend has no CPU / no program counter
    if (*pc == target) return true;

    std::uint64_t cycles = 0;
    while (cycles < maxCycles) {
        const std::uint64_t ran = stepInstruction();
        if (ran == 0) return false;   // backend can't instruction-step
        cycles += ran;
        pc = getProgramCounter();
        if (pc && *pc == target) return true;
    }
    return false;
}

// The slot layout is [len:4 LE][savestate bytes][unused tail]. The tail is
// never read (readers honour len), so it's left stale rather than re-zeroed.
static constexpr std::size_t kStateLenPrefix = 4;

bool SystemBase::enableStateSnapshot() {
    if (stateSnapshotEnabled_) return true;
    const std::size_t sz = stateSnapshotSize();
    if (sz == 0 || sz > kMaxStateSnapshotBytes) return false;  // unsupported / absurd
    stateSnapshot_ = std::make_unique<MemorySnapshotTriple>(kStateLenPrefix + sz);
    stateRegions_  = stateSnapshotRegions();
    // Arm an immediate first publish so a Save right after load works without
    // waiting a full interval.
    stateSnapSamples_     = UINT64_MAX / 2;
    stateOverflowLogged_  = false;
    stateSnapshotEnabled_ = true;
    return true;
}

void SystemBase::rearmStateSnapshot() {
    if (!stateSnapshotEnabled_ || !stateSnapshot_) return;
    // Same trick as enable: push the accumulator past the interval so the next block republishes
    // instead of leaving the old core's blob + regions up for as much as half a second. Note this
    // deliberately does NOT touch stateRegions_ — the table is refreshed by the publish itself, so it
    // can never get ahead of the blob it describes.
    stateSnapSamples_    = UINT64_MAX / 2;
    stateOverflowLogged_ = false;
}

void SystemBase::publishStateSnapshot(std::uint32_t frames, double sampleRate) {
    if (!stateSnapshotEnabled_ || !stateSnapshot_) return;
    stateSnapSamples_ += frames;
    const std::uint64_t threshold =
        static_cast<std::uint64_t>(kStateSnapshotIntervalSec * sampleRate);
    if (stateSnapSamples_ < threshold) return;
    stateSnapSamples_ = 0;

    if (!captureStateSnapshot(stateScratch_) || stateScratch_.empty()) return;

    const std::size_t cap = stateSnapshot_->size();
    if (stateScratch_.size() + kStateLenPrefix > cap) {
        // Grew beyond the slot. Never realloc mid-life: this can run on the audio thread, and the
        // registry sized its shadow slot off our capacity at claim. Backends size the triple to a
        // ceiling precisely so this cannot happen; if it does, freezing on the last good (blob,
        // regions) pair is the safe degradation — stale but self-consistent, never a wrong slice.
        if (!stateOverflowLogged_) {
            stateOverflowLogged_ = true;
            std::fprintf(stderr,
                         "[RetroPlug] state snapshot for system %u too large (%zu > %zu), skipping\n",
                         id_, stateScratch_.size() + kStateLenPrefix, cap);
        }
        return;
    }

    // Refresh the region table IN LOCKSTEP with the blob it describes, and only now that the capture is
    // known to fit. The core may have been rebuilt under a different model since the last publish, which
    // moves these offsets; updating them anywhere else would let them describe a blob we never published.
    stateRegions_ = stateSnapshotRegions();

    std::uint8_t* slot = stateSnapshot_->writeSlot();
    const std::uint32_t len = static_cast<std::uint32_t>(stateScratch_.size());
    std::memcpy(slot, &len, sizeof(len));
    std::memcpy(slot + kStateLenPrefix, stateScratch_.data(), len);
    stateSnapshot_->publish();
}

std::size_t SystemBase::stateSnapshotCapacity() const {
    // The triple was sized kStateLenPrefix + stateSnapshotSize(); the payload cap is what's left.
    return stateSnapshot_ ? stateSnapshot_->size() - kStateLenPrefix : 0;
}

std::size_t SystemBase::readStateSnapshot(std::uint8_t* dst, std::size_t dstCapacity) {
    if (!stateSnapshot_) return 0;
    // readInto copies the triple's whole slot, so it refuses a `dst` smaller than that rather than truncating.
    if (!stateSnapshot_->readInto(dst, dstCapacity)) return 0;
    std::uint32_t len = 0;
    std::memcpy(&len, dst, sizeof(len));
    if (len == 0 || static_cast<std::size_t>(len) + kStateLenPrefix > stateSnapshot_->size()) return 0;
    return len;
}
