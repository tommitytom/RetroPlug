#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "graph/Descriptor.hpp"
#include "graph/rp_node.h"

namespace rp::graph {

// Every node type the planner can build, gathered from rp_package structs - statically linked packages
// and (later) downloaded ones go through the same addPackage, so there is one loading path. Built on the
// control thread before any graph is planned; read-only afterwards.
class NodeRegistry {
public:
    struct Entry {
        const rp_node_type* type = nullptr;
        TypeDescriptor      descriptor;
        std::string         package;
    };

    // Register every node type in a package. Fails, registering nothing, on an ABI version mismatch, an
    // unreadable descriptor, or a node type id that is already registered.
    bool addPackage(const rp_package* package, std::string* error = nullptr);

    const Entry* find(std::string_view typeId) const;
    std::size_t  size() const { return entries_.size(); }

private:
    std::vector<Entry> entries_;
};

}  // namespace rp::graph
