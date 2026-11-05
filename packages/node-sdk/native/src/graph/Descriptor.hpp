#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <rfl/Result.hpp>

#include "graph/PortTypes.hpp"

namespace rp::graph {

// The descriptor model: what a node type declares (TypeDescriptor) and what one instance resolves to
// for its settings (Layout). Both travel across the C ABI as canonical JSON (CanonicalJson.hpp), and
// this file is the one place that spells and parses that JSON on the C++ side. The port kinds, the param types and
// the bounds a port is sized by (the queue's default and most, kMaxChannels) are PortTypes.hpp's, which a device-mode
// node reads with no descriptor model at all.

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

// An output's declared queue capacity (PortTypes.hpp says what one bounds). Either a size, or `match`: the id of one of
// the node's own inputs of the output's kind (events or bytes), whose queue this output's is sized as, the capacity of
// the output wired into it (a pass-through, such as rp.core.midi-filter, then drops nothing its source could carry).
// The planner resolves a match from the wires (an unwired input's is the default); a layout keeps it as declared.
struct QueueCapacity {
    std::optional<std::uint32_t> messages;  // as declared: unset is the default, and is written as nothing
    std::optional<std::uint32_t> bytes;
    std::string                  match;  // the input sized as, or empty; never with a size
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
    QueueCapacity capacity;      // events / bytes outputs only
};

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
    // At most one node of this type in a plan, which the planner refuses a second of: a type that binds a host-owned
    // device declares it, since two nodes would share the one link. Written only when true, so a type that is not
    // unique spells exactly what it did.
    bool        unique = false;
    Layout      layout;          // the layout for default settings
    std::string settingsSchema;  // canonical JSON Schema of the settings object
};

std::string layoutJson(const Layout& layout);
std::string descriptorJson(const TypeDescriptor& descriptor);

// The gate for a node's JSON, which may come from a downloaded pack: a missing field takes its default,
// and a field present with the wrong type or an unusable value is refused with a reason that names it
// (an inverted or excluded param range, an enum default not among its values, a channel count outside
// 1..2^32-1 or at odds with its layout name, an "any" output, a match that names no other audio port, a queue
// capacity past its cap or on a port with no queue of its own (an input, or one not events or bytes), an id used
// twice across ports and params, an unknown truthy `requires` flag, ...). Nothing is coerced.
rfl::Result<Layout>         parseLayout(std::string_view json);
rfl::Result<TypeDescriptor> parseDescriptor(std::string_view json);

const PortDesc*  findPort(const Layout& layout, std::string_view id);
const ParamDesc* findParam(const Layout& layout, std::string_view id);

std::string_view kindName(PortKind kind);

// Why a channel count outside 1..kMaxChannels cannot be used, or nullopt. `what` names the thing in the message ("a
// delay", "inputs together"), which is the part of a settings refusal a document's author reads.
std::optional<std::string> checkChannels(std::uint64_t n, std::string_view what);

// Why a port's declared capacity cannot be used (it is on a port that is not events or bytes, or on an input, or a
// part lies outside 1 to its kMaxQueue*), or nullopt. The parser refuses the same, in the same words; the C++
// authoring layer asks it of every layout it builds, so a capacity adjust_layout sets from settings is checked too.
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

// Why a layout name cannot carry `channels` channels (an unknown name, or one that means another count), or nullopt.
// The settings refusal of every node that names its output's layout beside a channel count.
std::optional<std::string> checkLayout(std::string_view layout, std::uint32_t channels);

// The layout name a bare channel count (at least 1) gets: mono, stereo, else discrete(n).
std::string layoutForCount(std::uint32_t channels);

}  // namespace rp::graph
