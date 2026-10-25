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

// An events or bytes port's queue: the most messages, and the most payload bytes, one block carries. A push past
// either is dropped and counted (rp_queue::dropped). A port may declare either part or both, as
// "capacity": {"messages": n, "bytes": n}; a part it leaves undeclared takes the default. A wire carries its
// source's queue (an output queue, or a feedback wire's front, which is sized from the source too), so where a
// wire's two ends declare different capacities the source's wins, and an input's own capacity bounds nothing.
constexpr std::uint32_t kDefaultQueueMessages = 256;
constexpr std::uint32_t kDefaultQueueBytes = 4096;
// The most a port may declare, so a typo cannot allocate gigabytes.
constexpr std::uint32_t kMaxQueueMessages = 1u << 20;  // 1 Mi
constexpr std::uint32_t kMaxQueueBytes = 16u << 20;    // 16 MiB

struct QueueCapacity {
    std::optional<std::uint32_t> messages;  // as declared: unset is the default, and is written as nothing
    std::optional<std::uint32_t> bytes;
    std::uint32_t messageCount() const { return messages.value_or(kDefaultQueueMessages); }
    std::uint32_t byteCount() const { return bytes.value_or(kDefaultQueueBytes); }
};

struct PortDesc {
    std::string   id;
    PortDir       dir  = PortDir::In;
    PortKind      kind = PortKind::Audio;
    ChannelSpec   channels;      // audio only
    std::string   resourceType;  // resource only
    bool          write = false; // resource input that writes the resource (one writer per mutable resource)
    QueueCapacity capacity;      // events / bytes only
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
// 1..2^32-1 or at odds with its layout name, an "any" output, a match that names no other audio port, a queue
// capacity past its cap or on a port with no queue, an id used twice across ports and params, an unknown truthy
// `requires` flag, ...). Nothing is coerced.
rfl::Result<Layout>         parseLayout(std::string_view json);
rfl::Result<TypeDescriptor> parseDescriptor(std::string_view json);

const PortDesc*  findPort(const Layout& layout, std::string_view id);
const ParamDesc* findParam(const Layout& layout, std::string_view id);

std::string_view kindName(PortKind kind);

// The most channels one audio port carries; the planner refuses a port that resolves to more.
constexpr std::uint32_t kMaxChannels = 64;

// Why a port's declared capacity cannot be used (it is on a port that is not events or bytes, or a part lies
// outside 1 to its kMaxQueue*), or nullopt. The parser refuses the same, in the same words; the C++ authoring
// layer asks it of every layout it builds, so a capacity adjust_layout sets from settings is checked too.
std::optional<std::string> checkCapacity(const PortDesc& port);

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
