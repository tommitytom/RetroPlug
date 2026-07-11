#include "system/SystemBase.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

constexpr std::size_t typeIndex(rp::MemoryType t) {
    return static_cast<std::size_t>(t);
}

} // namespace

void SystemBase::onProcess(const AudioBlockInfo& info, float* const* outs) {
    // The degenerate 1-member unit: drive the triad to the block target, then
    // finish. Identical to a link group of size one (system/BlockRunner.cpp).
    prepareForBlock(info);
    while (stepIfBelowTarget(info.frames)) {}
    finishBlock(info, outs, 2); // the convenience entry is stereo (one Mix stream = two lanes)
}

bool SystemBase::enableMemorySnapshot(rp::MemoryType type) {
    const std::size_t idx = typeIndex(type);
    if (idx >= rp::kMemoryTypeCount) return false;
    auto& entry = snapshots_[idx];

    // Already streaming: bump the refcount and we're done.
    if (entry.triple) {
        ++entry.refcount;
        return true;
    }

    // Probe the region first. Unsupported types return an invalid accessor
    // and we refuse subscription so the UI can branch cleanly.
    rp::MemoryAccessor probe = getMemory(type, rp::AccessType::Read);
    if (!probe.valid()) return false;

    // Size cap protects the audio thread from per-block memcpy of multi-MB
    // regions. ROM / GBA EWRAM / large SRAM are one-shot only.
    if (probe.size() > kMaxStreamableBytes) return false;

    entry.triple   = std::make_unique<MemorySnapshotTriple>(probe.size());
    entry.refcount = 1;
    return true;
}

void SystemBase::disableMemorySnapshot(rp::MemoryType type) {
    const std::size_t idx = typeIndex(type);
    if (idx >= rp::kMemoryTypeCount) return;
    auto& entry = snapshots_[idx];
    if (!entry.triple) return;
    if (entry.refcount > 0) --entry.refcount;
    if (entry.refcount == 0) entry.triple.reset();
}

MemorySnapshotTriple* SystemBase::memorySnapshot(rp::MemoryType type) {
    const std::size_t idx = typeIndex(type);
    if (idx >= rp::kMemoryTypeCount) return nullptr;
    return snapshots_[idx].triple.get();
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

void SystemBase::publishMemorySnapshots() {
    for (std::size_t i = 0; i < rp::kMemoryTypeCount; ++i) {
        auto& entry = snapshots_[i];
        if (!entry.triple) continue;
        rp::MemoryAccessor accessor =
            getMemory(static_cast<rp::MemoryType>(i), rp::AccessType::Read);
        if (!accessor.valid()) continue;
        const std::size_t n = std::min(accessor.size(), entry.triple->size());
        std::memcpy(entry.triple->writeSlot(), accessor.data(), n);
        entry.triple->publish();
    }
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
    stateSnapshotEnabled_ = true;
    return true;
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
        // Grew beyond the slot — never realloc mid-life (would dangle the
        // UI-side pointer); skip this publish.
        std::fprintf(stderr,
                     "[RetroPlug] state snapshot for system %u too large (%zu > %zu), skipping\n",
                     id_, stateScratch_.size() + kStateLenPrefix, cap);
        return;
    }

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

bool SystemBase::readStateSnapshot(std::vector<std::uint8_t>& out) {
    if (!stateSnapshot_) return false;
    const std::size_t cap = stateSnapshot_->size();
    if (stateReadScratch_.size() < cap) stateReadScratch_.resize(cap);
    if (!stateSnapshot_->readInto(stateReadScratch_.data(), stateReadScratch_.size()))
        return false;
    std::uint32_t len = 0;
    std::memcpy(&len, stateReadScratch_.data(), sizeof(len));
    if (len == 0 || static_cast<std::size_t>(len) + kStateLenPrefix > cap) return false;
    out.assign(stateReadScratch_.begin() + kStateLenPrefix,
               stateReadScratch_.begin() + kStateLenPrefix + len);
    return true;
}
