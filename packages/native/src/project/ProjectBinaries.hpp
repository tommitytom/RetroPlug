#pragma once

#include <cstddef>
#include <string>
#include <utility>

#include "rfl/TaggedUnion.hpp"

#include "project/ProjectConfig.hpp"
#include "system/SystemConfig.hpp"
#include "system/RoleConfig.hpp"
#include "system/mesen/MesenGbaConfig.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/roles/LsdjKitPatchRole.hpp"
#include "util/MinizZip.hpp"

// Visit every binary blob inside a ProjectConfig and route its bytes
// through a zip writer / reader. Used by ProjectSerialization to keep the
// JSON entry small — the blobs (ROMs, SRAM, savestates, LSDj kits) live as
// separate zip entries at deterministic keys.
//
// Key shape:
//   systems/{i}/rom
//   systems/{i}/sram
//   systems/{i}/state
//   systems/{i}/roles/{r}/kits/{k}/compiled
//
// strip/restore are templated on the sink/source so the same walk drives a
// MinizWriter/MinizReader (the plugin's .rplg codec) or a plain entry
// collector/map (the CLI harness, which hands the entries to TS orchestration).
// A Sink needs `add(string_view, span<const uint8_t>) -> bool`; a Source needs
// `has(string_view) -> bool` and `read(string_view) -> vector<uint8_t>`.

namespace project_binaries {

namespace detail {

inline std::string key(const std::string& prefix, const char* suffix) {
    return prefix + suffix;
}

// Strip a single blob into the sink, then empty it on the source.
template <class Sink>
inline bool stripBlob(Sink& sink,
                      const std::string& name,
                      std::vector<std::uint8_t>& blob) {
    if (blob.empty()) return true;
    if (!sink.add(name, blob)) return false;
    blob.clear();
    blob.shrink_to_fit();
    return true;
}

template <class Source>
inline bool restoreBlob(const Source& src,
                        const std::string& name,
                        std::vector<std::uint8_t>& blob) {
    if (!src.has(name)) return true;
    blob = src.read(name);
    return true;
}

template <class Sink>
inline bool stripRoles(Sink& zip,
                       const std::string& prefix,
                       std::vector<RoleConfig>& roles) {
    for (std::size_t r = 0; r < roles.size(); ++r) {
        auto& rc = roles[r];
        if (auto* kitCfg = rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&rc.variant())) {
            const std::string rolePrefix =
                prefix + "roles/" + std::to_string(r) + "/";
            for (std::size_t k = 0; k < kitCfg->kits.size(); ++k) {
                const std::string kitKey =
                    rolePrefix + "kits/" + std::to_string(k) + "/compiled";
                if (!stripBlob(zip, kitKey, kitCfg->kits[k].compiledBytes))
                    return false;
            }
        }
    }
    return true;
}

template <class Source>
inline bool restoreRoles(const Source& zip,
                         const std::string& prefix,
                         std::vector<RoleConfig>& roles) {
    for (std::size_t r = 0; r < roles.size(); ++r) {
        auto& rc = roles[r];
        if (auto* kitCfg = rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&rc.variant())) {
            const std::string rolePrefix =
                prefix + "roles/" + std::to_string(r) + "/";
            for (std::size_t k = 0; k < kitCfg->kits.size(); ++k) {
                const std::string kitKey =
                    rolePrefix + "kits/" + std::to_string(k) + "/compiled";
                if (!restoreBlob(zip, kitKey, kitCfg->kits[k].compiledBytes))
                    return false;
            }
        }
    }
    return true;
}

inline void clearBlob(std::vector<std::uint8_t>& blob) {
    blob.clear();
    blob.shrink_to_fit();
}

inline void clearRoles(std::vector<RoleConfig>& roles) {
    for (auto& rc : roles) {
        if (auto* kitCfg = rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&rc.variant())) {
            for (auto& kit : kitCfg->kits) {
                // Drop the derived bytes + their hash; the kit is recompiled from
                // `samples` on load (see ProjectKitRecompile).
                clearBlob(kit.compiledBytes);
                kit.compiledHash = 0;
            }
        }
    }
}

} // namespace detail

