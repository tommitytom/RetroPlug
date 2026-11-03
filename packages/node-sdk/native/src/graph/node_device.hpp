#pragma once

// The device-mode instance (RP_DEVICE, docs/modular-graph-phase-e1-design.md section 6): what graph/node.hpp adds to
// its portable core (node_core.hpp) when it is built for a device. A node includes graph/node.hpp, never this.
//
// A device runs a plan the desktop made, planned and validated, so nothing here describes a type or checks one: the
// type's descriptor and layout_for are null, its vtable's prepare_settings, commit_settings and release_settings are
// null (a device's plan never changes a node's settings), and validate and adjust_layout are never called. What is
// left is the lifecycle: query_memory and create read the settings from msgpack, and the instance owns them in place,
// counted in the size query_memory asks for, beside a fixed array of pins with one per resource field. Nothing here
// allocates or throws, and no entry catches: a node built for the device throws nothing either.
//
// A type is built for the device only when it can run there: it declares neither `heap` nor `threads` (the planner
// refuses those on the Pico, and a device build never has them at all), and its Settings is portable
// (settings_msgpack.hpp's kPortableSettings). Either one failing is a compile error naming the type
// (DeviceTypeCheck<T> in the error's instantiation context).

#ifndef RP_DEVICE
#error "node_device.hpp is the device-mode instance: include graph/node.hpp, which picks the build"
#endif

#include "graph/node_core.hpp"

