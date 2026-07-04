#pragma once

#include <filesystem>
#include <string>

#include "rfl/Variant.hpp"

#include "project/ProjectConfig.hpp"
#include "system/SystemConfig.hpp"
#include "system/mesen/MesenGbaConfig.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/sameboy/SameBoyConfig.hpp"

// Missing-file detection + relink for a moved thin `.rplg` — the scan / relink /
// autoFindSiblings logic — now lives in shared TS (@retroplug/retroplug
// missingFiles.ts), driven by the UI over the native fileExists + commitProject
// primitives. What stays native is the one piece the headless DAW state path
// needs: sanitizing dangling paired-save write-targets on setState.

namespace rp {

// Clear any per-system paired `savPath` write-target whose parent directory no
// longer exists. A moved DAW chunk (getState/setState) carries an absolute
// paired-save path from another machine; the battery RAM itself is restored from
// the embedded bytes, but a later mirror flush would try to write to — or be
// blocked by — that dangling absolute target. Clearing it falls back to the
// suffix sibling next to the ROM (or no loose mirror when the ROM is embedded).
// A savPath whose directory still exists is kept (a not-yet-written save for a
// fresh cart is legitimate). Returns the number cleared. See porting/23 (D5).
inline int sanitizeSavTargets(ProjectConfig& cfg) {
    int cleared = 0;
    auto clearIfDangling = [&](std::string& savPath) {
        if (savPath.empty()) return;
        std::error_code ec;
        const auto parent = std::filesystem::path(savPath).parent_path();
        if (!parent.empty() && std::filesystem::exists(parent, ec)) return;
        savPath.clear();
        ++cleared;
    };
    for (auto& sys : cfg.systems) {
        if (auto* sb = rfl::get_if<SameBoyConfig>(&sys.variant()))      clearIfDangling(sb->savPath);
        else if (auto* mb = rfl::get_if<MesenNesConfig>(&sys.variant())) clearIfDangling(mb->savPath);
        else if (auto* gb = rfl::get_if<MesenGbaConfig>(&sys.variant())) clearIfDangling(gb->savPath);
    }
    return cleared;
}

} // namespace rp
