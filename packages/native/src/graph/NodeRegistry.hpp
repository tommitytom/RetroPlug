#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>

#include "graph/Descriptor.hpp"
#include "graph/rp_node.h"

namespace rp::graph {

// Every node type the planner can build, gathered from rp_package structs - statically linked packages
// and (later) downloaded ones go through the same addPackage, so there is one loading path, and it is the
// trust boundary for a pack's metadata: everything the host will later call or read is checked here.
//
// Entries are never moved or removed, so an Entry* stays valid for the registry's life (a planned instance
// keeps one). Not thread-safe: addPackage must not overlap any other call, so a registry shared by several
// hosts is filled before any of them plans; after that, concurrent find() is safe.
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
    // descriptor whose id is not the node type's; and a type id registered already or listed twice.
    bool addPackage(const rp_package* package, std::string* error = nullptr);

    const Entry* find(std::string_view typeId) const;
    std::size_t  size() const { return entries_.size(); }

private:
    std::map<std::string, Entry, std::less<>> entries_;  // node-based: an Entry never moves
    std::set<std::string, std::less<>>        packages_;
};

}  // namespace rp::graph
