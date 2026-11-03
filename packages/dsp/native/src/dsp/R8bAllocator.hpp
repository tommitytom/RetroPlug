#pragma once

// r8brain's memory hooks, pointed at an allocator that throws.
//
// Every buffer r8brain allocates comes from `R8B_MEMALLOCCLASS` (its `CFixedBuffer`) and every object from
// `R8B_BASECLASS`, and it checks each result with `R8BASSERT`, which r8bconf.h compiles to nothing by default. A failed
// buffer allocation would therefore leave a built resampler holding a null buffer, and its first `process` would write
// through it. Both hooks are defined here, before the library's headers are read, so a failed allocation is a
// `std::bad_alloc` out of `Resampler::init` (leaving `ready()` false) instead of a crash in a later block.
//
// Every r8brain class in this program is built on this allocator, its static members included: `R8bBase.cpp` compiles
// r8brain's own `r8bbase.cpp` through this header, the tree declares no target for the vendored library (so there is
// none to link), and the dep's headers are on the include path only for the translation units that include this header
// first, so there is no second definition of a class for the linker to keep. The free side has to stay
// malloc/free-compatible whatever this struct grows into,
// because r8brain's static caches (`CDSPFIRFilterCache`, `CDSPRealFFTKeeper`, the frac-bank lists) free what it
// allocated from a static destructor at exit, where a throw would end the process, and its `reallocmem` expects a block
// `malloc` handed out.
//
// A failed allocation cannot corrupt what r8brain already owns, with one exception inside its shared FIR filter cache.
// Its `realloc` allocates before it frees, so a failure there leaves the old buffer intact, and its in-place `alloc`
// runs on a buffer a constructor has just default-built (null, not stale). A throw from the middle of a resampler's
// construction does leak the processors it had already built (its constructor's cleanup never runs).
// `CDSPFIRFilterCache::getLPFilter` is the exception: it allocates a filter, adds one to the process-wide `ObjCount`,
// and only then builds the filter (whose kernel and FFT allocation can throw), so a failure there leaks that filter and
// leaves `ObjCount` permanently one above the list's length. Each such failure costs the cache one of its
// `R8B_FILTER_CACHE_MAX` (96) slots, and once the count is at the maximum over a one-element list a miss reaches the
// eviction branch with no previous object and dereferences null. That takes repeated exhaustion, which is accepted:
// the alternatives are patching a vendored library or not throwing at all, and both are worse for an out-of-memory
// case than a leaked filter.

#include <cstddef>
#include <cstdlib>
#include <new>

namespace rp::dsp {

struct R8bAllocator {
    // A test seam, not a setting: while nonzero, a request larger than this throws, so a case can drive `Resampler`'s
    // failure path on a host whose memory is not exhausted. Nothing else in the process may resample while it is set.
    static inline std::size_t allocationLimitForTest = 0;

    // A request for no bytes still gets a block. `malloc(0)` and `realloc(block, 0)` are allowed to return null, which
    // would be read here as an out-of-memory throw for a request that asks for nothing, so both hooks round a zero-byte
    // request up to one byte: a null result from either always means exhaustion.
    static constexpr std::size_t bytesFor(std::size_t size) noexcept { return size == 0 ? 1 : size; }

    static void* allocmem(std::size_t size) {
        const std::size_t bytes = bytesFor(size);
        if (allocationLimitForTest != 0 && bytes > allocationLimitForTest) throw std::bad_alloc();
        void* const block = std::malloc(bytes);
        if (block == nullptr) throw std::bad_alloc();
        return block;
    }

    static void* reallocmem(void* block, std::size_t size) {
        const std::size_t bytes = bytesFor(size);
        if (allocationLimitForTest != 0 && bytes > allocationLimitForTest) throw std::bad_alloc();
        void* const grown = std::realloc(block, bytes);
        if (grown == nullptr) throw std::bad_alloc();
        return grown;
    }

    static void freemem(void* block) noexcept { std::free(block); }

    static void* operator new(std::size_t size) { return allocmem(size); }
    static void* operator new[](std::size_t size) { return allocmem(size); }
    static void operator delete(void* block) noexcept { freemem(block); }
    static void operator delete[](void* block) noexcept { freemem(block); }
};

} // namespace rp::dsp

#define R8B_BASECLASS ::rp::dsp::R8bAllocator
#define R8B_MEMALLOCCLASS ::rp::dsp::R8bAllocator

#include <CDSPResampler.h>
