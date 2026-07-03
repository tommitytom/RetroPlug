#pragma once

#include <filesystem>
#include <string>
#include <utility>

#include "rfl/Variant.hpp"

#include "project/ProjectConfig.hpp"
#include "system/SystemConfig.hpp"
#include "system/mesen/MesenGbaConfig.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/roles/LsdjKitPatchRole.hpp"

// Rebase a ProjectConfig's asset paths between absolute and project-relative form
// for the thin-JSON `.rplg` save/load. Absolute in memory; stored relative to the
// `.rplg`'s directory when the asset lives at/under it, so a project folder can be
// moved or shared without every path breaking. Applied only at the file-based
// save/load seam (PluginRpcService); the zip / DAW-state forms embed their bytes
// and carry no file location, so they stay absolute.
//
// Traverses the same fields as ProjectMissingFiles: each system's romPath +
// savPath, plus LSDj kit sample WAV paths. `biosPath` is deliberately excluded —
// it's a fixed firmware path the runtime resets, not a portable per-project asset.

namespace rp::project_paths {

// Apply `fn(std::string&)` to every rebaseable path field, in place.
template <class Fn>
inline void visitPaths(ProjectConfig& cfg, Fn&& fn) {
    for (auto& sysCfg : cfg.systems) {
        auto& variant = sysCfg.variant();
        if (auto* sb = rfl::get_if<SameBoyConfig>(&variant)) {
            fn(sb->romPath);
            fn(sb->savPath);
            for (auto& rc : sb->roles) {
                auto* kitCfg = rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&rc.variant());
                if (!kitCfg) continue;
                for (auto& kit : kitCfg->kits)
                    for (auto& sample : kit.samples)
                        fn(sample.path);
            }
        } else if (auto* mb = rfl::get_if<MesenNesConfig>(&variant)) {
            fn(mb->romPath);
            fn(mb->savPath);
        } else if (auto* gb = rfl::get_if<MesenGbaConfig>(&variant)) {
            fn(gb->romPath);
            fn(gb->savPath);   // biosPath intentionally left absolute
        }
    }
}

namespace detail {
inline std::filesystem::path canonicalBase(const std::string& baseDir) {
    std::error_code ec;
    const std::filesystem::path in = baseDir.empty() ? std::filesystem::path(".")
                                                     : std::filesystem::path(baseDir);
    std::filesystem::path out = std::filesystem::weakly_canonical(in, ec);
    if (out.empty()) out = std::filesystem::absolute(in, ec);
    return out;
}
} // namespace detail

// Absolute -> relative when the asset is at/under `baseDir`; otherwise left
// absolute. Relatives are stored with forward slashes (generic_string) so a
// project authored on one OS resolves on another. Only touches absolute fields.
inline void toRelative(ProjectConfig& cfg, const std::string& baseDir) {
    const std::filesystem::path bb = detail::canonicalBase(baseDir);
    if (bb.empty()) return;
    visitPaths(cfg, [&](std::string& field) {
        if (field.empty()) return;
        std::filesystem::path p(field);
        if (!p.is_absolute()) return;   // already relative — leave as-is
        std::error_code ec;
        const std::filesystem::path pb = std::filesystem::weakly_canonical(p, ec);
        if (pb.empty()) return;
        const std::filesystem::path rel = pb.lexically_relative(bb);
        if (rel.empty()) return;
        // A leading ".." means the asset is outside baseDir — keep it absolute
        // rather than emitting fragile ../ chains.
        if (rel.begin() != rel.end() && rel.begin()->string() == "..") return;
        field = rel.generic_string();
    });
}

// Relative (non-empty) -> absolute against `baseDir`. Absolute fields (including
// old absolute-path projects) and empty fields are left untouched, so this is
// backward-compatible with no migration.
inline void toAbsolute(ProjectConfig& cfg, const std::string& baseDir) {
    const std::filesystem::path bb = detail::canonicalBase(baseDir);
    if (bb.empty()) return;
    visitPaths(cfg, [&](std::string& field) {
        if (field.empty()) return;
        std::filesystem::path p(field);
        if (p.is_absolute()) return;    // already absolute — leave as-is
        std::error_code ec;
        std::filesystem::path abs = std::filesystem::weakly_canonical(bb / p, ec);
        if (abs.empty()) abs = (bb / p).lexically_normal();
        field = abs.string();
    });
}

} // namespace rp::project_paths
