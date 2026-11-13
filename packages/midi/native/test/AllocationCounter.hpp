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
