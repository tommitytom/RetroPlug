#include "util/FileSync.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace rp {

bool fsyncPath(const std::string& path) {
#if defined(_WIN32)
    // GENERIC_WRITE, not READ: FlushFileBuffers documents a write handle as its requirement.
    const HANDLE h = ::CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const bool ok = ::FlushFileBuffers(h) != 0;
    ::CloseHandle(h);
    return ok;
#else
    // O_WRONLY rather than O_RDONLY: fsync on a read-only descriptor is allowed on Linux but is not
    // portable (POSIX leaves it unspecified, and some systems answer EBADF). The file always exists
    // by the time we get here - we just wrote it - so no O_CREAT.
    const int fd = ::open(path.c_str(), O_WRONLY);
    if (fd < 0) return false;
    int rc;
    do {
        rc = ::fsync(fd);
    } while (rc != 0 && errno == EINTR);
    ::close(fd);
    return rc == 0;
#endif
}

} // namespace rp
