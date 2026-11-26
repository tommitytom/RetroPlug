#pragma once

// The heap test's tagging allocator (docs/downloadable-packs-m5a-plan.md). Both sides of the boundary replace the global
// operator new and delete in their own module: the test binary (the host, HostHeap.cpp) and the mesen-heap pack
// (PackHeap.cpp). Every block carries a header naming the side that allocated it, so a delete that finds the other
// side's tag is a block that crossed the boundary: it is counted and LEAKED, never freed, since freeing it in the
// other module's C runtime is the bug being caught (on Windows' /MT, each module's malloc is its own).
//
// No allocation happens in here: a delete runs inside every container's destructor, and an allocation there would recurse.

#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace rp::test::heap {

inline constexpr std::uint64_t kHostTag = 0x484F53542D484541ull;  // "HOST-HEA"
inline constexpr std::uint64_t kPackTag = 0x5041434B2D484541ull;  // "PACK-HEA"

// Just before every block: the pointer malloc returned (the block is aligned past it) and the allocating side's tag.
struct Header {
    void*         raw;
    std::uint64_t tag;
};
static_assert(sizeof(Header) == 16, "the header keeps a block 16-byte aligned");

// A block of `size` bytes aligned to `align` (at least 16), tagged `tag`; null when malloc has none.
inline void* allocateTagged(std::size_t size, std::size_t align, std::uint64_t tag) noexcept {
    if (align < sizeof(Header)) align = sizeof(Header);
    void* raw = std::malloc(size + align + sizeof(Header));
    if (raw == nullptr) return nullptr;
    std::uintptr_t at = reinterpret_cast<std::uintptr_t>(raw) + sizeof(Header);
    at = (at + align - 1) & ~static_cast<std::uintptr_t>(align - 1);
    Header* header = reinterpret_cast<Header*>(at) - 1;
    header->raw = raw;
    header->tag = tag;
    return reinterpret_cast<void*>(at);
}

// The tag of a block allocateTagged made (on either side).
inline std::uint64_t tagOf(const void* block) noexcept { return (static_cast<const Header*>(block) - 1)->tag; }

// Free a block this side allocated.
inline void freeTagged(void* block) noexcept { std::free((static_cast<Header*>(block) - 1)->raw); }

// The host's counts (HostHeap.cpp): its own allocations, and the pack's blocks it was asked to delete.
std::uint64_t hostBlocks();
std::uint32_t hostViolations();

}  // namespace rp::test::heap
