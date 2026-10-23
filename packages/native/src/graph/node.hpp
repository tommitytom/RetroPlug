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
// keeps a count in a member called `inputs` would be reflected as if it were its port list. Optional members a node may define, each detected and wired only if present:
//
//   using Settings = SomeNamedStruct;                     reflect-cpp-readable; its JSON Schema ships in the descriptor
//   void on_settings(const Settings&) noexcept;           at create (control) and each commit (audio): copy, never allocate
//   static std::optional<std::string> validate(const Settings&);      reject settings with a reason
//   static void adjust_layout(rp::graph::Layout&, const Settings&);   patch reflected specs or append ports
//   static bool needs_rebuild(const Settings& from, const Settings& to);
//   static std::size_t extra_memory(const Settings&);     host-supplied tail memory, handed over by attach()
//   void attach(std::span<std::byte>) noexcept;
//   bool activate(double sampleRate, std::uint32_t maxFrames);   void deactivate();
//   void ports_changed(std::span<const std::uint8_t> connectedOutputs) noexcept;
//   void prepare(const rp::Block&) / step(std::uint32_t target) / void finish(const rp::Block&)        -> rp.step/1
//       (step returns bool - true once the block is produced - or an rp_step_result to report a fault)
//   void set_peers(std::span<rp_node* const>) noexcept                                             -> rp.link/1
//   void bind_host(const rp_host*) noexcept;              at create: host services (host-implemented nodes reach the host here)
//   static constexpr const char *name, *category, *version;  static constexpr bool heap, threads;
//
// process() may return void or rp_status. Exceptions never cross the ABI: every control-thread entry is
// wrapped, and process is noexcept.

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <new>
#include <optional>
#include <span>
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

template <graph::PortKind Kind>
struct queue_in {
    static constexpr auto rp_kind = Kind;
    static constexpr auto rp_dir  = graph::PortDir::In;
    const rp_queue* queue = nullptr;
    std::span<const rp_message> messages() const { return {queue->items, queue->count}; }
    std::span<const std::uint8_t> data(const rp_message& m) const { return graph::messageData(*queue, m); }
};

template <graph::PortKind Kind>
struct queue_out {
    static constexpr auto rp_kind = Kind;
    static constexpr auto rp_dir  = graph::PortDir::Out;
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
    static_assert(R.min <= R.max && R.min <= R.init && R.init <= R.max, "a param range needs min <= init <= max");
    static_assert(R.min >= -2147483648.0 && R.max <= 2147483647.0, "an int param's range fits an int, which value() returns");
    static_assert(R.min == static_cast<double>(static_cast<int>(R.min)) &&
                      R.max == static_cast<double>(static_cast<int>(R.max)) &&
                      R.init == static_cast<double>(static_cast<int>(R.init)),
                  "an int param's range is whole numbers");
    static graph::ParamDesc rp_param(std::string id) {
        return {std::move(id), std::string(Name.view()), graph::ParamType::Int, R.min, R.max, R.init, {},
                std::string(Unit.view())};
    }
    int value() const { return static_cast<int>(start() + (start() < 0 ? -0.5 : 0.5)); }
};

template <fixed_string Name, bool Default = false>
struct bool_param : detail::param_base {
    static graph::ParamDesc rp_param(std::string id) {
        return {std::move(id), std::string(Name.view()), graph::ParamType::Bool, 0.0, 1.0, Default ? 1.0 : 0.0, {}, {}};
    }
    bool value() const { return start() >= 0.5; }
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
}  // namespace detail

