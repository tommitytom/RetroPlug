// The test binary's half of the heap test's allocator (HeapTag.hpp): every block this binary allocates is tagged the
// host's, and a delete of the pack's block is counted, not freed.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

#include "HeapTag.hpp"

namespace {

std::atomic<std::uint64_t> gBlocks{0};
std::atomic<std::uint32_t> gViolations{0};

// A breakpoint target: a debugger stopped here sees the stack that freed another side's block.
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
    return rp::test::heap::allocateTagged(size, align, rp::test::heap::kHostTag);
}

void heapRelease(void* block) noexcept {
    if (block == nullptr) return;
    if (rp::test::heap::tagOf(block) != rp::test::heap::kHostTag) {
        crossed();  // the pack's block: counted and leaked
        return;
    }
    rp::test::heap::freeTagged(block);
}

}  // namespace

namespace rp::test::heap {
std::uint64_t hostBlocks() { return gBlocks.load(std::memory_order_relaxed); }
std::uint32_t hostViolations() { return gViolations.load(std::memory_order_relaxed); }
}  // namespace rp::test::heap

#include "HeapOperators.inc"
