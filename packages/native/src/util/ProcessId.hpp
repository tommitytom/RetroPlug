#pragma once

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace rp {

// This process's id. Used to name scratch directories under a FIXED absolute root (/tmp/...), which
// several harnesses run concurrently: the reaper suite, the native test runner's per-file host
// processes, a consumer repo's per-test processes. Those roots are shared by the whole MACHINE, not
// the process, so anything written under one without a pid segment is a cross-process data race -
// one run truncating, or deleting, a file another run is reading.
inline long long currentProcessId() {
#if defined(_WIN32)
    return static_cast<long long>(::GetCurrentProcessId());
#else
    return static_cast<long long>(::getpid());
#endif
}

} // namespace rp
