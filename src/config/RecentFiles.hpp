#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// Per-user list of recently-loaded ROMs and projects. Persisted as
//   <config dir>/recent.json
// alongside config.json. Lives on the UI thread; the UI is the only writer
// (no efsw watcher needed). Reads are mutex-guarded so the RPC snapshot
// call is safe from any thread.
//
// On disk we store canonicalized paths so the same file referenced via
// `./foo.gb` vs an absolute path dedupes. Capped at kMaxEntries — adding
// a duplicate moves it to the front; adding past the cap drops the oldest.

struct RecentFileEntry {
    std::string path;
    std::string kind;   // "rom" | "project"
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
    bool add(const std::string& path, const std::string& kind);

    using OnChangeFn = std::function<void()>;
    void setOnChange(OnChangeFn fn) { onChange_ = std::move(fn); }

    // Resolved file path. Available after start() — empty before.
    const std::filesystem::path& filePath() const { return recentFile_; }

private:
    bool writeAtomic(const std::string& contents) const;

    std::filesystem::path        rootOverride_;
    std::filesystem::path        recentFile_;

    mutable std::mutex           mu_;
    std::vector<RecentFileEntry> entries_;

    OnChangeFn                   onChange_;
};
