#pragma once

// What a stretch of code allocates and frees on the calling thread. CountingAllocator.cpp replaces the global
// operator new and delete for the binary it is linked into (every form, so a block any of them allocated is
// freed by any other), and counts each call made on a thread while that thread is counting. It also refuses, on
// a thread that asked it to, every allocation from a given size up. Link it into one binary at most once.
//
// A C library's blocks are malloc's, not operator new's, so the C library's allocation entry points (malloc,
// calloc, realloc, free) are replaced too, where the platform's allocator can be reached by name: on glibc, and not
// under AddressSanitizer (whose allocator must see every block) or on Windows (which links the static CRT, where the
// CRT's malloc cannot be defined twice). countsCAllocations() says whether this binary sees them, so a case that holds
// a C allocation can say what it measured. They are counted only on request, because a window can be measuring a
// subsystem whose own allocations ARE the C library's: the script host's QuickJS allocates through malloc, and the
// script cases read QuickJS's counters for its side and hold operator new to zero for ours.

#include <cstddef>

namespace rp::test {

struct AllocationCount {
    int allocations = 0;
    int frees = 0;
};

// Start counting on this thread, from zero. With `includeCAllocations`, the C library's blocks are counted too (see
// above), which a case does when a C allocation is part of the promise it holds. Only one thread counts at a time.
void beginCountingAllocations(bool includeCAllocations = false);
// Stop, and what this thread allocated and freed since beginCountingAllocations.
AllocationCount endCountingAllocations();

// Whether beginCountingAllocations also counts the C library's allocations in this binary (see above).
bool countsCAllocations();

// From now on, this thread's allocations of at least `bytes` fail (a throwing form throws std::bad_alloc, a
// nothrow form returns null). SIZE_MAX, the default, refuses none.
void refuseAllocationsFrom(std::size_t bytes);

}  // namespace rp::test
