#pragma once

#include <cfenv>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define RP_OFFLINE_FP_MXCSR 1
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#define RP_OFFLINE_FP_FPCR 1
#endif

namespace rp::host {

// The default floating-point environment, held for a job's run, with the caller's saved first and put back after: round
// to nearest, no exception trapping, and subnormals kept (no flush-to-zero, no denormals-are-zero). A worker thread
// starts with the environment of the thread that submitted the job which started it under glibc (its documented
// pthread_create behaviour; a Windows thread starts in the default mode), and an audio host commonly sets
// flush-to-zero on its threads. A job's floats are a kit's bytes, which the kernels compute in the default mode (a
// biquad's and the resampler's tails pass through subnormals, and a normalise divides one by the source's peak, and a
// subnormal peak reads as zero under denormals-are-zero, leaving the gain as written, where in the default mode it
// overflows), so a job must not compute them in whatever mode its submitter happened to run in. fesetenv(FE_DFL_ENV)
// gives the standard part; the flush bits are cleared from that default explicitly as well, since not every C library's
// default environment clears them (x86's MXCSR FTZ and DAZ, AArch64's FPCR FZ and FZ16).
class DefaultFloatEnvironment {
public:
    DefaultFloatEnvironment() {
        std::fegetenv(&saved_);
#if defined(RP_OFFLINE_FP_MXCSR)
        savedMxcsr_ = _mm_getcsr();
#elif defined(RP_OFFLINE_FP_FPCR)
        asm volatile("mrs %0, fpcr" : "=r"(savedFpcr_));
#endif
        std::fesetenv(FE_DFL_ENV);
#if defined(RP_OFFLINE_FP_MXCSR)
        _mm_setcsr(_mm_getcsr() & ~(kFtz | kDaz));
#elif defined(RP_OFFLINE_FP_FPCR)
        std::uint64_t fpcr = 0;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        asm volatile("msr fpcr, %0" : : "r"(fpcr & ~(kFz | kFz16)));
#endif
    }
    ~DefaultFloatEnvironment() {
        std::fesetenv(&saved_);
#if defined(RP_OFFLINE_FP_MXCSR)
        _mm_setcsr(savedMxcsr_);
#elif defined(RP_OFFLINE_FP_FPCR)
        asm volatile("msr fpcr, %0" : : "r"(savedFpcr_));
#endif
    }
    DefaultFloatEnvironment(const DefaultFloatEnvironment&) = delete;
    DefaultFloatEnvironment& operator=(const DefaultFloatEnvironment&) = delete;

private:
    std::fenv_t saved_{};
#if defined(RP_OFFLINE_FP_MXCSR)
    static constexpr unsigned kFtz = 0x8000u;  // MXCSR bit 15, flush-to-zero
    static constexpr unsigned kDaz = 0x0040u;  // MXCSR bit 6, denormals-are-zero
    unsigned savedMxcsr_ = 0;
#elif defined(RP_OFFLINE_FP_FPCR)
    static constexpr std::uint64_t kFz = std::uint64_t(1) << 24;    // FPCR.FZ
    static constexpr std::uint64_t kFz16 = std::uint64_t(1) << 19;  // FPCR.FZ16
    std::uint64_t savedFpcr_ = 0;
#endif
};

}  // namespace rp::host
