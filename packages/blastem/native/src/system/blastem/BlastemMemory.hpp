#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "system/SystemCapabilities.hpp"

// The core's context, for the battery's own byte order below. A global-scope declaration, like the one BlastemMdSystem.hpp
// makes: one inside the namespace would name a different, unrelated type.
struct genesis_context;

namespace rp::blastem {

// The system, forward-declared INSIDE this namespace: a global-scope `class BlastemMdSystem;` declares a different,
// unrelated type, and MdMemory would then bind to an incomplete one.
class BlastemMdSystem;

// Whether this core has a battery to read: save_type stays SAVE_NONE for a cart whose header declared no RAM, and
// save_storage is the buffer romdb calloc'd for the declared range (halved for an odd- or even-byte cart, which is
// BlastEm's compacted layout and stays BlastEm's: design section 5). The I2C and NOR carts hold their device state in
// the same pointer, and both are exported as they stand, like every other core here.
bool hasBattery(const genesis_context* gen);

// The raw storage index of the battery byte a caller's offset names. The caller's order is the .sav's (and every other
// emulator's); a RAM_FLAG_BOTH cart keeps it in big-endian word order, so its index is the offset's pair-swapped one,
// and an odd- or even-byte cart is compacted with no swap (see the .cpp).
std::size_t batteryStorageIndex(const genesis_context* gen, std::size_t at);

// The 68k's work RAM in the 68k's own address order. BlastEm keeps it as 16-bit words, so its byte view swaps each
// pair; a view in address order is a per-call copy, refreshed on every getMemory. The copy is the system's one buffer:
// a getMemory does not allocate after the first, which grew it to the region's size, and the NEXT ask re-fills that
// same buffer under any accessor still held, so an accessor is only good until the next getMemory of its type (the
// registry copies it into its own triple in the same block it asked in). The scratch is the system's, never this
// mapper's: it owns nothing, which is what lets a caller build one over its own buffer on the spot.
class WorkRamView {
public:
    explicit WorkRamView(std::vector<std::uint8_t>& scratch) : scratch_(scratch) {}
    // Fills the scratch from `source` and returns the view: byte `i` of the result is address `i`.
    std::span<const std::uint8_t> refresh(const std::uint8_t* source, std::size_t size);

private:
    std::vector<std::uint8_t>& scratch_;
};

// rp.system.memory/1's write half: an offset in 68k address order, poked into BlastEm's word-ordered storage. Only
// region 0, the 68k's work RAM, is a listed region: the registry publishes the work RAM every block, and the engine's
// job paths only consult a published region, so listing the Z80's own RAM would advertise a region every job naming it
// is refused on.
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

// rp.system.sram/1's live write: an offset in the battery's own order (its .sav's, the one the control plane reads),
// written into BlastEm's storage with the pair swap a 16-bit cart needs. Not rp::CoreSram, for the reason MdMemory is
// not rp::CoreMemory: CoreSram writes through getMemory(Sram), which here is the address-ordered copy.
class MdSram final : public SramCapability {
public:
    explicit MdSram(BlastemMdSystem& sys) : sys_(sys) {}
    WriteResult write(std::uint32_t offset, const std::uint8_t* data, std::size_t size,
                      const std::uint8_t* expect) override;

private:
    BlastemMdSystem& sys_;
};

}  // namespace rp::blastem
