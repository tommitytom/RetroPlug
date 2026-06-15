#pragma once

#include <cstdio>
#include <vector>

#include "rfl/Variant.hpp"

#include "lsdj/KitCompiler.hpp"
#include "project/ProjectConfig.hpp"
#include "system/SystemConfig.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/roles/LsdjKitPatchRole.hpp"

// Recompile LSDj kits that carry source-sample metadata but no compiled bytes.
//
// The path-only JSON project save drops every kit's 16 KB `compiledBytes` (kits
// are derived artifacts — see project_binaries::clear). On load we rebuild them
// from each sample's source WAV + offset/length + effects, exactly as the kit
// editor did originally, so the project reloads its kits without bundling them.
// The zip export keeps `compiledBytes`, so those kits are left untouched here.
//
// Runs off the audio thread (UI thread for the plugin; the single harness
// thread in tests) — KitCompiler does file I/O + resampling.

namespace rp::lsdj {

// True if any kit in the project has samples but no compiled bytes — i.e. there
// is work for recompileMissingKits. Lets callers avoid spinning up a KitCompiler
// (and its thread pool) for projects that don't need one.
inline bool projectHasKitsNeedingRecompile(const ProjectConfig& cfg) {
    for (const auto& sys : cfg.systems) {
        const auto* sb = rfl::get_if<SameBoyConfig>(&sys.variant());
        if (!sb) continue;
        for (const auto& rc : sb->roles) {
            const auto* kitCfg = rfl::get_if<LsdjKitPatchConfig>(&rc.variant());
            if (!kitCfg) continue;
            for (const auto& kit : kitCfg->kits)
                if (kit.compiledBytes.empty() && !kit.samples.empty()) return true;
        }
    }
    return false;
}

// Fill in `compiledBytes` / `compiledHash` for every kit that has samples but no
// bytes. A kit whose recompile fails (missing/moved WAV, decode error) is left
// empty — it simply won't be applied to the ROM — with a stderr note.
inline void recompileMissingKits(ProjectConfig& cfg, KitCompiler& compiler) {
    for (auto& sys : cfg.systems) {
        auto* sb = rfl::get_if<SameBoyConfig>(&sys.variant());
        if (!sb) continue;
        for (auto& rc : sb->roles) {
            auto* kitCfg = rfl::get_if<LsdjKitPatchConfig>(&rc.variant());
            if (!kitCfg) continue;
            for (auto& kit : kitCfg->kits) {
                if (!kit.compiledBytes.empty() || kit.samples.empty()) continue;

                std::vector<CompileSampleSpec> specs;
                specs.reserve(kit.samples.size());
                for (const auto& s : kit.samples) {
                    CompileSampleSpec c;
                    c.path    = s.path;
                    c.name    = s.name;
                    c.offset  = s.offset;
                    c.length  = s.length;
                    c.effects = s.effects;
                    specs.push_back(std::move(c));
                }

                CompiledKit compiled = compiler.compileKit(kit.name, specs);
                if (!compiled.ok || compiled.bytes.size() != Kit::kSize) {
                    std::fprintf(stderr,
                        "[ProjectKitRecompile] slot %u (%s): recompile failed: %s\n",
                        kit.slot, kit.name.c_str(),
                        compiled.error.empty() ? "unknown error" : compiled.error.c_str());
                    continue;
                }
                kit.compiledBytes = std::move(compiled.bytes);
                kit.compiledHash  = compiled.hash;
            }
        }
    }
}

} // namespace rp::lsdj
