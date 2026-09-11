#pragma once

#if defined(_WIN32)
// <process.h> (the CRT's _getpid) rather than the SDK's <processthreadsapi.h>: that header is
// not standalone-includable -- only <windows.h> defines the _AMD64_ target-arch macro
// that winnt.h demands, so including it directly is a "No Target Architecture" error.
#include <process.h>
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
    return static_cast<long long>(::_getpid());
#else
    return static_cast<long long>(::getpid());
#endif
}

// Is `pid` a live process? For reclaiming a pid-named scratch directory whose owner is gone: a host that
// is KILLED rather than exited - every Reaper the test suite starts - never runs its cleanup, so without
// a sweep the scratch root grows by one directory per run.
//
// Deliberately conservative, because the answer decides whether we delete a directory: "exists but is
// not ours" counts as alive, and only a definite "no such process" counts as dead. Answers for this pid
// NAMESPACE, which is what the scratch root is scoped to as well.
//
// Out of line, unlike currentProcessId above: the Windows implementation needs OpenProcess, so it needs
// <windows.h>, and this header is included by NesEverdriveFifo.hpp - i.e. by the whole NES core. Keeping
// <windows.h> in one TU is what stops that from becoming every TU's problem.
bool processAlive(long long pid);

} // namespace rp
