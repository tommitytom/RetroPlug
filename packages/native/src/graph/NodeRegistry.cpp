#include "graph/NodeRegistry.hpp"

#include <vector>

#include <rfl/from_generic.hpp>

#include "graph/CanonicalJson.hpp"

namespace rp::graph {

namespace {

struct PackageManifest {
    std::string id;
};

std::string q(std::string_view s) { return jsonString(s); }

// The manifest's id, or why it has none. Through parseJson, so a repeated key reads as JSON.parse reads it.
rfl::Result<std::string> packageId(const rp_package* p) {
    if (!p->manifest_json) return rfl::Unexpected<rfl::Error>(rfl::Error("package has no manifest"));
    auto m = parseJson(p->manifest_json).and_then([](const rfl::Generic& g) { return rfl::from_generic<PackageManifest>(g); });
    if (!m) return rfl::Unexpected<rfl::Error>(rfl::Error(std::string("package manifest: ") + m.error().what()));
    if (!isPlainId(m->id))
        return rfl::Unexpected<rfl::Error>(
            rfl::Error("package manifest id " + q(m->id) + " must be non-empty, with no whitespace or control characters"));
    return m->id;
}

}  // namespace

bool NodeRegistry::addPackage(const rp_package* package, std::string* error) {
    auto fail = [&](std::string why) {
        if (error) *error = std::move(why);
        return false;
    };
    if (!package) return fail("null package");
    // First, before reading anything else: under another ABI the later fields may not be where we look.
    if (package->abi_version != RP_ABI_VERSION)
        return fail("package ABI version " + std::to_string(package->abi_version) + ", host is " +
                    std::to_string(RP_ABI_VERSION));
    const auto id = packageId(package);
    if (!id) return fail(id.error().what());
    const std::string& pkg = *id;
    if (packages_.count(pkg)) return fail("package " + q(pkg) + " is already registered");
    if (package->node_type_count > 0 && !package->node_types)
        return fail("package " + q(pkg) + " lists " + std::to_string(package->node_type_count) + " node types but no array");

    std::vector<Entry> added;
    for (std::uint32_t i = 0; i < package->node_type_count; ++i) {
        const rp_node_type* type = package->node_types[i];
        const std::string at = "package " + q(pkg) + " node type #" + std::to_string(i);
        if (!type) return fail(at + " is null");
        if (!type->id || !*type->id) return fail(at + " has no id");
        const std::string where = "node type " + q(type->id);
        for (const auto& [fn, name] : {std::pair{type->descriptor != nullptr, "descriptor"},
                                       std::pair{type->layout_for != nullptr, "layout_for"},
                                       std::pair{type->query_memory != nullptr, "query_memory"},
                                       std::pair{type->create != nullptr, "create"},
                                       std::pair{type->destroy != nullptr, "destroy"}})
            if (!fn) return fail(where + ": " + name + " is null");
        const char* json = type->descriptor();
        if (!json) return fail(where + ": descriptor() returned null");
        if (!*json) return fail(where + " produced no descriptor (building its default layout or settings schema failed)");
        auto descriptor = parseDescriptor(json);
        if (!descriptor) return fail(where + ": " + descriptor.error().what());
        if (descriptor->id != type->id) return fail(where + " describes itself as " + q(descriptor->id));
        if (find(type->id)) return fail(where + " is already registered");
        for (const auto& a : added)
            if (a.descriptor.id == descriptor->id) return fail(where + " appears twice in package " + q(pkg));
        added.push_back(Entry{type, std::move(*descriptor), pkg});
    }
    for (auto& e : added) {
        std::string key = e.descriptor.id;
        entries_.emplace(std::move(key), std::move(e));
    }
    packages_.insert(pkg);
    return true;
}

const NodeRegistry::Entry* NodeRegistry::find(std::string_view typeId) const {
    const auto it = entries_.find(typeId);
    return it != entries_.end() ? &it->second : nullptr;
}

}  // namespace rp::graph
