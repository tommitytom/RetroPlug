#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <efsw/efsw.hpp>

#include "UserConfigSerialization.hpp"

// Per-user, machine-global preferences. Currently:
//   - config.json holds a single field: which bindings profile is active.
//   - bindings/<name>.json holds a keyboard + gamepad map.
//
// Lives on the UI thread. efsw fires `handleFileAction` on a background
// thread, which only flips an atomic dirty flag; the actual file reads
// happen inside pumpReloadsOnUiThread() called from PluginUI::uiIdle.
//
// First-run behaviour: if config.json or bindings/default.json are
// missing, they are atomically written with the defaults baked into
// UserConfigSerialization::defaultBindingMap(). Existing-but-malformed
// files are NOT overwritten — parse errors are logged, the previous
// in-memory snapshot is retained.
class UserConfig final : public efsw::FileWatchListener {
public:
    // rootOverride: explicit directory (used by tests). When empty, the
    // platform resolver in UserConfigPaths.cpp picks the OS-native path
    // (or RETROPLUG_USER_CONFIG_DIR if set).
    explicit UserConfig(std::filesystem::path rootOverride = {});
    ~UserConfig() override;

    UserConfig(const UserConfig&)            = delete;
    UserConfig& operator=(const UserConfig&) = delete;

    // Resolve root dir, create the tree, write defaults if missing, read
    // initial state, start the watcher. Safe to call once.
    void start();

    // Stop the watcher. Safe to call from the destructor.
    void stop();

    // Drain the dirty flag set by efsw. Re-parses files, swaps the
    // snapshot, invokes the onReload callback. Call from the UI thread
    // (typically inside uiIdle).
    void pumpReloadsOnUiThread();

    using ReloadFn = std::function<void()>;
    void setOnReload(ReloadFn fn) { onReload_ = std::move(fn); }

    // Cheap mutex-guarded copy. Safe to call from any thread; in
    // practice the only caller is the RPC method on the UI thread.
    UserConfigDto snapshot() const;

    // Resolved config directory. Available after start() — empty before.
    const std::filesystem::path& rootDir() const { return rootDir_; }

    // Switch the active keyboard / gamepad profile. Writes config.json and
    // immediately updates the in-memory snapshot (without waiting for efsw
    // to round-trip the change). Returns false on write failure.
    bool setActiveKeyboardBindings(std::string name);
    bool setActiveGamepadBindings(std::string name);

    // Read a single profile file from disk. Returns nullopt if the file
    // doesn't exist or fails to parse. Used by the in-app bindings editor
    // so it can edit one channel (keyboard or gamepad) of a profile
    // without clobbering the other when saving.
    std::optional<BindingMapJson> loadProfile(std::string_view name) const;

    // Profile management used by the in-app bindings editor. All three
    // validate `name` (and `newName`) as `[A-Za-z0-9_-]+`, refuse reserved
    // stems ("config"), and refresh the in-memory snapshot synchronously
    // before returning.
    //
    // saveProfile   — overwrites bindings/<name>.json (creates if missing).
    // renameProfile — fs::rename; if `oldName` is the active keyboard or
    //                 gamepad profile, config.json is rewritten to point
    //                 at `newName` so the active reference stays valid.
    // deleteProfile — refuses if `name` matches either active profile
    //                 (caller must switch first); otherwise unlinks.
    bool saveProfile  (std::string name, BindingMapJson bindings);
    bool renameProfile(std::string oldName, std::string newName);
    bool deleteProfile(std::string name);

    // Validate a profile name: non-empty, contains only [A-Za-z0-9_-], and
    // not equal to a reserved stem. Exposed for the RPC layer so the UI can
    // surface the rejection without round-tripping a write.
    static bool isValidProfileName(std::string_view name);

    // efsw callback — runs on a background thread. Only flips dirty_.
    void handleFileAction(efsw::WatchID,
                          const std::string& dir,
                          const std::string& filename,
                          efsw::Action,
                          const std::string& oldFilename = "") override;

private:
    void reloadFromDisk();
    void writeDefaultsIfMissing();
    static bool atomicWrite(const std::filesystem::path& target,
                            const std::string& contents);

    std::filesystem::path rootDirOverride_;
    std::filesystem::path rootDir_;
    std::filesystem::path bindingsDir_;
    std::filesystem::path configFile_;

    mutable std::mutex mu_;
    UserConfigDto      current_;

    std::atomic<bool>  dirty_{false};
    std::atomic<bool>  started_{false};

    std::unique_ptr<efsw::FileWatcher> watcher_;
    efsw::WatchID                      rootWatchId_      = 0;
    efsw::WatchID                      bindingsWatchId_  = 0;

    ReloadFn onReload_;
};

// Implemented in UserConfigPaths.cpp. Returns the platform-native config
// directory (or the contents of RETROPLUG_USER_CONFIG_DIR if set).
std::filesystem::path resolveDefaultUserConfigDir();
