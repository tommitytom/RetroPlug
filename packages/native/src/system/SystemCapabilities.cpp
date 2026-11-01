#include "system/SystemCapabilities.hpp"

#include <cstring>

#include "system/MemoryAccessor.hpp"
#include "system/SystemBase.hpp"

namespace rp {

namespace {

// The work RAM, the one region the snapshot registry publishes every block (SnapshotRegistry::publishRam).
constexpr MemoryCapability::Region kCoreRegions[] = {{"ram", MemoryType::Ram}};

}  // namespace

std::span<const MemoryCapability::Region> CoreMemory::regions() const { return kCoreRegions; }

bool CoreMemory::write(std::size_t region, std::uint32_t offset, const std::uint8_t* data, std::size_t size) {
    if (region >= std::size(kCoreRegions) || !data || size == 0) return false;
    // ReadWrite, so a backend with a read-only view reports failure rather than dropping the write into a copy nobody
    // reads.
    MemoryAccessor acc = core_.getMemory(kCoreRegions[region].type, AccessType::ReadWrite);
    if (!acc.valid() || static_cast<std::size_t>(offset) + size > acc.size()) return false;
    std::memcpy(acc.data() + offset, data, size);
    return true;
}

std::string CoreRom::title() const {
    std::string out;
    for (std::size_t i = titleOffset_; i < titleOffset_ + titleLength_ && i < rom_.size(); ++i) {
        if (rom_[i] == 0) break;
        out.push_back(static_cast<char>(rom_[i]));
    }
    return out;
}

}  // namespace rp
