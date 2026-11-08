#include "launchpad/desktop/PortClaim.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#else
#    include <cerrno>
#    include <fcntl.h>
#    include <sys/file.h>
#    include <unistd.h>
#endif

namespace rp::launchpad {
namespace {

/** A port name as a file name: FNV-1a, since names carry spaces, colons and client numbers. */
std::string lockFileName(const std::string& port) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : port) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char buf[40];
    std::snprintf(buf, sizeof buf, "control-%016llx.lock", static_cast<unsigned long long>(h));
    return buf;
}

}  // namespace

PortClaim claimPort(const std::string& dir, const std::string& port) {
    if (dir.empty() || port.empty()) return std::make_shared<int>(0);  // nowhere to coordinate: claim locally
    std::error_code ec;
    const std::filesystem::path locks = std::filesystem::path(dir) / "locks";
    std::filesystem::create_directories(locks, ec);
    const std::filesystem::path path = locks / lockFileName(port);
#if defined(_WIN32)
    // Share mode 0: a second CreateFile on the same path, from any process, fails until this handle closes.
    HANDLE h = CreateFileW(path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        // Held elsewhere is a sharing violation, and also the window in which the holder is closing a
        // FILE_FLAG_DELETE_ON_CLOSE handle (the file is delete-pending, and any open reports access denied): both mean
        // taken, never "cannot coordinate". Any other failure (an unwritable dir) must not refuse.
        const DWORD error = GetLastError();
        return error == ERROR_SHARING_VIOLATION || error == ERROR_ACCESS_DENIED ? nullptr : std::make_shared<int>(0);
    }
    return PortClaim(h, [](void* p) { CloseHandle(static_cast<HANDLE>(p)); });
#else
    const int fd = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
    if (fd < 0) return std::make_shared<int>(0);  // cannot coordinate (read-only dir): do not refuse over it
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        // Only "someone else holds it" refuses. Any other failure - a home on SMB/NFS without lock support
        // (ENOLCK, EOPNOTSUPP), some FUSE mounts - means we cannot coordinate, and must not lock the user out
        // of their own device over it.
        const bool held = errno == EWOULDBLOCK || errno == EAGAIN;
        ::close(fd);
        return held ? nullptr : std::make_shared<int>(0);
    }
    return PortClaim(new int(fd), [](void* p) {
        int* fdp = static_cast<int*>(p);
        ::close(*fdp);  // closing the descriptor drops the lock
        delete fdp;
    });
#endif
}

}  // namespace rp::launchpad
