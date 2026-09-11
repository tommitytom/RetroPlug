#include "util/ProcessId.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#endif

namespace rp {

bool processAlive(long long pid) {
    if (pid <= 0) return true; // not a pid we named; leave it alone
#if defined(_WIN32)
    // SYNCHRONIZE is the least the wait below needs. A null handle means either "gone" or "exists but
    // you may not touch it" - the second is ERROR_ACCESS_DENIED, and it is alive.
    const HANDLE h = ::OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (h == nullptr) return ::GetLastError() == static_cast<DWORD>(ERROR_ACCESS_DENIED);
    const bool alive = ::WaitForSingleObject(h, 0) == WAIT_TIMEOUT; // signalled = the process has exited
    ::CloseHandle(h);
    return alive;
#else
    if (::kill(static_cast<pid_t>(pid), 0) == 0) return true;
    return errno != ESRCH; // EPERM: someone else's process, and running
#endif
}

} // namespace rp
