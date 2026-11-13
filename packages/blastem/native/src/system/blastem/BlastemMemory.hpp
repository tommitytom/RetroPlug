#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "system/SystemCapabilities.hpp"

namespace rp::blastem {

// The system, forward-declared INSIDE this namespace: a global-scope `class BlastemMdSystem;` declares a different,
// unrelated type, and MdMemory would then bind to an incomplete one.
class BlastemMdSystem;

// The 68k's work RAM in the 68k's own address order. BlastEm keeps it as 16-bit words, so its byte view swaps each
// pair; a view in address order is a per-call copy, refreshed on every getMemory (the registry copies it into its own
// triple the same block, so the copy's lifetime is the call). The scratch is the system's, never this mapper's: it
// owns nothing, which is what lets a caller build one over its own buffer on the spot.
class WorkRamView {
public:
    explicit WorkRamView(std::vector<std::uint8_t>& scratch) : scratch_(scratch) {}
    // Fills the scratch from `source` and returns the view: byte `i` of the result is address `i`.
    std::span<const std::uint8_t> refresh(const std::uint8_t* source, std::size_t size);

private:
    std::vector<std::uint8_t>& scratch_;
};

// rp.system.memory/1's write half: an offset in 68k address order, poked into BlastEm's word-ordered storage. Region 0
// is the 68k's work RAM and region 1 the Z80's own memory, the same order and names regions() lists.
//
// Not rp::CoreMemory, even though only region 0's write differs: CoreMemory writes through the Ram accessor, and this
// core's Ram accessor is the address-ordered COPY, so the poke it made would land in a buffer nobody reads.
class MdMemory final : public MemoryCapability {
public:
    explicit MdMemory(BlastemMdSystem& sys) : sys_(sys) {}
    std::span<const Region> regions() const override;
    bool write(std::size_t region, std::uint32_t offset, const std::uint8_t* data, std::size_t size) override;

private:
    BlastemMdSystem& sys_;
};

}  // namespace rp::blastem
