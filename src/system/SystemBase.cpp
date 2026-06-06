#include "system/SystemBase.hpp"

#include <algorithm>
#include <cstring>

namespace {

constexpr std::size_t typeIndex(rp::MemoryType t) {
    return static_cast<std::size_t>(t);
}

} // namespace

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
