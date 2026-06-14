#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "rfl/json/read.hpp"
#include "rfl/json/write.hpp"

#include "project/ProjectBinaries.hpp"
#include "project/ProjectConfig.hpp"
#include "util/MinizZip.hpp"

// Single source of truth for ProjectConfig <-> on-disk form. Used by
// LVGLPluginDSP::getState/setState (DPF preset state) and by
// PluginRpcService::saveProjectToPath/loadProjectFromPath (.rplg files).
//
// On-disk form is a PKZIP blob (deflate) with one `project.json` entry plus
// per-binary entries (ROM/SRAM/savestate/LSDj-kit bytes). See ProjectBinaries.hpp
// for the entry-key contract.

// JSON-only helpers — left in place for `project.json` itself and for the
// rare test that wants to inspect the metadata layer without zipping.
inline std::string projectConfigToJson(const ProjectConfig& cfg) {
    return rfl::json::write(cfg);
}

inline std::optional<ProjectConfig> projectConfigFromJson(std::string_view json) {
    auto result = rfl::json::read<ProjectConfig>(json);
    if (!result) return std::nullopt;
    return std::move(result.value());
}

inline std::vector<std::uint8_t> projectConfigToZip(const ProjectConfig& cfg) {
    MinizWriter zip;
    if (!zip.valid()) return {};

    // Walk a mutable copy so we can null out blobs as they're written to zip.
    ProjectConfig stripped = cfg;
    if (!project_binaries::strip(zip, stripped)) return {};

    const std::string json = projectConfigToJson(stripped);
    if (!zip.add("project.json", json)) return {};

    return zip.finish();
}

inline std::optional<ProjectConfig> projectConfigFromZip(std::span<const std::uint8_t> blob) {
    if (blob.empty()) return std::nullopt;
    MinizReader zip(blob);
    if (!zip.valid()) return std::nullopt;

    const std::string json = zip.readString("project.json");
    if (json.empty()) return std::nullopt;

    auto parsed = projectConfigFromJson(json);
    if (!parsed) return std::nullopt;

    if (!project_binaries::restore(zip, *parsed)) return std::nullopt;
    return parsed;
}