namespace rp {

namespace graph {
// Declared and never defined in a device-mode build: the description layer's Layout is the desktop's. A node's
// adjust_layout left outside `#ifndef RP_DEVICE` (node.hpp's header comment) fails on this incomplete type, with this
// note beside the error, rather than on a name that does not exist.
struct Layout;
}  // namespace graph

namespace detail {

template <class T>
struct DeviceTypeCheck {
    using S = typename SettingsOf<T>::type;
    static_assert(!heapOf<T>() && !threadsOf<T>(),
                  "a node type that declares `heap` or `threads` is never built for the device: leave it out of the "
                  "device build (#ifndef RP_DEVICE around the type and its package entry)");
    static_assert(std::is_same_v<S, NoSettings> || kPortableSettings<S>,
                  "a device node's Settings holds only numbers, bools, enums (a fixed underlying type, no operator|), "
                  "rp::choice, rp::text, rp::list of those and rp::resource_ref, in a trivially copyable struct");
    // A deliberate check of node.hpp's rule, which otherwise holds only where a hook's body names something a device
    // build lacks (the incomplete graph::Layout; on libstdc++ the heap-header #error): on another standard library a
    // validate whose body happens to compile would slip through without it.
    static_assert(!requires { &T::validate; } && !requires { &T::adjust_layout; },
                  "validate and adjust_layout are the desktop's: a node built for the device keeps them (and what only "
                  "they use) inside #ifndef RP_DEVICE");
    // The descriptor's strings are never read here (a device has none), but they are the type's all the same.
    static_assert(nameOf<T>() != nullptr && categoryOf<T>() != nullptr && versionOf<T>() != nullptr);
    static constexpr bool ok = true;
};

// The pins on the resources a type's settings name, one per resource field; nothing at all for a type that names none.
template <std::size_t N>
struct Pins {
    ResourceHold          hold[N];
    std::span<ResourceHold> span() { return hold; }
};
template <>
struct Pins<0> {
    std::span<ResourceHold> span() { return {}; }
};

// The settings live in the instance, after the pins and before the node, so they are destroyed after it: a node may
// read its settings until its own destructor has run, as on the desktop.
template <class T>
struct Instance : rp_node {
    using S = typename SettingsOf<T>::type;
    // Released after the node and its settings (the node may read one in its destructor). Takes no room when empty.
    [[no_unique_address]] Pins<kResourceFields<S>> resources;
    S                                              settings;
    T                                              node;
    std::span<std::byte>                           extra;
};

template <class T>
std::size_t extraOffset() { return alignUp(sizeof(Instance<T>), alignof(std::max_align_t)); }

// The settings in `s`, read into `out` (which holds the defaults). False with the reason for any encoding but msgpack:
// a device has no JSON reader.
template <class T>
bool readSettings(const rp_settings* s, typename SettingsOf<T>::type& out, ErrorText& why) {
    using S = typename SettingsOf<T>::type;
    if (s && s->encoding != RP_SETTINGS_MSGPACK) {
        why << "settings encoding " << std::uint64_t{s->encoding} << " is not supported; a device node reads msgpack";
        return false;
    }
    const void*       data = s ? s->data : nullptr;
    const std::size_t size = s && s->data ? s->size : 0;
    if constexpr (std::is_same_v<S, NoSettings>) {
        (void)out;
        return decodeNoSettings(data, size, why);
    } else {
        return decodeSettings(data, size, out, why);
    }
}

template <class T>
bool activateFn([[maybe_unused]] rp_node* n, [[maybe_unused]] double sampleRate,
                [[maybe_unused]] std::uint32_t maxFrames, [[maybe_unused]] rp_error* err) {
    if constexpr (!requires(T& t) { t.activate(sampleRate, maxFrames); }) {
        return true;
    } else if constexpr (std::is_same_v<decltype(self<T>(n)->node.activate(sampleRate, maxFrames)), bool>) {
        if (self<T>(n)->node.activate(sampleRate, maxFrames)) return true;
        copyError(err, "activate failed");
        return false;
    } else {
        self<T>(n)->node.activate(sampleRate, maxFrames);
        return true;
    }
}

template <class T>
void deactivateFn([[maybe_unused]] rp_node* n) {
    if constexpr (requires(T& t) { t.deactivate(); }) self<T>(n)->node.deactivate();
}

template <class T>
inline constexpr rp_node_vtable kVtable{&activateFn<T>, &deactivateFn<T>, &portsChangedFn<T>,
                                        &processFn<T>,  nullptr,          nullptr,
                                        nullptr,        &interfaceFn<T>};

template <class T>
bool queryMemoryFn(const rp_node_type*, const rp_settings* s, rp_memory_req* out, rp_error* err) {
    typename SettingsOf<T>::type settings{};
    ErrorText why;
    if (!readSettings<T>(s, settings, why)) {
        copyError(err, why.view());
        return false;
    }
    const std::size_t extra = extraBytes<T>(settings);
    if (extra > std::numeric_limits<std::size_t>::max() - extraOffset<T>()) {
        copyError(err, "extra memory request is too large");
        return false;
    }
    out->size = extra ? extraOffset<T>() + extra : sizeof(Instance<T>);
    out->align = alignof(Instance<T>) > alignof(std::max_align_t) ? alignof(Instance<T>) : alignof(std::max_align_t);
    return true;
}

template <class T>
rp_node* createFn(const rp_node_type*, [[maybe_unused]] const rp_host* host, void* memory, const rp_settings* s,
                  rp_error* err) {
    using S = typename SettingsOf<T>::type;
    auto* inst = new (memory) Instance<T>{};
    inst->vt = &kVtable<T>;
    ErrorText why;
    // The settings are read where they live. The pins are taken before any hook sees the settings, so on_settings and
    // activate read the handles; bind_host comes first, as on the desktop.
    bool ok = readSettings<T>(s, inst->settings, why);
    if (ok) {
        if constexpr (requires(T& t) { t.bind_host(host); }) inst->node.bind_host(host);
        ok = acquireResources(inst->resources.span(), host, inst->settings, &why);
    }
    if (ok) {
        if (const std::size_t extra = extraBytes<T>(inst->settings)) {
            inst->extra = std::span<std::byte>(static_cast<std::byte*>(memory) + extraOffset<T>(), extra);
            std::memset(inst->extra.data(), 0, extra);
            if constexpr (requires(T& t, std::span<std::byte> e) { t.attach(e); }) inst->node.attach(inst->extra);
        }
        if constexpr (requires(T& t, const S& st) { t.on_settings(st); }) inst->node.on_settings(inst->settings);
        return inst;
    }
    // Settings that would not read, or a resource that would not pin: leave nothing behind.
    copyError(err, why.view());
    inst->~Instance<T>();
    return nullptr;
}

template <class T>
void destroyFn(rp_node* n) {
    self<T>(n)->~Instance<T>();
}

template <class T>
inline constexpr rp_node_type kNodeType{T::id,          nullptr,          nullptr,
                                        &queryMemoryFn<T>, &createFn<T>, &destroyFn<T>,
                                        typeFlags<T>(), nullptr};

}  // namespace detail
}  // namespace rp
