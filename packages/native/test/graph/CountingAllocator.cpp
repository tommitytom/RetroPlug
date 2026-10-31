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

// A C library's allocations (blip_buf's blip_new, for one) are malloc's, not operator new's, so they are counted
// too: the interposed forms below forward to the platform's own allocator. Only glibc exports that allocator under a
// name we can call (`__libc_*`), and only where nothing else owns the allocator: a sanitizer build hands every block
// to the sanitizer's allocator and must see all of them (a block it allocated, freed through ours, would be freed the
// wrong way), and Windows links the static CRT, whose malloc would then be defined twice. On those the C allocations
// are not counted, and countsCAllocations() says so, so a case whose promise includes one SKIPS rather than measuring
// nothing and passing. Only malloc, calloc, realloc and free are interposed: a block from strdup, posix_memalign or
// aligned_alloc is invisible as an allocation while the free that releases it is counted, so a window that demands no
// frees should not put one of those in another window's way.
#if defined(__has_feature)
#  if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#    define RP_ALLOCATOR_IS_SANITIZED 1
#  endif
#elif defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#  define RP_ALLOCATOR_IS_SANITIZED 1
#endif

#if defined(__GLIBC__) && !defined(_WIN32) && !defined(RP_ALLOCATOR_IS_SANITIZED)
#define RP_COUNT_C_ALLOCATIONS 1
extern "C" void* __libc_malloc(std::size_t size);
extern "C" void* __libc_calloc(std::size_t count, std::size_t size);
extern "C" void* __libc_realloc(void* ptr, std::size_t size);
extern "C" void  __libc_free(void* ptr);
#endif

namespace {

thread_local bool counting = false;
thread_local bool countingC = false;  // the C library's blocks too (see beginCountingAllocations)
thread_local std::size_t refuseFrom = SIZE_MAX;  // an allocation of at least this many bytes fails
std::atomic<int>  allocations{0};  // while counting
std::atomic<int>  frees{0};        // while counting

// The allocator the blocks are actually allocated from. Under C counting that is glibc's: systemFree calls it by name
// so the interposed free below does not see (and count) the frees our own operator delete makes, and systemAlloc asks
// it directly rather than through malloc, which this file redefines.
void* systemAlloc(std::size_t n, std::size_t align) {
    if (align < alignof(std::max_align_t)) align = alignof(std::max_align_t);
    if (n > SIZE_MAX - align) return nullptr;  // rounding up would wrap
    const std::size_t size = (n ? n : 1) + align - 1;
#if defined(_WIN32)
    return _aligned_malloc(size, align);
#else
    return std::aligned_alloc(align, size / align * align);
#endif
}

void systemFree(void* p) noexcept {
#if defined(RP_COUNT_C_ALLOCATIONS)
    __libc_free(p);
#elif defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

// Every block is allocated over-aligned through one pair of calls, so any delete form can free any new
// form - the platform's aligned allocator does not mix with plain free on Windows.
void* rawAlloc(std::size_t n, std::size_t align) {
    return systemAlloc(n, align);
}

void rawFree(void* p) noexcept {
    if (counting && p) frees.fetch_add(1, std::memory_order_relaxed);
    systemFree(p);
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

void beginCountingAllocations(bool includeCAllocations) {
    allocations = 0;
    frees = 0;
    countingC = includeCAllocations;
    counting = true;
}

AllocationCount endCountingAllocations() {
    counting = false;
    countingC = false;
    return {allocations.load(), frees.load()};
}

bool countsCAllocations() {
#if defined(RP_COUNT_C_ALLOCATIONS)
    return true;
#else
    return false;
#endif
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

#if defined(RP_COUNT_C_ALLOCATIONS)
// The C library's allocation entry points, counted on the thread that asked for the window. Each forwards to glibc's
// own (see the note at the top). free counts only a real block, as operator delete does.
extern "C" void* malloc(std::size_t size) {
    if (countingC) allocations.fetch_add(1, std::memory_order_relaxed);
    return __libc_malloc(size);
}

extern "C" void* calloc(std::size_t count, std::size_t size) {
    if (countingC) allocations.fetch_add(1, std::memory_order_relaxed);
    return __libc_calloc(count, size);
}

extern "C" void* realloc(void* ptr, std::size_t size) {
    // A realloc to a real size is one allocation (its block may be the same one); to zero it releases.
    if (countingC) {
        if (size) {
            allocations.fetch_add(1, std::memory_order_relaxed);
        } else if (ptr) {
            frees.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return __libc_realloc(ptr, size);
}

extern "C" void free(void* ptr) {
    if (countingC && ptr) frees.fetch_add(1, std::memory_order_relaxed);
    __libc_free(ptr);
}
#endif
