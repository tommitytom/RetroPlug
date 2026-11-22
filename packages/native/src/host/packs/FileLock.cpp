#include "host/packs/FileLock.hpp"

#include <cstring>
#include <system_error>
#include <utility>

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
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "host/packs/PackDirs.hpp"

namespace fs = std::filesystem;

namespace rp::host::packs {

namespace {

void fail(std::string* why, FileLock::Failure* failure, FileLock::Failure kind, std::string reason) {
    if (why) *why = std::move(reason);
    if (failure) *failure = kind;
}

#if defined(_WIN32)
bool sameFile(HANDLE held, const fs::path& file) {
    // A fresh open of the path, to compare what it names now with what is held.
    HANDLE now = CreateFileW(file.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (now == INVALID_HANDLE_VALUE) return false;
    // The 128-bit id: the 64-bit index of GetFileInformationByHandle is not unique on every file system (ReFS).
    FILE_ID_INFO a{}, b{};
    const bool same = GetFileInformationByHandleEx(held, FileIdInfo, &a, sizeof a) &&
                      GetFileInformationByHandleEx(now, FileIdInfo, &b, sizeof b) &&
                      a.VolumeSerialNumber == b.VolumeSerialNumber &&
                      std::memcmp(&a.FileId, &b.FileId, sizeof a.FileId) == 0;
    CloseHandle(now);
    return same;
}
#else
bool sameFile(int held, const fs::path& file) {
    struct stat a {}, b {};
    return ::fstat(held, &a) == 0 && ::stat(file.c_str(), &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}
#endif

}  // namespace

std::optional<FileLock> FileLock::acquire(const fs::path& file, Mode mode, bool wait, std::string* why,
                                          Failure* failure, const std::function<void()>& afterOpen) {
    if (failure) *failure = Failure::None;
    FileLock lock;
    lock.mode_ = mode;
#if defined(_WIN32)
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        fail(why, failure,
             error == ERROR_ACCESS_DENIED || error == ERROR_WRITE_PROTECT ? Failure::Denied : Failure::Other,
             "cannot open " + displayPath(file) + ": error " + std::to_string(error));
        return std::nullopt;
    }
    lock.handle_ = h;  // released by the destructor on every return below
    if (afterOpen) afterOpen();
    DWORD flags = mode == Mode::Exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;
    if (!wait) flags |= LOCKFILE_FAIL_IMMEDIATELY;
    OVERLAPPED at{};
    if (!LockFileEx(h, flags, 0, 1, 0, &at)) {
        fail(why, failure, Failure::Other,
             "cannot lock " + displayPath(file) + ": error " + std::to_string(GetLastError()));
        return std::nullopt;
    }
    if (!sameFile(h, file)) {
        fail(why, failure, Failure::Replaced,
             displayPath(file) + " was replaced while it was locked: the folder is being deleted");
        return std::nullopt;
    }
#else
    const int fd = ::open(file.c_str(), O_RDONLY | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        const int error = errno;
        fail(why, failure,
             error == EACCES || error == EROFS || error == EPERM ? Failure::Denied : Failure::Other,
             "cannot open " + displayPath(file) + ": " + std::strerror(error));
        return std::nullopt;
    }
    lock.fd_ = fd;  // released by the destructor on every return below
    if (afterOpen) afterOpen();
    int op = mode == Mode::Exclusive ? LOCK_EX : LOCK_SH;
    if (!wait) op |= LOCK_NB;
    int rc = 0;
    do rc = ::flock(fd, op);
    while (rc != 0 && errno == EINTR);
    if (rc != 0) {
        fail(why, failure, Failure::Other, "cannot lock " + displayPath(file) + ": " + std::strerror(errno));
        return std::nullopt;
    }
    if (!sameFile(fd, file)) {
        fail(why, failure, Failure::Replaced,
             displayPath(file) + " was replaced while it was locked: the folder is being deleted");
        return std::nullopt;
    }
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
    mode_ = other.mode_;
}

FileLock& FileLock::operator=(FileLock&& other) noexcept {
    if (this != &other) {
        release();
        mode_ = other.mode_;
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

std::optional<FileLock> holdVersion(const fs::path& versionDir, std::string* why, FileLock::Failure* failure) {
    return FileLock::acquire(versionDir / kVersionLockFile, FileLock::Mode::Shared, false, why, failure);
}

bool versionInUse(const fs::path& versionDir) {
    std::error_code ec;
    if (!fs::exists(versionDir / kVersionLockFile, ec)) return false;
    return !FileLock::acquire(versionDir / kVersionLockFile, FileLock::Mode::Exclusive, false, nullptr).has_value();
}

}  // namespace rp::host::packs
