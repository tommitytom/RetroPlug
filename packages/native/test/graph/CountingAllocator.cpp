#include "CountingAllocator.hpp"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"  // these replacements pair aligned_alloc with free on purpose
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#if defined(_WIN32)
#include <malloc.h>
#endif

namespace {

thread_local bool counting = false;
thread_local std::size_t refuseFrom = SIZE_MAX;  // an allocation of at least this many bytes fails
std::atomic<int>  allocations{0};  // while counting
std::atomic<int>  frees{0};        // while counting

// Every block is allocated over-aligned through one pair of calls, so any delete form can free any new
// form - the platform's aligned allocator does not mix with plain free on Windows.
void* rawAlloc(std::size_t n, std::size_t align) {
    if (align < alignof(std::max_align_t)) align = alignof(std::max_align_t);
    if (n > SIZE_MAX - align) return nullptr;  // rounding up would wrap
    const std::size_t size = (n ? n : 1) + align - 1;
#if defined(_WIN32)
    return _aligned_malloc(size, align);
#else
    return std::aligned_alloc(align, size / align * align);
#endif
}

void rawFree(void* p) noexcept {
    if (counting && p) frees.fetch_add(1, std::memory_order_relaxed);
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

void* allocate(std::size_t n, std::size_t align) {
    if (counting) allocations.fetch_add(1, std::memory_order_relaxed);
    if (n >= refuseFrom) throw std::bad_alloc();
    void* p = rawAlloc(n, align);
    if (!p) throw std::bad_alloc();
    return p;
}

void* allocateOrNull(std::size_t n, std::size_t align) noexcept {
    if (counting) allocations.fetch_add(1, std::memory_order_relaxed);
    if (n >= refuseFrom) return nullptr;
    return rawAlloc(n, align);
}

}  // namespace

namespace rp::test {

void beginCountingAllocations() {
    allocations = 0;
    frees = 0;
    counting = true;
}

AllocationCount endCountingAllocations() {
    counting = false;
    return {allocations.load(), frees.load()};
}

void refuseAllocationsFrom(std::size_t bytes) { refuseFrom = bytes; }

}  // namespace rp::test

// Every form is replaced, nothrow ones included: a form left to the runtime allocates through it (the
// planner's aligned nothrow new, under ASan), and the replaced delete then frees what it never allocated.
void* operator new(std::size_t n) { return allocate(n, alignof(std::max_align_t)); }
void* operator new[](std::size_t n) { return allocate(n, alignof(std::max_align_t)); }
void* operator new(std::size_t n, std::align_val_t a) { return allocate(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return allocate(n, static_cast<std::size_t>(a)); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return allocateOrNull(n, alignof(std::max_align_t)); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return allocateOrNull(n, alignof(std::max_align_t)); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return allocateOrNull(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return allocateOrNull(n, static_cast<std::size_t>(a));
}
void operator delete(void* p) noexcept { rawFree(p); }
void operator delete[](void* p) noexcept { rawFree(p); }
void operator delete(void* p, std::size_t) noexcept { rawFree(p); }
void operator delete[](void* p, std::size_t) noexcept { rawFree(p); }
void operator delete(void* p, std::align_val_t) noexcept { rawFree(p); }
void operator delete[](void* p, std::align_val_t) noexcept { rawFree(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { rawFree(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { rawFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { rawFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { rawFree(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { rawFree(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { rawFree(p); }
