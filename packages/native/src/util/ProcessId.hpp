#pragma once

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
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

// Is `pid` a live process? For reclaiming a pid-named scratch directory whose owner is gone: a host that
// is KILLED rather than exited - every Reaper the test suite starts - never runs its cleanup, so without
// a sweep the scratch root grows by one directory per run.
//
// Deliberately conservative, because the answer decides whether we delete a directory: "exists but is
// not ours" (EPERM) counts as alive, and only a definite "no such process" counts as dead. Answers for
// this pid NAMESPACE, which is what the scratch root is scoped to as well.
inline bool processAlive(long long pid) {
    if (pid <= 0) return true; // not a pid we named; leave it alone
#if defined(_WIN32)
    const HANDLE h = ::OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (h == nullptr) return ::GetLastError() == ERROR_ACCESS_DENIED;
    const bool alive = ::WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    ::CloseHandle(h);
    return alive;
#else
    if (::kill(static_cast<pid_t>(pid), 0) == 0) return true;
    return errno != ESRCH;
#endif
}

} // namespace rp
