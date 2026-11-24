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
#include "host/packs/PackScript.hpp"

// The packs a process has loaded (docs/downloadable-packs-loading-design.md section 2): each pack's library opened once
// and never closed, its manifest, and the rp_packages its entry returned. Every Engine in the process reads one table
// (host/packs/ProcessPacks.hpp), so a plugin's instances, its engine jobs (renders, which build an Engine) and the
// standalone share each load; the offline pool's graph jobs build a bare registry of the core and DSP packages and
// register no pack. An Engine takes what the table holds when it is built, then each pack loaded since at its next
// setGraph (Engine::syncPacks), on its own control thread: the table never reaches into an Engine.
//
// The table is append-only and its packs are immutable once appended, so a pointer it hands out is valid for the
// table's life. Loads are serialised by one mutex, which loadedFrom takes too: any thread may load while Engines read.
namespace rp::host::packs {

struct LoadedPack {
    PackManifest                   manifest;
    std::filesystem::path          dir;
    PackOrigin                     origin = PackOrigin::Development;
    bool                           overridesInstalled = false;
    // What rp_pack_entry returned, in its order, then its script half's package; empty with neither.
    std::vector<const rp_package*> packages;
    const ScriptHalf*              script = nullptr;  // its script half, compiled and registered; the table owns it
    std::vector<std::string>       packageIds;  // each package's manifest id, index for index
    void*                          library = nullptr;  // never closed
    std::shared_ptr<FileLock>      lock;  // the hold on its version folder: installed, or development in the data root
};

// A load's answer: the pack, or null with the reason.
struct PackLoad {
    const LoadedPack* pack = nullptr;
    std::string       error;
    explicit operator bool() const { return pack != nullptr; }
};

// What loadAll did: the packs it loaded or found loaded, in load order, and the refusals it recorded, in the order
// recorded (the table's refusals() also hold other threads' loads, which a caller of one call must not claim).
struct LoadReport {
    std::vector<const LoadedPack*> loaded;
    std::vector<PackRefusal>       refused;
};

class PackTable {
public:
    // `builtIns` are the packages every Engine of this process registers before any pack (the product's
    // rp_register_packages): a pack may not take one of their node types or package ids. They must outlive the table,
    // as a product's do. requires.packs names packs only, never a built-in package.
    // `dataRoot` is the data root a process's installed packs live in (empty for none): a development folder under it
    // is held like an installed one, or refused, whoever loads it (a launch's discovery, or a live load later).
    explicit PackTable(std::span<const rp_package* const> builtIns = {}, std::filesystem::path dataRoot = {});
    PackTable(const PackTable&) = delete;
    PackTable& operator=(const PackTable&) = delete;

    // One pack, all or nothing: refused, it is recorded in refusals() and adds nothing to the table. A pack whose id
    // is loaded already answers that load when it is the same folder, and is refused when it is another (a second
    // version takes effect at the next launch: native code is never unloaded).
    PackLoad load(const PackCandidate& candidate);
    PackLoad load(const std::filesystem::path& dir) { return load(PackCandidate{dir}); }
    // Several packs in dependency order: each loads once every pack its requires.packs names has, whatever order the
    // candidates come in; one whose requirement never loads is refused naming it. The report lists the packs in the
    // order they loaded and the refusals in the order recorded.
    LoadReport loadAll(const std::vector<PackCandidate>& candidates);
    // A refusal that came before any load (an unreadable state.json, a development manifest that does not parse, a
    // folder that could not be held), so that refusals() is the one list a Settings page reads.
    void recordRefusal(PackRefusal refusal);

    std::size_t                    size() const;
    const LoadedPack*              find(std::string_view id) const;
    // The packs loaded at or after position `first`, in load order (dependency order): what an Engine that has
    // registered `first` of them has yet to register.
    std::vector<const LoadedPack*> loadedFrom(std::size_t first) const;
    std::vector<PackRefusal>       refusals() const;

private:
    PackLoad loadLocked(const PackCandidate& offered);
    PackLoad refuse(const PackCandidate& candidate, std::string id, std::string reason,
                    std::shared_ptr<FileLock> hold);

    std::vector<const rp_package*>           builtIns_;
    std::filesystem::path                    dataRoot_;
    mutable std::mutex                       mutex_;
    std::vector<std::unique_ptr<LoadedPack>> packs_;
    std::vector<PackRefusal>                 refusals_;
    // Every script half this table compiled, loaded or refused: the MicroQuickJS host keeps a pointer to each one it
    // registered for the life of the process (and compares halves by address), so none is ever freed.
    std::vector<std::unique_ptr<ScriptHalf>> scripts_;
    // The holds on installed folders a LoadedPack does not carry, one per folder: a pack refused after its library
    // opened (the library stays open, never closed, so its folder is held for the life of the process as a loaded
    // one's is), and an installed folder whose pack this table loaded earlier as a development one.
    std::map<std::filesystem::path, std::shared_ptr<FileLock>> folderHolds_;
    // The folder each id's library was first opened from, loaded or refused: a second library of one id would be a
    // second rp-<id> in the process, which Windows matches an already-loaded module by, so it never opens.
    std::map<std::string, std::filesystem::path> openedFrom_;
};

}  // namespace rp::host::packs
