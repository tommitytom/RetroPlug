#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rfl/json/read.hpp"
#include "rfl/json/write.hpp"

// JSON shape for the recent-files list. One file:
//   <config dir>/recent.json
// Mirrors the pattern in UserConfigSerialization.hpp.

struct RecentFileJson {
    std::string path;
    std::string kind;   // "rom" | "project"
};

struct RecentFilesJson {
    int schemaVersion = 1;
    // Most-recent first. Capped at RecentFiles::kMaxEntries on every write.
    std::vector<RecentFileJson> entries;
};

inline std::string recentFilesToJson(const RecentFilesJson& r) {
    return rfl::json::write(r);
}

inline std::optional<RecentFilesJson> recentFilesFromJson(std::string_view json) {
    auto r = rfl::json::read<RecentFilesJson>(json);
    if (!r) return std::nullopt;
    return std::move(r.value());
}
