#pragma once

// The C++ authoring layer: write a node as a plain struct, get the C ABI table for free.
//
//   struct Gain {
//       static constexpr const char* id = "rp.core.gain";
//       struct { rp::audio_in<rp::any> in; }                          inputs;
//       struct { rp::audio_out<rp::match<"in">> out; }                outputs;
//       struct { rp::float_param<"Gain", rp::range{-60, 12, 0}, "dB"> gain; } params;
//       void process(const rp::Block& b) noexcept;
//   };
//   const rp_node_type* t = rp::node_type<Gain>();
//
// Field names become port and param ids (reflect-cpp walks the three structs); the wrapper types carry
// the rest. The member names `inputs`, `outputs` and `params` are reserved for those structs: a node that
// keeps a count in a member called `inputs` would be reflected as if it were its port list. A port in the
// wrong struct (an output in `inputs`) is a compile error.
//
// Optional members a node may define. Each is detected by NAME, and a member FUNCTION with one of these
// names has its exact signature checked at compile time, so a near miss is an error rather than a hook
// silently ignored. (A data member of the same name - a sequencer's `step` position - is not a hook and is
// fine. Do not overload a hook name: an overload set cannot be inspected, so it would not be checked.)
//
//   using Settings = SomeNamedStruct;                     reflect-cpp-readable, in a NAMED namespace (its
//                                                         JSON Schema $defs key comes from the type name)
//   static std::optional<std::string> validate(const Settings&);      reject settings with a reason
//   static void adjust_layout(rp::graph::Layout&, const Settings&);   patch reflected specs (a queue's capacity
//                                                         among them), append ports or params; never remove or
//                                                         reorder reflected ones
//   static bool needs_rebuild(const Settings& from, const Settings& to);
//   static std::size_t extra_memory(const Settings&);     host-supplied tail memory (max_align_t aligned)
//   void attach(std::span<std::byte>) noexcept;           receives it, at create
//   void on_settings(const Settings&) noexcept;           at create (control) and each commit (audio): copy,
//                                                         never allocate
//   void bind_host(const rp_host*) noexcept;              at create: host services
//   bool activate(double sampleRate, std::uint32_t maxFrames);   (or void)   void deactivate();
//   void ports_changed(std::span<const std::uint8_t> connectedOutputs) noexcept;   (the span lives for the call)
//   void prepare(const rp::Block&) noexcept / step(std::uint32_t target) noexcept / void finish(const rp::Block&) noexcept
//       -> rp.step/1 (all three or none; step returns bool - true once the block is produced - or an rp_step_result)
//   void set_peers(std::span<rp_node* const>) noexcept    -> rp.link/1   (the span lives for the call)
//   static constexpr const char *name, *category, *version;  static constexpr bool heap, threads;
//   static constexpr bool reshapes;                       keep the instance through a settings change that alters
//                                                         its layout (ports or params adjust_layout appends): prepare
//                                                         answers RP_PREPARE_RESHAPE, the planner rebinds the ports
//                                                         by id, and on_settings sees the new settings at the swap.
//                                                         Default false: such a change rebuilds
//   static constexpr bool stateless_prepare;              prepare reads nothing a commit writes, so setGraph never
//                                                         waits for this node's commits (RP_TYPE_STATELESS_PREPARE);
//                                                         rules out needs_rebuild, which reads the current settings
//
// process(const rp::Block&) is required, noexcept, and returns void or rp_status. Exceptions never cross
// the ABI: every control-thread entry catches, and every audio-thread entry is noexcept.

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <rfl/DefaultIfMissing.hpp>
#include <rfl/enums.hpp>
#include <rfl/json.hpp>
#include <rfl/named_tuple_t.hpp>
#include <rfl/num_fields.hpp>
#include <rfl/to_view.hpp>

#include "graph/CanonicalJson.hpp"
#include "graph/Descriptor.hpp"
#include "graph/Queue.hpp"
#include "graph/rp_node.h"

