#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "system/MemoryType.hpp"

class SystemBase;

// The capability interfaces a system presents to the control plane's jobs (docs/modular-graph-phase-c5-design.md
// section 6), each named by its versioned id and asked for through a nullable accessor on SystemBase, as ChannelTap
// is: rp.system.memory/1 through memory(), rp.system.sram/1 through sram(), rp.system.rom/1 through rom(). A caller
// branches on null, so the engine runs a job without naming the console. Each covers what the control plane does with
// a system and no more: the work RAM it reads and pokes (readRam / writeRam), the battery it dumps and saves a song
// into (readSram / writeSram) and the ROM header it reads (readFilePrefix on the file the core was built from).
//
// The control plane never reads a live core: the engine serves every read of an emulated system from its snapshot
// registry's published copy (SnapshotRegistry), and applies every write on the block thread at a block boundary, or
// on the control thread while no audio thread runs. So an interface here holds the facts that are fixed for the
// core's life, which any thread may read, and the block-side half of a write. The system owns each object, and it
// lives as long as the core: the pointer an accessor returns is the same for the core's lifetime.

namespace rp {

// rp.system.memory/1: named regions of the core's memory, read and written by range. Reads are the registry's
// published copy of a region, so a region is listed only where the registry publishes it every block: the work RAM.
class MemoryCapability {
public:
    struct Region {
        const char* name;  // the name a job addresses it by
        MemoryType  type;  // the core's region behind it (SystemBase::getMemory)
    };

    virtual ~MemoryCapability() = default;

    // [any] The regions, fixed for the core's life. A region's size is the published copy's (the registry's), since a
    // live model switch can resize the region itself (SameBoy's DMG 8 KiB and CGB 32 KiB work RAM).
    virtual std::span<const Region> regions() const = 0;
    // [block, or control while quiescent] Copy `size` bytes into region `region` (an index into regions()) at `offset`
    // of the live core. False, writing nothing, for a range past the region's end: half a song block is not a song,
    // and a write past the end would corrupt whatever the core keeps after it. Allocates nothing.
    virtual bool write(std::size_t region, std::uint32_t offset, const std::uint8_t* data, std::size_t size) = 0;
};

// rp.system.sram/1: the cartridge's battery RAM, dumped whole and restored whole. An emulated core's dump is the
// registry's published copy; its restore is a rebuild that seeds the battery (docs/modular-graph-phase-c3-design.md
// section 5, SystemsStore.loadSram), never a live write, so the engine refuses that operation for a system node at
// submission. A core presents this to say it has a battery the registry publishes.
//
// The one live write is the old system-id one (Engine::writeSram, the writeSram RPC): a tracker's host saving a song
// INTO the running cart's battery, where a rebuild would reboot the cart and drop the song still in its work RAM. It
// is a compare-and-swap, since the caller built its bytes from a published copy the cart may have written since.
class SramCapability {
public:
    enum class WriteResult : std::uint8_t {
        Written,
        OutOfRange,  // no battery, or the range runs past its end: nothing written
        Changed,     // the battery no longer holds `expect` there: nothing written
    };

    virtual ~SramCapability() = default;

    // [block, or control while quiescent] Copy `size` bytes into the live battery at `offset`, but only if it still
    // holds the `size` bytes at `expect` there (none to compare when `expect` is null). The compare and the write run
    // between two blocks, so they are atomic with respect to the emulated program. Allocates nothing.
    virtual WriteResult write(std::uint32_t offset, const std::uint8_t* data, std::size_t size,
                              const std::uint8_t* expect) = 0;
};

// rp.system.rom/1: the ROM the core was built from. Its bytes are immutable for the core's life, so every member is a
// read any thread may make.
class RomCapability {
public:
    virtual ~RomCapability() = default;

    // [any] The whole ROM image, as the file the system was built from holds it: its header is a prefix of this.
    virtual std::span<const std::uint8_t> image() const = 0;
    // [any] The cartridge's title field as the ROM stores it, up to its first byte that is not printable ASCII, or
    // empty for a console whose header has none (a NES's iNES header, a Master System's).
    virtual std::string title() const = 0;
};

// The three, as every emulated core presents them: the work RAM through SystemBase::getMemory, a battery that is
// dumped (and written live only by Engine::writeSram), and the ROM vector the core keeps. A console holds one of each as members and returns their addresses.
class CoreMemory final : public MemoryCapability {
public:
    explicit CoreMemory(SystemBase& core) : core_(core) {}
    std::span<const Region> regions() const override;
    bool write(std::size_t region, std::uint32_t offset, const std::uint8_t* data, std::size_t size) override;

private:
    SystemBase& core_;
};

class CoreSram final : public SramCapability {
public:
    explicit CoreSram(SystemBase& core) : core_(core) {}
    WriteResult write(std::uint32_t offset, const std::uint8_t* data, std::size_t size,
                      const std::uint8_t* expect) override;

private:
    SystemBase& core_;
};

class CoreRom final : public RomCapability {
public:
    // `rom` is the vector the core keeps and never changes after construction; the title is the field of
    // `titleLength` bytes at `titleOffset` (0 bytes for a console with none).
    CoreRom(const std::vector<std::uint8_t>& rom, std::size_t titleOffset, std::size_t titleLength)
        : rom_(rom), titleOffset_(titleOffset), titleLength_(titleLength) {}
    std::span<const std::uint8_t> image() const override { return rom_; }
    std::string title() const override;

private:
    const std::vector<std::uint8_t>& rom_;
    std::size_t                      titleOffset_;
    std::size_t                      titleLength_;
};

}  // namespace rp
