#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rp {

// Cross-system memory region tags. Each concrete SystemBase maps the subset
// it actually backs onto its emulator's native region enum; unsupported
// values return an empty MemoryAccessor.
//
// Lives in the `rp` namespace so the type doesn't collide with Mesen's
// global `enum class MemoryType` in TUs that pull in both
// (MesenNesSystem.cpp / MesenGbaSystem.cpp).
//
// Per-system support matrix (concrete subclasses):
//
//   MemoryType     SameBoy (GB)     NES (Mesen)        GBA (Mesen)          SMS/GG (Mesen)
//   ------------   --------------   ----------------   ------------------   --------------
//   Ram            DIRECT_ACCESS_   NesInternalRam     GbaIntWorkRam (IWRAM) SmsWorkRam
//                   RAM (WRAM)
//   Rom            DIRECT_ACCESS_   NesPrgRom          GbaPrgRom            SmsPrgRom
//                   ROM
//   Sram           DIRECT_ACCESS_   NesSaveRam         GbaSaveRam           SmsCartRam
//                   CART_RAM
//   Vram           DIRECT_ACCESS_   NesChrRam (or      GbaVideoRam          SmsVideoRam
//                   VRAM             NesChrRom if no
//                                    CHR-RAM on cart)
//   IORegisters    DIRECT_ACCESS_   (unsupported)      (unsupported)        (unsupported)
//                   IO
//   HRam           DIRECT_ACCESS_   (unsupported)      (unsupported)        (unsupported)
//                   HRAM
//   OAM            DIRECT_ACCESS_   NesSpriteRam       GbaSpriteRam         (unsupported -
//                   OAM                                                      sprites live
//                                                                            in VRAM)
//   NametableRam   (unsupported)    NesNametableRam    (unsupported)        (unsupported)
//   ExtWorkRam     (unsupported)    (unsupported)      GbaExtWorkRam (EWRAM) (unsupported)
//
// Mesen exposes two more SMS regions with no tag here: SmsBootRom and the VDP's
// colour RAM. Adding either means adding an enum member, and these integer
// values are the RPC wire byte (mirrored in backend.ts and cli/sdk-types.d.ts)
// with kMemoryTypeCount sizing SystemBase::StateRegionTable - so it is a
// wire-format change, not a local one.
//
// The integer values double as the wire byte used in subscribe / getMemory
// RPC calls; keep them stable.
enum class MemoryType : std::uint8_t {
    Ram          = 0,
    Rom          = 1,
    Sram         = 2,
    Vram         = 3,
    IORegisters  = 4,
    HRam         = 5,
    OAM          = 6,
    NametableRam = 7,
    ExtWorkRam   = 8,
};

constexpr std::size_t kMemoryTypeCount = 9;

// Read-only vs read-write intent. Reading is always allowed; writing through
// a Read accessor is a programming error (the implementation may refuse or
// silently drop). Kept as a hint rather than an enforced const-correctness
// boundary because the underlying storage is a raw byte buffer in both
// cases.
enum class AccessType : std::uint8_t {
    Read      = 0,
    ReadWrite = 1,
};

// Record of one write through a MemoryAccessor that had patch tracking
// enabled. Future use: surface "what changed" to the UI without re-reading
// the whole region. Not currently persisted by any role; LSDJ kit patches
// are serialized via the kit role's own config.
struct MemoryPatch {
    std::size_t              offset = 0;
    std::vector<std::uint8_t> bytes;
};

} // namespace rp
