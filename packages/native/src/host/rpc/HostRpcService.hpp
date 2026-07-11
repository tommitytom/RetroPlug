#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "host/rpc/BackendTypes.hpp"

// The fs / config / codec half of the greenfield Backend: std::filesystem + miniz + config-dir
// resolution + LSDJ sav authoring. Pure and stateless — no Engine, no threads — so it's reusable
// verbatim across hosts.
class HostRpcService {
public:
    // --- filesystem ---
    std::optional<rfl::Bytestring> readFile(std::string path);
    bool writeFile(std::string path, rfl::Bytestring bytes);
    bool writeFileAtomic(std::string path, rfl::Bytestring bytes);
    bool fileExists(std::string path);
    bool rename(std::string from, std::string to);
    std::vector<std::string> listDir(std::string dir);
    bool deleteFile(std::string path);
    std::vector<std::string> drainChangedPaths();  // no watcher yet — always empty

    // --- paths / config ---
    std::string canonicalize(std::string path);
    std::optional<rfl::Bytestring> readFilePrefix(std::string path, std::uint32_t length);
    std::string configDir();
    std::string version();  // the app version string (Version.hpp) — surfaced to the UI menu title

    // --- codec (miniz) ---
    rfl::Bytestring zip(std::vector<BackendZipInput> entries);
    std::vector<BackendZipEntry> unzip(rfl::Bytestring bytes);
};
