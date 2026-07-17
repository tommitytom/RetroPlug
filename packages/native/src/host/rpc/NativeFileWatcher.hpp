#pragma once

// NativeFileWatcher — the C++ half of "watcher = C++, policy = TS" (doc-03 / spec/07).
//
// It owns an efsw file watcher and reports changed paths that the TS FileWatcher.pump() drains via
// HostRpcService::drainChangedPaths(): the user config, bindings profiles, and any watched ROM. It
// makes NO policy decisions — TS decides what a change means (reload config, refresh bindings, cold-boot
// a system with reloadOnRomChange on).
//
// efsw watches *directories*, so a single recursive watch on the config dir covers config.json +
// bindings/*.json, and each ROM is covered by watching its parent directory and filtering events to the
// registered basename. Any efsw action (Add/Delete/Modified/Moved) on a watched target counts as a
// change — that also catches save-by-rename editors.
//
// Threading: efsw delivers handleFileAction on its own background thread; setWatchedRoms + drain run on
// the UI thread. A single mutex guards the two pieces of cross-thread state (the changed-paths buffer and
// the watched-ROM filter set). The dir→watchid bookkeeping is UI-thread-only.

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <efsw/efsw.hpp>

class NativeFileWatcher final : public efsw::FileWatchListener {
public:
    // Starts watching `configDir` (recursively — config.json + bindings/) immediately. A missing /
    // unresolvable dir is tolerated: ROM watches still work and config events simply never fire.
    explicit NativeFileWatcher(std::string configDir);
    ~NativeFileWatcher() override = default;

    // UI thread. Replace the set of ROM files to watch. Reconciles the per-parent-dir efsw watches
    // (adds newly-needed dirs, drops ones no longer referenced) and swaps the basename filter set.
    void setWatchedRoms(const std::vector<std::string>& paths);

    // UI thread. Pull + clear every changed path seen since the last drain (deduped downstream in TS).
    std::vector<std::string> drainChangedPaths();

    // efsw watcher thread. Never call directly.
    void handleFileAction(efsw::WatchID watchid, const std::string& dir, const std::string& filename,
                          efsw::Action action, const std::string& oldFilename) override;

private:
    std::string configDir_;    // canonical; "" if unresolved
    std::string bindingsDir_;  // canonical configDir/bindings; "" if configDir_ is ""

    std::mutex                        mutex_;         // guards changed_ + watchedRoms_
    std::vector<std::string>          changed_;       // pending changes for the next drain
    std::unordered_set<std::string>   watchedRoms_;   // canonical ROM paths the listener filters against

    // UI-thread-only: canonical parent dir → its efsw watch id, for reconcile in setWatchedRoms.
    std::unordered_map<std::string, efsw::WatchID> romDirs_;

    // MUST be the last member: its destructor joins the efsw watcher thread, which calls
    // handleFileAction (touching mutex_ + changed_ + watchedRoms_). Declared last → destroyed FIRST, so
    // the thread is gone before those members are torn down (else a late callback is a use-after-free).
    efsw::FileWatcher watcher_;
};
