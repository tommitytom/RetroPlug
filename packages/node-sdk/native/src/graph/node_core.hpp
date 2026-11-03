#pragma once

// The portable core of the C++ authoring layer (docs/modular-graph-phase-e1-design.md section 6): what both builds of
// graph/node.hpp share. A node includes graph/node.hpp, never this; node.hpp's header comment is the authoring guide.
//
// Here: the port and param wrappers, rp::Block, the compile-time reflection of a node's `inputs`, `outputs` and
// `params` and the binding of a block's views to them, the checks of the optional hooks, the resources a node's
// settings name, the msgpack settings decode, and the instance's block-path entries (process, ports_changed,
// rp.step/1, rp.link/1, get_interface). Each build adds its own instance and the type-level entries around it:
// node_device.hpp (RP_DEVICE) an instance that owns its settings in place and reads them from msgpack, node_desc.hpp
// (the desktop) the description layer, JSON settings and the prepare / commit swap.
//
// Portable means a device can build it: no allocation, no exception, no RTTI, and none of the headers that carry them.
// So this file and the headers it includes stay off <string>, <vector>, <map>, <optional>, <stdexcept>, <exception>,
// <iostream>, reflect-cpp's JSON and yyjson; a device-mode build of node.hpp checks that at its end.

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "graph/PortTypes.hpp"
#include "graph/enums.hpp"
#include "graph/Queue.hpp"
#include "graph/fields.hpp"
#include "graph/rp_node.h"
#include "graph/settings_msgpack.hpp"
#include "graph/settings_types.hpp"

namespace rp {

// ---- channel specs -----------------------------------------------------------------------------------

// What an audio port's channel count is: any count, the count of another port, or a fixed count with its layout. Tags
// only: the desktop's description layer turns each into its graph::ChannelSpec (node_desc.hpp).

struct any {};
template <fixed_string Port> struct match {};
template <std::uint32_t N, fixed_string LayoutName> struct channels {};
using mono   = channels<1, "mono">;
using stereo = channels<2, "stereo">;


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

// A param's descriptor entry (its name, type, range and unit) is built from these template arguments by the desktop's
// description layer (node_desc.hpp's ParamDescOf); the wrappers themselves are what a block reads.
template <fixed_string Name, range R = range{}, fixed_string Unit = "">
struct float_param : detail::param_base {
    static_assert(R.min <= R.max && R.min <= R.init && R.init <= R.max, "a param range needs min <= init <= max");
};

template <fixed_string Name, range R = range{}, fixed_string Unit = "">
struct int_param : detail::param_base {
    static_assert(R.min <= R.max && R.min <= R.init && R.init <= R.max && R.min >= -2147483648.0 &&
                      R.max <= 2147483647.0 && R.min == static_cast<double>(static_cast<int>(R.min)) &&
                      R.max == static_cast<double>(static_cast<int>(R.max)) &&
                      R.init == static_cast<double>(static_cast<int>(R.init)),
                  "an int param's range is whole numbers with min <= init <= max, within int");
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
    bool value() const { return start() >= 0.5; }  // NaN reads as false
};

namespace detail {
template <class E, E Value>
constexpr bool isEnumerator() {
    for (std::size_t i = 0; i < Enumerators<E>::count; ++i)
        if (Enumerators<E>::value(i) == Value) return true;
    return false;
}
}  // namespace detail

// An enum param: labels are the enumerator names in kebab-case (the desktop's descriptor, node_desc.hpp), the runtime
// value is the index, rounded and clamped so any control can drive it (NaN reads as the default). E is an enum
// enums.hpp can read: a fixed underlying type and no operator|.
template <class E, fixed_string Name, E Default>
struct enum_param : detail::param_base {
    using Values = detail::Enumerators<E>;
    static_assert(Values::count > 0, "an enum param needs at least one enumerator");
    static_assert(detail::isEnumerator<E, Default>(), "an enum param's default must be one of its enumerators");

