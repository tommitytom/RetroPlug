#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "BackendTypes.hpp"

// The fs / config / codec half of the greenfield Backend: std::filesystem + miniz + config-dir
// resolution + LSDJ sav authoring. Pure and stateless — no Engine, no threads — so it's reusable
// verbatim across hosts.
class HostRpcService {
public:
    // --- filesystem ---
    std::optional<rfl::Bytestring> readFile(std::string path);
    bool writeFile(std::string path, std::vector<std::uint8_t> bytes);
    bool writeFileAtomic(std::string path, std::vector<std::uint8_t> bytes);
    bool fileExists(std::string path);
    bool rename(std::string from, std::string to);
    std::vector<std::string> listDir(std::string dir);
    bool deleteFile(std::string path);
    std::vector<std::string> drainChangedPaths();  // no watcher yet — always empty

    // --- paths / config ---
    std::string canonicalize(std::string path);
    std::optional<rfl::Bytestring> readFilePrefix(std::string path, std::uint32_t length);
    std::string configDir();

    // --- codec (miniz) ---
    rfl::Bytestring zip(std::vector<BackendZipInput> entries);
    std::vector<BackendZipEntry> unzip(std::vector<std::uint8_t> bytes);

    // --- LSDJ sav authoring (test/tooling) ---
    // JSON (an rp::lsdj::model::Sav, lenient) -> encoded .sav bytes. Lets a test author song/sync
    // state directly and boot LSDJ into it.
    rfl::Bytestring savFromJson(std::string json);
};
