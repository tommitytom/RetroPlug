#include "host/packs/PackState.hpp"

#include <atomic>
#include <fstream>
#include <iterator>
#include <system_error>

#include <rfl.hpp>
#include <rfl/json.hpp>

#include "host/packs/PackDirs.hpp"
#include "host/packs/PackManifest.hpp"
#include "util/FileSync.hpp"

#if defined(_WIN32)
#include <process.h>
#define RP_GETPID _getpid
#else
#include <unistd.h>
#define RP_GETPID getpid
#endif

namespace fs = std::filesystem;

namespace rp::host::packs {

namespace {

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
    fs::rename(temp, dataRoot / "state.json", ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        return refuse(why, "state.json: cannot replace it: " + ec.message());
    }
    return true;
}

// `dir`, a version folder no entry names any more, deleted unless a process holds it: the exclusive pack.lock is taken
// at once or not at all, and kept while the folder goes, so no process can take the folder in between. Windows cannot
// remove the lock file while this call has it open, so the folder is removed again once the lock is let go.
void deleteUnheld(const fs::path& dir) {
    auto lock = FileLock::acquire(dir / kVersionLockFile, FileLock::Mode::Exclusive, false, nullptr);
    if (!lock) return;  // held: left for a later launch
    std::error_code ec;
    fs::remove_all(dir, ec);
    lock.reset();
    fs::remove_all(dir, ec);
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
    auto hold = FileLock::acquire(dataRoot / kStateLockFile, FileLock::Mode::Shared, true, nullptr);
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

bool settlePackState(const fs::path& dataRoot, const FileLock&, std::string* why) {
    auto state = readHeld(dataRoot, why);
    if (!state) return false;
    std::vector<InstalledPack> kept;
    for (auto& p : state->packs) {
        if (p.remove) continue;
        if (!p.pending.empty()) {
            std::error_code ec;
            // A pending version whose folder is gone (a half-finished install cleaned away) is forgotten, and the
            // active one keeps loading.
            if (fs::exists(dataRoot / p.id / p.pending / "pack.json", ec)) p.active = p.pending;
            p.pending.clear();
        }
        kept.push_back(p);
    }
    state->packs = std::move(kept);
    if (!writeHeld(dataRoot, *state, why)) return false;
    // Every version folder no entry names any more goes, unless a process holds it. A name that is no pack id or no
    // version (an install in progress starts with '.') is left alone.
    std::error_code ec;
    for (const auto& idDir : fs::directory_iterator(dataRoot, ec)) {
        const std::string id = displayPath(idDir.path().filename());
        if (!idDir.is_directory() || !isPackId(id)) continue;
        const InstalledPack* entry = state->find(id);
        std::error_code inner;
        for (const auto& versionDir : fs::directory_iterator(idDir.path(), inner)) {
            const std::string version = displayPath(versionDir.path().filename());
            if (!versionDir.is_directory() || !isPackVersion(version)) continue;
            if (entry && (entry->active == version || entry->pending == version)) continue;
            deleteUnheld(versionDir.path());
        }
        std::error_code empty;
        if (fs::is_empty(idDir.path(), empty)) fs::remove(idDir.path(), empty);
    }
    return true;
}

}  // namespace rp::host::packs
