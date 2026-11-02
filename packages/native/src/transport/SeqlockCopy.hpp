#pragma once

#include <cstddef>
#include <cstring>

// The reader's copy of a seqlock slot (MemorySnapshotTriple, FrameBufferTriple): `n` bytes from `src` into `dst`.
//
// The read of `src` races the writer by design: the writer may be overwriting the slot while it is copied, and the
// reader re-reads the sequence counter afterwards and discards a copy a publish interleaved with. That race is the
// seqlock's, and it is benign, so under ThreadSanitizer the copy's own accesses are ignored. The WRITE of `dst` is
// not the seqlock's: `dst` is the caller's memory (a control-thread buffer, or a registry slot the block thread
// fills), and a race on it is a real bug. So it is recorded explicitly, with the caller's stack, after the copy.
// That keeps the sanitizer's view of `dst` exact without a suppression naming readInto, which would hide a race on
// the destination as well as the benign one on the source.
#if defined(__SANITIZE_THREAD__)
#  define RP_SEQLOCK_TSAN 1
#elif defined(__has_feature)
#  if __has_feature(thread_sanitizer)
#    define RP_SEQLOCK_TSAN 1
#  endif
#endif

#ifdef RP_SEQLOCK_TSAN
extern "C" {
void __tsan_ignore_thread_begin();
void __tsan_ignore_thread_end();
// GCC declares __tsan_write_range itself under -fsanitize=thread, as (void*, long); Clang leaves it to the runtime's
// interface, whose size is an unsigned long, so only Clang is given a declaration.
#if defined(__clang__)
void __tsan_write_range(void* addr, unsigned long size);
#endif
}
#endif

namespace rp {

inline void seqlockCopy(void* dst, const void* src, std::size_t n) {
#ifdef RP_SEQLOCK_TSAN
    __tsan_ignore_thread_begin();
    std::memcpy(dst, src, n);
    __tsan_ignore_thread_end();
    __tsan_write_range(dst, n);  // converts to either declaration's size type
#else
    std::memcpy(dst, src, n);
#endif
}

}  // namespace rp
