// The mesen-heap pack's half of the heap test's allocator (HeapTag.hpp), and the hooks the test reads it through. The
// module is the Mesen package built as a pack again, with a heap of its own: its C++ runtime linked statically on Linux,
// Windows' static CRT. The hooks are exported beside rp_pack_entry (retroplug_pack_module's EXPORTS) and called by the
// test alone; no host looks for them.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

#include "HeapTag.hpp"
#include "graph/rp_node.h"  // RP_EXPORT

namespace {

std::atomic<std::uint64_t> gBlocks{0};
std::atomic<std::uint32_t> gViolations{0};

#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void crossed() noexcept {
    gViolations.fetch_add(1, std::memory_order_relaxed);
}

void* heapAllocate(std::size_t size, std::size_t align) noexcept {
    gBlocks.fetch_add(1, std::memory_order_relaxed);
    return rp::test::heap::allocateTagged(size, align, rp::test::heap::kPackTag);
}

void heapRelease(void* block) noexcept {
    if (block == nullptr) return;
    if (rp::test::heap::tagOf(block) != rp::test::heap::kPackTag) {
        crossed();  // the host's block: counted and leaked
        return;
    }
    rp::test::heap::freeTagged(block);
}

}  // namespace

#include "HeapOperators.inc"

// How many of the host's blocks this module was asked to delete.
extern "C" RP_EXPORT std::uint32_t rp_heap_violations(void) { return gViolations.load(std::memory_order_relaxed); }
// How many blocks this module's operator new has made: a count that moves proves the module allocates through it.
extern "C" RP_EXPORT std::uint64_t rp_heap_blocks(void) { return gBlocks.load(std::memory_order_relaxed); }
// A block of this module's, for the test's negative control (the host deleting it must count a violation).
extern "C" RP_EXPORT void* rp_heap_allocate(std::size_t size) { return ::operator new(size, std::nothrow); }
// Delete `block` in this module, for the test's other negative control (a host block must count here).
extern "C" RP_EXPORT void rp_heap_free(void* block) { ::operator delete(block); }