template <class Sink>
inline bool strip(Sink& zip, ProjectConfig& cfg) {
    for (std::size_t i = 0; i < cfg.systems.size(); ++i) {
        const std::string prefix = "systems/" + std::to_string(i) + "/";
        auto& sys = cfg.systems[i];
        if (auto* sb = rfl::get_if<SameBoyConfig>(&sys.variant())) {
            if (!detail::stripBlob(zip, detail::key(prefix, "rom"),   sb->romBytes))   return false;
            if (!detail::stripBlob(zip, detail::key(prefix, "sram"),  sb->sram))       return false;
            if (!detail::stripBlob(zip, detail::key(prefix, "state"), sb->savestate))  return false;
            if (!detail::stripRoles(zip, prefix, sb->roles))                           return false;
        } else if (auto* mb = rfl::get_if<MesenNesConfig>(&sys.variant())) {
            if (!detail::stripBlob(zip, detail::key(prefix, "rom"),   mb->romBytes))   return false;
            if (!detail::stripBlob(zip, detail::key(prefix, "sram"),  mb->sram))       return false;
            if (!detail::stripBlob(zip, detail::key(prefix, "state"), mb->savestate))  return false;
            if (!detail::stripRoles(zip, prefix, mb->roles))                           return false;
        } else if (auto* gb = rfl::get_if<MesenGbaConfig>(&sys.variant())) {
            if (!detail::stripBlob(zip, detail::key(prefix, "rom"),   gb->romBytes))   return false;
            if (!detail::stripBlob(zip, detail::key(prefix, "sram"),  gb->sram))       return false;
            if (!detail::stripBlob(zip, detail::key(prefix, "state"), gb->savestate))  return false;
            if (!detail::stripRoles(zip, prefix, gb->roles))                           return false;
        }
    }
    return true;
}

// Empty every binary blob in-place, leaving config + paths intact. This is the
// path-only on-disk form: ROM is re-read from `romPath`, SRAM from the sibling
// `<rom>.sav`; savestate and kit bytes are dropped. Mirror of `strip` minus the
// zip writes. See ProjectSerialization::projectConfigToJsonFile.
inline void clear(ProjectConfig& cfg) {
    for (auto& sys : cfg.systems) {
        if (auto* sb = rfl::get_if<SameBoyConfig>(&sys.variant())) {
            detail::clearBlob(sb->romBytes);
            detail::clearBlob(sb->sram);
            detail::clearBlob(sb->savestate);
            detail::clearRoles(sb->roles);
        } else if (auto* mb = rfl::get_if<MesenNesConfig>(&sys.variant())) {
            detail::clearBlob(mb->romBytes);
            detail::clearBlob(mb->sram);
            detail::clearBlob(mb->savestate);
            detail::clearRoles(mb->roles);
        } else if (auto* gb = rfl::get_if<MesenGbaConfig>(&sys.variant())) {
            detail::clearBlob(gb->romBytes);
            detail::clearBlob(gb->sram);
            detail::clearBlob(gb->savestate);
            detail::clearRoles(gb->roles);
        }
    }
}

template <class Source>
inline bool restore(const Source& zip, ProjectConfig& cfg) {
    for (std::size_t i = 0; i < cfg.systems.size(); ++i) {
        const std::string prefix = "systems/" + std::to_string(i) + "/";
        auto& sys = cfg.systems[i];
        if (auto* sb = rfl::get_if<SameBoyConfig>(&sys.variant())) {
            if (!detail::restoreBlob(zip, detail::key(prefix, "rom"),   sb->romBytes))   return false;
            if (!detail::restoreBlob(zip, detail::key(prefix, "sram"),  sb->sram))       return false;
            if (!detail::restoreBlob(zip, detail::key(prefix, "state"), sb->savestate))  return false;
            if (!detail::restoreRoles(zip, prefix, sb->roles))                           return false;
        } else if (auto* mb = rfl::get_if<MesenNesConfig>(&sys.variant())) {
            if (!detail::restoreBlob(zip, detail::key(prefix, "rom"),   mb->romBytes))   return false;
            if (!detail::restoreBlob(zip, detail::key(prefix, "sram"),  mb->sram))       return false;
            if (!detail::restoreBlob(zip, detail::key(prefix, "state"), mb->savestate))  return false;
            if (!detail::restoreRoles(zip, prefix, mb->roles))                           return false;
        } else if (auto* gb = rfl::get_if<MesenGbaConfig>(&sys.variant())) {
            if (!detail::restoreBlob(zip, detail::key(prefix, "rom"),   gb->romBytes))   return false;
            if (!detail::restoreBlob(zip, detail::key(prefix, "sram"),  gb->sram))       return false;
            if (!detail::restoreBlob(zip, detail::key(prefix, "state"), gb->savestate))  return false;
            if (!detail::restoreRoles(zip, prefix, gb->roles))                           return false;
        }
    }
    return true;
}

} // namespace project_binaries
