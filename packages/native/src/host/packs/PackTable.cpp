#include "host/packs/PackTable.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <system_error>

#include <rfl.hpp>
#include <rfl/from_generic.hpp>

#include "graph/CanonicalJson.hpp"
#include "graph/NodeRegistry.hpp"
#include "graph/rp_pack.h"
#include "host/packs/NativeLibrary.hpp"
#include "host/packs/PackDirs.hpp"
#include "host/packs/PackScript.hpp"

namespace fs = std::filesystem;

namespace rp::host::packs {

// Named, not anonymous (nor local to the function): reflect-cpp converts through a fake object of the type, which Apple
// clang refuses for a type without external linkage.
namespace detail::table {
struct PackageId {
    std::string id;
};
}  // namespace detail::table

namespace {

// An rp_package's id, from its manifest JSON; empty when it has none (the registry refuses such a package anyway).
std::string packageIdOf(const rp_package* package) {
    using Id = detail::table::PackageId;
    if (package == nullptr || package->manifest_json == nullptr) return {};
    // The registry's own parser, so a repeated key reads here as it reads there.
    const auto parsed = rp::graph::parseJson(package->manifest_json).and_then([](const rfl::Generic& g) {
        return rfl::from_generic<Id>(g);
    });
    return parsed ? parsed->id : std::string();
}

// Which side is out of date, for a level that is not this host's.
std::string levelAdvice(std::uint32_t level) {
    return level > RP_API_LEVEL ? "update RetroPlug" : "update the pack";
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

// The scratch registry's check of one package, which calls into the pack's code (each node type's descriptor), inside
// the same kind of handler as the entry's: an exception from the pack's C code is refused, not unwound into the host.
// `thrown` is set when one was caught, and `at` names the node type whose descriptor threw when it can be told. (No
// fixture variant throws from a descriptor, so this is held by reading, not by a case.)
bool trialAdd(rp::graph::NodeRegistry& trial, const rp_package* package, std::string* why, bool& thrown,
              std::string& at) noexcept {
    try {
        return trial.addPackage(package, why);
    } catch (...) {
        thrown = true;
        for (std::uint32_t k = 0; package->node_types && k < package->node_type_count; ++k) {
            const rp_node_type* type = package->node_types[k];
            if (type == nullptr || type->descriptor == nullptr || type->id == nullptr) continue;
            try {
                (void)type->descriptor(type);
            } catch (...) {
                at = type->id;
                break;
            }
        }
        return false;
    }
}

}  // namespace

PackTable::PackTable(std::span<const rp_package* const> builtIns, fs::path dataRoot)
    : builtIns_(builtIns.begin(), builtIns.end()), dataRoot_(std::move(dataRoot)) {}

namespace {

// The script halves of tables that went away, kept for the life of the process: reachable from here, so a leak checker
// (LeakSanitizer) sees a deliberate keep, not a leak. Never destroyed, so no exit-time order can free one early.
std::vector<std::unique_ptr<ScriptHalf>>& keptScriptHalves() {
    static auto* kept = new std::vector<std::unique_ptr<ScriptHalf>>();
    return *kept;
}

std::mutex& keptScriptHalvesMutex() {
    static std::mutex m;
    return m;
}

}  // namespace

// The MicroQuickJS host keeps a pointer into every script half it registered for the life of the process, so a table
// that goes away (a test's; the process table never does) hands its halves to keptScriptHalves() rather than free
// what the host reads.
PackTable::~PackTable() {
    const std::lock_guard<std::mutex> hold(keptScriptHalvesMutex());
    auto& kept = keptScriptHalves();
    for (auto& half : scripts_) kept.push_back(std::move(half));
}

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

void PackTable::setProtocolTrial(ProtocolTrial trial) {
    std::lock_guard<std::mutex> hold(mutex_);
    protocolTrial_ = std::move(trial);
}

void PackTable::setKindsTrial(KindsTrial trial) {
    std::lock_guard<std::mutex> hold(mutex_);
    kindsTrial_ = std::move(trial);
}

void PackTable::recordRefusal(PackRefusal refusal) {
    std::lock_guard<std::mutex> hold(mutex_);
    refusals_.push_back(std::move(refusal));
}

LoadReport PackTable::loadAll(const std::vector<PackCandidate>& candidates) {
    std::lock_guard<std::mutex> hold(mutex_);
    const std::size_t refusedBefore = refusals_.size();
    std::vector<PackLoad> out;
    // Each candidate's requirements, read once; one whose manifest does not parse is loaded straight away, which
    // refuses it with the parse's reason.
    struct Pending {
        PackCandidate            candidate;
        std::string              id;
        std::vector<std::string> needs;      // requires.packs
        std::vector<std::string> platforms;  // requires.platforms
        std::vector<std::string> provides;   // provides.platforms
    };
    std::vector<Pending> pending;
    for (const auto& c : candidates) {
        std::string why;
        const auto manifest = readPackManifest(c.dir, &why, RP_API_LEVEL);
        if (!manifest) {
            out.push_back(loadLocked(c));
            continue;
        }
        pending.push_back({c, manifest->id, manifest->requiresPacks, manifest->requiresPlatforms,
                           manifest->providesPlatforms});
    }
    const auto loaded = [&](const std::string& dep) {
        for (const auto& p : packs_)
            if (p->manifest.id == dep) return true;
        return false;
    };
    // A platform a pack requires holds it back while another pending pack provides it: a pack needing the NES loads
    // after the pack whose consoles bring the NES, whichever order the folders were found in. A platform no other
    // pending pack provides is the product's or nobody's; the TS loader judges that, and it holds nothing back here.
    const auto waitsOnPlatform = [&](const Pending& p) {
        for (const auto& q : pending) {
            if (&q == &p) continue;
            for (const auto& platform : p.platforms)
                if (std::find(q.provides.begin(), q.provides.end(), platform) != q.provides.end()) return true;
        }
        return false;
    };
    // Kahn's order without a graph: each pass loads every pending pack whose requirements are all loaded (and whose
    // required platforms no other pending pack still provides), in the order given, until a pass loads none. What is
    // left waits on a pack that never loaded (absent, refused, or a cycle), and its own load refuses it naming the
    // requirement.
    for (bool progress = true; progress && !pending.empty();) {
        progress = false;
        for (auto it = pending.begin(); it != pending.end();) {
            bool ready = !waitsOnPlatform(*it);
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
    // A leftover that can reach itself through the other leftovers' requirements is in a cycle, and says so; any other
    // waits on a pack that is absent or refused, which its own load names.
    const auto inCycle = [&](const Pending& start) {
        std::vector<std::string> seen;
        std::vector<std::string> todo = start.needs;
        while (!todo.empty()) {
            const std::string at = todo.back();
            todo.pop_back();
            if (at == start.id) return true;
            if (std::find(seen.begin(), seen.end(), at) != seen.end()) continue;
            seen.push_back(at);
            for (const auto& q : pending)
                if (q.id == at) todo.insert(todo.end(), q.needs.begin(), q.needs.end());
        }
        return false;
    };
    for (const auto& p : pending) {
        if (inCycle(p))
            out.push_back(refuse(p.candidate, p.id, "pack " + p.id + " is part of a requires cycle: it waits, through "
                                                    "requires.packs, on itself", nullptr));
        else
            out.push_back(loadLocked(p.candidate));
    }
    LoadReport report;
    for (const PackLoad& load : out)
        if (load.pack != nullptr) report.loaded.push_back(load.pack);
    report.refused.assign(refusals_.begin() + static_cast<std::ptrdiff_t>(refusedBefore), refusals_.end());
    return report;
}

PackLoad PackTable::refuse(const PackCandidate& candidate, std::string id, std::string reason,
                           std::shared_ptr<FileLock> hold) {
    if (id.empty()) id = candidate.id;
    // An installed folder is packs/<id>/<version>, and a development one often is (build/packs/<id>/<version>): before
    // its manifest parses, its pack is named by its folder, which is the parent when the folder is a version.
    if (id.empty()) {
        const bool byVersion = candidate.dir.has_parent_path() &&
                               (candidate.origin == PackOrigin::Installed ||
                                isPackVersion(candidate.dir.filename().generic_string()));
        id = displayPath(byVersion ? candidate.dir.parent_path().filename() : candidate.dir.filename());
    }
    // One hold per folder, however often a load of it is retried and refused.
    if (hold) folderHolds_.emplace(canonicalOr(candidate.dir), std::move(hold));
    refusals_.push_back({id, candidate.dir, reason});
    return PackLoad{nullptr, std::move(reason)};
}

PackLoad PackTable::loadLocked(const PackCandidate& offered) {
    PackCandidate candidate = offered;
    candidate.dir = normalisedDir(offered.dir);
    std::string why;
    // An installed folder is held before anything of it is read, and kept whatever the answer: a launch's settle must
    // never delete a folder this process has opened a library from, loaded or refused. So is a development folder
    // inside the data root, which a settle can delete as readily (it keeps its Development origin). A folder that is
    // not there is not held: the manifest read below says so, with no retries first.
    std::shared_ptr<FileLock> hold = candidate.lock;
    std::error_code existsEc;
    const bool managed = candidate.origin == PackOrigin::Installed ||
                         (!dataRoot_.empty() && isUnder(candidate.dir, dataRoot_));
    if (managed && !hold && fs::is_directory(candidate.dir, existsEc)) {
        FileLock::Failure failure = FileLock::Failure::None;
        std::optional<FileLock> taken = holdVersionBriefly(candidate.dir, &why, &failure);
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

    auto manifest = readPackManifest(candidate.dir, &why, RP_API_LEVEL);
    if (!manifest) return refuseHere({}, why);
    const std::string& id = manifest->id;
    const std::string pack = "pack " + id;

    // The manifest's API level before anything else of it is used (a manifest of another level read no further).
    if (manifest->apiLevel != RP_API_LEVEL)
        return refuseHere(id, pack + " is built for API level " + std::to_string(manifest->apiLevel) +
                                  ", and this RetroPlug is level " + std::to_string(RP_API_LEVEL) + ": " +
                                  levelAdvice(manifest->apiLevel));

    // An installed pack is packs/<id>/<version>/: anything else is a folder state.json does not describe.
    if (candidate.origin == PackOrigin::Installed &&
        (candidate.dir.filename() != manifest->version || candidate.dir.parent_path().filename() != id))
        return refuseHere(id, pack + " " + manifest->version + " is installed in " + displayPath(candidate.dir) +
                                  ", which is not packs/" + id + "/" + manifest->version);

    // The TS halves load when a context imports them (host/js/PackModules, docs/downloadable-packs-m2c-plan.md), long
    // after this load, so each file the manifest names is checked here, where a refusal still leaves nothing loaded.
    for (const auto& [kind, file] : manifest->halves) {
        if (kind == "native") continue;
        std::error_code ec;
        if (!fs::is_regular_file(candidate.dir / fs::path(file), ec))
            return refuseHere(id, pack + ": halves." + kind + " names " + file + ", which the pack does not have");
    }
    // A script half's runtime is a file of its own (the loop above checked the whole pack's).
    if (manifest->script) {
        std::error_code ec;
        if (!fs::is_regular_file(candidate.dir / fs::path(manifest->script->runtime), ec))
            return refuseHere(id, pack + ": script.runtime names " + manifest->script->runtime +
                                      ", which the pack does not have");
    }

    for (const auto& p : packs_) {
        if (p->manifest.id != id) continue;
        if (canonicalOr(p->dir) == canonicalOr(candidate.dir)) {
            // An installed load of a folder first loaded as a development one: the library is open from it, so the
            // hold this call took is kept rather than dropped with the call. (A development load of an installed
            // folder, the other order, takes no hold: it is the caller's to ask for the folder as installed.)
            if (hold && !p->lock) folderHolds_.emplace(canonicalOr(candidate.dir), hold);
            return PackLoad{p.get(), {}};
        }
        return refuseHere(id, pack + " is loaded already from " + displayPath(p->dir) +
                                  "; another copy takes effect at the next launch");
    }

    // A library of this id opened before and refused: a second copy of it never opens in this process.
    if (const auto it = openedFrom_.find(id); it != openedFrom_.end() && it->second != canonicalOr(candidate.dir))
        return refuseHere(id, pack + "'s library was opened already from " + displayPath(it->second) +
                                  " and refused; another copy takes effect at the next launch");

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
        openedFrom_.emplace(id, canonicalOr(candidate.dir));
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
    }
    // Who holds each type id now: the built-ins, then every pack loaded before this one.
    std::map<std::string, std::string, std::less<>> holder;
    const auto holdTypes = [&](const rp_package* p) {
        if (p == nullptr) return;
        for (std::uint32_t k = 0; k < p->node_type_count; ++k)
            if (p->node_types && p->node_types[k] && p->node_types[k]->id)
                holder.emplace(p->node_types[k]->id, packageIdOf(p));
    };
    for (const rp_package* b : builtIns_) holdTypes(b);
    for (const auto& p : packs_) {
        for (const rp_package* q : p->packages) holdTypes(q);
        if (p->protocol)
            for (const auto& t : p->protocol->typeIds) holder.emplace(t, p->protocol->scriptId);
    }
    // The checks every package of the pack meets, whatever half brought it (null, declared, held): an empty answer is a
    // pass, otherwise the refusal's reason. Run on the native packages before the script half is compiled, so a bad
    // native half never spends a MicroQuickJS slot, and then on the script package.
    const auto checkPackage = [&](const rp_package* p) -> std::string {
        if (p == nullptr) return pack + "'s library returned a null package";
        if (p->abi_version != RP_ABI_VERSION) return {};  // the registry's trial below names the ABI mismatch
        for (std::uint32_t k = 0; p->node_types && k < p->node_type_count; ++k) {
            const rp_node_type* type = p->node_types[k];
            if (type == nullptr || type->id == nullptr) continue;  // the trial names it
            const std::string typeId = type->id;
            registered.insert(typeId);
            bool declared = false;
            for (const auto& d : manifest->providesNodeTypes) declared = declared || d == typeId;
            if (!declared)
                return pack + " registers node type " + typeId +
                       ", which its pack.json's provides.nodeTypes does not declare";
            if (const auto it = holder.find(typeId); it != holder.end())
                return pack + " registers node type " + typeId + ", which package " + it->second +
                       " has registered already";
        }
        return {};
    };
    for (const rp_package* p : loaded->packages)
        if (std::string bad = checkPackage(p); !bad.empty()) return refuseHere(id, bad);
    // Who holds a type id before this pack's own halves: a package already registered, or one of this pack's native
    // packages (empty for none). Asked before anything is compiled, so a refused type costs no compile and no slot.
    const auto heldBy = [&](const std::string& typeId) {
        const auto it = holder.find(typeId);
        std::string by = it != holder.end() ? it->second : std::string();
        for (const rp_package* p : loaded->packages)
            for (std::uint32_t k = 0; by.empty() && p && p->abi_version == RP_ABI_VERSION && p->node_types &&
                                      k < p->node_type_count; ++k)
                if (p->node_types[k] && p->node_types[k]->id && typeId == p->node_types[k]->id) by = packageIdOf(p);
        return by;
    };
    // The protocol half: read, compiled and trial-loaded beside the built-in script nodes by the backend's trial, which
    // also refuses a clash with them; a type another package holds is refused first, as the script half's is.
    if (manifest->protocol) {
        if (!protocolTrial_) return refuseHere(id, pack + " has a protocol half, and this host loads none");
        for (const auto& typeId : manifest->protocol->nodeTypes)
            if (const std::string by = heldBy(typeId); !by.empty())
                return refuseHere(id, pack + " registers node type " + typeId + ", which package " + by +
                                          " has registered already");
        const auto source = readTextFile(candidate.dir / fs::path(manifest->halves.at("protocol")), &why);
        if (!source) return refuseHere(id, pack + ": " + why);
        auto half = protocolTrial_("rp." + id + ".protocol", *source, &why);
        if (!half) return refuseHere(id, pack + ": " + why);
        const auto& declared = manifest->protocol->nodeTypes;
        for (const auto& t : half->typeIds)
            if (std::find(declared.begin(), declared.end(), t) == declared.end())
                return refuseHere(id, pack + "'s protocol half defines node type " + t +
                                          ", which its pack.json's protocol.nodes does not declare");
        for (const auto& t : declared)
            if (std::find(half->typeIds.begin(), half->typeIds.end(), t) == half->typeIds.end())
                return refuseHere(id, pack + "'s pack.json's protocol.nodes declares " + t +
                                          ", which its protocol half does not define");
        for (const auto& t : half->typeIds) registered.insert(t);
        loaded->protocol = std::move(*half);
    }
    // The kinds half: read, compiled and run after the built-in script nodes by the backend's trial, which refuses a
    // runtime that throws, registers an id a built-in package holds or names a kind the product does not declare. An id
    // an earlier pack holds is refused by this trial itself, which runs the earlier packs' chunks first (every Engine's
    // registrar refuses it too).
    if (manifest->kinds) {
        if (!kindsTrial_) return refuseHere(id, pack + " has a kinds half, and this host loads none");
        const auto source = readTextFile(candidate.dir / fs::path(manifest->halves.at("kinds")), &why);
        if (!source) return refuseHere(id, pack + ": " + why);
        std::vector<std::vector<std::uint8_t>> earlier;
        for (const auto& p : packs_)
            if (p->kinds) earlier.push_back(p->kinds->bytecode);
        auto half = kindsTrial_(*source, earlier, manifest->kinds->ids, &why);
        if (!half) return refuseHere(id, pack + ": " + why);
        loaded->kinds = std::move(*half);
    }
    // The script half: compiled now, then registered on the MicroQuickJS host, which loads the whole pack in a control
    // context and refuses definitions that are not its type ids. Held by the table from its registration on, loaded or
    // refused (registerPack keeps a pointer to it).
    if (manifest->script) {
        // A type the manifest names that is held already is refused before anything is compiled: a registration on the
        // MicroQuickJS host takes one of its few pack slots for the life of the process, refused or not.
        for (const auto& typeId : manifest->script->nodeTypes)
            if (const std::string by = heldBy(typeId); !by.empty())
                return refuseHere(id, pack + " registers node type " + typeId + ", which package " + by +
                                          " has registered already");
        auto half = compileScriptHalf(candidate.dir, *manifest, &why);
        if (!half) return refuseHere(id, pack + ": " + why);
        ScriptHalf& held = *half;
        scripts_.push_back(std::move(half));
        if (!registerScriptHalf(held, &why)) return refuseHere(id, pack + ": " + why);
        if (std::string bad = checkPackage(held.package); !bad.empty()) return refuseHere(id, bad);
        loaded->packages.push_back(held.package);
        loaded->script = &held;
    }
    // The trial runs a pack's native packages first, then its script package, which is the order an Engine registers
    // them in.
    if (!loaded->packages.empty()) {
        // Everything else the registry checks (descriptors, layouts, package ids, the ABI version, a clash between the
        // pack's own packages), on a scratch registry holding what every Engine will hold before it, so an Engine never
        // meets a refusal the table let through.
        rp::graph::NodeRegistry trial;
        for (const rp_package* b : builtIns_) trial.addPackage(b);
        for (const auto& p : packs_)
            for (const rp_package* q : p->packages) trial.addPackage(q);
        for (const rp_package* p : loaded->packages) {
            bool thrown = false;
            std::string at;
            if (!trialAdd(trial, p, &why, thrown, at)) {
                if (thrown) {
                    const std::string what = at.empty() ? "types were checked" : "type " + at + " was described";
                    return refuseHere(id, pack + " threw while its node " + what);
                }
                return refuseHere(id, pack + ": " + why);
            }
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
