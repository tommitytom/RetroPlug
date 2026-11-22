#include "host/packs/PackTable.hpp"

#include <chrono>
#include <map>
#include <set>
#include <system_error>
#include <thread>

#include <rfl.hpp>
#include <rfl/json.hpp>

#include "graph/NodeRegistry.hpp"
#include "graph/rp_pack.h"
#include "host/packs/NativeLibrary.hpp"
#include "host/packs/PackDirs.hpp"

namespace fs = std::filesystem;

namespace rp::host::packs {

namespace {

// An rp_package's id, from its manifest JSON; empty when it has none (the registry refuses such a package anyway).
std::string packageIdOf(const rp_package* package) {
    struct Id {
        std::string id;
    };
    if (package == nullptr || package->manifest_json == nullptr) return {};
    const auto parsed = rfl::json::read<Id>(package->manifest_json);
    return parsed ? parsed->id : std::string();
}

// Which side is out of date, for a level that is not this host's.
std::string levelAdvice(std::uint32_t level) {
    return level > RP_API_LEVEL ? "update RetroPlug" : "update the pack";
}

fs::path canonicalOr(const fs::path& p) {
    std::error_code ec;
    fs::path c = fs::weakly_canonical(p, ec);
    return ec ? p : c;
}

// The entry, called inside a handler. The entry must not throw (it is C), and one that does is refused rather than let
// unwind into the host. The call goes through a pointer whose type is declared outside any extern "C", which keeps the
// handler on compilers that tie /EHsc-style assumptions to language linkage; MSVC does not put linkage in a function's
// type, so there the handler rests on the call being through a pointer, which it cannot assume never throws. The
// throwing fixture variant holds it on Linux in the packs test, and the Windows gate (the plan's Task 6) on MSVC.
using EntryCall = const rp_package* const* (*)(std::uint32_t*, std::size_t*);

bool callEntry(EntryCall entry, std::uint32_t& level, std::size_t& count, const rp_package* const*& list) noexcept {
    try {
        list = entry(&level, &count);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

PackTable::PackTable(std::span<const rp_package* const> builtIns) : builtIns_(builtIns.begin(), builtIns.end()) {}

std::size_t PackTable::size() const {
    std::lock_guard<std::mutex> hold(mutex_);
    return packs_.size();
}

const LoadedPack* PackTable::find(std::string_view id) const {
    std::lock_guard<std::mutex> hold(mutex_);
    for (const auto& p : packs_)
        if (p->manifest.id == id) return p.get();
    return nullptr;
}

std::vector<const LoadedPack*> PackTable::loadedFrom(std::size_t first) const {
    std::lock_guard<std::mutex> hold(mutex_);
    std::vector<const LoadedPack*> out;
    for (std::size_t i = first; i < packs_.size(); ++i) out.push_back(packs_[i].get());
    return out;
}

std::vector<PackRefusal> PackTable::refusals() const {
    std::lock_guard<std::mutex> hold(mutex_);
    return refusals_;
}

PackLoad PackTable::load(const PackCandidate& candidate) {
    std::lock_guard<std::mutex> hold(mutex_);
    return loadLocked(candidate);
}

std::vector<PackLoad> PackTable::loadAll(const std::vector<PackCandidate>& candidates) {
    std::lock_guard<std::mutex> hold(mutex_);
    std::vector<PackLoad> out;
    // Each candidate's requirements, read once; one whose manifest does not parse is loaded straight away, which
    // refuses it with the parse's reason.
    struct Pending {
        PackCandidate            candidate;
        std::vector<std::string> needs;
    };
    std::vector<Pending> pending;
    for (const auto& c : candidates) {
        std::string why;
        const auto manifest = readPackManifest(c.dir, &why);
        if (!manifest) {
            out.push_back(loadLocked(c));
            continue;
        }
        pending.push_back({c, manifest->requiresPacks});
    }
    const auto loaded = [&](const std::string& dep) {
        for (const auto& p : packs_)
            if (p->manifest.id == dep) return true;
        return false;
    };
    // Kahn's order without a graph: each pass loads every pending pack whose requirements are all loaded, in the order
    // given, until a pass loads none. What is left waits on a pack that never loaded (absent, refused, or a cycle), and
    // its own load refuses it naming the requirement.
    for (bool progress = true; progress && !pending.empty();) {
        progress = false;
        for (auto it = pending.begin(); it != pending.end();) {
            bool ready = true;
            for (const auto& dep : it->needs) ready = ready && loaded(dep);
            if (!ready) {
                ++it;
                continue;
            }
            out.push_back(loadLocked(it->candidate));
            it = pending.erase(it);
            progress = true;
        }
    }
    for (const auto& p : pending) out.push_back(loadLocked(p.candidate));
    return out;
}

PackLoad PackTable::refuse(const PackCandidate& candidate, std::string id, std::string reason,
                           std::shared_ptr<FileLock> hold) {
    if (id.empty()) id = displayPath(candidate.dir.filename());
    // One hold per folder, however often a load of it is retried and refused.
    if (hold) refusedHolds_.emplace(canonicalOr(candidate.dir), std::move(hold));
    refusals_.push_back({id, candidate.dir, reason});
    return PackLoad{nullptr, std::move(reason)};
}

PackLoad PackTable::loadLocked(const PackCandidate& candidate) {
    std::string why;
    // An installed folder is held before anything of it is read, and kept whatever the answer: a launch's settle must
    // never delete a folder this process has opened a library from, loaded or refused.
    std::shared_ptr<FileLock> hold = candidate.lock;
    if (candidate.origin == PackOrigin::Installed && !hold) {
        FileLock::Failure failure = FileLock::Failure::None;
        std::optional<FileLock> taken;
        // A failure that is only contention (versionInUse's instant of an exclusive lock, a settle's hold), or an
        // access error that may be a scanner's brief handle on Windows (Denied), is tried again briefly; a folder
        // being deleted (Replaced) is refused at once.
        for (int attempt = 0; attempt < 5; ++attempt) {
            taken = holdVersion(candidate.dir, &why, &failure);
            if (taken || failure == FileLock::Failure::Replaced) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (taken) {
            hold = std::make_shared<FileLock>(std::move(*taken));
        } else if (failure != FileLock::Failure::Denied) {
            return refuse(candidate, {}, "cannot hold its version folder: " + why, nullptr);
        }
        // Denied: a folder this process cannot write, which no settle can delete either, so it loads unheld.
    }
    // A refusal keeps the folder's hold only once the pack's library is open: until then nothing of the folder is
    // mapped, and the hold goes with the refusal.
    bool opened = false;
    const auto refuseHere = [&](std::string id, std::string reason) {
        return refuse(candidate, std::move(id), std::move(reason), opened ? hold : nullptr);
    };

    auto manifest = readPackManifest(candidate.dir, &why);
    if (!manifest) return refuseHere({}, why);
    const std::string& id = manifest->id;
    const std::string pack = "pack " + id;

    // An installed pack is packs/<id>/<version>/: anything else is a folder state.json does not describe.
    if (candidate.origin == PackOrigin::Installed &&
        (candidate.dir.filename() != manifest->version || candidate.dir.parent_path().filename() != id))
        return refuseHere(id, pack + " " + manifest->version + " is installed in " + displayPath(candidate.dir) +
                                  ", which is not packs/" + id + "/" + manifest->version);

    // The API level before anything else of the pack is used.
    if (manifest->apiLevel != RP_API_LEVEL)
        return refuseHere(id, pack + " is built for API level " + std::to_string(manifest->apiLevel) +
                                  ", and this RetroPlug is level " + std::to_string(RP_API_LEVEL) + ": " +
                                  levelAdvice(manifest->apiLevel));

    // Only the native half loads in M1, and a pack loads whole or not at all.
    for (const auto& [kind, file] : manifest->halves)
        if (kind != "native")
            return refuseHere(id, pack + " has a TS half, halves." + kind + " (" + file +
                                      "), which this RetroPlug does not load yet; a pack loads whole or not at all");

    for (const auto& p : packs_) {
        if (p->manifest.id != id) continue;
        if (canonicalOr(p->dir) == canonicalOr(candidate.dir)) return PackLoad{p.get(), {}};
        return refuseHere(id, pack + " is loaded already from " + displayPath(p->dir) +
                                  "; another copy takes effect at the next launch");
    }

    // requires.packs names packs, and only packs: a built-in package is not a pack a manifest may name.
    for (const auto& dep : manifest->requiresPacks) {
        bool found = false;
        for (const auto& p : packs_) found = found || p->manifest.id == dep;
        if (!found) return refuseHere(id, pack + " requires pack " + dep + ", which is not loaded");
    }

    auto loaded = std::make_unique<LoadedPack>();
    loaded->dir = candidate.dir;
    loaded->origin = candidate.origin;
    loaded->overridesInstalled = candidate.overridesInstalled;

    std::set<std::string> registered;  // every node type the library registers
    const std::string stem = manifest->nativeHalf();
    if (!stem.empty()) {
        const std::string_view platform = currentPlatform();
        if (platform.empty()) return refuseHere(id, pack + " has a native half, and this platform has none");
        const fs::path rel = fs::path("native") / std::string(platform) / libraryFileName(stem);
        std::error_code ec;
        const fs::path file = fs::absolute(candidate.dir / rel, ec);
        if (ec || !fs::is_regular_file(file, ec))
            return refuseHere(id, pack + " has no library for " + std::string(platform) + " (" + rel.generic_string() +
                                      ")");
        void* library = openLibrary(file, &why);
        if (library == nullptr) return refuseHere(id, pack + ": " + why);
        loaded->library = library;
        opened = true;
        auto entry = reinterpret_cast<EntryCall>(librarySymbol(library, RP_PACK_ENTRY_SYMBOL));
        if (entry == nullptr) return refuseHere(id, pack + "'s library exports no " RP_PACK_ENTRY_SYMBOL);
        std::uint32_t level = 0;
        std::size_t count = 0;
        const rp_package* const* list = nullptr;
        if (!callEntry(entry, level, count, list)) return refuseHere(id, pack + "'s " RP_PACK_ENTRY_SYMBOL " threw");
        if (level != RP_API_LEVEL)
            return refuseHere(id, pack + "'s library reports API level " + std::to_string(level) +
                                      ", and this RetroPlug is level " + std::to_string(RP_API_LEVEL) + ": " +
                                      levelAdvice(level));
        if (list == nullptr || count == 0) return refuseHere(id, pack + "'s library registers no package");
        loaded->packages.assign(list, list + count);

        // Who holds each type id now: the built-ins, then every pack loaded before this one.
        std::map<std::string, std::string, std::less<>> holder;
        const auto holdTypes = [&](const rp_package* p) {
            if (p == nullptr) return;
            for (std::uint32_t k = 0; k < p->node_type_count; ++k)
                if (p->node_types && p->node_types[k] && p->node_types[k]->id)
                    holder.emplace(p->node_types[k]->id, packageIdOf(p));
        };
        for (const rp_package* b : builtIns_) holdTypes(b);
        for (const auto& p : packs_)
            for (const rp_package* q : p->packages) holdTypes(q);
        for (const rp_package* p : loaded->packages) {
            if (p == nullptr) return refuseHere(id, pack + "'s library returned a null package");
            if (p->abi_version != RP_ABI_VERSION) continue;  // the registry's trial below names the ABI mismatch
            for (std::uint32_t k = 0; p->node_types && k < p->node_type_count; ++k) {
                const rp_node_type* type = p->node_types[k];
                if (type == nullptr || type->id == nullptr) continue;  // the trial names it
                const std::string typeId = type->id;
                registered.insert(typeId);
                bool declared = false;
                for (const auto& d : manifest->providesNodeTypes) declared = declared || d == typeId;
                if (!declared)
                    return refuseHere(id, pack + " registers node type " + typeId +
                                              ", which its pack.json's provides.nodeTypes does not declare");
                if (const auto it = holder.find(typeId); it != holder.end())
                    return refuseHere(id, pack + " registers node type " + typeId + ", which package " + it->second +
                                              " has registered already");
            }
        }
        // Everything else the registry checks (descriptors, layouts, package ids, the ABI version, a clash between the
        // pack's own packages), on a scratch registry holding what every Engine will hold before it, so an Engine never
        // meets a refusal the table let through.
        rp::graph::NodeRegistry trial;
        for (const rp_package* b : builtIns_) trial.addPackage(b);
        for (const auto& p : packs_)
            for (const rp_package* q : p->packages) trial.addPackage(q);
        for (const rp_package* p : loaded->packages) {
            if (!trial.addPackage(p, &why)) return refuseHere(id, pack + ": " + why);
            loaded->packageIds.push_back(packageIdOf(p));
        }
    }
    // provides stays truthful: part 5 resolves a project's missing type to the pack that declares it, so a declared
    // type the library does not register is refused as an undeclared one is.
    for (const auto& d : manifest->providesNodeTypes)
        if (registered.count(d) == 0)
            return refuseHere(id, pack + "'s pack.json declares node type " + d +
                                      ", which its library does not register");

    loaded->lock = std::move(hold);
    loaded->manifest = std::move(*manifest);
    packs_.push_back(std::move(loaded));
    return PackLoad{packs_.back().get(), {}};
}

}  // namespace rp::host::packs
