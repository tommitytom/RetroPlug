#include "host/packs/PackState.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <thread>

#include <rfl.hpp>
#include <rfl/json.hpp>

#include "host/packs/PackDirs.hpp"
#include "host/packs/PackManifest.hpp"
#include "util/FileSync.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#define RP_GETPID _getpid
#else
#include <unistd.h>
#define RP_GETPID getpid
#endif

namespace fs = std::filesystem;

namespace rp::host::packs {

// Named, not anonymous: reflect-cpp's conversion through a fake object of the type is refused by Apple clang for a type
// of internal linkage.
namespace detail::state {

struct RawVersion {
    std::uint32_t version = 0;
};
struct RawEntry {
    std::string                id;
    std::string                active;
    std::optional<std::string> pending;
    std::optional<bool>        remove;
};
struct RawState {
    std::uint32_t         version = kPackStateVersion;
    std::vector<RawEntry> packs;
};

}  // namespace detail::state

using namespace detail::state;

namespace {

// A folder being deleted is renamed to `.<version>` + this + `<pid>-<n>`; a crash can leave one for the next settle.
constexpr const char* kTrashMarker = ".deleting-";

bool refuse(std::string* why, std::string reason) {
    if (why) *why = std::move(reason);
    return false;
}

// The file read and checked; the caller holds state.lock.
std::optional<PackState> readHeld(const fs::path& dataRoot, std::string* why) {
    const fs::path file = dataRoot / "state.json";
    std::error_code ec;
    if (!fs::exists(file, ec)) return PackState{};
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        refuse(why, "state.json: cannot read " + displayPath(file));
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // The version first, on its own: a newer RetroPlug's schema need not parse as this one's, and its file must be
    // refused as newer rather than as malformed.
    const auto stamp = rfl::json::read<RawVersion>(text);
    if (!stamp) {
        refuse(why, std::string("state.json: no version: ") + stamp.error().what());
        return std::nullopt;
    }
    if (stamp->version < 1) {
        refuse(why, "state.json: version " + std::to_string(stamp->version) + " is not a version");
        return std::nullopt;
    }
    if (stamp->version > kPackStateVersion) {
        refuse(why, "state.json: written by a newer RetroPlug (version " + std::to_string(stamp->version) +
                        ", this one reads " + std::to_string(kPackStateVersion) + "); left as it is");
        return std::nullopt;
    }
    auto raw = rfl::json::read<RawState>(text);
    if (!raw) {
        refuse(why, std::string("state.json: ") + raw.error().what());
        return std::nullopt;
    }
    PackState state;
    for (const auto& e : raw->packs) {
        if (!isPackId(e.id)) {
            refuse(why, "state.json: \"" + e.id + "\" is not a pack id");
            return std::nullopt;
        }
        if (!isPackVersion(e.active) || (e.pending && !isPackVersion(*e.pending))) {
            refuse(why, "state.json: pack " + e.id + " names a version that is not MAJOR.MINOR.PATCH");
            return std::nullopt;
        }
        if (state.find(e.id)) {
            refuse(why, "state.json: pack " + e.id + " is listed twice");
            return std::nullopt;
        }
        state.packs.push_back({e.id, e.active, e.pending.value_or(""), e.remove.value_or(false)});
    }
    return state;
}

// `from` renamed over `to`. Windows refuses while another process has `to` open for an instant (a reader's antivirus
// scan, an indexer), so it retries briefly, and writes through so the rename is on the device when it returns.
bool replaceFile(const fs::path& from, const fs::path& to, std::error_code* ec) {
#if defined(_WIN32)
    for (int attempt = 0;; ++attempt) {
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        const DWORD error = GetLastError();
        if (attempt >= 9 || (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION)) {
            *ec = std::error_code(static_cast<int>(error), std::system_category());
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
#else
    fs::rename(from, to, *ec);
    return !*ec;
#endif
}

// The file replaced; the caller holds state.lock exclusively.
bool writeHeld(const fs::path& dataRoot, const PackState& state, std::string* why) {
    RawState raw;
    for (const auto& p : state.packs) {
        RawEntry e{p.id, p.active, std::nullopt, std::nullopt};
        if (!p.pending.empty()) e.pending = p.pending;
        if (p.remove) e.remove = true;
        raw.packs.push_back(std::move(e));
    }
    // A name of this process's and this call's own, so two writers (two processes, or two threads of one) never share
    // a temporary file.
    static std::atomic<std::uint32_t> serial{0};
    const fs::path temp = dataRoot / ("state.json.tmp-" + std::to_string(RP_GETPID()) + "-" +
                                      std::to_string(serial.fetch_add(1)));
    std::error_code ec;
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return refuse(why, "state.json: cannot write " + displayPath(temp));
        out << rfl::json::write(raw, rfl::json::pretty) << "\n";
        if (!out.flush()) return refuse(why, "state.json: cannot write " + displayPath(temp));
    }
    if (!rp::fsyncPath(temp)) {
        fs::remove(temp, ec);
        return refuse(why, "state.json: cannot flush " + displayPath(temp));
    }
    if (!replaceFile(temp, dataRoot / "state.json", &ec)) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        return refuse(why, "state.json: cannot replace it: " + ec.message());
    }
    // The rename is only durable once its directory is flushed. Best effort: a file system that cannot sync a directory
    // still has the new file, and the contract allows a crash to keep the old one.
    rp::fsyncDirectory(dataRoot);
    return true;
}

// `dir`, a version folder no entry names any more, deleted unless a process holds it. The exclusive pack.lock is taken
// at once or not at all, and every entry but pack.lock is removed while it is held (the same on every platform, and
// independent of how the OS treats a delete of an open file): a process that tries to hold the folder meanwhile fails
// to lock, and one that holds it afterwards holds an empty folder whose manifest read refuses it. Then the lock file
// and the folder go. On POSIX the folder is first renamed to a trash name beside it while the lock is held, so a
// process that opened the old lock file finds the path gone (FileLock refuses it); if that rename fails the empty
// folder is left for a later settle. Windows cannot rename a folder with a file open in it, so there pack.lock and the
// folder are removed once the lock is let go, and a failure (a racer holding it) leaves them for a later settle.
void deleteUnheld(const fs::path& dir) {
    auto lock = FileLock::acquire(dir / kVersionLockFile, FileLock::Mode::Exclusive, false, nullptr);
    if (!lock) return;  // held: left for a later launch
    std::error_code ec;
    // The manifest first, so one is present only in a complete folder and a walk cut short leaves one the manifest
    // read refuses.
    fs::remove(dir / "pack.json", ec);
    for (fs::directory_iterator it(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (displayPath(it->path().filename()) != kVersionLockFile) {
            std::error_code removeEc;
            fs::remove_all(it->path(), removeEc);
        }
#if defined(_WIN32)
    lock.reset();
    fs::remove(dir / kVersionLockFile, ec);
    fs::remove(dir, ec);  // not remove_all: a folder something was put back in is left alone
#else
    static std::atomic<std::uint32_t> serial{0};
    const fs::path trash =
        dir.parent_path() / ("." + displayPath(dir.filename()) + kTrashMarker + std::to_string(RP_GETPID()) + "-" +
                             std::to_string(serial.fetch_add(1)));
    fs::rename(dir, trash, ec);
    const bool renamed = !ec;
    lock.reset();
    if (renamed) fs::remove_all(trash, ec);
#endif
}

bool startsWith(const std::string& text, std::string_view prefix) { return text.rfind(prefix, 0) == 0; }

bool sameState(const std::vector<InstalledPack>& a, const std::vector<InstalledPack>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].id != b[i].id || a[i].active != b[i].active || a[i].pending != b[i].pending ||
            a[i].remove != b[i].remove)
            return false;
    return true;
}

}  // namespace

InstalledPack* PackState::find(const std::string& id) {
    for (auto& p : packs)
        if (p.id == id) return &p;
    return nullptr;
}

const InstalledPack* PackState::find(const std::string& id) const {
    for (const auto& p : packs)
        if (p.id == id) return &p;
    return nullptr;
}

std::optional<FileLock> lockPackState(const fs::path& dataRoot, FileLock::Mode mode, std::string* why) {
    std::error_code ec;
    fs::create_directories(dataRoot, ec);
    if (ec) {
        refuse(why, "state.json: cannot create " + displayPath(dataRoot) + ": " + ec.message());
        return std::nullopt;
    }
    return FileLock::acquire(dataRoot / kStateLockFile, mode, true, why);
}

std::optional<PackState> readPackState(const fs::path& dataRoot, std::string* why) {
    std::error_code ec;
    if (!fs::exists(dataRoot, ec)) return PackState{};
    // A root this process cannot write (a read-only profile, a shared install) has no state.lock to take, and nothing
    // can write state.json there either, so the read goes ahead without one.
    FileLock::Failure failure = FileLock::Failure::None;
    std::string lockWhy;
    auto hold = FileLock::acquire(dataRoot / kStateLockFile, FileLock::Mode::Shared, true, &lockWhy, &failure);
    if (!hold && failure != FileLock::Failure::Denied) {
        refuse(why, "state.json: " + lockWhy);
        return std::nullopt;
    }
    return readHeld(dataRoot, why);
}

std::optional<PackState> readPackState(const fs::path& dataRoot, const FileLock&, std::string* why) {
    return readHeld(dataRoot, why);
}

bool writePackState(const fs::path& dataRoot, const PackState& state, std::string* why) {
    auto hold = lockPackState(dataRoot, FileLock::Mode::Exclusive, why);
    if (!hold) return false;
    return writeHeld(dataRoot, state, why);
}

bool updatePackState(const fs::path& dataRoot, const std::function<bool(PackState&)>& change, std::string* why) {
    auto hold = lockPackState(dataRoot, FileLock::Mode::Exclusive, why);
    if (!hold) return false;
    return updatePackState(dataRoot, *hold, change, why);
}

bool updatePackState(const fs::path& dataRoot, const FileLock& held, const std::function<bool(PackState&)>& change,
                     std::string* why) {
    if (held.mode() != FileLock::Mode::Exclusive) return refuse(why, "state.json: state.lock is not held exclusively");
    auto state = readHeld(dataRoot, why);
    if (!state) return false;
    if (!change(*state)) return true;
    return writeHeld(dataRoot, *state, why);
}

bool settlePackState(const fs::path& dataRoot, std::string* why) {
    auto hold = lockPackState(dataRoot, FileLock::Mode::Exclusive, why);
    if (!hold) return false;
    return settlePackState(dataRoot, *hold, why);
}

bool settlePackState(const fs::path& dataRoot, const FileLock& held, std::string* why) {
    if (held.mode() != FileLock::Mode::Exclusive) return refuse(why, "state.json: state.lock is not held exclusively");
    std::error_code ec;
    // An absent state.json reads as an empty state, which would make every installed folder "unnamed" and delete it:
    // with nothing recorded there is nothing to settle, and nothing is swept.
    if (!fs::exists(dataRoot / "state.json", ec)) return true;
    auto state = readHeld(dataRoot, why);
    if (!state) return false;
    // This call holds the exclusive lock every writer takes, so a temporary file here is a crashed writer's.
    std::string listing;
    fs::directory_iterator root(dataRoot, ec);
    for (; !ec && root != fs::directory_iterator(); root.increment(ec)) {
        std::error_code entryEc;
        if (startsWith(displayPath(root->path().filename()), "state.json.tmp-") && root->is_regular_file(entryEc))
            fs::remove(root->path(), entryEc);
    }
    if (ec) listing = "state.json: cannot list " + displayPath(dataRoot) + ": " + ec.message();

    const std::vector<InstalledPack> before = state->packs;  // the promotion below edits state->packs in place
    std::vector<InstalledPack> kept;
    for (auto& p : state->packs) {
        if (p.remove) continue;
        if (!p.pending.empty()) {
            // A pending version whose folder is gone (a half-finished install cleaned away) is forgotten, and the
            // active one keeps loading.
            // A manifest that cannot be looked for (an unreadable folder) is not a gone one: the pending version stays
            // for a launch that can read it.
            std::error_code lookEc;
            const bool present = fs::exists(dataRoot / p.id / p.pending / "pack.json", lookEc);
            if (present) p.active = p.pending;
            if (present || !lookEc) p.pending.clear();
        }
        kept.push_back(p);
    }
    const bool changed = !sameState(before, kept);
    state->packs = std::move(kept);
    if (changed && !writeHeld(dataRoot, *state, why)) return false;
    // Every version folder no entry names any more goes, unless a process holds it. A name that is no pack id or no
    // version (an install in progress starts with '.') is left alone, except the trash a deletion left behind.
    ec.clear();
    fs::directory_iterator ids(dataRoot, ec);
    for (; !ec && ids != fs::directory_iterator(); ids.increment(ec)) {
        std::error_code entryEc;
        const std::string id = displayPath(ids->path().filename());
        if (!ids->is_directory(entryEc) || !isPackId(id)) continue;
        const InstalledPack* entry = state->find(id);
        std::error_code inner;
        fs::directory_iterator versions(ids->path(), inner);
        for (; !inner && versions != fs::directory_iterator(); versions.increment(inner)) {
            const std::string version = displayPath(versions->path().filename());
            if (!versions->is_directory(entryEc)) continue;
            if (startsWith(version, ".") && version.find(kTrashMarker) != std::string::npos) {
                fs::remove_all(versions->path(), entryEc);
                continue;
            }
            if (!isPackVersion(version)) continue;
            if (entry && (entry->active == version || entry->pending == version)) continue;
            deleteUnheld(versions->path());
        }
        if (inner && listing.empty())
            listing = "state.json: cannot list " + displayPath(ids->path()) + ": " + inner.message();
        std::error_code empty;
        if (fs::is_empty(ids->path(), empty) && !empty) fs::remove(ids->path(), empty);
    }
    if (ec && listing.empty()) listing = "state.json: cannot list " + displayPath(dataRoot) + ": " + ec.message();
    if (!listing.empty()) return refuse(why, listing);
    return true;
}

}  // namespace rp::host::packs
