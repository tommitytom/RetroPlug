#include "system/blastem/BlastemMemory.hpp"

#include <cstring>

#include "system/blastem/BlastemCore.hpp"       // genesis_context (through BlastemMdSystem.hpp)
#include "system/blastem/BlastemMdSystem.hpp"

namespace rp::blastem {

namespace {

// BlastEm's work RAM byte order. Its storage is a word array (`genesis_context::work_ram` is a uint16_t*), and on a
// little-endian host a 68k word's high half - the byte at the even address - is the word's SECOND byte, so the byte at
// address `a` is storage byte `a ^ 1`. backend.c's byte accessors apply that same xor (its `opts->byte_swap`), which is
// why the 68k never notices and why a view of the raw array is swapped.
std::size_t wordOrder(std::size_t address) { return address ^ 1u; }

// The regions, in the order a caller addresses them. Region 0 is the one the snapshot registry publishes every block
// and the one Engine::writeRam looks up by type, so it is the work RAM.
constexpr MemoryCapability::Region kRegions[] = {
    {"work", MemoryType::Ram},
    {"z80", MemoryType::ExtWorkRam},
};

}  // namespace

std::span<const std::uint8_t> WorkRamView::refresh(const std::uint8_t* source, std::size_t size) {
    // The scratch keeps the size it grew to (the region's size is fixed for a core's life), so a refresh is the copy
    // and nothing else: no block thread ever allocates here.
    scratch_.resize(size);
    for (std::size_t i = 0; i < size; ++i) scratch_[i] = source[wordOrder(i)];
    return scratch_;
}

std::span<const MemoryCapability::Region> MdMemory::regions() const { return kRegions; }

bool MdMemory::write(std::size_t region, std::uint32_t offset, const std::uint8_t* data, std::size_t size) {
    if (!data || size == 0) return false;
    if (region == 0) {  // work RAM: address order in, word order out
        // The accessor is the liveness and range check, never the destination (that is what this class exists for).
        auto live = sys_.getMemory(MemoryType::Ram, AccessType::ReadWrite);
        if (!live.valid() || static_cast<std::size_t>(offset) + size > live.size()) return false;
        genesis_context* gen = sys_.core();
        if (!gen || !gen->work_ram) return false;
        std::uint8_t* raw = reinterpret_cast<std::uint8_t*>(gen->work_ram);
        for (std::size_t i = 0; i < size; ++i) raw[wordOrder(offset + i)] = data[i];
        return true;
    }
    if (region == 1) {  // the Z80's RAM: a plain byte array
        auto live = sys_.getMemory(MemoryType::ExtWorkRam, AccessType::ReadWrite);
        if (!live.valid() || static_cast<std::size_t>(offset) + size > live.size()) return false;
        std::memcpy(live.data() + offset, data, size);
        return true;
    }
    return false;  // no such region: a poke past the listed regions is a caller that read the list wrong
}

}  // namespace rp::blastem