    E value() const {
        const double v = start();
        if (!(v == v)) return Default;
        const std::size_t last = Values::count - 1;
        std::size_t       i = 0;
        if (v >= static_cast<double>(last)) i = last;
        else if (v > 0.0) i = static_cast<std::size_t>(v + 0.5);
        return Values::value(i);
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

// What rebuild_key returns, or NoKey for a type without one. Detected by the call, as needs_rebuild is in prepareFn,
// so the key the instance keeps and the key prepare compares are found the same way.
struct NoKey {};
template <class T> struct RebuildKeyOf { using type = NoKey; };
template <class T>
    requires requires(const typename SettingsOf<T>::type& s) { T::rebuild_key(s); }
struct RebuildKeyOf<T> {
    using type = std::remove_cvref_t<decltype(T::rebuild_key(std::declval<const typename SettingsOf<T>::type&>()))>;
};
template <class T> inline constexpr bool kHasRebuildKey = !std::is_same_v<typename RebuildKeyOf<T>::type, NoKey>;

template <class T> auto& inputsOf(T& t) { if constexpr (requires { t.inputs; }) return t.inputs; else { static NoPorts none; return none; } }
template <class T> auto& outputsOf(T& t) { if constexpr (requires { t.outputs; }) return t.outputs; else { static NoPorts none; return none; } }
template <class T> auto& paramsOf(T& t) { if constexpr (requires { t.params; }) return t.params; else { static NoPorts none; return none; } }

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
    // validate and adjust_layout name desktop types; the desktop's checkDescriptionHooks checks them (node_desc.hpp).
    // stateless_prepare against needs_rebuild is checked in prepareFn, where it is called.
    if constexpr (requires { requires kIsFunction<decltype(&T::needs_rebuild)>; })
        static_assert(requires(const S& s) { { T::needs_rebuild(s, s) } -> std::same_as<bool>; },
                      "needs_rebuild must be `static bool needs_rebuild(const Settings&, const Settings&)`");
    if constexpr (requires { requires kIsFunction<decltype(&T::rebuild_key)>; }) {
        static_assert(requires(const S& s) { T::rebuild_key(s); },
                      "rebuild_key must be `static Key rebuild_key(const Settings&)`");
        // Only a key the call really gives has a type to check: a signature near-miss falls back to NoKey here.
        if constexpr (requires(const S& s) { T::rebuild_key(s); }) {
            using K = typename RebuildKeyOf<T>::type;
            static_assert(!std::is_void_v<K> && std::copy_constructible<K> && std::equality_comparable<K>,
                          "rebuild_key's Key must be copyable and compare with ==");
        }
    }
    static_assert(!(kHasRebuildKey<T> && requires(const S& s) { T::needs_rebuild(s, s); }),
                  "a node has needs_rebuild or rebuild_key, not both: the key already decides what rebuilds");
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
    if constexpr (requires { requires kIsFunction<decltype(&T::get_interface)>; })
        static_assert(
            requires(T& t, std::string_view id) { { t.get_interface(id) } noexcept -> std::same_as<const void*>; },
            "get_interface must be `const void* get_interface(std::string_view) noexcept`");
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

// ---- the instance ----------------------------------------------------------------------------------------------

// One pin an instance holds on a resource its settings name, and how to drop it: the host's table, by id. A field
// that names no resource still holds its place, so the pins line up with the settings' resource fields in order.
struct ResourceHold {
    const rp_host_resources* service = nullptr;
    std::uint32_t            named = 0;   // the id the settings named (0: none), so prepare can tell whether it moved
    std::uint32_t            id = 0;      // the id pinned: equal to `named` while a pin is held, 0 after drop
    const rp_resource*       resource = nullptr;

    ResourceHold() = default;
    ResourceHold(const ResourceHold&) = delete;
    ResourceHold& operator=(const ResourceHold&) = delete;
    ResourceHold(ResourceHold&& other) noexcept
        : service(std::exchange(other.service, nullptr)),
          named(other.named),
          id(std::exchange(other.id, 0)),
          resource(std::exchange(other.resource, nullptr)) {}
    ResourceHold& operator=(ResourceHold&& other) noexcept {
        if (this != &other) {
            drop();
            service = std::exchange(other.service, nullptr);
            named = other.named;
            id = std::exchange(other.id, 0);
            resource = std::exchange(other.resource, nullptr);
        }
        return *this;
    }
    ~ResourceHold() { drop(); }

    void drop() noexcept {
        if (service && id) service->release(service->ctx, id);
        service = nullptr;
        id = 0;
        resource = nullptr;
    }
};

// One instance of T as each build lays it out (node_device.hpp, node_desc.hpp): an rp_node first, then the pins on the
// resources its settings name, declared before the node so they are released after it, which may read one of them in
// its destructor.
template <class T> struct Instance;

template <class T>
Instance<T>* self(rp_node* n) noexcept { return static_cast<Instance<T>*>(n); }

inline void copyError(rp_error* err, std::string_view what) {
    if (!err) return;
    const std::size_t n = what.size() < sizeof err->message - 1 ? what.size() : sizeof err->message - 1;
    std::memcpy(err->message, what.data(), n);
    err->message[n] = '\0';
}

template <class T>
std::size_t extraBytes([[maybe_unused]] const typename SettingsOf<T>::type& s) {
    if constexpr (requires { T::extra_memory(s); }) return T::extra_memory(s);
    else return 0;
}

constexpr std::size_t alignUp(std::size_t n, std::size_t a) { return (n + a - 1) / a * a; }

// ---- resources named by settings (docs/modular-graph-phase-c3-design.md section 4) ------------------------

template <class Field, class F>
void resourceField(std::string_view name, Field& field, F&& f) {
    using P = std::remove_cvref_t<Field>;
    if constexpr (IsResourceRef<P>::value) f(name, field);
}

// A settings struct's resource fields, in declaration order, each with its JSON name: the order create pins them in
// and prepare compares them by. Only settings fields are walked this way; a port or param of the same type is not.
template <class S, class F>
void forEachResourceField(S& s, F&& f) {
    forEachNamedField(s, [&](std::string_view name, auto& field) { resourceField(name, field, f); });
}

// How many resource fields S has: the pins an instance holds, one per field whether or not it names a resource.
template <class S>
consteval std::size_t resourceFieldCount() {
    if constexpr (std::is_same_v<S, NoSettings>) {
        return 0;
    } else {
        return []<std::size_t... I>(std::index_sequence<I...>) {
            return (std::size_t{IsResourceRef<FieldType<S, I>>::value} + ... + 0);
        }(std::make_index_sequence<kFieldCount<S>>{});
    }
}
template <class S> inline constexpr std::size_t kResourceFields = resourceFieldCount<S>();

// Pin every resource `s` names, one ResourceHold per resource field in field order (`pins` has kResourceFields of
// them), resolving each field's handle. False, with the reason in *why, for a host that offers no table, an id that
// is unknown or dropped, or a resource of another type than the field declares (a "rom" where the document names an
// sram). The reason is built in place, so a device-mode create allocates nothing here either.
template <class S>
bool acquireResources(std::span<ResourceHold> pins, const rp_host* host, S& s, ErrorText* why) {
    if (pins.empty()) return true;  // a node that names none never looks the service up
    const auto* service =
        host ? static_cast<const rp_host_resources*>(host->get_interface(host, RP_HOST_RESOURCES_INTERFACE)) : nullptr;
    bool        ok = true;
    std::size_t i = 0;
    forEachResourceField(s, [&](std::string_view name, auto& field) {
        ResourceHold& held = pins[i++];
        held.named = field.id;
        if (!ok || field.empty()) return;  // the document names none here
        if (!service) {
            *why << name << ": the host offers no resource table";
            ok = false;
            return;
        }
        // The field's type as a C string for the service, from its compile-time spelling.
        constexpr auto kType = std::remove_cvref_t<decltype(field)>::resource_type();
        char type[kType.size() + 1] = {};
        for (std::size_t k = 0; k < kType.size(); ++k) type[k] = kType[k];
        rp_error err{};
        const rp_resource* r = service->acquire(service->ctx, field.id, type, &err);
        if (!r) {
            // The service's reason continues the sentence: "rom: \"res:7\" is not a live resource".
            *why << name << ": \"res:" << std::uint64_t{field.id} << "\" "
                 << (err.message[0] ? std::string_view(err.message) : std::string_view("cannot be used"));
            ok = false;
            return;
        }
        held.service = service;
        held.id = field.id;
        held.resource = r;
        field.resource = r;
    });
    return ok;
}

// ---- the block path ---------------------------------------------------------------------------------------------

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
const void* interfaceFn([[maybe_unused]] rp_node* n, const char* id) noexcept {
    [[maybe_unused]] const std::string_view want(id);
    if constexpr (requires(T& t, const Block& b, std::uint32_t f) { t.prepare(b); t.step(f); t.finish(b); }) {
        static constexpr rp_step_interface kStep{&stepPrepare<T>, &stepStep<T>, &stepFinish<T>};
        if (want == RP_STEP_INTERFACE) return &kStep;
    }
    if constexpr (requires(T& t, std::span<rp_node* const> peers) { t.set_peers(peers); }) {
        static constexpr rp_link_interface kLink{&linkSetPeers<T>};
        if (want == RP_LINK_INTERFACE) return &kLink;
    }
    if constexpr (requires(T& t, std::string_view w) { t.get_interface(w); })
        return self<T>(n)->node.get_interface(want);
    else
        return nullptr;
}

template <class T> constexpr const char* nameOf() { if constexpr (requires { T::name; }) return T::name; else return T::id; }
template <class T> constexpr const char* categoryOf() { if constexpr (requires { T::category; }) return T::category; else return ""; }
template <class T> constexpr const char* versionOf() { if constexpr (requires { T::version; }) return T::version; else return "1.0.0"; }
template <class T> constexpr bool heapOf() { if constexpr (requires { T::heap; }) return T::heap; else return false; }
template <class T> constexpr bool threadsOf() { if constexpr (requires { T::threads; }) return T::threads; else return false; }
// At most one node of the type in a plan (the planner refuses a second): a type that binds a host-owned device sets it.
template <class T> constexpr bool uniqueOf() {
    if constexpr (requires { T::unique; }) return T::unique; else return false;
}
template <class T> constexpr std::uint32_t typeFlags() {
    std::uint32_t flags = 0;
    if constexpr (statelessPrepareOf<T>()) flags |= RP_TYPE_STATELESS_PREPARE;
    return flags;
}

}  // namespace detail

}  // namespace rp
