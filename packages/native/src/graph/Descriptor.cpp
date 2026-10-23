#include "graph/Descriptor.hpp"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "graph/CanonicalJson.hpp"

namespace rp::graph {

namespace {

using Object = rfl::Generic::Object;
using Array  = rfl::Generic::Array;

const char* const kKindNames[] = {"audio", "events", "bytes", "control", "link", "resource"};
const char* const kDirNames[]  = {"in", "out", "link"};
const char* const kTypeNames[] = {"float", "int", "enum", "bool"};

constexpr const char* kNoSettingsSchema = R"({"type":"object"})";

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
    if (p.kind == PortKind::Resource) {
        put(o, "resourceType", rfl::Generic(p.resourceType));
        if (p.write) put(o, "access", rfl::Generic(std::string("write")));
    }
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
            put(o, "default", rfl::Generic(p.def >= 0.5));  // as a node reads it
            break;
        case ParamType::Enum: {
            Array values;
            for (const auto& v : p.values) values.emplace_back(v);
            put(o, "values", rfl::Generic(std::move(values)));
            // A hand-built ParamDesc can carry any index; only a real one names a label.
            const bool valid = p.def >= 0.0 && p.def < static_cast<double>(p.values.size());
            put(o, "default", rfl::Generic(valid ? p.values[static_cast<std::size_t>(p.def)] : std::string()));
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
//
// A descriptor or layout may come from a downloaded pack, so the parser is the gate: a field that is
// missing takes its documented default, and a field that is present with the wrong type or an unusable
// value is refused, naming where it was. Nothing is silently coerced.

rfl::Unexpected<rfl::Error> fail(const std::string& what) { return rfl::Unexpected<rfl::Error>(rfl::Error(what)); }

const rfl::Generic* field(const Object& o, std::string_view key) {
    for (const auto& [k, v] : o)
        if (k == key) return &v;
    return nullptr;
}

std::string q(std::string_view s) { return jsonString(s); }

// A field this parser does not know is refused, not ignored: a misspelt "acess" would otherwise declare a
// reader where a writer was meant, and "exclusve" would drop a group, both silently. (New fields come with
// a new descriptor version; `requires` is the one open set, see parseDescriptor.)
std::optional<std::string> unknownField(const Object& o, std::initializer_list<std::string_view> known,
                                        const std::string& where) {
    for (const auto& [k, v] : o)
        if (std::find(known.begin(), known.end(), k) == known.end()) return where + ": unknown field " + q(k);
    return std::nullopt;
}

rfl::Result<std::optional<std::string>> optString(const Object& o, std::string_view key, const std::string& where) {
    const auto* v = field(o, key);
    if (!v) return std::optional<std::string>();
    auto s = v->to_string();
    if (!s) return fail(where + ": " + q(key) + " must be a string");
    return std::optional<std::string>(std::move(*s));
}

rfl::Result<std::string> reqString(const Object& o, std::string_view key, const std::string& where) {
    auto s = optString(o, key, where);
    if (!s) return fail(s.error().what());
    if (!*s || (*s)->empty()) return fail(where + ": needs a non-empty " + q(key));
    return **s;
}

rfl::Result<std::optional<double>> optNumber(const Object& o, std::string_view key, const std::string& where) {
    const auto* v = field(o, key);
    if (!v) return std::optional<double>();
    auto d = v->to_double();
    if (!d || !std::isfinite(*d)) return fail(where + ": " + q(key) + " must be a finite number");
    return std::optional<double>(*d);
}

rfl::Result<std::optional<bool>> optBool(const Object& o, std::string_view key, const std::string& where) {
    const auto* v = field(o, key);
    if (!v) return std::optional<bool>();
    auto b = v->to_bool();
    if (!b) return fail(where + ": " + q(key) + " must be true or false");
    return std::optional<bool>(*b);
}

template <std::size_t N>
rfl::Result<int> enumIndex(const std::string& s, const char* const (&names)[N], std::string_view what,
                           const std::string& where) {
    for (std::size_t i = 0; i < N; ++i)
        if (s == names[i]) return static_cast<int>(i);
    return fail(where + ": unknown " + std::string(what) + " " + q(s));
}

rfl::Result<ChannelSpec> parseChannels(const rfl::Generic& g, const std::string& where) {
    auto o = g.to_object();
    if (!o) return fail(where + ": \"channels\" must be an object");
    if (auto bad = unknownField(*o, {"any", "match", "count", "layout"}, where + " channels")) return fail(*bad);
    const bool hasAny = field(*o, "any") != nullptr;
    const bool hasMatch = field(*o, "match") != nullptr;
    const bool hasCount = field(*o, "count") != nullptr;
    if (int(hasAny) + int(hasMatch) + int(hasCount) != 1)
        return fail(where + ": \"channels\" needs exactly one of any / match / count");
    if (hasAny) {
        auto any = optBool(*o, "any", where);
        if (!any) return fail(any.error().what());
        if (!**any) return fail(where + ": \"channels.any\" must be true");
        return ChannelSpec::any();
    }
    if (hasMatch) {
        auto m = reqString(*o, "match", where);
        if (!m) return fail(m.error().what());
        return ChannelSpec::matching(*m);
    }
    auto n = optNumber(*o, "count", where);
    if (!n) return fail(n.error().what());
    const double count = **n;
    if (count < 1.0 || count > static_cast<double>(std::numeric_limits<std::uint32_t>::max()) || count != std::floor(count))
        return fail(where + ": \"channels.count\" must be a whole number from 1 to 4294967295");
    auto layoutName = reqString(*o, "layout", where);
    if (!layoutName) return fail(layoutName.error().what());
    const auto known = layoutChannels(*layoutName);
    if (!known && layoutName->rfind("discrete(", 0) == 0)
        return fail(where + ": " + q(*layoutName) + " is not a discrete(n) layout; n is a whole number from 1, no leading zeros");
    if (known && *known != static_cast<std::uint32_t>(count))
        return fail(where + ": layout " + q(*layoutName) + " is " + std::to_string(*known) + " channels, not " +
                    jsNumber(count));
    return ChannelSpec::fixed(static_cast<std::uint32_t>(count), *layoutName);
}

// A port or param id: a plain id (it is quoted in messages and compared as a string) with no '*', which
// would make an exclusive-group pattern ambiguous.
std::optional<std::string> checkMemberId(const std::string& id, const std::string& at) {
    if (!isPlainId(id) || id.find('*') != std::string::npos)
        return at + ": id " + q(id) + " must not contain whitespace, control characters or '*'";
    return std::nullopt;
}

rfl::Result<PortDesc> parsePort(const rfl::Generic& g, std::size_t index) {
    const std::string at = "port #" + std::to_string(index);
    auto o = g.to_object();
    if (!o) return fail(at + " must be an object");
    if (auto bad = unknownField(*o, {"id", "dir", "kind", "channels", "resourceType", "access"}, at)) return fail(*bad);
    PortDesc p;
    auto id = reqString(*o, "id", at);
    if (!id) return fail(id.error().what());
    if (auto bad = checkMemberId(*id, at)) return fail(*bad);
    p.id = *id;
    const std::string where = "port " + q(p.id);
    auto dir = reqString(*o, "dir", where).and_then([&](const std::string& s) { return enumIndex(s, kDirNames, "dir", where); });
    if (!dir) return fail(dir.error().what());
    p.dir = static_cast<PortDir>(*dir);
    auto kind = reqString(*o, "kind", where).and_then([&](const std::string& s) { return enumIndex(s, kKindNames, "kind", where); });
    if (!kind) return fail(kind.error().what());
    p.kind = static_cast<PortKind>(*kind);
    if ((p.kind == PortKind::Link) != (p.dir == PortDir::Link))
        return fail(where + ": link ports, and only link ports, have dir \"link\"");

    const auto* ch = field(*o, "channels");
    if (p.kind == PortKind::Audio) {
        if (!ch) return fail(where + ": an audio port needs \"channels\"");
        auto spec = parseChannels(*ch, where);
        if (!spec) return fail(spec.error().what());
        p.channels = std::move(*spec);
        if (p.dir == PortDir::Out && p.channels.mode == ChannelSpec::Mode::Any)
            return fail(where + ": an output cannot be \"any\"; use match or a fixed count");
    } else if (ch) {
        return fail(where + ": only audio ports have \"channels\"");
    }

    const bool hasType = field(*o, "resourceType") != nullptr;
    const bool hasAccess = field(*o, "access") != nullptr;
    if (p.kind == PortKind::Resource) {
        auto type = reqString(*o, "resourceType", where);
        if (!type) return fail(type.error().what());
        if (!isPlainId(*type))  // as ResourceTable requires of every resource type
            return fail(where + ": \"resourceType\" " + q(*type) + " must not contain whitespace or control characters");
        p.resourceType = *type;
        auto access = optString(*o, "access", where);
        if (!access) return fail(access.error().what());
        if (*access && **access != "write") return fail(where + ": \"access\" can only be \"write\"");
        p.write = access->has_value();
        if (p.write && p.dir != PortDir::In) return fail(where + ": only a resource input may have access \"write\"");
    } else if (hasType || hasAccess) {
        return fail(where + ": only resource ports have \"resourceType\" or \"access\"");
    }
    return p;
}

rfl::Result<ParamDesc> parseParam(const rfl::Generic& g, std::size_t index) {
    const std::string at = "param #" + std::to_string(index);
    auto o = g.to_object();
    if (!o) return fail(at + " must be an object");
    if (auto bad = unknownField(*o, {"id", "name", "type", "min", "max", "default", "values", "unit"}, at)) return fail(*bad);
    ParamDesc p;
    auto id = reqString(*o, "id", at);
    if (!id) return fail(id.error().what());
    if (auto bad = checkMemberId(*id, at)) return fail(*bad);
    p.id = *id;
    const std::string where = "param " + q(p.id);
    auto name = optString(*o, "name", where);
    if (!name) return fail(name.error().what());
    p.name = (*name && !(*name)->empty()) ? **name : p.id;
    auto unit = optString(*o, "unit", where);
    if (!unit) return fail(unit.error().what());
    p.unit = unit->value_or(std::string());
    auto type = reqString(*o, "type", where).and_then([&](const std::string& s) { return enumIndex(s, kTypeNames, "type", where); });
    if (!type) return fail(type.error().what());
    p.type = static_cast<ParamType>(*type);

    switch (p.type) {
        case ParamType::Float:
        case ParamType::Int: {
            auto mn = optNumber(*o, "min", where);
            auto mx = optNumber(*o, "max", where);
            auto df = optNumber(*o, "default", where);
            if (!mn) return fail(mn.error().what());
            if (!mx) return fail(mx.error().what());
            if (!df) return fail(df.error().what());
            p.min = mn->value_or(0.0);
            p.max = mx->value_or(1.0);
            p.def = df->value_or(p.min);
            // The planner clamps every value into [min, max]; an inverted range would be undefined there.
            if (!(p.min <= p.max)) return fail(where + ": \"min\" must not exceed \"max\"");
            if (!(p.min <= p.def && p.def <= p.max)) return fail(where + ": \"default\" must lie within [min, max]");
            if (p.type == ParamType::Int &&
                (p.min != std::floor(p.min) || p.max != std::floor(p.max) || p.def != std::floor(p.def)))
                return fail(where + ": an int param's min, max and default must be whole numbers");
            break;
        }
        case ParamType::Bool: {
            auto df = optBool(*o, "default", where);
            if (!df) return fail(df.error().what());
            p.min = 0.0;
            p.max = 1.0;
            p.def = df->value_or(false) ? 1.0 : 0.0;
            break;
        }
        case ParamType::Enum: {
            const auto* values = field(*o, "values");
            auto arr = values ? values->to_array() : rfl::Result<Array>(fail(""));
            if (!arr || arr->empty()) return fail(where + ": an enum needs a non-empty \"values\" array");
            std::unordered_set<std::string> seen;
            for (const auto& v : *arr) {
                auto s = v.to_string();
                if (!s || s->empty()) return fail(where + ": enum values must be non-empty strings");
                if (!seen.insert(*s).second) return fail(where + ": enum value " + q(*s) + " is listed twice");
                p.values.push_back(*s);
            }
            auto df = optString(*o, "default", where);
            if (!df) return fail(df.error().what());
            p.min = 0.0;
            p.max = static_cast<double>(p.values.size() - 1);
            p.def = 0.0;
            if (*df) {
                bool found = false;
                for (std::size_t i = 0; i < p.values.size(); ++i)
                    if (p.values[i] == **df) {
                        p.def = static_cast<double>(i);
                        found = true;
                    }
                if (!found) return fail(where + ": default " + q(**df) + " is not one of its values");
            }
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
        for (std::size_t i = 0; i < arr->size(); ++i) {
            auto port = parsePort((*arr)[i], i);
            if (!port) return fail(port.error().what());
            layout.ports.push_back(std::move(*port));
        }
    }
    if (const auto* params = field(o, "params")) {
        auto arr = params->to_array();
        if (!arr) return fail("\"params\" must be an array");
        for (std::size_t i = 0; i < arr->size(); ++i) {
            auto param = parseParam((*arr)[i], i);
            if (!param) return fail(param.error().what());
            layout.params.push_back(std::move(*param));
        }
    }
    std::unordered_map<std::string_view, const PortDesc*> portsById;
    std::unordered_set<std::string_view> paramIds;
    for (const auto& p : layout.ports)
        if (!portsById.emplace(p.id, &p).second) return fail(q(p.id) + " is used by more than one port or param");
    for (const auto& p : layout.params)
        if (portsById.count(p.id) || !paramIds.insert(p.id).second)
            return fail(q(p.id) + " is used by more than one port or param");
    auto port = [&](std::string_view id) -> const PortDesc* {
        const auto it = portsById.find(id);
        return it == portsById.end() ? nullptr : it->second;
    };
    // Checks that need the whole port list: what a match names, and what an exclusive group names.
    for (const auto& p : layout.ports) {
        if (p.kind != PortKind::Audio || p.channels.mode != ChannelSpec::Mode::Match) continue;
        const PortDesc* target = port(p.channels.match);
        if (!target || target == &p || target->kind != PortKind::Audio)
            return fail("port " + q(p.id) + ": matches " + q(p.channels.match) + ", which is not another audio port");
    }
    if (const auto* exclusive = field(o, "exclusive")) {
        auto groups = exclusive->to_array();
        if (!groups) return fail("\"exclusive\" must be an array of arrays");
        for (const auto& g : *groups) {
            auto members = g.to_array();
            if (!members || members->empty()) return fail("\"exclusive\" groups must be non-empty arrays");
            std::vector<std::string> group;
            for (const auto& m : *members) {
                auto s = m.to_string();
                if (!s || s->empty()) return fail("\"exclusive\" members must be non-empty strings");
                const bool wildcard = s->size() > 2 && s->compare(s->size() - 2, 2, ".*") == 0;
                const PortDesc* exact = wildcard ? nullptr : port(*s);
                if (!wildcard && (!exact || exact->dir != PortDir::Out))
                    return fail("exclusive member " + q(*s) + " must be an output's id or a \"prefix.*\" pattern");
                // Members must not overlap: one connected port matching two of them would be reported as
                // two members in use at once.
                for (const auto& other : group) {
                    const std::string_view a = *s, b = other;
                    const bool aw = wildcard, bw = b.size() > 2 && b.substr(b.size() - 2) == ".*";
                    const bool overlap = a == b || (aw && matchesPattern(a, bw ? b.substr(0, b.size() - 1) : b)) ||
                                         (bw && matchesPattern(b, aw ? a.substr(0, a.size() - 1) : a));
                    if (overlap) return fail("exclusive members " + q(other) + " and " + q(*s) + " overlap");
                }
                group.push_back(*s);
            }
            layout.exclusive.push_back(std::move(group));
        }
    }
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
    // A node without settings declares an empty object schema, the same spelling the TS side writes.
    auto schema = parseJson(d.settingsSchema.empty() ? std::string(kNoSettingsSchema) : d.settingsSchema);
    assert(schema && "a TypeDescriptor's settingsSchema is JSON");
    put(o, "settings", schema ? *schema : *parseJson(kNoSettingsSchema));
    return canonicalJson(rfl::Generic(std::move(o)));
}

rfl::Result<Layout> parseLayout(std::string_view json) {
    auto g = parseJson(json);
    if (!g) return fail(std::string("layout is not JSON: ") + g.error().what());
    auto o = g->to_object();
    if (!o) return fail("layout must be an object");
    if (auto bad = unknownField(*o, {"ports", "params", "exclusive"}, "layout")) return fail(*bad);
    return parseLayoutObject(*o);
}

rfl::Result<TypeDescriptor> parseDescriptor(std::string_view json) {
    auto g = parseJson(json);
    if (!g) return fail(std::string("descriptor is not JSON: ") + g.error().what());
    auto o = g->to_object();
    if (!o) return fail("descriptor must be an object");
    if (auto bad = unknownField(*o, {"id", "version", "name", "category", "requires", "ports", "params", "exclusive", "settings"},
                                "descriptor"))
        return fail(*bad);
    TypeDescriptor d;
    auto id = reqString(*o, "id", "descriptor");
    if (!id) return fail(id.error().what());
    if (!isPlainId(*id)) return fail("descriptor id " + q(*id) + " must not contain whitespace or control characters");
    d.id = *id;
    const std::string where = "descriptor " + q(d.id);
    auto version = optString(*o, "version", where);
    auto name = optString(*o, "name", where);
    auto category = optString(*o, "category", where);
    if (!version) return fail(version.error().what());
    if (!name) return fail(name.error().what());
    if (!category) return fail(category.error().what());
    d.version = version->value_or(std::string());
    d.name = (*name && !(*name)->empty()) ? **name : d.id;
    d.category = category->value_or(std::string());
    if (const auto* req = field(*o, "requires")) {
        auto r = req->to_object();
        if (!r) return fail(where + ": \"requires\" must be an object");
        for (const auto& [flag, value] : *r) {
            auto on = value.to_bool();
            if (!on) return fail(where + ": requires." + flag + " must be true or false");
            if (flag == "heap") d.heap = *on;
            else if (flag == "threads") d.threads = *on;
            else if (*on) return fail(where + ": requires " + q(flag) + ", which this host does not provide");
        }
    }
    auto layout = parseLayoutObject(*o);
    if (!layout) return fail(where + ": " + layout.error().what());
    d.layout = std::move(*layout);
    if (const auto* s = field(*o, "settings")) {
        if (!s->to_object()) return fail(where + ": \"settings\" must be a JSON Schema object");
        d.settingsSchema = canonicalJson(*s);
    } else {
        d.settingsSchema = kNoSettingsSchema;
    }
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

bool isPlainId(std::string_view s) {
    if (s.empty()) return false;
    for (const char c : s) {
        const auto b = static_cast<unsigned char>(c);
        if (b <= 0x20 || b == 0x7f) return false;
    }
    return true;
}

bool matchesPattern(std::string_view pattern, std::string_view portId) {
    if (pattern.size() > 2 && pattern.substr(pattern.size() - 2) == ".*")  // as the exclusive parser reads it
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
        if (digits.size() > 1 && digits[0] == '0') return std::nullopt;  // one spelling per count
        std::uint32_t n = 0;
        const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), n);
        if (ec == std::errc() && ptr == digits.data() + digits.size() && n > 0) return n;
    }
    return std::nullopt;
}

std::string layoutForCount(std::uint32_t channels) {
    assert(channels > 0 && "a port has at least one channel");
    if (channels == 1) return "mono";
    if (channels == 2) return "stereo";
    return "discrete(" + std::to_string(channels) + ")";
}

}  // namespace rp::graph
