#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "rfl/Variant.hpp"

#include "project/ProjectConfig.hpp"
#include "system/SystemConfig.hpp"
#include "system/mesen/MesenGbaConfig.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/roles/LsdjKitPatchRole.hpp"

// Detect and repair project files that reference source assets (a system's ROM,
// an LSDj kit's sample WAVs) which no longer exist on disk. The thin JSON save
// stores those by path, so a moved file makes the load incomplete. The UI uses
// this to show a "locate missing files" menu before applying the project.
//
// Items are addressed by config index (systemIndex / kitSlot / sampleIndex) —
// the systems don't exist yet at load time, so there are no SystemIds to use.

namespace rp {

// One missing asset. Also the RPC/event DTO (reflect-cpp serializable).
struct MissingFile {
    std::uint32_t systemIndex = 0;       // index into ProjectConfig.systems
    std::string   itemKind;              // "rom" | "sample"
    std::string   path;                  // the missing path (for display + matching)
    std::int32_t  kitSlot     = -1;      // sample only (kit slot 0..15; -1 for rom)
    std::int32_t  sampleIndex = -1;      // sample only (index into kit.samples)
};

struct MissingFilesResponse {
    std::vector<MissingFile> items;
};

namespace missing_files {

inline bool fileExists(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

// A ROM is "present" if its bytes are embedded (zip export) or its path exists.
template <class Cfg>
inline bool romPresent(const Cfg& c) {
    return !c.romBytes.empty() || fileExists(c.romPath);
}

// Append any missing kit-sample entries for a SameBoy system. Kits only need
// their source WAVs when they'll be recompiled (compiledBytes empty — a JSON
// load); a zip kit carries its bytes and is self-sufficient.
inline void scanKitSamples(const SameBoyConfig& sb,
                           std::uint32_t systemIndex,
                           std::vector<MissingFile>& out) {
    for (const auto& rc : sb.roles) {
        const auto* kitCfg = rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&rc.variant());
        if (!kitCfg) continue;
        for (const auto& kit : kitCfg->kits) {
            if (!kit.compiledBytes.empty()) continue; // bundled — no WAVs needed
            for (std::size_t s = 0; s < kit.samples.size(); ++s) {
                if (fileExists(kit.samples[s].path)) continue;
                out.push_back(MissingFile{
                    systemIndex, "sample", kit.samples[s].path,
                    static_cast<std::int32_t>(kit.slot),
                    static_cast<std::int32_t>(s)});
            }
        }
    }
}

} // namespace missing_files

// Every referenced-but-absent file in the project, in config order.
inline std::vector<MissingFile> scanMissingFiles(const ProjectConfig& cfg) {
    using namespace missing_files;
    std::vector<MissingFile> out;
    for (std::size_t i = 0; i < cfg.systems.size(); ++i) {
        const auto idx = static_cast<std::uint32_t>(i);
        const auto& sys = cfg.systems[i];
        if (const auto* sb = rfl::get_if<SameBoyConfig>(&sys.variant())) {
            if (!romPresent(*sb)) out.push_back(MissingFile{idx, "rom", sb->romPath});
            scanKitSamples(*sb, idx, out);
        } else if (const auto* mb = rfl::get_if<MesenNesConfig>(&sys.variant())) {
            if (!romPresent(*mb)) out.push_back(MissingFile{idx, "rom", mb->romPath});
        } else if (const auto* gb = rfl::get_if<MesenGbaConfig>(&sys.variant())) {
            if (!romPresent(*gb)) out.push_back(MissingFile{idx, "rom", gb->romPath});
        }
    }
    return out;
}

// Point a missing item at `newPath`. ROM: set romPath + clear romBytes so the
// load re-reads from disk. Sample: set the sample's path. Returns false if the
// indices don't resolve.
inline bool relinkInConfig(ProjectConfig& cfg, const MissingFile& item,
                           const std::string& newPath) {
    if (item.systemIndex >= cfg.systems.size()) return false;
    auto& sys = cfg.systems[item.systemIndex];

    if (item.itemKind == "rom") {
        if (auto* sb = rfl::get_if<SameBoyConfig>(&sys.variant())) {
            sb->romPath = newPath; sb->romBytes.clear(); return true;
        }
        if (auto* mb = rfl::get_if<MesenNesConfig>(&sys.variant())) {
            mb->romPath = newPath; mb->romBytes.clear(); return true;
        }
        if (auto* gb = rfl::get_if<MesenGbaConfig>(&sys.variant())) {
            gb->romPath = newPath; gb->romBytes.clear(); return true;
        }
        return false;
    }

    // sample
    auto* sb = rfl::get_if<SameBoyConfig>(&sys.variant());
    if (!sb) return false;
    for (auto& rc : sb->roles) {
        auto* kitCfg = rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&rc.variant());
        if (!kitCfg) continue;
        for (auto& kit : kitCfg->kits) {
            if (kit.slot != item.kitSlot) continue;
            if (item.sampleIndex < 0 ||
                static_cast<std::size_t>(item.sampleIndex) >= kit.samples.size())
                return false;
            kit.samples[item.sampleIndex].path = newPath;
            return true;
        }
    }
    return false;
}

// After locating one file, look in its folder for the other still-missing files
// by basename and relink any matches. Lets one pick fix a whole moved folder.
// Returns the number of additional items resolved.
inline int autoFindSiblings(ProjectConfig& cfg, const std::string& newDir) {
    int resolved = 0;
    for (const auto& item : scanMissingFiles(cfg)) {
        const std::filesystem::path candidate =
            std::filesystem::path(newDir) / std::filesystem::path(item.path).filename();
        if (!missing_files::fileExists(candidate.string())) continue;
        if (relinkInConfig(cfg, item, candidate.string())) ++resolved;
    }
    return resolved;
}

} // namespace rp
