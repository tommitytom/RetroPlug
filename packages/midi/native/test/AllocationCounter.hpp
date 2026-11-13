#pragma once

// A test-only replacement of the global allocation functions, so a case can prove a Threaded push allocates nothing on
// the caller's thread (docs/known-bugs-batch-3-design.md section 6): the audio callback may not allocate, and what it
// calls is PortWriter::push, through MidiStream::send.
//
// The count is per THREAD, and only for the thread that armed it: the writer thread's own allocations (the log string,
// the fake's records) are its own business, and a case that counted them would measure the fake.
//
// The operator new/delete replacements themselves are defined ONCE, in PortWriter.test.cpp, and cover the whole
// retroplug-midi-test binary; this header is how the stream's own case (MidiStream.test.cpp) arms and reads the count.
//
// What the replacements do not cover: the ALIGNED forms (operator new(std::align_val_t)), which nothing on the push path
// uses, and on Windows (/MD) every allocation made inside the C++ runtime's own DLL, which calls the DLL's operator new
// and not the executable's. Neither gap touches what the cases measure: the push is a memcpy into the ring and a futex
// (a `lock xadd` plus, for a sleeping writer, one wait or wake syscall), and none of those allocates.

#include <cstddef>

namespace rp::midi::test {

extern thread_local bool        countAllocations;
extern thread_local std::size_t allocations;

/** Counts this thread's allocations for as long as it lives: what a case wraps the pushes it measures in. */
class AllocationCount {
public:
    AllocationCount() {
        allocations      = 0;
        countAllocations = true;
    }
    ~AllocationCount() { countAllocations = false; }
    AllocationCount(const AllocationCount&)            = delete;
    AllocationCount& operator=(const AllocationCount&) = delete;

    std::size_t value() const { return allocations; }
};

}  // namespace rp::midi::test
