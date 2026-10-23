#include "graph/NodeRegistry.hpp"

#include <rfl/json.hpp>

namespace rp::graph {

namespace {

std::string packageId(const rp_package* p) {
    struct Manifest {
        std::string id;
    };
    auto m = rfl::json::read<Manifest, rfl::DefaultIfMissing>(p->manifest_json ? p->manifest_json : "{}");
    return m ? m->id : std::string();
}

}  // namespace

bool NodeRegistry::addPackage(const rp_package* package, std::string* error) {
    auto fail = [&](std::string why) {
        if (error) *error = std::move(why);
        return false;
    };
    if (!package) return fail("null package");
    if (package->abi_version != RP_ABI_VERSION)
        return fail("package ABI version " + std::to_string(package->abi_version) + ", host is " +
                    std::to_string(RP_ABI_VERSION));
    const std::string pkg = packageId(package);

    std::vector<Entry> added;
    for (std::uint32_t i = 0; i < package->node_type_count; ++i) {
        const rp_node_type* type = package->node_types[i];
        if (!type || !type->id || !type->descriptor) return fail("package \"" + pkg + "\": malformed node type");
        auto descriptor = parseDescriptor(type->descriptor());
        if (!descriptor) return fail("node type \"" + std::string(type->id) + "\": " + descriptor.error().what());
        if (descriptor->id != type->id)
            return fail("node type \"" + std::string(type->id) + "\" describes itself as \"" + descriptor->id + "\"");
        if (find(type->id)) return fail("node type \"" + std::string(type->id) + "\" is already registered");
        for (const auto& a : added)
            if (a.descriptor.id == descriptor->id) return fail("node type \"" + descriptor->id + "\" appears twice");
        added.push_back(Entry{type, std::move(*descriptor), pkg});
    }
    for (auto& e : added) entries_.push_back(std::move(e));
    return true;
}

const NodeRegistry::Entry* NodeRegistry::find(std::string_view typeId) const {
    for (const auto& e : entries_)
        if (e.descriptor.id == typeId) return &e;
    return nullptr;
}

}  // namespace rp::graph
