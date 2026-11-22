#include "host/packs/PackDiscovery.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

#include "graph/rp_pack.h"
#include "host/packs/PackManifest.hpp"
#include "host/packs/PackState.hpp"
#include "host/packs/PackTable.hpp"

namespace fs = std::filesystem;

namespace rp::host::packs {

namespace {

// Whether `dir` lies under `root`, both canonical: a development folder that is really an installed version folder.
bool isUnder(const fs::path& dir, const fs::path& root) {
    std::error_code ec;
    const fs::path d = fs::weakly_canonical(dir, ec);
    if (ec) return false;
    const fs::path r = fs::weakly_canonical(root, ec);
    if (ec) return false;
    const fs::path rel = d.lexically_relative(r);
    return !rel.empty() && *rel.begin() != ".." && *rel.begin() != ".";
}

// `dir` as PackTable keys it (lexically normal, no trailing separator), so a spelling of a folder never reads as
// another one and a refusal never carries an empty name.
fs::path normalised(const fs::path& dir) {
    fs::path p = dir.lexically_normal();
    if (!p.has_filename() && p.has_parent_path()) p = p.parent_path();
    return p;
}

// The candidates of `state` (empty when it did not read, `why` then saying why, or null when that was reported
// already) and of `devDirs`.
PackDiscovery discoverFrom(const PackRoots& roots, const std::optional<PackState>& state, const std::string* why,
                           const std::vector<fs::path>& devDirs) {
    PackDiscovery out;
    std::vector<std::string> ids;  // out.candidates' ids, index for index
    if (state) {
        for (const auto& p : state->packs) {
            if (p.remove) continue;
            out.candidates.push_back({roots.data / p.id / p.active, PackOrigin::Installed, false, nullptr});
            ids.push_back(p.id);
        }
    } else if (!roots.data.empty() && why != nullptr) {
        out.refused.push_back({"state.json", roots.data / "state.json", *why});
    }
    std::vector<fs::path> given;  // each development folder's canonical path, to drop the same one given twice
    for (const auto& offered : devDirs) {
        const fs::path dir = normalised(offered);
        std::error_code canonEc;
        const fs::path real = fs::weakly_canonical(dir, canonEc);
        if (std::find(given.begin(), given.end(), canonEc ? dir : real) != given.end()) continue;
        given.push_back(canonEc ? dir : real);
        std::string why;
        const auto manifest = readPackManifest(dir, &why, RP_API_LEVEL);  // a level mismatch still names its id
        if (!manifest) {
            out.refused.push_back({displayPath(dir.filename()), dir, why});
            continue;
        }
        bool placed = false;
        for (std::size_t i = 0; i < out.candidates.size(); ++i) {
            if (ids[i] != manifest->id) continue;
            placed = true;
            if (out.candidates[i].origin == PackOrigin::Installed) {
                out.candidates[i] = {dir, PackOrigin::Development, true, nullptr};
            } else {
                out.refused.push_back({manifest->id, dir,
                                       "development pack " + manifest->id + " is given twice: " +
                                           displayPath(out.candidates[i].dir) + " loads, " + displayPath(dir) +
                                           " does not"});
            }
            break;
        }
        if (!placed) {
            out.candidates.push_back({dir, PackOrigin::Development, false, nullptr});
            ids.push_back(manifest->id);
        }
    }
    return out;
}

}  // namespace

PackDiscovery discoverPacks(const PackRoots& roots, const std::vector<fs::path>& devDirs) {
    std::string why;
    std::optional<PackState> state;
    if (!roots.data.empty()) state = readPackState(roots.data, &why);
    return discoverFrom(roots, state, &why, devDirs);
}

std::vector<PackRefusal> loadLaunchPacks(PackTable& table, const PackRoots& roots,
                                         const std::vector<fs::path>& extraDevDirs) {
    std::vector<PackRefusal> refused;
    // The explicit folders first: an id given twice loads the first, so a host's --pack beats the environment.
    std::vector<fs::path> devDirs = extraDevDirs;
    const std::vector<fs::path> fromEnv = packPathFromEnv();
    devDirs.insert(devDirs.end(), fromEnv.begin(), fromEnv.end());
    PackDiscovery found;
    {
        // One exclusive hold for the settle, the read, the discovery and the version holds, so another launch's settle
        // (which takes it too) cannot delete a folder this one has found and not yet held. No data root, or none on
        // disk yet, has no installed pack and nothing to settle.
        std::string why;
        std::error_code ec;
        std::optional<FileLock> hold;
        std::optional<PackState> state;
        bool reported = false;  // a state.json that will not read is reported once, by the settle or by the read
        if (!roots.data.empty() && fs::exists(roots.data, ec)) {
            hold = lockPackState(roots.data, FileLock::Mode::Exclusive, &why);
            if (hold) {
                if (!settlePackState(roots.data, *hold, &why)) {
                    refused.push_back({"state.json", roots.data / "state.json", why});
                    reported = true;
                }
                state = readPackState(roots.data, *hold, &why);
            } else {
                // A root this process cannot write: nothing to settle, and the read goes ahead without a hold.
                state = readPackState(roots.data, &why);
            }
        } else {
            state = PackState{};
        }
        found = discoverFrom(roots, state, reported ? nullptr : &why, devDirs);
        for (auto it = found.candidates.begin(); it != found.candidates.end();) {
            PackCandidate& c = *it;
            // An installed folder is held; so is a development one that lies inside the data root, which a settle can
            // delete as readily (the candidate keeps its Development origin: the loader adopts a hold either way).
            if (c.origin != PackOrigin::Installed && !(!roots.data.empty() && isUnder(c.dir, roots.data))) {
                ++it;
                continue;
            }
            std::string held;
            FileLock::Failure failure = FileLock::Failure::None;
            if (auto lock = holdVersionBriefly(c.dir, &held, &failure)) {
                c.lock = std::make_shared<FileLock>(std::move(*lock));
                ++it;
            } else if (c.origin == PackOrigin::Development && failure != FileLock::Failure::Denied) {
                // Inside the managed root a development folder must be held, not loaded unheld. (An installed one
                // that is not held here is held, or refused, by the table's own bounded retry.)
                found.refused.push_back(
                    {displayPath(c.dir.filename()), c.dir, "cannot hold its version folder: " + held});
                it = found.candidates.erase(it);
            } else {
                ++it;
            }
        }
    }
    refused.insert(refused.end(), found.refused.begin(), found.refused.end());
    std::vector<PackRefusal> loadRefused;
    table.loadAll(found.candidates, &loadRefused);
    refused.insert(refused.end(), loadRefused.begin(), loadRefused.end());
    return refused;
}

}  // namespace rp::host::packs
