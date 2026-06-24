#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// Per-user list of recently-opened projects. Persisted as
//   <config dir>/recent.json
// alongside config.json. Lives on the UI thread; the UI is the only writer
// (no efsw watcher needed). Reads are mutex-guarded so the RPC snapshot
// call is safe from any thread.
//
// On disk we store canonicalized paths so the same file referenced via
// `./foo.rplg` vs an absolute path dedupes. Capped at kMaxEntries — adding
// a duplicate moves it to the front; adding past the cap drops the oldest.
// `name` is an optional display alias (rename); empty means the UI derives a
// label from the path basename.

struct RecentFileEntry {
    std::string path;
    std::string name;   // display alias; empty => UI derives from basename(path)
};

class RecentFiles final {
public:
    static constexpr std::size_t kMaxEntries = 10;

    // rootOverride: explicit directory (used by tests). When empty, the
    // platform resolver in UserConfigPaths.cpp picks the OS-native path
    // (or RETROPLUG_USER_CONFIG_DIR if set) — same root as UserConfig.
    explicit RecentFiles(std::filesystem::path rootOverride = {});

    RecentFiles(const RecentFiles&)            = delete;
    RecentFiles& operator=(const RecentFiles&) = delete;

    // Resolve root, slurp recent.json if present. Safe to call once.
    void start();

    // Snapshot in display order (most recent first). Thread-safe.
    std::vector<RecentFileEntry> snapshot() const;

    // Prepend `path` (deduped by canonicalized form). Trims to kMaxEntries
    // and writes the file atomically. On success, fires onChange_.
    // Returns false on write failure (in-memory state is still updated).
    // Re-adding an existing path preserves its stored display name unless a
    // non-empty `name` is supplied (so a rename stays sticky across reloads).
    bool add(const std::string& path, const std::string& name = {});

    // Remove the entry whose canonicalized path matches `path`. No-op (returns
    // false, fires nothing) when absent. Writes + fires onChange on removal.
    bool remove(const std::string& path);

    // Replace the entry at `oldPath` with `newPath` in place, preserving its
    // position and display name. `newPath` is canonicalized; if it collides
    // with another entry, that other entry is dropped. Returns false when
    // `oldPath` is not present.
    bool relink(const std::string& oldPath, const std::string& newPath);

    // Set the display alias for the entry at `path`. Returns false when `path`
    // is not present. An empty `name` clears the alias.
    bool rename(const std::string& path, const std::string& name);

    using OnChangeFn = std::function<void()>;
    void setOnChange(OnChangeFn fn) { onChange_ = std::move(fn); }

    // Resolved file path. Available after start() — empty before.
    const std::filesystem::path& filePath() const { return recentFile_; }

private:
    bool writeAtomic(const std::string& contents) const;

    // Serialize entries_, write atomically, fire onChange_. Caller holds no
    // lock. Returns the write result (in-memory state already mutated).
    bool writeAndNotify();

    std::filesystem::path        rootOverride_;
    std::filesystem::path        recentFile_;

    mutable std::mutex           mu_;
    std::vector<RecentFileEntry> entries_;

    OnChangeFn                   onChange_;
};
