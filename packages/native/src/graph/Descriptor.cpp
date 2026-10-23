#include "graph/Descriptor.hpp"

#include <charconv>
#include <cmath>
#include <utility>

#include "graph/CanonicalJson.hpp"

namespace rp::graph {

namespace {

using Object = rfl::Generic::Object;
using Array  = rfl::Generic::Array;

const char* const kKindNames[] = {"audio", "events", "bytes", "control", "link", "resource"};
const char* const kDirNames[]  = {"in", "out", "link"};
const char* const kTypeNames[] = {"float", "int", "enum", "bool"};

// rfl::Object's variadic insert(pairs...) outranks insert(key, value) for a string-literal key, so every
// keyed insert goes through here.
void put(Object& o, std::string key, rfl::Generic value) { o.insert(std::make_pair(std::move(key), std::move(value))); }

rfl::Generic channelsGeneric(const ChannelSpec& c) {
    Object o;
    switch (c.mode) {
        case ChannelSpec::Mode::Any:   put(o, "any", rfl::Generic(true)); break;
        case ChannelSpec::Mode::Match: put(o, "match", rfl::Generic(c.match)); break;
        case ChannelSpec::Mode::Fixed:
            put(o, "count", rfl::Generic(static_cast<std::int64_t>(c.count)));
            put(o, "layout", rfl::Generic(c.layout));
            break;
    }
    return rfl::Generic(std::move(o));
}

rfl::Generic portGeneric(const PortDesc& p) {
    Object o;
    put(o, "id", rfl::Generic(p.id));
    put(o, "dir", rfl::Generic(std::string(kDirNames[static_cast<int>(p.dir)])));
    put(o, "kind", rfl::Generic(std::string(kindName(p.kind))));
    if (p.kind == PortKind::Audio) put(o, "channels", channelsGeneric(p.channels));
    if (p.kind == PortKind::Resource) put(o, "resourceType", rfl::Generic(p.resourceType));
    if (p.write) put(o, "access", rfl::Generic(std::string("write")));
    return rfl::Generic(std::move(o));
}

rfl::Generic paramGeneric(const ParamDesc& p) {
    Object o;
    put(o, "id", rfl::Generic(p.id));
    put(o, "name", rfl::Generic(p.name));
    put(o, "type", rfl::Generic(std::string(kTypeNames[static_cast<int>(p.type)])));
    switch (p.type) {
        case ParamType::Float:
        case ParamType::Int:
            put(o, "min", rfl::Generic(p.min));
            put(o, "max", rfl::Generic(p.max));
            put(o, "default", rfl::Generic(p.def));
            break;
        case ParamType::Bool:
            put(o, "default", rfl::Generic(p.def != 0.0));
            break;
        case ParamType::Enum: {
            Array values;
            for (const auto& v : p.values) values.emplace_back(v);
            put(o, "values", rfl::Generic(std::move(values)));
            const auto i = static_cast<std::size_t>(p.def);
            put(o, "default", rfl::Generic(i < p.values.size() ? p.values[i] : std::string()));
            break;
        }
    }
    if (!p.unit.empty()) put(o, "unit", rfl::Generic(p.unit));
    return rfl::Generic(std::move(o));
}

void insertLayout(Object& o, const Layout& layout) {
    Array ports, params;
    for (const auto& p : layout.ports) ports.push_back(portGeneric(p));
    for (const auto& p : layout.params) params.push_back(paramGeneric(p));
    put(o, "ports", rfl::Generic(std::move(ports)));
    put(o, "params", rfl::Generic(std::move(params)));
    if (!layout.exclusive.empty()) {
        Array groups;
        for (const auto& g : layout.exclusive) {
            Array group;
            for (const auto& id : g) group.emplace_back(id);
            groups.emplace_back(std::move(group));
        }
        put(o, "exclusive", rfl::Generic(std::move(groups)));
    }
}

// ---- parsing ---------------------------------------------------------------------------------------

rfl::Unexpected<rfl::Error> fail(const std::string& what) { return rfl::Unexpected<rfl::Error>(rfl::Error(what)); }

const rfl::Generic* field(const Object& o, std::string_view key) {
    for (const auto& [k, v] : o)
        if (k == key) return &v;
    return nullptr;
}

rfl::Result<std::string> stringField(const Object& o, std::string_view key, bool required = true) {
    const auto* v = field(o, key);
    if (!v) return required ? rfl::Result<std::string>(fail("missing \"" + std::string(key) + "\""))
                            : rfl::Result<std::string>(std::string());
    return v->to_string();
}

rfl::Result<double> numberField(const Object& o, std::string_view key, double fallback) {
    const auto* v = field(o, key);
    if (!v) return fallback;
    return v->to_double();
}

template <std::size_t N>
rfl::Result<int> enumIndex(const std::string& s, const char* const (&names)[N], std::string_view what) {
    for (std::size_t i = 0; i < N; ++i)
        if (s == names[i]) return static_cast<int>(i);
    return fail("unknown " + std::string(what) + " \"" + s + "\"");
}

rfl::Result<ChannelSpec> parseChannels(const rfl::Generic& g) {
    auto o = g.to_object();
    if (!o) return fail("\"channels\" must be an object");
    if (field(*o, "any")) return ChannelSpec::any();
    if (field(*o, "match")) return stringField(*o, "match").transform(ChannelSpec::matching);
    const auto* count = field(*o, "count");
    if (!count) return fail("\"channels\" needs one of any / match / count");
    auto n = count->to_int64();
    if (!n || *n < 1) return fail("\"channels.count\" must be a positive integer");
    auto layoutName = stringField(*o, "layout");
    if (!layoutName) return fail(layoutName.error().what());
    return ChannelSpec::fixed(static_cast<std::uint32_t>(*n), *layoutName);
}

rfl::Result<PortDesc> parsePort(const rfl::Generic& g) {
    auto o = g.to_object();
    if (!o) return fail("a port must be an object");
    PortDesc p;
    auto id = stringField(*o, "id");
    if (!id) return fail(id.error().what());
    p.id = *id;
    auto dir = stringField(*o, "dir").and_then([](const std::string& s) { return enumIndex(s, kDirNames, "dir"); });
    if (!dir) return fail("port \"" + p.id + "\": " + dir.error().what());
    p.dir = static_cast<PortDir>(*dir);
    auto kind = stringField(*o, "kind").and_then([](const std::string& s) { return enumIndex(s, kKindNames, "kind"); });
    if (!kind) return fail("port \"" + p.id + "\": " + kind.error().what());
    p.kind = static_cast<PortKind>(*kind);
    if (p.kind == PortKind::Audio) {
        const auto* ch = field(*o, "channels");
        if (!ch) return fail("audio port \"" + p.id + "\" needs \"channels\"");
        auto spec = parseChannels(*ch);
        if (!spec) return fail("port \"" + p.id + "\": " + spec.error().what());
        p.channels = *spec;
    }
    if (p.kind == PortKind::Resource) {
        auto type = stringField(*o, "resourceType");
        if (!type) return fail("resource port \"" + p.id + "\" needs \"resourceType\"");
        p.resourceType = *type;
        p.write = stringField(*o, "access", false).value_or(std::string()) == "write";
        if (p.write && p.dir != PortDir::In) return fail("port \"" + p.id + "\": only a resource input may have access \"write\"");
    }
    if ((p.kind == PortKind::Link) != (p.dir == PortDir::Link))
        return fail("port \"" + p.id + "\": link ports, and only link ports, have dir \"link\"");
    return p;
}

rfl::Result<ParamDesc> parseParam(const rfl::Generic& g) {
    auto o = g.to_object();
    if (!o) return fail("a param must be an object");
    ParamDesc p;
    auto id = stringField(*o, "id");
    if (!id) return fail(id.error().what());
    p.id = *id;
    p.name = stringField(*o, "name", false).value_or(p.id);
    if (p.name.empty()) p.name = p.id;
    p.unit = stringField(*o, "unit", false).value_or(std::string());
    auto type = stringField(*o, "type").and_then([](const std::string& s) { return enumIndex(s, kTypeNames, "param type"); });
    if (!type) return fail("param \"" + p.id + "\": " + type.error().what());
    p.type = static_cast<ParamType>(*type);
    const auto* def = field(*o, "default");
    switch (p.type) {
        case ParamType::Float:
        case ParamType::Int: {
            p.min = numberField(*o, "min", 0.0).value_or(0.0);
            p.max = numberField(*o, "max", 1.0).value_or(1.0);
            p.def = def ? def->to_double().value_or(p.min) : p.min;
            break;
        }
        case ParamType::Bool:
            p.min = 0.0;
            p.max = 1.0;
            p.def = def && def->to_bool().value_or(false) ? 1.0 : 0.0;
            break;
        case ParamType::Enum: {
            const auto* values = field(*o, "values");
            auto arr = values ? values->to_array() : rfl::Result<Array>(fail("enum needs values"));
            if (!arr || arr->empty()) return fail("enum param \"" + p.id + "\" needs a non-empty \"values\"");
            for (const auto& v : *arr) {
                auto s = v.to_string();
                if (!s) return fail("enum param \"" + p.id + "\": values must be strings");
                p.values.push_back(*s);
            }
            p.min = 0.0;
            p.max = static_cast<double>(p.values.size() - 1);
            const std::string label = def ? def->to_string().value_or(std::string()) : p.values[0];
            p.def = 0.0;
            for (std::size_t i = 0; i < p.values.size(); ++i)
                if (p.values[i] == label) p.def = static_cast<double>(i);
            break;
        }
    }
    return p;
}

rfl::Result<Layout> parseLayoutObject(const Object& o) {
    Layout layout;
    if (const auto* ports = field(o, "ports")) {
        auto arr = ports->to_array();
        if (!arr) return fail("\"ports\" must be an array");
        for (const auto& p : *arr) {
            auto port = parsePort(p);
            if (!port) return fail(port.error().what());
            layout.ports.push_back(std::move(*port));
        }
    }
    if (const auto* params = field(o, "params")) {
        auto arr = params->to_array();
        if (!arr) return fail("\"params\" must be an array");
        for (const auto& p : *arr) {
            auto param = parseParam(p);
            if (!param) return fail(param.error().what());
            layout.params.push_back(std::move(*param));
        }
    }
    if (const auto* exclusive = field(o, "exclusive")) {
        auto groups = exclusive->to_array();
        if (!groups) return fail("\"exclusive\" must be an array of arrays");
        for (const auto& g : *groups) {
            auto members = g.to_array();
            if (!members) return fail("\"exclusive\" must be an array of arrays");
            std::vector<std::string> group;
            for (const auto& m : *members) group.push_back(m.to_string().value_or(std::string()));
            layout.exclusive.push_back(std::move(group));
        }
    }
    for (std::size_t i = 0; i < layout.ports.size(); ++i)
        for (std::size_t j = i + 1; j < layout.ports.size(); ++j)
            if (layout.ports[i].id == layout.ports[j].id) return fail("duplicate port id \"" + layout.ports[i].id + "\"");
    for (const auto& param : layout.params)
        if (findPort(layout, param.id)) return fail("param \"" + param.id + "\" shares an id with a port");
    return layout;
}

}  // namespace

std::string_view kindName(PortKind kind) { return kKindNames[static_cast<int>(kind)]; }

std::string layoutJson(const Layout& layout) {
    Object o;
    insertLayout(o, layout);
    return canonicalJson(rfl::Generic(std::move(o)));
}

std::string descriptorJson(const TypeDescriptor& d) {
    Object o;
    put(o, "id", rfl::Generic(d.id));
    put(o, "version", rfl::Generic(d.version));
    put(o, "name", rfl::Generic(d.name));
    put(o, "category", rfl::Generic(d.category));
    Object needs;
    put(needs, "heap", rfl::Generic(d.heap));
    put(needs, "threads", rfl::Generic(d.threads));
    put(o, "requires", rfl::Generic(std::move(needs)));
    insertLayout(o, d.layout);
    auto schema = parseJson(d.settingsSchema.empty() ? std::string("{}") : d.settingsSchema);
    put(o, "settings", schema ? *schema : rfl::Generic(Object()));
    return canonicalJson(rfl::Generic(std::move(o)));
}

rfl::Result<Layout> parseLayout(std::string_view json) {
    auto g = parseJson(json);
    if (!g) return fail(std::string("layout is not JSON: ") + g.error().what());
    auto o = g->to_object();
    if (!o) return fail("layout must be an object");
    return parseLayoutObject(*o);
}

rfl::Result<TypeDescriptor> parseDescriptor(std::string_view json) {
    auto g = parseJson(json);
    if (!g) return fail(std::string("descriptor is not JSON: ") + g.error().what());
    auto o = g->to_object();
    if (!o) return fail("descriptor must be an object");
    TypeDescriptor d;
    auto id = stringField(*o, "id");
    if (!id) return fail(id.error().what());
    d.id = *id;
    d.version = stringField(*o, "version", false).value_or(std::string());
    d.name = stringField(*o, "name", false).value_or(d.id);
    d.category = stringField(*o, "category", false).value_or(std::string());
    if (const auto* req = field(*o, "requires")) {
        if (auto r = req->to_object()) {
            if (const auto* heap = field(*r, "heap")) d.heap = heap->to_bool().value_or(false);
            if (const auto* threads = field(*r, "threads")) d.threads = threads->to_bool().value_or(false);
        }
    }
    auto layout = parseLayoutObject(*o);
    if (!layout) return fail("descriptor \"" + d.id + "\": " + layout.error().what());
    d.layout = std::move(*layout);
    if (const auto* s = field(*o, "settings")) d.settingsSchema = canonicalJson(*s);
    return d;
}

const PortDesc* findPort(const Layout& layout, std::string_view id) {
    for (const auto& p : layout.ports)
        if (p.id == id) return &p;
    return nullptr;
}

const ParamDesc* findParam(const Layout& layout, std::string_view id) {
    for (const auto& p : layout.params)
        if (p.id == id) return &p;
    return nullptr;
}

bool matchesPattern(std::string_view pattern, std::string_view portId) {
    if (pattern.size() >= 2 && pattern.substr(pattern.size() - 2) == ".*")
        return portId.substr(0, pattern.size() - 1) == pattern.substr(0, pattern.size() - 1);
    return pattern == portId;
}

std::optional<std::uint32_t> layoutChannels(std::string_view layout) {
    if (layout == "mono") return 1;
    if (layout == "stereo") return 2;
    if (layout == "5.1") return 6;
    if (layout == "7.1") return 8;
    constexpr std::string_view kDiscrete = "discrete(";
    if (layout.size() > kDiscrete.size() && layout.substr(0, kDiscrete.size()) == kDiscrete && layout.back() == ')') {
        const auto digits = layout.substr(kDiscrete.size(), layout.size() - kDiscrete.size() - 1);
        std::uint32_t n = 0;
        const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), n);
        if (ec == std::errc() && ptr == digits.data() + digits.size() && n > 0) return n;
    }
    return std::nullopt;
}

std::string layoutForCount(std::uint32_t channels) {
    if (channels == 1) return "mono";
    if (channels == 2) return "stereo";
    return "discrete(" + std::to_string(channels) + ")";
}

}  // namespace rp::graph
