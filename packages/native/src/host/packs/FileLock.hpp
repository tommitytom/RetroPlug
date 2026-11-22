#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

// An advisory lock on a file, held by this process until the FileLock goes (or the process ends, however it ends: the
// OS drops it, so a crash leaves no stale lock behind). flock on POSIX, LockFileEx on Windows. Both lock per open
// file, not per process, so two FileLocks on one file in one process conflict as two processes' would, which is what
// lets one test process stand in for two. Two limits of the OS: flock on NFS is emulated with POSIX byte-range locks,
// which belong to the process (so two FileLocks in one process do not conflict there) and which some servers do not
// grant at all; and a child made by fork without exec shares its parent's open file descriptions, so it shares the
// parent's locks and keeps them after the parent's FileLock goes. The pack folders are local files and a host never
// forks without exec, so neither is handled.
namespace rp::host::packs {

class FileLock {
public:
    enum class Mode { Shared, Exclusive };
    // Why acquire gave nothing, for the caller that treats one cause apart.
    enum class Failure { None, Denied, Replaced, Other };

    // The lock on `file` (created when missing), or nothing with the reason in *why and the cause in *failure. `wait`
    // blocks until the lock is free; otherwise a lock held elsewhere in a conflicting mode answers nothing at once.
    // The file is opened read-only (a lock needs no write access, so a read-only folder's existing lock file can still
    // be taken). Once locked, the file at `file` must still be the one held: a lock on a file that was unlinked, or
    // replaced by another, while this call waited protects nothing, so it is released and refused (Failure::Replaced).
    // `afterOpen`, when given, runs between the open and the lock: a test's way of making that happen.
    static std::optional<FileLock> acquire(const std::filesystem::path& file, Mode mode, bool wait, std::string* why,
                                           Failure* failure = nullptr,
                                           const std::function<void()>& afterOpen = {});

    FileLock(FileLock&& other) noexcept;
    FileLock& operator=(FileLock&& other) noexcept;
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    ~FileLock();

    Mode mode() const { return mode_; }

private:
    FileLock() = default;
    void release() noexcept;
    Mode mode_ = Mode::Shared;
#if defined(_WIN32)
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};

// The lock file a process holds in each installed version folder it loaded.
inline constexpr const char* kVersionLockFile = "pack.lock";

// This process's hold on `versionDir`: a shared lock on its lock file, kept for the life of the process by the pack
// table, since a loaded pack is never unloaded. A folder a settle is deleting is emptied under its exclusive lock, so a
// hold that races the deletion fails to lock; one that comes after it holds an empty folder, whose manifest read
// refuses it (on POSIX the folder is also renamed away, so a hold that opened the old lock file is refused as
// "replaced"). What is NOT guaranteed on Windows is that the lock file is never recreated in the emptied folder
// before the settle removes it: callers hold under state.lock where the folder must not vanish (loadLaunchPacks),
// and a hold taken without it is only as good as the manifest read that follows. May fail transiently while
// versionInUse (below) has its brief exclusive lock.
// *failure says why a hold failed: Denied (a folder this process cannot write, which no settle can delete either, so
// the caller may load it unheld), Replaced (being deleted), or Other (contended: worth a brief retry).
std::optional<FileLock> holdVersion(const std::filesystem::path& versionDir, std::string* why,
                                    FileLock::Failure* failure = nullptr);

// holdVersion, tried again briefly while the failure is only contention (versionInUse's instant of an exclusive lock,
// a settle's hold) or, on Windows, an access error that may be a scanner's brief handle on the file (Denied): the one
// bounded loop the table and a launch's discovery share. A folder being deleted (Replaced) is refused at once, and so
// is a Denied one elsewhere, where it means a folder this process cannot write and waiting changes nothing.
std::optional<FileLock> holdVersionBriefly(const std::filesystem::path& versionDir, std::string* why,
                                           FileLock::Failure* failure);

// Whether any process (this one included) holds `versionDir`: true when an exclusive lock cannot be taken at once. A
// folder with no lock file is held by nobody. The probe takes the exclusive lock for an instant and lets it go, so a
// holdVersion that lands in that instant fails once; callers retry or treat it as in use.
bool versionInUse(const std::filesystem::path& versionDir);

}  // namespace rp::host::packs
