#pragma once

// The desktop's description layer (docs/modular-graph-phase-e1-design.md section 6): what graph/node.hpp adds to its
// portable core (node_core.hpp) when it is built for the desktop, which plans. A node includes graph/node.hpp, never
// this.
//
// Here: each type's descriptor (its ports, params and settings schema, reflected), layout_for (the layout one set of
// settings gives, with adjust_layout), validate, the settings read from JSON by reflect-cpp (or from msgpack, by the
// core's decode), and an instance whose settings live on the heap so that prepare_settings can build the next object
// and commit_settings swap a pointer. Every control-thread entry catches, so an exception never crosses the ABI.

#ifdef RP_DEVICE
#error "node_desc.hpp is the desktop's description layer: include graph/node.hpp, which picks the build"
#endif

#include <algorithm>
#include <exception>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <rfl/DefaultIfMissing.hpp>
#include <rfl/Flatten.hpp>
#include <rfl/Rename.hpp>
#include <rfl/enums.hpp>
#include <rfl/json.hpp>

#include "graph/CanonicalJson.hpp"
#include "graph/Descriptor.hpp"
#include "graph/GraphDocument.hpp"
#include "graph/node_core.hpp"
#include "graph/settings_json.hpp"

namespace rp {

namespace graph {

// The settings an instance holds, written back as JSON: every field, a default as much as a value its document gave.
// An interface of the SDK's rather than the ABI's: every node graph/node.hpp builds for the desktop answers it through
// get_interface, and a device-mode build, which has no JSON writer, does not; nor does a type written against
// rp_node.h by hand. What reads it is desktop tooling: the device check's case generator
// (packages/graph-runtime/device-check) writes each portable type's default settings through it, and converts them to
// the msgpack a device reads. [control], and not while a commit_settings for the same instance may be in flight.
inline constexpr const char* kSettingsJsonInterface = "rp.sdk.settings-json/1";
struct SettingsJsonInterface {
    // The JSON, NUL-terminated into out[0..capacity) when it fits; its length without the NUL either way, so a caller
    // whose buffer is too small learns the size and asks again (as layout_for answers). 0 if writing it failed.
    std::size_t (*write)(rp_node* node, char* out, std::size_t capacity);
};

}  // namespace graph

namespace detail {

// The two reflect-cpp field wrappers the field walk does not follow (fields.hpp), marked so the walk refuses them.
template <rfl::internal::StringLiteral Name, class T>
struct IsRenameOrFlatten<rfl::Rename<Name, T>> : std::true_type {};
template <class T>
struct IsRenameOrFlatten<rfl::Flatten<T>> : std::true_type {};
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

// A param's descriptor entry, from the wrapper's template arguments (enum_param's is below, with the kebab-case
// labels). A param type of a node's own supplies `static graph::ParamDesc rp_param(std::string id)` instead.
template <class P>
struct ParamDescOf {
    static graph::ParamDesc get(std::string id) { return P::rp_param(std::move(id)); }
};
template <fixed_string Name, range R, fixed_string Unit>
struct ParamDescOf<float_param<Name, R, Unit>> {
    static graph::ParamDesc get(std::string id) {
        return {std::move(id), std::string(Name.view()), graph::ParamType::Float, R.min, R.max, R.init, {},
                std::string(Unit.view())};
    }
};
template <fixed_string Name, range R, fixed_string Unit>
struct ParamDescOf<int_param<Name, R, Unit>> {
    static graph::ParamDesc get(std::string id) {
        return {std::move(id), std::string(Name.view()), graph::ParamType::Int, R.min, R.max, R.init, {},
                std::string(Unit.view())};
    }
};
template <fixed_string Name, bool Default>
struct ParamDescOf<bool_param<Name, Default>> {
    static graph::ParamDesc get(std::string id) {
        return {std::move(id), std::string(Name.view()), graph::ParamType::Bool, 0.0, 1.0, Default ? 1.0 : 0.0, {}, {}};
    }
};

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

// Whether enums.hpp's enumerators of E are reflect-cpp's, name for name and value for value: an enum_param's labels and
// an enum settings field's JSON spellings are reflect-cpp's on the desktop and enums.hpp's on a device, so every
// desktop build that names one checks the two agree (enum_param's descriptor, a portable Settings' msgpack reading).
template <class E>
consteval bool enumMatchesReflect() {
    constexpr auto kReflected = rfl::get_enumerator_array<E>();
    if (kReflected.size() != Enumerators<E>::count) return false;
    for (std::size_t i = 0; i < kReflected.size(); ++i)
        if (kReflected[i].first != Enumerators<E>::name(i) || kReflected[i].second != Enumerators<E>::value(i))
            return false;
    return true;
}

// An enum param's descriptor entry: labels are the enumerator names in kebab-case, its default the default's index.
template <class E, fixed_string Name, E Default>
struct ParamDescOf<enum_param<E, Name, Default>> {
    static_assert(enumMatchesReflect<E>(), "enums.hpp reads this enum's enumerators differently from reflect-cpp");
    static graph::ParamDesc get(std::string id) {
        using En = Enumerators<E>;
        graph::ParamDesc d{std::move(id), std::string(Name.view()), graph::ParamType::Enum, 0.0, 0.0, 0.0, {}, {}};
        for (std::size_t i = 0; i < En::count; ++i) {
            if (En::value(i) == Default) d.def = static_cast<double>(i);
            d.values.push_back(kebabCase(En::name(i)));
        }
        d.max = static_cast<double>(En::count - 1);
        return d;
    }
};

// Every enum a portable Settings holds (a field, or a list's element) checked as enum_param's labels are.
template <class F>
consteval bool fieldEnumMatchesReflect() {
    if constexpr (IsList<F>::value) return fieldEnumMatchesReflect<typename F::value_type>();
    else if constexpr (std::is_enum_v<F>) return enumMatchesReflect<F>();
    else return true;
}
template <class S>
consteval bool settingsEnumsMatchReflect() {
    return []<std::size_t... I>(std::index_sequence<I...>) {
        return (fieldEnumMatchesReflect<FieldType<S, I>>() && ...);
    }(std::make_index_sequence<kFieldCount<S>>{});
}

// ---- the description: layouts and descriptors ------------------------------------------------------------------

enum class Side { Inputs, Outputs };

template <Side W, class S, std::size_t I>
graph::PortDesc portDesc() {
    using P = FieldType<S, I>;
    if constexpr (W == Side::Inputs) static_assert(P::rp_dir != graph::PortDir::Out, "a port in `inputs` must be an input or a link");
    else static_assert(P::rp_dir == graph::PortDir::Out, "a port in `outputs` must be an output");
    graph::PortDesc d;
    d.id = std::string(fieldName<S, I>());
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

template <class S, std::size_t I>
graph::ParamDesc paramDesc() {
    return ParamDescOf<FieldType<S, I>>::get(std::string(fieldName<S, I>()));
}

template <Side W, class S, std::size_t... I>
void appendPorts(std::vector<graph::PortDesc>& out, std::index_sequence<I...>) {
    (out.push_back(portDesc<W, S, I>()), ...);
}

template <class S, std::size_t... I>
void appendParams(std::vector<graph::ParamDesc>& out, std::index_sequence<I...>) {
    (out.push_back(paramDesc<S, I>()), ...);
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

// The hooks that name desktop types, checked where the desktop builds its table (checkHooks has the rest).
template <class T>
constexpr bool checkDescriptionHooks() {
    using S = typename SettingsOf<T>::type;
    if constexpr (requires { requires kIsFunction<decltype(&T::validate)>; })
        static_assert(requires(const S& s) { { T::validate(s) } -> std::same_as<std::optional<std::string>>; },
                      "validate must be `static std::optional<std::string> validate(const Settings&)`");
    if constexpr (requires { requires kIsFunction<decltype(&T::adjust_layout)>; })
        static_assert(requires(graph::Layout& l, const S& s) { T::adjust_layout(l, s); },
                      "adjust_layout must be `static void adjust_layout(rp::graph::Layout&, const Settings&)`");
    return true;
}

// ---- the instance ----------------------------------------------------------------------------------------------

// The desktop's instance keeps its settings on the heap, so a commit swaps one pointer on the audio thread and the old
// object is freed back on the control thread (release_settings).
template <class T>
struct Instance : rp_node {
    // The resources this instance's settings name, pinned at create (ResourceHold). Declared first so they are
    // released after the node, which may read one of them in its destructor.
    std::vector<ResourceHold>     resources;
    T                             node;
    typename SettingsOf<T>::type* settings = nullptr;
    std::string                   layoutJson;  // as created, which prepare compares against; empty if it reshapes
    std::span<std::byte>          extra;
    // The rebuild_key of the settings it was created with, which prepare compares against (empty without the
    // hook). create is its only writer: a commit cannot change it, since prepare answers rebuild for a different
    // key.
    std::optional<typename RebuildKeyOf<T>::type> key;
};

inline std::string_view settingsText(const rp_settings* s) {
    if (!s || !s->data || s->size == 0) return "{}";
    return {s->data, s->size};
}

inline rfl::Unexpected<rfl::Error> settingsError(std::string what) {
    return rfl::Unexpected<rfl::Error>(rfl::Error(std::move(what)));
}

// A settings object in either encoding: JSON through reflect-cpp, msgpack through the decode a device runs
// (settings_msgpack.hpp), which reads only a portable Settings; a type whose settings hold desktop types reads JSON
// alone. Validated the same way whichever it came as.
template <class T>
rfl::Result<typename SettingsOf<T>::type> parseSettings(const rp_settings* s) {
    using S = typename SettingsOf<T>::type;
    if (s && s->encoding == RP_SETTINGS_MSGPACK) {
        const void*       data = s->data;
        const std::size_t size = s->data ? s->size : 0;
        ErrorText         why;
        if constexpr (std::is_same_v<S, NoSettings>) {
            if (!decodeNoSettings(data, size, why)) return settingsError(std::string(why.view()));
            return S{};
        } else if constexpr (kPortableSettings<S>) {
            static_assert(settingsEnumsMatchReflect<S>(),
                          "enums.hpp reads an enum in these settings differently from reflect-cpp's JSON");
            S out{};
            if (!decodeSettings(data, size, out, why)) return settingsError(std::string(why.view()));
            if constexpr (requires(const S& st) { T::validate(st); }) {
                if (auto reason = T::validate(out)) return settingsError(*reason);
            }
            return out;
        } else {
            return settingsError("this node's settings hold types a device cannot read, so it reads JSON alone");
        }
    }
    if (s && s->encoding != RP_SETTINGS_JSON)
        return settingsError("settings encoding " + std::to_string(s->encoding) +
                             " is not supported; this node reads JSON or msgpack");
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
std::size_t extraOffset() { return alignUp(sizeof(Instance<T>), alignof(std::max_align_t)); }

// Put the instance's held handles into the resource fields of `s`, which prepare parsed from a new document, and
// report whether they are the same resources: a ref that moved is a rebuild, since the fresh instance pins what it
// names and this one keeps reading what it holds until its plan is reclaimed. Reads only what create fixed (the
// pins), never the committed settings, so a type whose prepare is stateless may still call it.
template <class T>
bool resolveResources(Instance<T>& inst, typename SettingsOf<T>::type& s) {
    bool same = true;
    std::size_t i = 0;
    forEachResourceField(s, [&](std::string_view, auto& field) {
        if (!same) return;
        if (i >= inst.resources.size() || inst.resources[i].named != field.id) {
            same = false;
            return;
        }
        field.resource = inst.resources[i].resource;
        ++i;
    });
    return same && i == inst.resources.size();
}

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

// Compares against the instance's extra memory, its rebuild_key and, unless it reshapes, its layout, none of which
// changes after create; a node that reshapes never compares layouts, since the planner takes a reshape that keeps
// the layout as a swap. Only needs_rebuild reads the instance's current settings, which is safe because a host never
// prepares while a commit for the same instance may be in flight: GraphHost waits for a published commit to be
// adopted before it plans, but only for commits from types without RP_TYPE_STATELESS_PREPARE (awaitsAdoption). A C++
// node without stateless_prepare is such a type; a stateless_prepare type (every script node) is not waited for,
// which is why that flag rules needs_rebuild out. The rule is asserted on the call itself, so no form of
// needs_rebuild this would call (a template, an overload set, a callable static member) slips past it. A
// rebuild_key decides the same thing from the key create kept, so a type that rebuilds on a key may prepare
// statelessly.
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
        if constexpr (kHasRebuildKey<T>) {
            // create engages it for a keyed type, so this only ever fires if the invariant broke.
            if (!inst->key) throw std::logic_error("a keyed instance was created without its key");
            rebuild = rebuild || !(T::rebuild_key(*parsed) == *inst->key);
        }
        // A resource field that moved is a rebuild whatever the type's key says, and the one that stays puts this
        // instance's pins into the prepared settings (nothing is pinned or dropped here: only create and destroy do
        // that, both on the control thread). Resolved before needs_rebuild, and that hook is skipped on a moved ref, so
        // it never sees a settings object whose handle is null: a node that reads a resource there would otherwise
        // rebuild on every prepare.
        const bool resourcesMoved = !resolveResources(*inst, *parsed);
        if constexpr (requires(const S& a) { T::needs_rebuild(a, a); }) {
            static_assert(!statelessPrepareOf<T>(),
                          "a stateless_prepare node cannot have needs_rebuild, which reads its current settings");
            if (!resourcesMoved) rebuild = rebuild || T::needs_rebuild(*inst->settings, *parsed);
        }
        rebuild = rebuild || resourcesMoved;
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

template <class T>
std::size_t writeSettingsFn(rp_node* n, char* out, std::size_t capacity) noexcept {
    using S = typename SettingsOf<T>::type;
    try {
        std::string json = "{}";
        if constexpr (!std::is_same_v<S, NoSettings>) json = rfl::json::write(*self<T>(n)->settings);
        if (json.size() < capacity) {
            std::memcpy(out, json.data(), json.size());
            out[json.size()] = '\0';
        }
        return json.size();
    } catch (...) {
        return 0;
    }
}

// The portable core's interfaces (rp.step/1, rp.link/1, the node's own), and the desktop's settings writer.
template <class T>
const void* desktopInterfaceFn(rp_node* n, const char* id) noexcept {
    static constexpr graph::SettingsJsonInterface kSettings{&writeSettingsFn<T>};
    if (std::string_view(id) == graph::kSettingsJsonInterface) return &kSettings;
    return interfaceFn<T>(n, id);
}

template <class T>
inline constexpr rp_node_vtable kVtable{&activateFn<T>, &deactivateFn<T>, &portsChangedFn<T>,
                                        &processFn<T>,  &prepareFn<T>,    &commitFn<T>,
                                        &releaseFn<T>,  &desktopInterfaceFn<T>};

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
            d.unique = uniqueOf<T>();
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
        // Everything that can throw on the node's behalf (adjust_layout, rebuild_key) runs before the instance exists.
        // Only a node that does not reshape keeps its layout, for prepare to compare against.
        std::string layout;
        if constexpr (reshapesOf<T>()) (void)layoutFor<T>(*parsed);
        else layout = graph::layoutJson(layoutFor<T>(*parsed));
        std::optional<typename RebuildKeyOf<T>::type> key;
        if constexpr (kHasRebuildKey<T>) key.emplace(T::rebuild_key(*parsed));
        const std::size_t extra = extraBytes<T>(*parsed);
        inst = new (memory) Instance<T>{};
        inst->vt = &kVtable<T>;
        inst->layoutJson = std::move(layout);
        inst->key = std::move(key);
        inst->settings = new S(std::move(*parsed));
        if constexpr (requires(T& t) { t.bind_host(host); }) inst->node.bind_host(host);
        // The resources the settings name are pinned before any hook sees the settings, so on_settings and activate
        // read the handles; a resource that cannot be pinned fails the create, and with it the document.
        inst->resources.resize(kResourceFields<S>);
        ErrorText refused;
        if (!acquireResources(std::span<ResourceHold>(inst->resources), host, *inst->settings, &refused)) {
            copyError(err, refused.view());
        } else {
            if (extra) {
                inst->extra = std::span<std::byte>(static_cast<std::byte*>(memory) + extraOffset<T>(), extra);
                std::memset(inst->extra.data(), 0, extra);
                if constexpr (requires(T& t, std::span<std::byte> e) { t.attach(e); }) inst->node.attach(inst->extra);
            }
            if constexpr (requires(T& t, const S& st) { t.on_settings(st); }) inst->node.on_settings(*inst->settings);
            return inst;
        }
    } catch (const std::exception& e) {
        copyError(err, e.what());
    } catch (...) {
        copyError(err, "create threw");
    }
    // An allocation failure, or a resource that would not pin, after the instance exists: leave nothing behind.
    if (inst) {
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

template <class T>
inline constexpr rp_node_type kNodeType{T::id,           &descriptorFn<T>, &layoutForFn<T>,
                                        &queryMemoryFn<T>, &createFn<T>,   &destroyFn<T>,
                                        typeFlags<T>(),  nullptr};

}  // namespace detail

}  // namespace rp
