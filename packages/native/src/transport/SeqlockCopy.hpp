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
// Declared with GCC's builtin type, (void*, long), for every compiler: GCC accepts a matching redeclaration in either
// dialect and needs one under -std=c++20, where its builtin is not visible (an unsigned long size would clash with the
// builtin under gnu++20); Clang has no builtin. The runtime's symbol is extern "C", so long against its uptr is the
// same ABI.
void __tsan_write_range(void* addr, long size);
}
#endif

namespace rp {

inline void seqlockCopy(void* dst, const void* src, std::size_t n) {
#ifdef RP_SEQLOCK_TSAN
    __tsan_ignore_thread_begin();
    std::memcpy(dst, src, n);
    __tsan_ignore_thread_end();
    __tsan_write_range(dst, static_cast<long>(n));
#else
    std::memcpy(dst, src, n);
#endif
}

}  // namespace rp

#undef RP_SEQLOCK_TSAN  // private to this header: the copy above is all that reads it
