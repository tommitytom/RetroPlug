#pragma once

#include <filesystem>
#include <optional>
#include <string>

// An advisory lock on a file, held by this process until the FileLock goes (or the process ends, however it ends: the
// OS drops it, so a crash leaves no stale lock behind). flock on POSIX, LockFileEx on Windows. Both lock per open
// file, not per process, so two FileLocks on one file in one process conflict as two processes' would, which is what
// lets one test process stand in for two.
namespace rp::host::packs {

class FileLock {
public:
    enum class Mode { Shared, Exclusive };

    // The lock on `file` (created when missing), or nothing with the reason in *why. `wait` blocks until the lock is
    // free; otherwise a lock held elsewhere in a conflicting mode answers nothing at once.
    static std::optional<FileLock> acquire(const std::filesystem::path& file, Mode mode, bool wait, std::string* why);

    FileLock(FileLock&& other) noexcept;
    FileLock& operator=(FileLock&& other) noexcept;
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    ~FileLock();

private:
    FileLock() = default;
    void release() noexcept;
#if defined(_WIN32)
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};

// The lock file a process holds in each installed version folder it loaded.
inline constexpr const char* kVersionLockFile = "pack.lock";

// This process's hold on `versionDir`: a shared lock on its lock file, kept for the life of the process by the pack
// table, since a loaded pack is never unloaded.
std::optional<FileLock> holdVersion(const std::filesystem::path& versionDir, std::string* why);

// Whether any process (this one included) holds `versionDir`: true when an exclusive lock cannot be taken at once. A
// folder with no lock file is held by nobody.
bool versionInUse(const std::filesystem::path& versionDir);

}  // namespace rp::host::packs