// An enum param: labels are the enumerator names in kebab-case, the runtime value is the index, rounded
// and clamped so any control can drive it.
template <class E, fixed_string Name, E Default>
struct enum_param : detail::param_base {
    static graph::ParamDesc rp_param(std::string id) {
        graph::ParamDesc d{std::move(id), std::string(Name.view()), graph::ParamType::Enum, 0.0, 0.0, 0.0, {}, {}};
        for (const auto& [label, e] : rfl::get_enumerator_array<E>()) {
            if (e == Default) d.def = static_cast<double>(d.values.size());
            d.values.push_back(detail::kebabCase(label));
        }
        d.max = static_cast<double>(d.values.size() - 1);
        return d;
    }
    E value() const {
        static constexpr auto kEnumerators = rfl::get_enumerator_array<E>();
        double v = start() + 0.5;
        if (v < 0.0) v = 0.0;
        auto i = static_cast<std::size_t>(v);
        if (i >= kEnumerators.size()) i = kEnumerators.size() - 1;
        return kEnumerators[i].second;
    }
};

// ---- the block -------------------------------------------------------------------------------------------

struct Block {
    const rp_process& raw;
    std::uint32_t frames() const { return raw.frames; }
    const rp_transport& transport() const { return *raw.transport; }
    // Ports beyond the reflected ones (appended by adjust_layout) are reached by index here.
    const rp_port_view& input(std::uint32_t i) const { return raw.inputs[i]; }
    const rp_port_view& output(std::uint32_t i) const { return raw.outputs[i]; }
};

// ---- reflection glue ---------------------------------------------------------------------------------------

