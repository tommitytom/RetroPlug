#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "host/rpc/BackendTypes.hpp"

class NativeFileWatcher;

// The fs / config / codec half of the Backend: std::filesystem + miniz + config-dir
// resolution + LSDJ sav authoring. Stateless and thread-free by default — reusable verbatim across
// hosts — UNLESS a host calls enableWatching(), which spins up an efsw NativeFileWatcher so
// drainChangedPaths / setWatchedRoms become live (the plugin does this; the test host + CLI don't).
class HostRpcService {
public:
    HostRpcService();
    ~HostRpcService();

    // --- filesystem ---
    std::optional<rfl::Bytestring> readFile(std::string path);
    bool writeFile(std::string path, rfl::Bytestring bytes);
    bool writeFileAtomic(std::string path, rfl::Bytestring bytes);
    // Streaming write pair for the CLI WAV renderer (write bytes as they render, no whole-file buffer):
    // appendFile adds to the end (creating the file if absent); writeFileAt overwrites at a byte offset
    // (the file must already exist). Used to write a placeholder WAV header, append PCM, then patch the
    // header's two length fields at offset 0. u32 offset is enough — RIFF caps a WAV at ~4 GiB.
    bool appendFile(std::string path, rfl::Bytestring bytes);
    bool writeFileAt(std::string path, std::uint32_t offset, rfl::Bytestring bytes);
    bool fileExists(std::string path);
    bool rename(std::string from, std::string to);
    std::vector<std::string> listDir(std::string dir);
    bool deleteFile(std::string path);
    // The native side of the file watcher (spec/07). drainChangedPaths pulls the paths the efsw watcher
    // saw since the last call (empty when watching isn't enabled); setWatchedRoms tells it which ROM
    // files to watch (their parent dirs), recomputed by TS whenever the systems list changes.
    std::vector<std::string> drainChangedPaths();
    bool setWatchedRoms(std::vector<std::string> paths);

    // --- paths / config ---
    std::string canonicalize(std::string path);
    std::optional<rfl::Bytestring> readFilePrefix(std::string path, std::uint32_t length);
    std::string configDir();
    std::string version();  // the app version string (Version.hpp) — surfaced to the UI menu title

    // --- codec (miniz) ---
    rfl::Bytestring zip(std::vector<BackendZipInput> entries);
    std::vector<BackendZipEntry> unzip(rfl::Bytestring bytes);

    // Host opt-in: start the efsw watcher over `configDir` (config.json + bindings/). Idempotent —
    // a second call is ignored. Until called, drainChangedPaths returns {} and setWatchedRoms no-ops.
    void enableWatching(std::string configDir);

private:
    std::unique_ptr<NativeFileWatcher> watcher_;  // null unless enableWatching() ran
};
