#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "graph/rp_node.h"
#include "host/packs/FileLock.hpp"
#include "host/packs/PackCandidate.hpp"
#include "host/packs/PackManifest.hpp"

// The packs a process has loaded (docs/downloadable-packs-loading-design.md section 2): each pack's library opened
// once and never closed, its manifest, and the rp_packages its entry returned. Every Engine in the process reads one
// table (host/packs/ProcessPacks.hpp), so a plugin's instances, its render jobs and the standalone share each load. An
// Engine takes what the table holds when it is built, then each pack loaded since at its next setGraph
// (Engine::syncPacks), on its own control thread: the table never reaches into an Engine.
//
// The table is append-only and its packs are immutable once appended, so a pointer it hands out is valid for the
// table's life. Loads are serialised by one mutex, which loadedFrom takes too: any thread may load while Engines read.
namespace rp::host::packs {

struct LoadedPack {
    PackManifest                   manifest;
    std::filesystem::path          dir;
    PackOrigin                     origin = PackOrigin::Development;
    bool                           overridesInstalled = false;
    std::vector<const rp_package*> packages;  // what rp_pack_entry returned, in its order; empty with no native half
    std::vector<std::string>       packageIds;  // each package's manifest id, index for index
    void*                          library = nullptr;  // never closed
    std::shared_ptr<FileLock>      lock;               // an installed pack's hold on its version folder
};

// A load's answer: the pack, or null with the reason.
struct PackLoad {
    const LoadedPack* pack = nullptr;
    std::string       error;
    explicit operator bool() const { return pack != nullptr; }
};

class PackTable {
public:
    // `builtIns` are the packages every Engine of this process registers before any pack (the product's
    // rp_register_packages): a pack may not take one of their node types or package ids. They must outlive the table,
    // as a product's do. requires.packs names packs only, never a built-in package.
    explicit PackTable(std::span<const rp_package* const> builtIns = {});
    PackTable(const PackTable&) = delete;
    PackTable& operator=(const PackTable&) = delete;

    // One pack, all or nothing: refused, it is recorded in refusals() and adds nothing to the table. A pack whose id
    // is loaded already answers that load when it is the same folder, and is refused when it is another (a second
    // version takes effect at the next launch: native code is never unloaded).
    PackLoad load(const PackCandidate& candidate);
    PackLoad load(const std::filesystem::path& dir) { return load(PackCandidate{dir}); }
    // Several packs in dependency order: each loads once every pack its requires.packs names has, whatever order the
    // candidates come in; one whose requirement never loads is refused naming it. Answers in the order they loaded,
    // the refused ones last.
    std::vector<PackLoad> loadAll(const std::vector<PackCandidate>& candidates);

    std::size_t                    size() const;
    const LoadedPack*              find(std::string_view id) const;
    // The packs loaded at or after position `first`, in load order (dependency order): what an Engine that has
    // registered `first` of them has yet to register.
    std::vector<const LoadedPack*> loadedFrom(std::size_t first) const;
    std::vector<PackRefusal>       refusals() const;

private:
    PackLoad loadLocked(const PackCandidate& candidate);
    PackLoad refuse(const PackCandidate& candidate, std::string id, std::string reason,
                    std::shared_ptr<FileLock> hold);

    std::vector<const rp_package*>           builtIns_;
    mutable std::mutex                       mutex_;
    std::vector<std::unique_ptr<LoadedPack>> packs_;
    std::vector<PackRefusal>                 refusals_;
    // The holds on the installed folders of packs this table refused after opening their libraries, one per folder: a
    // refused pack's library stays open (it is never closed), so its folder is held for the life of the process as a
    // loaded one's is.
    std::map<std::filesystem::path, std::shared_ptr<FileLock>> refusedHolds_;
};

}  // namespace rp::host::packs