namespace rp {

template <std::size_t N>
struct fixed_string {
    char v[N]{};
    constexpr fixed_string(const char (&s)[N]) {
        for (std::size_t i = 0; i < N; ++i) v[i] = s[i];
    }
    constexpr std::string_view view() const { return {v, N - 1}; }
};

// ---- channel specs -----------------------------------------------------------------------------------

struct any {};
template <fixed_string Port> struct match {};
template <std::uint32_t N, fixed_string LayoutName> struct channels {};
using mono   = channels<1, "mono">;
using stereo = channels<2, "stereo">;

namespace detail {
template <class S> struct ChannelSpecOf;
template <> struct ChannelSpecOf<any> {
    static graph::ChannelSpec get() { return graph::ChannelSpec::any(); }
};
template <fixed_string P> struct ChannelSpecOf<match<P>> {
    static graph::ChannelSpec get() { return graph::ChannelSpec::matching(std::string(P.view())); }
};
template <std::uint32_t N, fixed_string L> struct ChannelSpecOf<channels<N, L>> {
    static graph::ChannelSpec get() { return graph::ChannelSpec::fixed(N, std::string(L.view())); }
};
}  // namespace detail

// ---- ports ---------------------------------------------------------------------------------------------

template <class Spec>
struct audio_in {
    static constexpr auto rp_kind = graph::PortKind::Audio;
    static constexpr auto rp_dir  = graph::PortDir::In;
    using rp_channels = Spec;
    const float* const* channels = nullptr;
    std::uint32_t       channel_count = 0;
    const float* operator[](std::uint32_t c) const { return channels[c]; }
};

template <class Spec>
struct audio_out {
    static constexpr auto rp_kind = graph::PortKind::Audio;
    static constexpr auto rp_dir  = graph::PortDir::Out;
    using rp_channels = Spec;
    float* const* channels = nullptr;
    std::uint32_t channel_count = 0;
    float* operator[](std::uint32_t c) const { return channels[c]; }
};

// An events or bytes output's queue capacity (graph::QueueCapacity): the most messages, and payload bytes, one block
// carries. A part left 0 takes the default (graph::kDefaultQueueMessages / kDefaultQueueBytes). It rides on the
// port's type, rp::bytes_out::sized<rp::capacity{.bytes = 65536}>, or adjust_layout sets it from settings. A wire
// carries its source's queue, so only an output has one: an input has no `sized` (naming it is a compile error),
// and a capacity adjust_layout sets on one is refused with the node's settings.
struct capacity {
    std::uint32_t messages = 0;
    std::uint32_t bytes = 0;
};

template <graph::PortKind Kind>
struct queue_in {
    static constexpr auto rp_kind = Kind;
    static constexpr auto rp_dir  = graph::PortDir::In;
    const rp_queue* queue = nullptr;
    std::span<const rp_message> messages() const { return {queue->items, queue->count}; }
    std::span<const std::uint8_t> data(const rp_message& m) const { return graph::messageData(*queue, m); }
};

template <graph::PortKind Kind, capacity C = capacity{}>
struct queue_out {
    static_assert(C.messages <= graph::kMaxQueueMessages && C.bytes <= graph::kMaxQueueBytes,
                  "a queue capacity is at most graph::kMaxQueueMessages messages and graph::kMaxQueueBytes bytes");
    static constexpr auto     rp_kind = Kind;
    static constexpr auto     rp_dir  = graph::PortDir::Out;
    static constexpr capacity rp_capacity = C;
    template <capacity N> using sized = queue_out<Kind, N>;
    rp_queue* queue = nullptr;
    bool push(std::uint32_t frame, std::span<const std::uint8_t> data, std::uint32_t flags = 0) const {
        return graph::pushMessage(*queue, frame, data, flags);
    }
};

using events_in  = queue_in<graph::PortKind::Events>;
using events_out = queue_out<graph::PortKind::Events>;
using bytes_in   = queue_in<graph::PortKind::Bytes>;
using bytes_out  = queue_out<graph::PortKind::Bytes>;

struct control_in {
    static constexpr auto rp_kind = graph::PortKind::Control;
    static constexpr auto rp_dir  = graph::PortDir::In;
    const rp_control_queue* queue = nullptr;
    double start() const { return queue->start; }
    std::span<const rp_control_change> changes() const { return {queue->changes, queue->count}; }
};

struct control_out {
    static constexpr auto rp_kind = graph::PortKind::Control;
    static constexpr auto rp_dir  = graph::PortDir::Out;
    rp_control_queue* queue = nullptr;
    bool push(std::uint32_t frame, double value) const { return graph::pushControl(*queue, frame, value); }
};

template <fixed_string Type>
struct resource_in {
    static constexpr auto rp_kind = graph::PortKind::Resource;
    static constexpr auto rp_dir  = graph::PortDir::In;
    static constexpr std::string_view rp_resource_type() { return Type.view(); }
    rp_resource* const* slot = nullptr;
    const rp_resource* get() const { return slot ? *slot : nullptr; }
};

// A resource input the node WRITES (a record buffer). At most one write port may be bound to a given
// mutable resource, and readers must be wired from the writer's output so the planner orders them.
template <fixed_string Type>
struct resource_write {
    static constexpr auto rp_kind = graph::PortKind::Resource;
    static constexpr auto rp_dir  = graph::PortDir::In;
    static constexpr bool rp_write = true;
    static constexpr std::string_view rp_resource_type() { return Type.view(); }
    rp_resource* const* slot = nullptr;
    rp_resource* get() const { return slot ? *slot : nullptr; }
};

template <fixed_string Type>
struct resource_out {
    static constexpr auto rp_kind = graph::PortKind::Resource;
    static constexpr auto rp_dir  = graph::PortDir::Out;
    static constexpr std::string_view rp_resource_type() { return Type.view(); }
    rp_resource** slot = nullptr;
    void set(rp_resource* r) const { *slot = r; }
};

struct link {
    static constexpr auto rp_kind = graph::PortKind::Link;
    static constexpr auto rp_dir  = graph::PortDir::Link;
};

// ---- params ----------------------------------------------------------------------------------------------

struct range {
    double min = 0.0, max = 1.0, init = 0.0;
};

namespace detail {
struct param_base {
    const rp_control_queue* queue = nullptr;
    // The value at block start, and this block's frame-stamped changes (sample-accurate nodes walk them).
    double start() const { return queue->start; }
    std::span<const rp_control_change> changes() const { return {queue->changes, queue->count}; }
    double latest() const { return graph::controlEnd(*queue); }
};
}  // namespace detail

template <fixed_string Name, range R = range{}, fixed_string Unit = "">
struct float_param : detail::param_base {
    static_assert(R.min <= R.max && R.min <= R.init && R.init <= R.max, "a param range needs min <= init <= max");
    static graph::ParamDesc rp_param(std::string id) {
        return {std::move(id), std::string(Name.view()), graph::ParamType::Float, R.min, R.max, R.init, {},
                std::string(Unit.view())};
    }
};

template <fixed_string Name, range R = range{}, fixed_string Unit = "">
struct int_param : detail::param_base {
    static_assert(R.min <= R.max && R.min <= R.init && R.init <= R.max && R.min >= -2147483648.0 &&
                      R.max <= 2147483647.0 && R.min == static_cast<double>(static_cast<int>(R.min)) &&
                      R.max == static_cast<double>(static_cast<int>(R.max)) &&
                      R.init == static_cast<double>(static_cast<int>(R.init)),
                  "an int param's range is whole numbers with min <= init <= max, within int");
    static graph::ParamDesc rp_param(std::string id) {
        return {std::move(id), std::string(Name.view()), graph::ParamType::Int, R.min, R.max, R.init, {},
                std::string(Unit.view())};
    }
    // The value at block start, rounded to the nearest int (half away from zero); NaN reads as the declared
    // default and anything beyond int saturates. It is NOT clamped to the declared range: a wire can carry
    // any double and adjust_layout may widen the range, so a node clamps to its own resolved range where
    // that matters (Delay does).
    int value() const {
        const double v = start();
        if (!(v == v)) return static_cast<int>(R.init);
        if (v <= -2147483648.0) return std::numeric_limits<int>::min();
        if (v >= 2147483647.0) return std::numeric_limits<int>::max();
        return static_cast<int>(v + (v < 0 ? -0.5 : 0.5));
    }
};

template <fixed_string Name, bool Default = false>
struct bool_param : detail::param_base {
    static graph::ParamDesc rp_param(std::string id) {
        return {std::move(id), std::string(Name.view()), graph::ParamType::Bool, 0.0, 1.0, Default ? 1.0 : 0.0, {}, {}};
    }
    bool value() const { return start() >= 0.5; }  // NaN reads as false
};

namespace detail {
// Enumerator names become kebab-case labels - MidiSync -> "midi-sync", HighPassTPDF -> "high-pass-tpdf",
// N8Fifo -> "n8-fifo" - so C++ and TS enums share one label convention in saved state.
inline std::string kebabCase(std::string_view name) {
    std::string out;
    auto upper = [](char c) { return c >= 'A' && c <= 'Z'; };
    auto lowerOrDigit = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (i > 0 && upper(c)) {
            const bool afterLower = lowerOrDigit(name[i - 1]);
            const bool acronymEnd = upper(name[i - 1]) && i + 1 < name.size() && lowerOrDigit(name[i + 1]) &&
                                    !(name[i + 1] >= '0' && name[i + 1] <= '9');
            if (afterLower || acronymEnd) out += '-';
        }
        out += upper(c) ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return out;
}

template <class E, E Value>
constexpr bool isEnumerator() {
    for (const auto& entry : rfl::get_enumerator_array<E>())
        if (entry.second == Value) return true;
    return false;
}
}  // namespace detail

// An enum param: labels are the enumerator names in kebab-case, the runtime value is the index, rounded
// and clamped so any control can drive it (NaN reads as the default).
template <class E, fixed_string Name, E Default>
struct enum_param : detail::param_base {
    static constexpr auto kEnumerators = rfl::get_enumerator_array<E>();
    static_assert(kEnumerators.size() > 0, "an enum param needs at least one enumerator");
    static_assert(detail::isEnumerator<E, Default>(), "an enum param's default must be one of its enumerators");

