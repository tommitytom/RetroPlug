#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <rfl/Result.hpp>

namespace rp::graph {

// The descriptor model: what a node type declares (TypeDescriptor) and what one instance resolves to
// for its settings (Layout). Both travel across the C ABI as canonical JSON (CanonicalJson.hpp), and
// this file is the one place that spells and parses that JSON on the C++ side.

enum class PortKind : std::uint8_t { Audio, Events, Bytes, Control, Link, Resource };
enum class PortDir : std::uint8_t { In, Out, Link };  // link ports are undirected

struct ChannelSpec {
    enum class Mode : std::uint8_t { Fixed, Any, Match };
    Mode          mode = Mode::Fixed;
    std::uint32_t count = 0;  // Fixed
    std::string   layout;     // Fixed: "mono", "stereo", "5.1", "discrete(6)", ... or "any" (count only)
    std::string   match;      // Match: the id of the port whose count and layout this one copies

    static ChannelSpec fixed(std::uint32_t n, std::string layoutName) { return {Mode::Fixed, n, std::move(layoutName), {}}; }
    static ChannelSpec any() { return {Mode::Any, 0, {}, {}}; }
    static ChannelSpec matching(std::string port) { return {Mode::Match, 0, {}, std::move(port)}; }
};

struct PortDesc {
    std::string id;
    PortDir     dir  = PortDir::In;
    PortKind    kind = PortKind::Audio;
    ChannelSpec channels;      // audio only
    std::string resourceType;  // resource only
    bool        write = false; // resource input that writes the resource (one writer per mutable resource)
};

enum class ParamType : std::uint8_t { Float, Int, Enum, Bool };

struct ParamDesc {
    std::string              id;
    std::string              name;
    ParamType                type = ParamType::Float;
    double                   min = 0.0, max = 1.0, def = 0.0;  // def is an index for an enum
    std::vector<std::string> values;                           // enum labels
    std::string              unit;
};

struct Layout {
    std::vector<PortDesc>                 ports;
    std::vector<ParamDesc>                params;
    std::vector<std::vector<std::string>> exclusive;  // groups of output-id patterns ("pins.*")
};

struct TypeDescriptor {
    std::string id, version, name, category;
    bool        heap = false, threads = false;
    Layout      layout;          // the layout for default settings
    std::string settingsSchema;  // canonical JSON Schema of the settings object
};

std::string layoutJson(const Layout& layout);
std::string descriptorJson(const TypeDescriptor& descriptor);

// The gate for a node's JSON, which may come from a downloaded pack: a missing field takes its default,
// and a field present with the wrong type or an unusable value is refused with a reason that names it
// (an inverted or excluded param range, an enum default not among its values, a channel count outside
// 1..2^32-1 or at odds with its layout name, an "any" output, a match that names no other audio port, an
// id used twice across ports and params, an unknown truthy `requires` flag, ...). Nothing is coerced.
rfl::Result<Layout>         parseLayout(std::string_view json);
rfl::Result<TypeDescriptor> parseDescriptor(std::string_view json);

const PortDesc*  findPort(const Layout& layout, std::string_view id);
const ParamDesc* findParam(const Layout& layout, std::string_view id);

std::string_view kindName(PortKind kind);

// The most channels one audio port carries; the planner refuses a port that resolves to more.
constexpr std::uint32_t kMaxChannels = 64;

// An id the host stores and compares as a string - a node type or package id, a resource type: non-empty,
// with no whitespace and no control character (no byte at or below 0x20, nor 0x7f). An embedded NUL in
// particular would make the C string a node reads differ from the std::string the host checked.
bool isPlainId(std::string_view s);

// Does an exclusive-group pattern ("pins.*", at least one character before the ".*", or an exact id)
// match this port id?
bool matchesPattern(std::string_view pattern, std::string_view portId);

// How many channels a layout name means: mono 1, stereo 2, 5.1 6, 7.1 8, discrete(n) n. nullopt for "any"
// or an unknown name.
std::optional<std::uint32_t> layoutChannels(std::string_view layout);

// The layout name a bare channel count (at least 1) gets: mono, stereo, else discrete(n).
std::string layoutForCount(std::uint32_t channels);

}  // namespace rp::graph
