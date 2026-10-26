#pragma once

// What a stretch of code allocates and frees on the calling thread. CountingAllocator.cpp replaces the global
// operator new and delete for the binary it is linked into (every form, so a block any of them allocated is
// freed by any other), and counts each call made on a thread while that thread is counting. It also refuses, on
// a thread that asked it to, every allocation from a given size up. Link it into one binary at most once.

#include <cstddef>

namespace rp::test {

struct AllocationCount {
    int allocations = 0;
    int frees = 0;
};

// Start counting on this thread, from zero. Only one thread counts at a time.
void beginCountingAllocations();
// Stop, and what this thread allocated and freed since beginCountingAllocations.
AllocationCount endCountingAllocations();

// From now on, this thread's allocations of at least `bytes` fail (a throwing form throws std::bad_alloc, a
// nothrow form returns null). SIZE_MAX, the default, refuses none.
void refuseAllocationsFrom(std::size_t bytes);

}  // namespace rp::test
