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
// and never overwritten (AGENTS.md, Config migrations), and it keeps only its latest schema. Members this version does
// not know are not kept when it rewrites the file, so ANY change to the file's shape bumps kPackStateVersion (and adds
// a migration), an additive one included.
//
// The install contract (M4's installer): a version folder is moved into place under packs/<id>/<version>/ and recorded
// (the updatePackState overload that takes the hold) within ONE exclusive hold of state.lock (lockPackState). A
// settle deletes every folder no entry names, so a folder moved in outside that hold, or recorded after it, can be
// deleted before it is recorded. An install in progress is unpacked into a '.'-prefixed folder, which a settle leaves
// alone. A settle that could not finish deleting a version (a racing hold) can leave an emptied packs/<id>/<version>/
// holding only pack.lock: it blocks a reinstall of that version until a settle sweeps it, so the installer settles (or
// clears it) before it moves a folder into place.
//
// The functions that lock state.lock themselves (every overload without a `held` parameter) must not be called while
// this process already holds it, and not from inside a `change` callback: flock locks per open file description, so a
// second open in the same process conflicts with the first and the call waits for itself forever. A caller that holds
// the lock passes it to the `held` overloads, which check it is exclusive (readPackState accepts either mode).
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
// reason in *why, for a file whose version is newer than kPackStateVersion (read before the rest, since a newer
// schema need not parse as this one), that does not parse, or that holds an entry that is not well formed: such a file
// is left as it is. The unlocked read happens only when the lock file cannot be made for want of permission; any other
// failure to lock is a refusal.
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
// A folder is deleted while this call holds its pack.lock exclusively: it is renamed to a '.'-prefixed trash name in
// the same pack folder (so a process that opened the old lock file finds the path gone and is refused), then removed;
// trash a crash left behind, and state.json temporaries, are swept by the next settle. A state.json that does not exist
// is settled to nothing and sweeps nothing: an absent file must not read as "no pack is installed" and delete them
// all. state.json is rewritten only when the settle changed it. A directory listing that fails is reported through
// *why after the rest has been done (the state is already settled).
bool settlePackState(const std::filesystem::path& dataRoot, std::string* why);

// updatePackState for a caller that already holds state.lock exclusively (`held`): the folder move and the record in
// one hold, which the plain overload, taking its own lock, cannot do (two locks on one file conflict, in one process
// too).
bool updatePackState(const std::filesystem::path& dataRoot, const FileLock& held,
                     const std::function<bool(PackState&)>& change, std::string* why);

// The read and the settle for a caller that already holds state.lock exclusively (`held`, from lockPackState): a
// launch, which settles, reads and takes its version locks in that one hold.
std::optional<PackState> readPackState(const std::filesystem::path& dataRoot, const FileLock& held, std::string* why);
bool settlePackState(const std::filesystem::path& dataRoot, const FileLock& held, std::string* why);

}  // namespace rp::host::packs
