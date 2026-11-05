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
//                                                         JSON Schema $defs key comes from the type name). A
//                                                         field of type rp::resource_ref<Type> names a resource
//                                                         ("res:N"), which create pins and destroy releases
//                                                         (settings_types.hpp has the fixed-capacity types a
//                                                         device-built node's settings use)
//   static std::optional<std::string> validate(const Settings&);      reject settings with a reason
//   static void adjust_layout(rp::graph::Layout&, const Settings&);   patch reflected specs (a queue's capacity
//                                                         among them), append ports or params; never remove or
//                                                         reorder reflected ones
//   static bool needs_rebuild(const Settings& from, const Settings& to);
//   static Key rebuild_key(const Settings&);              a value only a rebuild can change (Key is any copyable
//                                                         type with ==): prepare compares the new settings' key
//                                                         with the key the instance was created with, which no
//                                                         commit writes, so unlike needs_rebuild it allows
//                                                         stateless_prepare. Never both on one type
//   static std::size_t extra_memory(const Settings&);     host-supplied tail memory (max_align_t aligned; in a
//                                                         device-mode build, rp::graph::kDeviceAlign aligned)
//   void attach(std::span<std::byte>) noexcept;           receives it, at create. The objects a node builds
//                                                         there are never destroyed - nothing runs a destructor
//                                                         over tail memory - so a node whose state owns anything
//                                                         (an allocation, a handle) keeps it in a member, built
//                                                         in activate, which destroy does free
//   void on_settings(const Settings&) noexcept;           at create (control) and each commit (audio): copy,
//                                                         never allocate
//   void bind_host(const rp_host*) noexcept;              at create: host services
//   bool activate(double sampleRate, std::uint32_t maxFrames);   (or void)   void deactivate();
//   void ports_changed(std::span<const std::uint8_t> connectedOutputs) noexcept;   (the span lives for the call)
//   void prepare(const rp::Block&) noexcept / step(std::uint32_t target) noexcept / void finish(const rp::Block&) noexcept
//       -> rp.step/1 (all three or none; step returns bool - true once the block is produced - or an rp_step_result)
//   void set_peers(std::span<rp_node* const>) noexcept    -> rp.link/1   (the span lives for the call)
//   const void* get_interface(std::string_view id) noexcept;   [any] an interface of the node's own beyond the two
//                                                         above (a device node's capability jobs), or null: what
//                                                         rp_node_vtable::get_interface answers after rp.step/1 and
//                                                         rp.link/1, so it may point into the instance. The ABI's
//                                                         ids are reserved (answered before this is asked), and a
//                                                         host casts an answer only to the struct of the id it
//                                                         asked by
//   static constexpr const char *name, *category, *version;  static constexpr bool heap, threads, unique;
//   static constexpr bool reshapes;                       keep the instance through a settings change that alters
//                                                         its layout (ports or params adjust_layout appends): prepare
//                                                         answers RP_PREPARE_RESHAPE, the planner rebinds the ports
//                                                         by id, and on_settings sees the new settings at the swap.
//                                                         Default false: such a change rebuilds
//   static constexpr bool stateless_prepare;              prepare reads nothing a commit writes, so setGraph never
//                                                         waits for this node's commits (RP_TYPE_STATELESS_PREPARE);
//                                                         rules out needs_rebuild, which reads the current settings
//                                                         (rebuild_key does not)
//
// process(const rp::Block&) is required, noexcept, and returns void or rp_status. Exceptions never cross
// the ABI: every control-thread entry catches, and every audio-thread entry is noexcept.
//
// Two builds (docs/modular-graph-phase-e1-design.md section 6). The desktop's, the default, is all of the above. A
// device-mode build (-DRP_DEVICE, with no exceptions and no RTTI) is the portable core alone: the wrappers, rp::Block,
// the reflection, the hooks a block or an instance's lifecycle calls, and create / destroy, with the settings read
// from msgpack into the instance itself. A device runs a plan the desktop planned and validated, so its build has no
// descriptor, no layout, no JSON and no prepare / commit, and never calls validate or adjust_layout: a node source
// that is built for the device puts those two (and whatever only they use) inside `#ifndef RP_DEVICE`, since they name
// desktop types (std::string, rp::graph::Layout). A type built for the device declares neither `heap` nor `threads`
// (a node's `threads` is what the node itself needs; a device link's own thread is the link's business),
// and its Settings holds only numbers, bools, enums (a fixed underlying type, no operator|), rp::choice, rp::text,
// rp::list and rp::resource_ref; either is checked at compile time, on the desktop too, whose default build compiles
// every `device` package entry in device mode as well (cmake/packages.cmake). An enum_param works on both builds.
// The files: node_core.hpp (both builds), node_device.hpp (RP_DEVICE), node_desc.hpp (the desktop).

#include "graph/node_core.hpp"
#ifdef RP_DEVICE
#include "graph/node_device.hpp"
#else
#include "graph/node_desc.hpp"
#endif

namespace rp {

// The ABI table for a C++ node type: one per T, a constant (so a package's list of them needs no run-time
// initialisation); every optional hook's signature is checked here, at compile time.
template <class T>
constexpr const rp_node_type* node_type() {
    static_assert(detail::checkHooks<T>());
#ifdef RP_DEVICE
    static_assert(detail::DeviceTypeCheck<T>::ok);
#else
    static_assert(detail::checkDescriptionHooks<T>());
#endif
    return &detail::kNodeType<T>;
}

// A coupled peer as its concrete type, or null when the peer is a different node type.
template <class T>
T* peer_as(rp_node* n) {
    if (!n || n->vt != &detail::kVtable<T>) return nullptr;
    return &detail::self<T>(n)->node;
}

}  // namespace rp

#if defined(RP_DEVICE) && defined(__GLIBCXX__)
// What a device-mode build must not carry: the standard library's heap and exception headers, and reflect-cpp's JSON.
// The check runs here, at the end of graph/node.hpp, so it sees what the translation unit included BEFORE this header
// (the node source's own includes, the kernels' headers) and what the SDK itself includes, and nothing included after
// it. It is an early, readable error, not the guarantee: the device build's link trap (malloc, operator new,
// __cxa_throw and the std::__throw_* family undefined) is. Each name is the header's own include guard in libstdc++,
// the device toolchain's library (the desktop's device-mode test builds with whichever library its compiler has, and
// only libstdc++'s guards are checked).
#if defined(_GLIBCXX_STRING) || defined(_GLIBCXX_VECTOR) || defined(_GLIBCXX_MAP) || defined(_GLIBCXX_OPTIONAL) || \
    defined(_GLIBCXX_STDEXCEPT) || defined(__EXCEPTION__) || defined(_GLIBCXX_IOSTREAM) ||                      \
    defined(_GLIBCXX_FUNCTIONAL) || defined(_GLIBCXX_MEMORY) || defined(_GLIBCXX_RANDOM)
#error "a device-mode build includes a heap or exception header: <string>, <vector>, <memory>, <random>, ... (above)"
#endif
#if defined(RFL_JSON_HPP_) || defined(YYJSON_H)
#error "a device-mode build includes reflect-cpp's JSON or yyjson"
#endif
#endif
