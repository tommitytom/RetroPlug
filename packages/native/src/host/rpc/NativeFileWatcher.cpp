#include "host/rpc/NativeFileWatcher.hpp"

#include <filesystem>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace {

// weakly_canonical resolves symlinks/.. without requiring the target to exist (a Delete event names a
// path that's already gone), so watcher events and setWatchedRoms register the same string TS will.
std::string canon(const fs::path& p) {
    std::error_code ec;
    const fs::path c = fs::weakly_canonical(p, ec);
    return (ec ? p : c).string();
}

} // namespace

NativeFileWatcher::NativeFileWatcher(std::string configDir) {
    if (!configDir.empty()) {
        configDir_   = canon(configDir);
        bindingsDir_ = canon(fs::path(configDir_) / "bindings");
        // Recursive so config.json and every bindings/<profile>.json land in one watch. A bad path
        // returns an error WatchID (< 0) — harmless, ROM watches still function.
        watcher_.addWatch(configDir_, this, /*recursive*/ true);
    }
    watcher_.watch();  // starts the background thread; add/removeWatch are safe afterwards
}

void NativeFileWatcher::setWatchedRoms(const std::vector<std::string>& paths) {
    std::unordered_set<std::string>          roms;   // canonical ROM paths
    std::unordered_map<std::string, int>     wanted; // canonical parent dir -> refcount (value unused)
    roms.reserve(paths.size());
    for (const auto& p : paths) {
        if (p.empty()) continue;
        const std::string c = canon(p);
        roms.insert(c);
        wanted.emplace(fs::path(c).parent_path().string(), 0);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        watchedRoms_ = std::move(roms);
    }

    // Reconcile the per-parent-dir watches (UI-thread-only bookkeeping). Drop dirs no longer referenced,
    // add newly-needed ones. Dirs inside the recursively-watched config tree double-fire harmlessly (TS
    // dedups), so we don't special-case them.
    for (auto it = romDirs_.begin(); it != romDirs_.end();) {
        if (!wanted.count(it->first)) {
            watcher_.removeWatch(it->second);
            it = romDirs_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& [dir, _] : wanted) {
        if (dir.empty() || romDirs_.count(dir)) continue;
        const efsw::WatchID id = watcher_.addWatch(dir, this, /*recursive*/ false);
        if (id >= 0) romDirs_.emplace(dir, id);  // a non-existent dir returns an error id — skip it
    }
}

std::vector<std::string> NativeFileWatcher::drainChangedPaths() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    out.swap(changed_);
    return out;
}

void NativeFileWatcher::handleFileAction(efsw::WatchID /*watchid*/, const std::string& dir,
                                         const std::string& filename, efsw::Action /*action*/,
                                         const std::string& /*oldFilename*/) {
    const std::string full = canon(fs::path(dir) / filename);
    const fs::path    fp(full);
    const std::string parent = fp.parent_path().string();

    bool relevant = false;
    if (!configDir_.empty() && parent == configDir_ && filename == "config.json") {
        relevant = true;  // the user config
    } else if (!bindingsDir_.empty() && parent == bindingsDir_ && fp.extension() == ".json") {
        relevant = true;  // a bindings profile
    } else {
        std::lock_guard<std::mutex> lock(mutex_);
        if (watchedRoms_.count(full)) relevant = true;  // a watched ROM
    }
    if (!relevant) return;

    std::lock_guard<std::mutex> lock(mutex_);
    changed_.push_back(full);
}
