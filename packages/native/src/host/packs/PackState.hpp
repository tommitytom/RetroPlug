#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "host/packs/FileLock.hpp"

// packs/state.json (docs/downloadable-packs-loading-design.md section 1): the installed packs, the version of each
// that loads, and what waits for the next launch. Versioned raw JSON like every config root: a newer stamp is refused
// and never overwritten (AGENTS.md, Config migrations), and it keeps only its latest schema.
//
// Every access holds <dataRoot>/state.lock: a reader shared, a writer exclusive. So a writer's rename never meets a
// reader with the file open (which Windows refuses), two writers' read-modify-writes never interleave, and a launch
// that settles, reads and takes its version locks in one exclusive hold (loadLaunchPacks) can never meet another
// launch's settle deleting the version it is about to load. The waits are unbounded, which the holds' brevity makes
// safe (a read, a write, a settle): a process that ends, however it ends, drops its lock, but one stopped while it
// holds it (a debugger's breakpoint, SIGSTOP) blocks every launch's start until it runs again.
namespace rp::host::packs {

inline constexpr std::uint32_t kPackStateVersion = 1;
inline constexpr const char*   kStateLockFile = "state.lock";

struct InstalledPack {
    std::string id;
    std::string active;   // the version folder that loads: packs/<id>/<active>/
    std::string pending;  // a version unpacked beside it, switched to at the next launch; empty for none
    bool        remove = false;  // removed at the next launch
};

struct PackState {
    std::vector<InstalledPack> packs;

    InstalledPack*       find(const std::string& id);
    const InstalledPack* find(const std::string& id) const;
};

// A hold of <dataRoot>/state.lock in `mode`, waiting for it; creates <dataRoot>. Nothing, with the reason, when the
// folder or the lock file cannot be made.
std::optional<FileLock> lockPackState(const std::filesystem::path& dataRoot, FileLock::Mode mode, std::string* why);

// <dataRoot>/state.json under a shared hold, or an empty state when there is none; without a hold when state.lock
// cannot be made (a root this process cannot write, which nothing can write state.json in either). Nothing, with the
// reason in *why,
// for a file whose version is newer than kPackStateVersion (read before the rest, since a newer schema need not
// parse as this one), that does not parse, or that holds an entry that is not well formed: such a file is left as it
// is.
std::optional<PackState> readPackState(const std::filesystem::path& dataRoot, std::string* why);

// `state` written to <dataRoot>/state.json under an exclusive hold, atomically: to a temporary file beside it, flushed
// to the disk, then renamed over it, so a reader sees the old file or the new one and never a torn one.
bool writePackState(const std::filesystem::path& dataRoot, const PackState& state, std::string* why);

// A read-modify-write under one exclusive hold, so two processes installing at once each see the other's change rather
// than one overwriting it. `change` returns false to write nothing.
bool updatePackState(const std::filesystem::path& dataRoot, const std::function<bool(PackState&)>& change,
                     std::string* why);

// What a launch does before it loads, under one exclusive hold: each pending version becomes active, each pending
// removal drops its entry, and every version folder no entry names any more is deleted, unless a process holds it.
// A folder is deleted while this call holds its pack.lock exclusively, so no process can take it in between.
bool settlePackState(const std::filesystem::path& dataRoot, std::string* why);

// The read and the settle for a caller that already holds state.lock exclusively (`held`, from lockPackState): a
// launch, which settles, reads and takes its version locks in that one hold.
std::optional<PackState> readPackState(const std::filesystem::path& dataRoot, const FileLock& held, std::string* why);
bool settlePackState(const std::filesystem::path& dataRoot, const FileLock& held, std::string* why);

}  // namespace rp::host::packs
