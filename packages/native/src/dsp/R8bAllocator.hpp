#pragma once

// r8brain's memory hooks, pointed at an allocator that throws.
//
// Every buffer r8brain allocates comes from `R8B_MEMALLOCCLASS` (its `CFixedBuffer`) and every object from
// `R8B_BASECLASS`, and it checks each result with `R8BASSERT`, which r8bconf.h compiles to nothing by default. A failed
// buffer allocation would therefore leave a built resampler holding a null buffer, and its first `process` would write
// through it. Both hooks are defined here, before the library's headers are read, and this header is the only way this
// tree reaches r8brain, so a failed allocation is a `std::bad_alloc` out of `Resampler::init` (leaving `ready()`
// false) instead of a crash in a later block.
//
// A throw cannot corrupt what r8brain already owns: its `realloc` allocates before it frees, so a failure there
// leaves the old buffer intact, and its in-place `alloc` runs on a buffer a constructor has just default-built (null,
// not stale). A throw from the middle of a resampler's construction does leak the processors it had already built
// (its constructor's cleanup never runs), which is a leak at exhaustion and the only thing it costs. r8bbase.cpp,
// which only defines r8brain's static members and allocates nothing, still sees the library's defaults; both bases
// hold no state, so a class that inherits either has one layout everywhere.

#include <cstddef>
#include <cstdlib>
#include <new>

namespace rp::dsp {

struct R8bAllocator {
    // A test seam, not a setting: while nonzero, a request larger than this throws, so a case can drive `Resampler`'s
    // failure path on a host whose memory is not exhausted. Nothing else in the process may resample while it is set.
    static inline std::size_t allocationLimitForTest = 0;

    static void* allocmem(std::size_t size) {
        if (allocationLimitForTest != 0 && size > allocationLimitForTest) throw std::bad_alloc();
        void* const block = std::malloc(size);
        if (block == nullptr) throw std::bad_alloc();
        return block;
    }

    static void* reallocmem(void* block, std::size_t size) {
        if (allocationLimitForTest != 0 && size > allocationLimitForTest) throw std::bad_alloc();
        void* const grown = std::realloc(block, size);
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
