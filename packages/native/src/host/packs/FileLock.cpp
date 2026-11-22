#include "host/packs/FileLock.hpp"

#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#include "host/packs/PackDirs.hpp"

namespace fs = std::filesystem;

namespace rp::host::packs {

std::optional<FileLock> FileLock::acquire(const fs::path& file, Mode mode, bool wait, std::string* why) {
    FileLock lock;
#if defined(_WIN32)
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (why) *why = "cannot open " + displayPath(file) + ": error " + std::to_string(GetLastError());
        return std::nullopt;
    }
    DWORD flags = mode == Mode::Exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;
    if (!wait) flags |= LOCKFILE_FAIL_IMMEDIATELY;
    OVERLAPPED at{};
    if (!LockFileEx(h, flags, 0, 1, 0, &at)) {
        if (why) *why = "cannot lock " + displayPath(file) + ": error " + std::to_string(GetLastError());
        CloseHandle(h);
        return std::nullopt;
    }
    lock.handle_ = h;
#else
    const int fd = ::open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        if (why) *why = "cannot open " + displayPath(file) + ": " + std::strerror(errno);
        return std::nullopt;
    }
    int op = mode == Mode::Exclusive ? LOCK_EX : LOCK_SH;
    if (!wait) op |= LOCK_NB;
    int rc = 0;
    do rc = ::flock(fd, op);
    while (rc != 0 && errno == EINTR);
    if (rc != 0) {
        if (why) *why = "cannot lock " + displayPath(file) + ": " + std::strerror(errno);
        ::close(fd);
        return std::nullopt;
    }
    lock.fd_ = fd;
#endif
    return lock;
}

FileLock::FileLock(FileLock&& other) noexcept {
#if defined(_WIN32)
    handle_ = other.handle_;
    other.handle_ = nullptr;
#else
    fd_ = other.fd_;
    other.fd_ = -1;
#endif
}

FileLock& FileLock::operator=(FileLock&& other) noexcept {
    if (this != &other) {
        release();
#if defined(_WIN32)
        handle_ = other.handle_;
        other.handle_ = nullptr;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
    }
    return *this;
}

FileLock::~FileLock() { release(); }

void FileLock::release() noexcept {
#if defined(_WIN32)
    if (handle_ != nullptr) {
        OVERLAPPED at{};
        UnlockFileEx(static_cast<HANDLE>(handle_), 0, 1, 0, &at);
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
#else
    if (fd_ >= 0) {
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
        fd_ = -1;
    }
#endif
}

std::optional<FileLock> holdVersion(const fs::path& versionDir, std::string* why) {
    return FileLock::acquire(versionDir / kVersionLockFile, FileLock::Mode::Shared, false, why);
}

bool versionInUse(const fs::path& versionDir) {
    std::error_code ec;
    if (!fs::exists(versionDir / kVersionLockFile, ec)) return false;
    return !FileLock::acquire(versionDir / kVersionLockFile, FileLock::Mode::Exclusive, false, nullptr).has_value();
}

}  // namespace rp::host::packs
