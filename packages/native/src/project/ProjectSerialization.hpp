#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "rfl/DefaultIfMissing.hpp"
#include "rfl/json/read.hpp"
#include "rfl/json/write.hpp"

#include "config/SchemaVersions.hpp"
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
    // DefaultIfMissing: a field absent from the JSON takes its struct default
    // rather than failing the whole parse. Lets a project saved before a field
    // existed (e.g. savSuffix / savPath) still load — the same forward-tolerance
    // the LSDj sav codec uses (lsdj/SavSerialization.hpp). Pre-release, this is
    // the migration story for additive schema changes; unknown fields are also
    // ignored, so a removed field in an old file is harmless too.
    auto result = rfl::json::read<ProjectConfig, rfl::DefaultIfMissing>(json);
    if (!result) return std::nullopt;
    return std::move(result.value());
}

// Path-only on-disk form: config + paths, no embedded binaries. The default
// disk save (PluginRpcService::saveProjectToPath). ROM is re-read from
// `romPath` on load (Project::addSystem), SRAM from the sibling `<rom>.sav`;
// savestate and LSDj kit bytes are dropped. Use projectConfigToZip / Export Zip
// for a self-contained bundle.
inline std::string projectConfigToJsonFile(const ProjectConfig& cfg) {
    ProjectConfig stripped = cfg;
    // Stamp the current schema version (not whatever was loaded) so every save
    // records the format the running build actually writes. See SchemaVersions.hpp.
    stripped.schemaVersion = std::to_string(rp::schema::kProject);
    project_binaries::clear(stripped);
    return projectConfigToJson(stripped);
}

inline std::vector<std::uint8_t> projectConfigToZip(const ProjectConfig& cfg) {
    MinizWriter zip;
    if (!zip.valid()) return {};

    // Walk a mutable copy so we can null out blobs as they're written to zip.
    ProjectConfig stripped = cfg;
    stripped.schemaVersion = std::to_string(rp::schema::kProject); // stamp current
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

// Autodetecting loader for a project file: a PKZIP blob (Export Zip / DAW state /
// legacy .rplg) is recognised by its `PK` magic and routed through the zip path;
// anything else is treated as path-only JSON. Lets a single load path accept both
// the JSON `.rplg` disk save and the `.zip` export.
inline std::optional<ProjectConfig> projectConfigFromBytes(std::span<const std::uint8_t> blob) {
    if (blob.empty()) return std::nullopt;
    if (blob.size() >= 2 && blob[0] == 'P' && blob[1] == 'K')
        return projectConfigFromZip(blob);
    return projectConfigFromJson(
        std::string_view(reinterpret_cast<const char*>(blob.data()), blob.size()));
}