    static graph::ParamDesc rp_param(std::string id) {
        graph::ParamDesc d{std::move(id), std::string(Name.view()), graph::ParamType::Enum, 0.0, 0.0, 0.0, {}, {}};
        for (const auto& [label, e] : kEnumerators) {
            if (e == Default) d.def = static_cast<double>(d.values.size());
            d.values.push_back(detail::kebabCase(label));
        }
        d.max = static_cast<double>(d.values.size() - 1);
        return d;
    }
    E value() const {
        const double v = start();
        if (!(v == v)) return Default;
        const auto last = kEnumerators.size() - 1;
        std::size_t i = 0;
        if (v >= static_cast<double>(last)) i = last;
        else if (v > 0.0) i = static_cast<std::size_t>(v + 0.5);
        return kEnumerators[i].second;
    }
};

// ---- the block -------------------------------------------------------------------------------------------

struct Block {
    const rp_process& raw;
    std::uint32_t frames() const { return raw.frames; }
    const rp_transport& transport() const { return *raw.transport; }
    // Ports and params beyond the reflected ones (appended by adjust_layout) are reached by index here.
    const rp_port_view& input(std::uint32_t i) const { return raw.inputs[i]; }
    const rp_port_view& output(std::uint32_t i) const { return raw.outputs[i]; }
    const rp_control_queue& param(std::uint32_t i) const { return *raw.params[i]; }
};

// ---- reflection glue ---------------------------------------------------------------------------------------

namespace detail {

struct NoSettings {};
struct NoPorts {};

template <class T> struct SettingsOf { using type = NoSettings; };
template <class T> requires requires { typename T::Settings; } struct SettingsOf<T> { using type = typename T::Settings; };

template <class T> constexpr bool reshapesOf() {
    if constexpr (requires { T::reshapes; }) return T::reshapes; else return false;
}
template <class T> constexpr bool statelessPrepareOf() {
    if constexpr (requires { T::stateless_prepare; }) return T::stateless_prepare; else return false;
}

template <class T> auto& inputsOf(T& t) { if constexpr (requires { t.inputs; }) return t.inputs; else { static NoPorts none; return none; } }
template <class T> auto& outputsOf(T& t) { if constexpr (requires { t.outputs; }) return t.outputs; else { static NoPorts none; return none; } }
template <class T> auto& paramsOf(T& t) { if constexpr (requires { t.params; }) return t.params; else { static NoPorts none; return none; } }

// The one place fields are walked (design 4.7's fields_of seam): swapping reflect-cpp's field reflection
// for another one (standalone PFR) touches only these three.
template <class S> using FieldsOf = typename rfl::named_tuple_t<S>::Fields;
template <class S> inline constexpr std::size_t kFieldCount = rfl::num_fields<S>;
template <class S, class F>
void forEachField(S& s, F&& f) noexcept {
    rfl::to_view(s).apply([&](auto& field) { f(*field.value()); });
}

enum class Side { Inputs, Outputs };

template <Side W, class Field>
graph::PortDesc portDesc() {
    using P = std::remove_cvref_t<typename Field::Type>;
    if constexpr (W == Side::Inputs) static_assert(P::rp_dir != graph::PortDir::Out, "a port in `inputs` must be an input or a link");
    else static_assert(P::rp_dir == graph::PortDir::Out, "a port in `outputs` must be an output");
    graph::PortDesc d;
    d.id = std::string(Field::name());
    d.kind = P::rp_kind;
    d.dir = P::rp_dir;
    if constexpr (requires { typename P::rp_channels; }) d.channels = ChannelSpecOf<typename P::rp_channels>::get();
    if constexpr (requires { P::rp_resource_type(); }) d.resourceType = std::string(P::rp_resource_type());
    if constexpr (requires { P::rp_write; }) d.write = P::rp_write;
    if constexpr (requires { P::rp_capacity; }) {
        if constexpr (P::rp_capacity.messages != 0) d.capacity.messages = P::rp_capacity.messages;
        if constexpr (P::rp_capacity.bytes != 0) d.capacity.bytes = P::rp_capacity.bytes;
    }
    return d;
}

template <class Field>
graph::ParamDesc paramDesc() {
    using P = std::remove_cvref_t<typename Field::Type>;
    return P::rp_param(std::string(Field::name()));
}

template <Side W, class S, std::size_t... I>
void appendPorts(std::vector<graph::PortDesc>& out, std::index_sequence<I...>) {
    (out.push_back(portDesc<W, rfl::tuple_element_t<I, FieldsOf<S>>>()), ...);
}

template <class S, std::size_t... I>
void appendParams(std::vector<graph::ParamDesc>& out, std::index_sequence<I...>) {
    (out.push_back(paramDesc<rfl::tuple_element_t<I, FieldsOf<S>>>()), ...);
}

template <Side W, class S>
void appendPortsOf(std::vector<graph::PortDesc>& out) {
    if constexpr (!std::is_same_v<S, NoPorts>) appendPorts<W, S>(out, std::make_index_sequence<kFieldCount<S>>{});
}

template <class S>
void appendParamsOf(std::vector<graph::ParamDesc>& out) {
    if constexpr (!std::is_same_v<S, NoPorts>) appendParams<S>(out, std::make_index_sequence<kFieldCount<S>>{});
}

template <class T>
graph::Layout reflectedLayout() {
    using In  = std::remove_cvref_t<decltype(inputsOf(std::declval<T&>()))>;
    using Out = std::remove_cvref_t<decltype(outputsOf(std::declval<T&>()))>;
    using Par = std::remove_cvref_t<decltype(paramsOf(std::declval<T&>()))>;
    graph::Layout l;
    appendPortsOf<Side::Inputs, In>(l.ports);
    appendPortsOf<Side::Outputs, Out>(l.ports);
    appendParamsOf<Par>(l.params);
    return l;
}

// Reflected ports and params bind by position, so adjust_layout may patch their specs and append, but each
// reflected one must keep its place, id, kind and direction. Throws (which every caller reports as invalid
// settings) when it did not.
inline void checkReflectedPrefix(const graph::Layout& reflected, const graph::Layout& adjusted) {
    auto side = [](const graph::Layout& l, bool outputs) {
        std::vector<const graph::PortDesc*> v;
        for (const auto& p : l.ports)
            if ((p.dir == graph::PortDir::Out) == outputs) v.push_back(&p);
        return v;
    };
    for (const bool outputs : {false, true}) {
        const auto want = side(reflected, outputs);
        const auto got = side(adjusted, outputs);
        for (std::size_t i = 0; i < want.size(); ++i)
            if (i >= got.size() || got[i]->id != want[i]->id || got[i]->kind != want[i]->kind || got[i]->dir != want[i]->dir)
                throw std::logic_error("adjust_layout moved, removed or re-kinded reflected port \"" + want[i]->id + "\"");
    }
    // An enum's labels too: enum_param::value() maps the index back through its compile-time list.
    for (std::size_t i = 0; i < reflected.params.size(); ++i)
        if (i >= adjusted.params.size() || adjusted.params[i].id != reflected.params[i].id ||
            adjusted.params[i].type != reflected.params[i].type ||
            (reflected.params[i].type == graph::ParamType::Enum && adjusted.params[i].values != reflected.params[i].values))
            throw std::logic_error("adjust_layout moved, removed, re-typed or relabelled reflected param \"" + reflected.params[i].id + "\"");
}

// Inputs first, then outputs, each in declaration order - adjust_layout appends after the reflected ones.
template <class T>
graph::Layout layoutFor([[maybe_unused]] const typename SettingsOf<T>::type& settings) {
    const graph::Layout reflected = reflectedLayout<T>();
    graph::Layout l = reflected;
    if constexpr (requires(graph::Layout& lr) { T::adjust_layout(lr, settings); }) T::adjust_layout(l, settings);
    std::stable_partition(l.ports.begin(), l.ports.end(),
                          [](const graph::PortDesc& p) { return p.dir != graph::PortDir::Out; });
    checkReflectedPrefix(reflected, l);
    // A capacity adjust_layout set from settings is refused here, so it is the settings that are invalid.
    for (const auto& p : l.ports)
        if (auto why = graph::checkCapacity(p)) throw std::invalid_argument(*why);
    return l;
}

template <class S>
void bindPorts([[maybe_unused]] S& ports, [[maybe_unused]] const rp_port_view* views) noexcept {
    if constexpr (!std::is_same_v<S, NoPorts>) {
        std::uint32_t i = 0;
        forEachField(ports, [&](auto& port) {
            using P = std::remove_cvref_t<decltype(port)>;
            const rp_port_view& v = views[i++];
            if constexpr (P::rp_kind == graph::PortKind::Audio) {
                port.channels = v.u.audio.channels;
                port.channel_count = v.u.audio.channel_count;
            } else if constexpr (P::rp_kind == graph::PortKind::Events || P::rp_kind == graph::PortKind::Bytes) {
                port.queue = v.u.queue;
            } else if constexpr (P::rp_kind == graph::PortKind::Control) {
                port.queue = v.u.control;
            } else if constexpr (P::rp_kind == graph::PortKind::Resource) {
                port.slot = v.u.resource;
            }
        });
    }
}

template <class S>
void bindParams([[maybe_unused]] S& params, [[maybe_unused]] rp_control_queue* const* queues) noexcept {
    if constexpr (!std::is_same_v<S, NoPorts>) {
        std::uint32_t i = 0;
        forEachField(params, [&](auto& param) { param.queue = queues[i++]; });
    }
}

// ---- compile-time checks of the optional hooks ---------------------------------------------------------------

// A hook is a function; a data member that happens to share its name is not one.
template <class P>
inline constexpr bool kIsFunction = std::is_member_function_pointer_v<P> || std::is_function_v<std::remove_pointer_t<P>>;

template <class T>
constexpr bool checkHooks() {
    using S = typename SettingsOf<T>::type;
    static_assert(requires(T& t, const Block& b) { { t.process(b) } noexcept; },
                  "a node needs `process(const rp::Block&) noexcept`");
    static_assert(requires(T& t, const Block& b) { { t.process(b) } -> std::same_as<void>; } ||
                      requires(T& t, const Block& b) { { t.process(b) } -> std::same_as<rp_status>; },
                  "process returns void or rp_status");
    if constexpr (requires { requires kIsFunction<decltype(&T::validate)>; })
        static_assert(requires(const S& s) { { T::validate(s) } -> std::same_as<std::optional<std::string>>; },
                      "validate must be `static std::optional<std::string> validate(const Settings&)`");
    if constexpr (requires { requires kIsFunction<decltype(&T::adjust_layout)>; })
        static_assert(requires(graph::Layout& l, const S& s) { T::adjust_layout(l, s); },
                      "adjust_layout must be `static void adjust_layout(rp::graph::Layout&, const Settings&)`");
    // stateless_prepare against needs_rebuild is checked in prepareFn, where it is called.
    if constexpr (requires { requires kIsFunction<decltype(&T::needs_rebuild)>; })
        static_assert(requires(const S& s) { { T::needs_rebuild(s, s) } -> std::same_as<bool>; },
                      "needs_rebuild must be `static bool needs_rebuild(const Settings&, const Settings&)`");
    if constexpr (requires { requires kIsFunction<decltype(&T::extra_memory)>; })
        static_assert(requires(const S& s) { { T::extra_memory(s) } -> std::same_as<std::size_t>; },
                      "extra_memory must be `static std::size_t extra_memory(const Settings&)`");
    if constexpr (requires { requires kIsFunction<decltype(&T::attach)>; })
        static_assert(requires(T& t, std::span<std::byte> m) { { t.attach(m) } noexcept; },
                      "attach must be `void attach(std::span<std::byte>) noexcept`");
    if constexpr (requires { requires kIsFunction<decltype(&T::on_settings)>; })
        static_assert(requires(T& t, const S& s) { { t.on_settings(s) } noexcept; },
                      "on_settings must be `void on_settings(const Settings&) noexcept`");
    if constexpr (requires { requires kIsFunction<decltype(&T::bind_host)>; })
        static_assert(requires(T& t, const rp_host* h) { { t.bind_host(h) } noexcept; },
                      "bind_host must be `void bind_host(const rp_host*) noexcept`");
    if constexpr (requires { requires kIsFunction<decltype(&T::activate)>; })
        static_assert(requires(T& t) { { t.activate(1.0, 1u) } -> std::same_as<bool>; } ||
                          requires(T& t) { { t.activate(1.0, 1u) } -> std::same_as<void>; },
                      "activate must be `bool activate(double, std::uint32_t)` or return void");
    if constexpr (requires { requires kIsFunction<decltype(&T::deactivate)>; })
        static_assert(requires(T& t) { t.deactivate(); }, "deactivate must be `void deactivate()`");
    if constexpr (requires { requires kIsFunction<decltype(&T::ports_changed)>; })
        static_assert(requires(T& t, std::span<const std::uint8_t> c) { { t.ports_changed(c) } noexcept; },
                      "ports_changed must be `void ports_changed(std::span<const std::uint8_t>) noexcept`");
    if constexpr (requires { requires kIsFunction<decltype(&T::set_peers)>; })
        static_assert(requires(T& t, std::span<rp_node* const> p) { { t.set_peers(p) } noexcept; },
                      "set_peers must be `void set_peers(std::span<rp_node* const>) noexcept`");
    constexpr bool anyStep = requires { requires kIsFunction<decltype(&T::prepare)>; } ||
                             requires { requires kIsFunction<decltype(&T::step)>; } ||
                             requires { requires kIsFunction<decltype(&T::finish)>; };
    if constexpr (anyStep) {
        static_assert(requires(T& t, const Block& b) { { t.prepare(b) } noexcept; { t.finish(b) } noexcept; },
                      "rp.step/1 needs `prepare(const rp::Block&) noexcept` and `finish(const rp::Block&) noexcept`");
        static_assert(requires(T& t, std::uint32_t f) { { t.step(f) } noexcept -> std::same_as<bool>; } ||
                          requires(T& t, std::uint32_t f) { { t.step(f) } noexcept -> std::same_as<rp_step_result>; },
                      "rp.step/1 needs `step(std::uint32_t) noexcept` returning bool or rp_step_result");
    }
    return true;
}

// ---- the ABI table ---------------------------------------------------------------------------------------------

template <class T>
struct Instance : rp_node {
    T                             node;
    typename SettingsOf<T>::type* settings = nullptr;
    std::string                   layoutJson;  // as created: its layout for life unless it reshapes
    std::span<std::byte>          extra;
};

inline void copyError(rp_error* err, std::string_view what) {
    if (!err) return;
    const std::size_t n = what.size() < sizeof err->message - 1 ? what.size() : sizeof err->message - 1;
    std::memcpy(err->message, what.data(), n);
    err->message[n] = '\0';
}

inline std::string_view settingsText(const rp_settings* s) {
    if (!s || !s->data || s->size == 0) return "{}";
    return {s->data, s->size};
}

inline rfl::Unexpected<rfl::Error> settingsError(std::string what) {
    return rfl::Unexpected<rfl::Error>(rfl::Error(std::move(what)));
}

template <class T>
rfl::Result<typename SettingsOf<T>::type> parseSettings(const rp_settings* s) {
    using S = typename SettingsOf<T>::type;
    if (s && s->encoding != RP_SETTINGS_JSON) return settingsError("settings encoding " + std::to_string(s->encoding) + " is not supported; this node reads JSON");
    if constexpr (std::is_same_v<S, NoSettings>) {
        auto g = graph::parseJson(settingsText(s));
        if (!g || !g->to_object()) return settingsError("settings must be a JSON object");
        return S{};
    } else {
        auto parsed = rfl::json::read<S, rfl::DefaultIfMissing>(settingsText(s));
        if constexpr (requires(const S& st) { T::validate(st); }) {
            if (parsed) {
                if (auto why = T::validate(*parsed)) return settingsError(*why);
            }
        }
        return parsed;
    }
}

template <class T>
std::size_t extraBytes([[maybe_unused]] const typename SettingsOf<T>::type& s) {
    if constexpr (requires { T::extra_memory(s); }) return T::extra_memory(s);
    else return 0;
}

constexpr std::size_t alignUp(std::size_t n, std::size_t a) { return (n + a - 1) / a * a; }

template <class T>
std::size_t extraOffset() { return alignUp(sizeof(Instance<T>), alignof(std::max_align_t)); }

template <class T> const rp_node_vtable* vtable();

template <class T>
Instance<T>* self(rp_node* n) noexcept { return static_cast<Instance<T>*>(n); }

template <class T>
std::size_t layoutForFn(const rp_node_type*, const rp_settings* s, char* out, std::size_t outSize, rp_error* err) {
    try {
        auto parsed = parseSettings<T>(s);
        if (!parsed) {
            copyError(err, parsed.error().what());
            return 0;
        }
        const std::string json = graph::layoutJson(layoutFor<T>(*parsed));
        if (out && json.size() < outSize) {
            std::memcpy(out, json.data(), json.size());
            out[json.size()] = '\0';
        }
        return json.size();
    } catch (const std::exception& e) {
        copyError(err, e.what());
    } catch (...) {
        copyError(err, "settings threw");
    }
    return 0;
}

template <class T>
bool activateFn([[maybe_unused]] rp_node* n, [[maybe_unused]] double sampleRate,
                [[maybe_unused]] std::uint32_t maxFrames, [[maybe_unused]] rp_error* err) {
    if constexpr (!requires(T& t) { t.activate(sampleRate, maxFrames); }) {
        return true;
    } else {
        try {
            if constexpr (std::is_same_v<decltype(self<T>(n)->node.activate(sampleRate, maxFrames)), bool>) {
                if (!self<T>(n)->node.activate(sampleRate, maxFrames)) {
                    copyError(err, "activate failed");
                    return false;
                }
            } else {
                self<T>(n)->node.activate(sampleRate, maxFrames);
            }
            return true;
        } catch (const std::exception& e) {
            copyError(err, e.what());
            return false;
        } catch (...) {
            copyError(err, "activate threw");
            return false;
        }
    }
}

template <class T>
void deactivateFn([[maybe_unused]] rp_node* n) {
    if constexpr (requires(T& t) { t.deactivate(); }) {
        try {
            self<T>(n)->node.deactivate();
        } catch (...) {
            // Nothing to report through: deactivate has no error channel, and it must not unwind the host.
        }
    }
}

template <class T>
void portsChangedFn([[maybe_unused]] rp_node* n, [[maybe_unused]] const std::uint8_t* connected,
                    [[maybe_unused]] std::uint32_t count) noexcept {
    if constexpr (requires(T& t, std::span<const std::uint8_t> c) { t.ports_changed(c); })
        self<T>(n)->node.ports_changed(std::span<const std::uint8_t>(connected, count));
}

template <class T>
void bindAll(T& node, const rp_process* p) noexcept {
    bindPorts(inputsOf(node), p->inputs);
    bindPorts(outputsOf(node), p->outputs);
    bindParams(paramsOf(node), p->params);
}

template <class T>
rp_status processFn(rp_node* n, const rp_process* p) noexcept {
    T& node = self<T>(n)->node;
    bindAll(node, p);
    const Block block{*p};
    if constexpr (std::is_same_v<decltype(node.process(block)), rp_status>) return node.process(block);
    else {
        node.process(block);
        return RP_OK;
    }
}

// Compares against the instance's extra memory and, unless it reshapes, its layout, neither of which changes
// after create; a node that reshapes never compares layouts, since the planner takes a reshape that keeps the
// layout as a swap. Only needs_rebuild reads the instance's current settings, which is safe because a host never
// prepares while a commit for the same instance may be in flight: GraphHost waits for a published commit to be
// adopted before it plans, but only for commits from types without RP_TYPE_STATELESS_PREPARE (awaitsAdoption). A C++
// node without stateless_prepare is such a type; a stateless_prepare type (every script node) is not waited for,
// which is why that flag rules needs_rebuild out. The rule is asserted on the call itself, so no form of
// needs_rebuild this would call (a template, an overload set, a callable static member) slips past it.
template <class T>
rp_prepare_result prepareFn(rp_node* n, const rp_settings* s, void** prepared, rp_error* err) {
    using S = typename SettingsOf<T>::type;
    try {
        auto parsed = parseSettings<T>(s);
        if (!parsed) {
            copyError(err, parsed.error().what());
            return RP_PREPARE_INVALID;
        }
        auto* inst = self<T>(n);
        bool rebuild = extraBytes<T>(*parsed) != inst->extra.size();
        if constexpr (!reshapesOf<T>())
            rebuild = rebuild || graph::layoutJson(layoutFor<T>(*parsed)) != inst->layoutJson;
        if constexpr (requires(const S& a) { T::needs_rebuild(a, a); }) {
            static_assert(!statelessPrepareOf<T>(),
                          "a stateless_prepare node cannot have needs_rebuild, which reads its current settings");
            rebuild = rebuild || T::needs_rebuild(*inst->settings, *parsed);
        }
        if (rebuild) return RP_PREPARE_REBUILD;
        *prepared = new S(std::move(*parsed));
        if constexpr (reshapesOf<T>()) return RP_PREPARE_RESHAPE;
        else return RP_PREPARE_SWAP;
    } catch (const std::exception& e) {
        copyError(err, e.what());
        return RP_PREPARE_INVALID;
    } catch (...) {
        copyError(err, "settings threw");
        return RP_PREPARE_INVALID;
    }
}

template <class T>
void commitFn(rp_node* n, void* prepared, void** old) noexcept {
    using S = typename SettingsOf<T>::type;
    auto* inst = self<T>(n);
    *old = inst->settings;
    inst->settings = static_cast<S*>(prepared);
    if constexpr (requires(T& t, const S& st) { t.on_settings(st); }) inst->node.on_settings(*inst->settings);
}

template <class T>
void releaseFn(rp_node*, void* old) noexcept {
    delete static_cast<typename SettingsOf<T>::type*>(old);
}

template <class T> void stepPrepare(rp_node* n, const rp_process* p) noexcept {
    T& node = self<T>(n)->node;
    bindAll(node, p);
    node.prepare(Block{*p});
}
template <class T> rp_step_result stepStep(rp_node* n, std::uint32_t target) noexcept {
    T& node = self<T>(n)->node;
    if constexpr (std::is_same_v<decltype(node.step(target)), bool>) return node.step(target) ? RP_STEP_DONE : RP_STEP_BELOW;
    else return node.step(target);
}
template <class T> void stepFinish(rp_node* n, const rp_process* p) noexcept {
    T& node = self<T>(n)->node;
    bindAll(node, p);
    node.finish(Block{*p});
}
template <class T> void linkSetPeers(rp_node* n, rp_node* const* peers, std::uint32_t count) noexcept {
    self<T>(n)->node.set_peers(std::span<rp_node* const>(peers, count));
}

template <class T>
const void* interfaceFn(rp_node*, const char* id) noexcept {
    [[maybe_unused]] const std::string_view want(id);
    if constexpr (requires(T& t, const Block& b, std::uint32_t f) { t.prepare(b); t.step(f); t.finish(b); }) {
        static constexpr rp_step_interface kStep{&stepPrepare<T>, &stepStep<T>, &stepFinish<T>};
        if (want == RP_STEP_INTERFACE) return &kStep;
    }
    if constexpr (requires(T& t, std::span<rp_node* const> peers) { t.set_peers(peers); }) {
        static constexpr rp_link_interface kLink{&linkSetPeers<T>};
        if (want == RP_LINK_INTERFACE) return &kLink;
    }
    return nullptr;
}

template <class T>
const rp_node_vtable* vtable() {
    static constexpr rp_node_vtable kVtable{&activateFn<T>, &deactivateFn<T>, &portsChangedFn<T>,
                                            &processFn<T>,  &prepareFn<T>,    &commitFn<T>,
                                            &releaseFn<T>,  &interfaceFn<T>};
    return &kVtable;
}

template <class T> constexpr const char* nameOf() { if constexpr (requires { T::name; }) return T::name; else return T::id; }
template <class T> constexpr const char* categoryOf() { if constexpr (requires { T::category; }) return T::category; else return ""; }
template <class T> constexpr const char* versionOf() { if constexpr (requires { T::version; }) return T::version; else return "1.0.0"; }
template <class T> constexpr bool heapOf() { if constexpr (requires { T::heap; }) return T::heap; else return false; }
template <class T> constexpr bool threadsOf() { if constexpr (requires { T::threads; }) return T::threads; else return false; }
template <class T> constexpr std::uint32_t typeFlags() {
    std::uint32_t flags = 0;
    if constexpr (statelessPrepareOf<T>()) flags |= RP_TYPE_STATELESS_PREPARE;
    return flags;
}

// An empty descriptor when building it throws (adjust_layout on default settings, say), which the registry
// refuses with a reason rather than letting the exception reach it.
template <class T>
const char* descriptorFn(const rp_node_type*) {
    static const std::string kJson = []() -> std::string {
        try {
            using S = typename SettingsOf<T>::type;
            graph::TypeDescriptor d;
            d.id = T::id;
            d.version = versionOf<T>();
            d.name = nameOf<T>();
            d.category = categoryOf<T>();
            d.heap = heapOf<T>();
            d.threads = threadsOf<T>();
            d.layout = layoutFor<T>(S{});
            if constexpr (!std::is_same_v<S, NoSettings>) d.settingsSchema = rfl::json::to_schema<S, rfl::DefaultIfMissing>();
            return graph::descriptorJson(d);
        } catch (...) {
            return std::string();
        }
    }();
    return kJson.c_str();
}

template <class T>
bool queryMemoryFn(const rp_node_type*, const rp_settings* s, rp_memory_req* out, rp_error* err) {
    try {
        auto parsed = parseSettings<T>(s);
        if (!parsed) {
            copyError(err, parsed.error().what());
            return false;
        }
        const std::size_t extra = extraBytes<T>(*parsed);
        if (extra > std::numeric_limits<std::size_t>::max() - extraOffset<T>()) {
            copyError(err, "extra memory request is too large");
            return false;
        }
        out->size = extra ? extraOffset<T>() + extra : sizeof(Instance<T>);
        out->align = alignof(Instance<T>) > alignof(std::max_align_t) ? alignof(Instance<T>) : alignof(std::max_align_t);
        return true;
    } catch (const std::exception& e) {
        copyError(err, e.what());
        return false;
    } catch (...) {
        copyError(err, "settings threw");
        return false;
    }
}

template <class T>
rp_node* createFn(const rp_node_type*, [[maybe_unused]] const rp_host* host, void* memory, const rp_settings* s,
                  rp_error* err) {
    using S = typename SettingsOf<T>::type;
    Instance<T>* inst = nullptr;
    try {
        auto parsed = parseSettings<T>(s);
        if (!parsed) {
            copyError(err, parsed.error().what());
            return nullptr;
        }
        // Everything that can throw on the node's behalf (adjust_layout) runs before the instance exists.
        std::string layout = graph::layoutJson(layoutFor<T>(*parsed));
        const std::size_t extra = extraBytes<T>(*parsed);
        inst = new (memory) Instance<T>{};
        inst->vt = vtable<T>();
        inst->layoutJson = std::move(layout);
        inst->settings = new S(std::move(*parsed));
        if constexpr (requires(T& t) { t.bind_host(host); }) inst->node.bind_host(host);
        if (extra) {
            inst->extra = std::span<std::byte>(static_cast<std::byte*>(memory) + extraOffset<T>(), extra);
            std::memset(inst->extra.data(), 0, extra);
            if constexpr (requires(T& t, std::span<std::byte> e) { t.attach(e); }) inst->node.attach(inst->extra);
        }
        if constexpr (requires(T& t, const S& st) { t.on_settings(st); }) inst->node.on_settings(*inst->settings);
        return inst;
    } catch (const std::exception& e) {
        copyError(err, e.what());
    } catch (...) {
        copyError(err, "create threw");
    }
    if (inst) {  // only an allocation failure gets here after construction; leave nothing behind
        delete inst->settings;
        inst->~Instance<T>();
    }
    return nullptr;
}

// The node goes first: it may hold a pointer to its settings until its own destructor has run.
template <class T>
void destroyFn(rp_node* n) {
    auto* inst = self<T>(n);
    auto* settings = inst->settings;
    inst->~Instance<T>();
    delete settings;
}

}  // namespace detail

// The ABI table for a C++ node type. One per T, built on first use; every optional hook's signature is
// checked here, at compile time.
template <class T>
const rp_node_type* node_type() {
    static_assert(detail::checkHooks<T>());
    static constexpr rp_node_type kType{T::id, &detail::descriptorFn<T>, &detail::layoutForFn<T>,
                                        &detail::queryMemoryFn<T>, &detail::createFn<T>, &detail::destroyFn<T>,
                                        detail::typeFlags<T>()};
    return &kType;
}

// A coupled peer as its concrete type, or null when the peer is a different node type.
template <class T>
T* peer_as(rp_node* n) {
    if (!n || n->vt != detail::vtable<T>()) return nullptr;
    return &detail::self<T>(n)->node;
}

}  // namespace rp
