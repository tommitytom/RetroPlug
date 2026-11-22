#include "host/packs/PackDiscovery.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

#include "graph/rp_pack.h"
#include "host/packs/PackDirs.hpp"
#include "host/packs/PackManifest.hpp"
#include "host/packs/PackState.hpp"
#include "host/packs/PackTable.hpp"

namespace fs = std::filesystem;

namespace rp::host::packs {

namespace {

// The candidates of `state` (empty when it did not read, `why` then saying why, or null when that was reported
// already) and of `devDirs`.
PackDiscovery discoverFrom(const PackRoots& roots, const std::optional<PackState>& state, const std::string* why,
                           const std::vector<fs::path>& devDirs) {
    PackDiscovery out;
    if (state) {
        for (const auto& p : state->packs) {
            if (p.remove) continue;
            out.candidates.push_back({roots.data / p.id / p.active, PackOrigin::Installed, false, nullptr, p.id});
        }
    } else if (!roots.data.empty() && why != nullptr) {
        out.refused.push_back({"state.json", roots.data / "state.json", *why});
    }
    std::vector<fs::path> given;  // each development folder's canonical path, to drop the same one given twice
    for (const auto& offered : devDirs) {
        const fs::path dir = normalisedDir(offered);
        const fs::path real = canonicalOr(dir);
        if (std::find(given.begin(), given.end(), real) != given.end()) continue;
        given.push_back(real);
        std::string manifestWhy;
        const auto manifest = readPackManifest(dir, &manifestWhy, RP_API_LEVEL);  // a level mismatch still names its id
        if (!manifest) {
            out.refused.push_back({displayPath(dir.filename()), dir, manifestWhy});
            continue;
        }
        bool placed = false;
        for (std::size_t i = 0; i < out.candidates.size(); ++i) {
            if (out.candidates[i].id != manifest->id) continue;
            placed = true;
            if (out.candidates[i].origin == PackOrigin::Installed) {
                out.candidates[i] = {dir, PackOrigin::Development, true, nullptr, manifest->id};
            } else {
                out.refused.push_back({manifest->id, dir,
                                       "development pack " + manifest->id + " is given twice: " +
                                           displayPath(out.candidates[i].dir) + " loads, " + displayPath(dir) +
                                           " does not"});
            }
            break;
        }
        if (!placed) {
            out.candidates.push_back({dir, PackOrigin::Development, false, nullptr, manifest->id});
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
        for (std::size_t i = 0; i < found.candidates.size();) {
            PackCandidate& c = found.candidates[i];
            // An installed folder is held; so is a development one that lies inside the data root, which a settle can
            // delete as readily (the candidate keeps its Development origin: the loader adopts a hold either way).
            if (c.origin != PackOrigin::Installed && !(!roots.data.empty() && isUnder(c.dir, roots.data))) {
                ++i;
                continue;
            }
            std::string held;
            FileLock::Failure failure = FileLock::Failure::None;
            std::error_code existsEc;
            if (!fs::is_directory(c.dir, existsEc)) {
                ++i;  // not there (a state.json entry whose folder is gone): the table's manifest read says so
            } else if (auto lock = holdVersionBriefly(c.dir, &held, &failure)) {
                c.lock = std::make_shared<FileLock>(std::move(*lock));
                ++i;
            } else if (c.origin == PackOrigin::Development && failure != FileLock::Failure::Denied) {
                // Inside the managed root a development folder must be held, not loaded unheld. (An installed one
                // that is not held here is held, or refused, by the table's own bounded retry.)
                found.refused.push_back({c.id, c.dir, "cannot hold its version folder: " + held});
                found.candidates.erase(found.candidates.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
    }
    refused.insert(refused.end(), found.refused.begin(), found.refused.end());
    // The refusals that came before any load are the table's too, so its refusals() is the one list a page reads.
    for (const PackRefusal& r : refused) table.recordRefusal(r);
    const LoadReport report = table.loadAll(found.candidates);
    refused.insert(refused.end(), report.refused.begin(), report.refused.end());
    return refused;
}

}  // namespace rp::host::packs