namespace detail {

struct NoSettings {};
struct NoPorts {};

template <class T> struct SettingsOf { using type = NoSettings; };
template <class T> requires requires { typename T::Settings; } struct SettingsOf<T> { using type = typename T::Settings; };

template <class T> auto& inputsOf(T& t) { if constexpr (requires { t.inputs; }) return t.inputs; else { static NoPorts none; return none; } }
template <class T> auto& outputsOf(T& t) { if constexpr (requires { t.outputs; }) return t.outputs; else { static NoPorts none; return none; } }
template <class T> auto& paramsOf(T& t) { if constexpr (requires { t.params; }) return t.params; else { static NoPorts none; return none; } }

template <class Field>
graph::PortDesc portDesc() {
    using P = std::remove_cvref_t<typename Field::Type>;
    graph::PortDesc d;
    d.id = std::string(Field::name());
    d.kind = P::rp_kind;
    d.dir = P::rp_dir;
    if constexpr (requires { typename P::rp_channels; }) d.channels = ChannelSpecOf<typename P::rp_channels>::get();
    if constexpr (requires { P::rp_resource_type(); }) d.resourceType = std::string(P::rp_resource_type());
    if constexpr (requires { P::rp_write; }) d.write = P::rp_write;
    return d;
}

template <class Field>
graph::ParamDesc paramDesc() {
    using P = std::remove_cvref_t<typename Field::Type>;
    return P::rp_param(std::string(Field::name()));
}

template <class S, std::size_t... I>
void appendPorts(std::vector<graph::PortDesc>& out, std::index_sequence<I...>) {
    using Fs = typename rfl::named_tuple_t<S>::Fields;
    (out.push_back(portDesc<rfl::tuple_element_t<I, Fs>>()), ...);
}

template <class S, std::size_t... I>
void appendParams(std::vector<graph::ParamDesc>& out, std::index_sequence<I...>) {
    using Fs = typename rfl::named_tuple_t<S>::Fields;
    (out.push_back(paramDesc<rfl::tuple_element_t<I, Fs>>()), ...);
}

template <class S>
void appendPortsOf(std::vector<graph::PortDesc>& out) {
    if constexpr (!std::is_same_v<S, NoPorts>) appendPorts<S>(out, std::make_index_sequence<rfl::num_fields<S>>{});
}

template <class S>
void appendParamsOf(std::vector<graph::ParamDesc>& out) {
    if constexpr (!std::is_same_v<S, NoPorts>) appendParams<S>(out, std::make_index_sequence<rfl::num_fields<S>>{});
}

template <class T>
graph::Layout reflectedLayout() {
    using In  = std::remove_cvref_t<decltype(inputsOf(std::declval<T&>()))>;
    using Out = std::remove_cvref_t<decltype(outputsOf(std::declval<T&>()))>;
    using Par = std::remove_cvref_t<decltype(paramsOf(std::declval<T&>()))>;
    graph::Layout l;
    appendPortsOf<In>(l.ports);
    appendPortsOf<Out>(l.ports);
    appendParamsOf<Par>(l.params);
    return l;
}

// Inputs first, then outputs, each in declaration order - adjust_layout appends after the reflected ones.
template <class T>
graph::Layout layoutFor(const typename SettingsOf<T>::type& settings) {
    graph::Layout l = reflectedLayout<T>();
    if constexpr (requires(graph::Layout& lr) { T::adjust_layout(lr, settings); }) T::adjust_layout(l, settings);
    std::stable_partition(l.ports.begin(), l.ports.end(),
                          [](const graph::PortDesc& p) { return p.dir != graph::PortDir::Out; });
    return l;
}

template <class S>
void bindPorts(S& ports, const rp_port_view* views) {
    if constexpr (!std::is_same_v<S, NoPorts>) {
        std::uint32_t i = 0;
        rfl::to_view(ports).apply([&](auto& f) {
            auto& port = *f.value();
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
void bindParams(S& params, rp_control_queue* const* queues) {
    if constexpr (!std::is_same_v<S, NoPorts>) {
        std::uint32_t i = 0;
        rfl::to_view(params).apply([&](auto& f) { f.value()->queue = queues[i++]; });
    }
}

template <class T>
struct Instance {
    rp_node                       header;  // first: an rp_node* IS an Instance<T>*
    T                             node;
    typename SettingsOf<T>::type* settings = nullptr;
    std::string                   layoutCache;
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

template <class T>
rfl::Result<typename SettingsOf<T>::type> parseSettings(const rp_settings* s) {
    using S = typename SettingsOf<T>::type;
    if constexpr (std::is_same_v<S, NoSettings>) {
        return S{};
    } else {
        auto parsed = rfl::json::read<S, rfl::DefaultIfMissing>(settingsText(s));
        if constexpr (requires(const S& st) { { T::validate(st) } -> std::same_as<std::optional<std::string>>; }) {
            if (parsed) {
                if (auto why = T::validate(*parsed)) return rfl::Unexpected<rfl::Error>(rfl::Error(*why));
            }
        }
        return parsed;
    }
}

template <class T>
std::size_t extraBytes(const typename SettingsOf<T>::type& s) {
    if constexpr (requires { T::extra_memory(s); }) return T::extra_memory(s);
    else return 0;
}

constexpr std::size_t alignUp(std::size_t n, std::size_t a) { return (n + a - 1) / a * a; }

template <class T>
std::size_t extraOffset() { return alignUp(sizeof(Instance<T>), alignof(std::max_align_t)); }

template <class T> const rp_node_vtable* vtable();

template <class T>
Instance<T>* self(rp_node* n) { return reinterpret_cast<Instance<T>*>(n); }

template <class T>
const char* layoutFn(rp_node* n) {
    auto* inst = self<T>(n);
    inst->layoutCache = graph::layoutJson(layoutFor<T>(*inst->settings));
    return inst->layoutCache.c_str();
}

template <class T>
bool activateFn(rp_node* n, double sampleRate, std::uint32_t maxFrames, rp_error* err) {
    try {
        if constexpr (requires(T& t) { { t.activate(sampleRate, maxFrames) } -> std::same_as<bool>; }) {
            if (!self<T>(n)->node.activate(sampleRate, maxFrames)) {
                copyError(err, "activate failed");
                return false;
            }
        } else if constexpr (requires(T& t) { t.activate(sampleRate, maxFrames); }) {
            self<T>(n)->node.activate(sampleRate, maxFrames);
        }
        return true;
    } catch (const std::exception& e) {
        copyError(err, e.what());
        return false;
    }
}

template <class T>
void deactivateFn(rp_node* n) {
    if constexpr (requires(T& t) { t.deactivate(); }) self<T>(n)->node.deactivate();
}

template <class T>
void portsChangedFn(rp_node* n, const std::uint8_t* connected, std::uint32_t count) {
    if constexpr (requires(T& t, std::span<const std::uint8_t> c) { t.ports_changed(c); })
        self<T>(n)->node.ports_changed(std::span<const std::uint8_t>(connected, count));
}

template <class T>
void bindAll(T& node, const rp_process* p) {
    bindPorts(inputsOf(node), p->inputs);
    bindPorts(outputsOf(node), p->outputs);
    bindParams(paramsOf(node), p->params);
}

template <class T>
rp_status processFn(rp_node* n, const rp_process* p) {
    T& node = self<T>(n)->node;
    bindAll(node, p);
    const Block block{*p};
    if constexpr (std::is_same_v<decltype(node.process(block)), rp_status>) return node.process(block);
    else {
        node.process(block);
        return RP_OK;
    }
}

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
        const bool layoutChanged = graph::layoutJson(layoutFor<T>(*parsed)) != graph::layoutJson(layoutFor<T>(*inst->settings));
        bool rebuild = layoutChanged || extraBytes<T>(*parsed) != extraBytes<T>(*inst->settings);
        if constexpr (requires(const S& a) { T::needs_rebuild(a, a); })
            rebuild = rebuild || T::needs_rebuild(*inst->settings, *parsed);
        if (rebuild) return RP_PREPARE_REBUILD;
        *prepared = new S(std::move(*parsed));
        return RP_PREPARE_SWAP;
    } catch (const std::exception& e) {
        copyError(err, e.what());
        return RP_PREPARE_INVALID;
    }
}

template <class T>
void commitFn(rp_node* n, void* prepared, void** old) {
    using S = typename SettingsOf<T>::type;
    auto* inst = self<T>(n);
    *old = inst->settings;
    inst->settings = static_cast<S*>(prepared);
    if constexpr (requires(T& t, const S& st) { t.on_settings(st); }) inst->node.on_settings(*inst->settings);
}

template <class T>
void releaseFn(rp_node*, void* old) {
    delete static_cast<typename SettingsOf<T>::type*>(old);
}

template <class T> void stepPrepare(rp_node* n, const rp_process* p) {
    T& node = self<T>(n)->node;
    bindAll(node, p);
    node.prepare(Block{*p});
}
template <class T> rp_step_result stepStep(rp_node* n, std::uint32_t target) {
    T& node = self<T>(n)->node;
    if constexpr (std::is_same_v<decltype(node.step(target)), bool>) return node.step(target) ? RP_STEP_DONE : RP_STEP_BELOW;
    else return node.step(target);
}
template <class T> void stepFinish(rp_node* n, const rp_process* p) {
    T& node = self<T>(n)->node;
    bindAll(node, p);
    node.finish(Block{*p});
}
template <class T> void linkSetPeers(rp_node* n, rp_node* const* peers, std::uint32_t count) {
    self<T>(n)->node.set_peers(std::span<rp_node* const>(peers, count));
}

template <class T>
const void* interfaceFn(rp_node*, const char* id) {
    const std::string_view want(id);
    if constexpr (requires(T& t, const Block& b, std::uint32_t f) { t.prepare(b); t.step(f); t.finish(b); }) {
        static const rp_step_interface kStep{&stepPrepare<T>, &stepStep<T>, &stepFinish<T>};
        if (want == RP_STEP_INTERFACE) return &kStep;
    }
    if constexpr (requires(T& t, std::span<rp_node* const> peers) { t.set_peers(peers); }) {
        static const rp_link_interface kLink{&linkSetPeers<T>};
        if (want == RP_LINK_INTERFACE) return &kLink;
    }
    return nullptr;
}

template <class T>
const rp_node_vtable* vtable() {
    static const rp_node_vtable kVtable{&layoutFn<T>,   &activateFn<T>, &deactivateFn<T>,
                                        &portsChangedFn<T>, &processFn<T>, &prepareFn<T>,
                                        &commitFn<T>,   &releaseFn<T>,  &interfaceFn<T>};
    return &kVtable;
}

template <class T> constexpr const char* nameOf() { if constexpr (requires { T::name; }) return T::name; else return T::id; }
template <class T> constexpr const char* categoryOf() { if constexpr (requires { T::category; }) return T::category; else return ""; }
template <class T> constexpr const char* versionOf() { if constexpr (requires { T::version; }) return T::version; else return "1.0.0"; }
template <class T> constexpr bool heapOf() { if constexpr (requires { T::heap; }) return T::heap; else return false; }
template <class T> constexpr bool threadsOf() { if constexpr (requires { T::threads; }) return T::threads; else return false; }

template <class T>
const char* descriptorFn() {
    static const std::string kJson = [] {
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
        else d.settingsSchema = R"({"type":"object"})";
        return graph::descriptorJson(d);
    }();
    return kJson.c_str();
}

template <class T>
bool queryMemoryFn(const rp_settings* s, rp_memory_req* out, rp_error* err) {
    try {
        auto parsed = parseSettings<T>(s);
        if (!parsed) {
            copyError(err, parsed.error().what());
            return false;
        }
        const std::size_t extra = extraBytes<T>(*parsed);
        out->size = extra ? extraOffset<T>() + extra : sizeof(Instance<T>);
        out->align = alignof(Instance<T>) > alignof(std::max_align_t) ? alignof(Instance<T>) : alignof(std::max_align_t);
        return true;
    } catch (const std::exception& e) {
        copyError(err, e.what());
        return false;
    }
}

template <class T>
rp_node* createFn(const rp_host* host, void* memory, const rp_settings* s, rp_error* err) {
    using S = typename SettingsOf<T>::type;
    try {
        auto parsed = parseSettings<T>(s);
        if (!parsed) {
            copyError(err, parsed.error().what());
            return nullptr;
        }
        auto* inst = new (memory) Instance<T>{};
        inst->header.vt = vtable<T>();
        inst->settings = new S(std::move(*parsed));
        if constexpr (requires(T& t) { t.bind_host(host); }) inst->node.bind_host(host);
        if (const std::size_t extra = extraBytes<T>(*inst->settings)) {
            inst->extra = std::span<std::byte>(static_cast<std::byte*>(memory) + extraOffset<T>(), extra);
            std::memset(inst->extra.data(), 0, extra);
            if constexpr (requires(T& t, std::span<std::byte> e) { t.attach(e); }) inst->node.attach(inst->extra);
        }
        if constexpr (requires(T& t, const S& st) { t.on_settings(st); }) inst->node.on_settings(*inst->settings);
        return &inst->header;
    } catch (const std::exception& e) {
        copyError(err, e.what());
        return nullptr;
    }
}

template <class T>
void destroyFn(rp_node* n) {
    auto* inst = self<T>(n);
    delete inst->settings;
    inst->~Instance<T>();
}

}  // namespace detail

// The ABI table for a C++ node type. One per T, built on first use.
template <class T>
const rp_node_type* node_type() {
    static const rp_node_type kType{T::id, &detail::descriptorFn<T>, &detail::queryMemoryFn<T>, &detail::createFn<T>,
                                    &detail::destroyFn<T>};
    return &kType;
}

// A coupled peer as its concrete type, or null when the peer is a different node type.
template <class T>
T* peer_as(rp_node* n) {
    if (!n || n->vt != detail::vtable<T>()) return nullptr;
    return &detail::self<T>(n)->node;
}

}  // namespace rp
