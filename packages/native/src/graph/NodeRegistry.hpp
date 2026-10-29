#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "graph/Descriptor.hpp"
#include "graph/rp_node.h"

namespace rp::graph {

// Every node type the planner can build, gathered from rp_package structs - statically linked packages
// and (later) downloaded ones go through the same addPackage, so there is one loading path, and it is the
// trust boundary for a pack's metadata: everything the host will later call or read is checked here.
//
// A package may be replaced by a new version of itself (replacePackage: a script node pack's reload,
// docs/modular-graph-phase-c1-design.md section 5), and that is the only way an entry stops being found. An entry is
// never moved, and never freed while anything may hold it: a planned instance keeps an Entry* (and destroys its node
// through it), so the entries a replacement swaps out are retired, kept where they are, until their version is put back
// (restorePackage, when what the replacement was for is refused: the same Entry objects found again, so a plan that
// holds them still matches them and nothing is rebuilt) or dropped (dropRetired, once nothing holds one). A version is
// named by the rp_package it was registered from, which its owner holds anyway (a script pack's generation is a package
// of its own, alive as long as the generation is): so whoever frees a version names it by what it frees, and no token
// has to be kept beside it. Not thread-safe: no call may overlap another, so a registry shared by several hosts is
// filled before any of them plans, and after that, concurrent find() is safe. One host may change it between its plans
// on the thread that plans (an Engine's script node packs: Engine::loadScriptPack adds and replaces, its reclaim
// drops), since planning is the only reader: an executor runs a plan's instances and never looks a type up.
class NodeRegistry {
public:
    struct Entry {
        const rp_node_type* type = nullptr;
        TypeDescriptor      descriptor;
        std::string         package;
    };

    // Register every node type in a package, or none of them. Refused, with a reason, for: an ABI version
    // mismatch; a manifest that is not a JSON object with a plain "id" (see isPlainId); a package id
    // already registered; a null node type, or one missing its id or any of its functions; a descriptor
    // that is null, empty (a C++ node whose default layout could not be built) or does not parse; a
    // descriptor whose id is not the node type's; and a type id registered already or listed twice. Never for
    // its flags: every bit is a relaxation (rp_node.h), so one this host does not know is ignored.
    bool addPackage(const rp_package* package, std::string* error = nullptr);

    // Swap the entries of package `id` for those of `next`, a new version of it, checked as addPackage checks a
    // package. Every type of the new version gets an Entry of its own, a type id it shares with the old one included,
    // so the planner, which compares an instance's entry with the one it finds, rebuilds every instance of the package;
    // a type the new version lacks is found no more. The old version's entries are retired, still valid for whatever
    // holds one. The rp_package the swapped-out version was registered from, the key restorePackage and dropRetired
    // take; null, with a reason, changing nothing, when no package `id` is registered (an empty `id` names none),
    // `next` would be refused by addPackage (a type id another package holds among the reasons), its manifest names
    // another package, or `next` is a version the registry holds already (`id`'s current one, or one retired).
    const rp_package* replacePackage(std::string_view id, const rp_package* next, std::string* error = nullptr);
    // Undo the replacement that retired `previous`, which must be the last one made to its package: its entries are
    // found again, the same Entry objects, and those of the version that replaced it are dropped, so nothing may hold
    // one (a plan made with them was refused, and freed with its instances). False, changing nothing, when `previous`
    // is no version a package's last replacement retired, or a type id it held is another package's since.
    bool restorePackage(const rp_package* previous);
    // A retired version's entries dropped: nothing may hold one any longer. False when `retired` is no retired version.
    bool dropRetired(const rp_package* retired);

    bool         hasPackage(std::string_view id) const { return packages_.count(id) != 0; }
    const Entry* find(std::string_view typeId) const;
    std::size_t  size() const { return entries_.size(); }  // the entries find finds
    std::size_t  retiredSize() const;                      // the entries retired and not yet put back or dropped

private:
    using Entries = std::map<std::string, Entry, std::less<>>;  // node-based: an Entry never moves
    // A package registered: the rp_package its entries came from, and their type ids.
    struct Version {
        const rp_package*        source = nullptr;
        std::vector<std::string> types;
    };
    // A version a replacement swapped out: its entries, extracted from the map whole (a node handle keeps its element
    // where it is, so each Entry stays at its address), its package, where it came from and what replaced it.
    struct Retired {
        std::string                     package;
        const rp_package*               source = nullptr;
        const rp_package*               replacedBy = nullptr;
        std::vector<Entries::node_type> entries;
    };

    // The entries `package` would register, checked, and its id; `replacing` names the package a replacement replaces
    // (empty for an addition: replacePackage refuses an id not registered first), whose own type ids the new version
    // may take again.
    bool check(const rp_package* package, std::string_view replacing, std::string& id, std::vector<Entry>& entries,
               std::string* error) const;
    // Whether the registry holds the version `source` came from: a package's current one, or one retired.
    bool holds(const rp_package* source) const;
    std::vector<Retired>::iterator retiredVersion(const rp_package* source);  // the retired version `source` came from

    Entries                                      entries_;
    std::map<std::string, Version, std::less<>>  packages_;
    std::vector<Retired>                         retired_;
};

}  // namespace rp::graph
