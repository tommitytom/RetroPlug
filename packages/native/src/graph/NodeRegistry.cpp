#include "graph/NodeRegistry.hpp"

#include <algorithm>
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

bool NodeRegistry::check(const rp_package* package, std::string_view replacing, std::string& pkg,
                         std::vector<Entry>& added, std::string* error) const {
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
    pkg = *id;
    if (replacing.empty()) {
        if (packages_.count(pkg)) return fail("package " + q(pkg) + " is already registered");
    } else {
        if (pkg != replacing)
            return fail("package " + q(pkg) + " cannot replace package " + q(replacing) +
                        ": a new version keeps its package's id");
        if (!packages_.count(pkg))
            return fail("package " + q(pkg) + " is not registered, so it has no version to replace");
        if (holds(package)) return fail("that version of package " + q(pkg) + " is registered already");
    }
    if (package->node_type_count > 0 && !package->node_types)
        return fail("package " + q(pkg) + " lists " + std::to_string(package->node_type_count) + " node types but no array");

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
        const char* json = type->descriptor(type);
        if (!json) return fail(where + ": descriptor() returned null");
        if (!*json) return fail(where + " produced no descriptor (building its default layout or settings schema failed)");
        auto descriptor = parseDescriptor(json);
        if (!descriptor) return fail(where + ": " + descriptor.error().what());
        if (descriptor->id != type->id) return fail(where + " describes itself as " + q(descriptor->id));
        // A replacement may take its own package's type ids again; never another package's.
        if (const Entry* held = find(type->id); held && held->package != replacing)
            return fail(where + " is already registered");
        for (const auto& a : added)
            if (a.descriptor.id == descriptor->id) return fail(where + " appears twice in package " + q(pkg));
        added.push_back(Entry{type, std::move(*descriptor), pkg});
    }
    return true;
}

bool NodeRegistry::holds(const rp_package* source) const {
    for (const auto& [id, version] : packages_)
        if (version.source == source) return true;
    return std::any_of(retired_.begin(), retired_.end(), [&](const Retired& r) { return r.source == source; });
}

std::vector<NodeRegistry::Retired>::iterator NodeRegistry::retiredVersion(const rp_package* source) {
    return std::find_if(retired_.begin(), retired_.end(), [&](const Retired& r) { return r.source == source; });
}

bool NodeRegistry::addPackage(const rp_package* package, std::string* error) {
    std::string pkg;
    std::vector<Entry> added;
    if (!check(package, {}, pkg, added, error)) return false;
    Version& version = packages_[pkg];
    version.source = package;
    for (auto& e : added) {
        version.types.push_back(e.descriptor.id);
        std::string key = e.descriptor.id;
        entries_.emplace(std::move(key), std::move(e));
    }
    return true;
}

const rp_package* NodeRegistry::replacePackage(std::string_view id, const rp_package* next, std::string* error) {
    std::string pkg;
    std::vector<Entry> added;
    if (!check(next, id, pkg, added, error)) return nullptr;
    Version& current = packages_.find(pkg)->second;
    Retired old{pkg, current.source, next, {}};
    for (const auto& type : current.types) old.entries.push_back(entries_.extract(type));
    current.source = next;
    current.types.clear();
    for (auto& e : added) {
        current.types.push_back(e.descriptor.id);
        std::string key = e.descriptor.id;
        entries_.emplace(std::move(key), std::move(e));
    }
    const rp_package* const previous = old.source;
    retired_.push_back(std::move(old));
    return previous;
}

bool NodeRegistry::restorePackage(const rp_package* previous) {
    const auto at = retiredVersion(previous);
    if (at == retired_.end()) return false;
    const auto pkg = packages_.find(at->package);
    if (pkg == packages_.end() || pkg->second.source != at->replacedBy) return false;  // not its package's last
    // Each of its type ids is free once the version that replaced it goes, unless another package took one since.
    for (const auto& e : at->entries)
        if (const auto taken = entries_.find(e.key()); taken != entries_.end() && taken->second.package != at->package)
            return false;
    for (const auto& type : pkg->second.types) entries_.erase(type);
    pkg->second.source = previous;
    pkg->second.types.clear();
    for (auto& e : at->entries) {
        pkg->second.types.push_back(e.key());
        entries_.insert(std::move(e));  // the node itself, so the Entry is where it was
    }
    retired_.erase(at);
    return true;
}

bool NodeRegistry::dropRetired(const rp_package* retired) {
    const auto at = retiredVersion(retired);
    if (at == retired_.end()) return false;
    retired_.erase(at);
    return true;
}

std::size_t NodeRegistry::retiredSize() const {
    std::size_t n = 0;
    for (const auto& r : retired_) n += r.entries.size();
    return n;
}

const NodeRegistry::Entry* NodeRegistry::find(std::string_view typeId) const {
    const auto it = entries_.find(typeId);
    return it != entries_.end() ? &it->second : nullptr;
}

}  // namespace rp::graph
