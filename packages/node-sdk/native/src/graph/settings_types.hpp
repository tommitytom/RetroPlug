#pragma once

// Fixed-capacity settings types (docs/modular-graph-phase-e1-design.md section 5). A node meant for the device keeps
// every settings field inline: no allocation to read, copy or destroy one. node.hpp includes this header, so a node
// names them as rp::text, rp::list and rp::choice.
//
//   struct Settings {
//       rp::text<16>                      layout = "stereo";              // JSON: "stereo"
//       rp::list<std::uint32_t, 16>       counts = {1, 1};                // JSON: [1, 1]
//       rp::choice<"audio", "control">    kind = decltype(kind)::make<"audio">();   // JSON: "audio"
//   };
//
// Each is read and written as the JSON the type it stands in for was (std::string, std::vector, rfl::Literal), and its
// JSON Schema is that type's, so a descriptor does not change when a field moves to one. A JSON value over the bound is
// a settings error; reflect-cpp names the field it was reading in the message. The JSON side is settings_json.hpp, a
// desktop one, since reading builds a std::string or std::vector first; the device reads the same fields from msgpack
// (settings_msgpack.hpp, design section 6), and the types themselves allocate nothing. This header is portable: it is
// part of both builds of node.hpp.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string_view>
#include <type_traits>
#ifndef RP_DEVICE
#include <string>
#endif

#include "graph/rp_node.h"
#include "graph/sdk_namespace.hpp"

RP_NAMESPACE_BEGIN

// A string as a template argument: a port's channel layout, a param's name, a resource type, a choice's labels.
template <std::size_t N>
struct fixed_string {
    char v[N]{};
    constexpr fixed_string(const char (&s)[N]) {  // NOLINT(google-explicit-constructor)
        for (std::size_t i = 0; i < N; ++i) v[i] = s[i];
    }
    constexpr std::string_view view() const { return {v, N - 1}; }
};

namespace detail {
// Called by the consteval constructors when a literal or a braced list is over its type's bound, and by choice's
// consteval names when a label is none of its labels. Each is declared and never defined, and not constexpr, so
// evaluating one is a compile error where the default is written, and no build ever carries a call to it; a run-time
// value goes through assign / push_back, which answer false instead.
void capacityExceeded();
void notALabel();
}  // namespace detail

// Up to N bytes of UTF-8 stored inline. The bound is bytes, not code points, and nothing checks the encoding: a
// value is a byte string that happens to be what a JSON string carried.
template <std::size_t N>
class text {
public:
    static_assert(N > 0 && N < 65536, "a text holds 1 to 65535 bytes");
    using size_type = std::conditional_t<(N <= 255), std::uint8_t, std::uint16_t>;

    constexpr text() = default;

    // From a literal, which is checked against the bound where it is written.
    template <std::size_t M>
    consteval text(const char (&s)[M]) {  // NOLINT(google-explicit-constructor): a default reads `text<16> x = "a"`
        static_assert(M - 1 <= N, "the literal does not fit the text's capacity");
        for (std::size_t i = 0; i + 1 < M; ++i) data_[i] = s[i];
        size_ = static_cast<size_type>(M - 1);
    }

    static constexpr std::size_t capacity() { return N; }

    // Replace the contents. False, with the contents unchanged, when `s` is over the bound.
    constexpr bool assign(std::string_view s) {
        if (s.size() > N) return false;
        for (std::size_t i = 0; i < N; ++i) data_[i] = i < s.size() ? s[i] : '\0';  // the tail stays zero
        size_ = static_cast<size_type>(s.size());
        return true;
    }

    constexpr std::string_view view() const { return {data_, size_}; }
    constexpr operator std::string_view() const { return view(); }  // NOLINT(google-explicit-constructor)
    constexpr std::size_t size() const { return size_; }
    constexpr bool        empty() const { return size_ == 0; }
    constexpr const char* data() const { return data_; }
#ifndef RP_DEVICE
    std::string           str() const { return std::string(view()); }  // the desktop's, which builds descriptors
#endif

    friend constexpr bool operator==(const text& a, std::string_view b) { return a.view() == b; }

private:
    char      data_[N] = {};
    size_type size_ = 0;
};

// Up to N elements of T stored inline. T is a plain value (a number, an enum, a small struct): it is default
// constructed N times, copied bytewise by assignment, and never destroyed one at a time.
template <class T, std::size_t N>
class list {
public:
    static_assert(N > 0 && N < 65536, "a list holds 1 to 65535 elements");
    static_assert(std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>,
                  "a list holds plain values");
    // A resource_ref is pinned only as a top-level settings field (node.hpp walks no further), so one in a list would
    // name a resource nothing holds.
    static_assert(!requires { T::resource_type(); }, "a resource_ref cannot be a list element");
    using value_type = T;
    using size_type = std::conditional_t<(N <= 255), std::uint8_t, std::uint16_t>;

    constexpr list() = default;
    // From a braced list, which is checked against the bound where it is written. A list is filled at run time with
    // push_back.
    consteval list(std::initializer_list<T> values) {  // NOLINT(google-explicit-constructor)
        if (values.size() > N) detail::capacityExceeded();
        for (const T& v : values) data_[size_++] = v;
    }

    static constexpr std::size_t capacity() { return N; }

    // Append. False, with the list unchanged, when it is full.
    constexpr bool push_back(const T& v) {
        if (size_ == N) return false;
        data_[size_++] = v;
        return true;
    }
    constexpr void clear() {
        for (std::size_t i = 0; i < size_; ++i) data_[i] = T{};  // the tail stays value-initialized
        size_ = 0;
    }

    constexpr std::size_t size() const { return size_; }
    constexpr bool        empty() const { return size_ == 0; }
    constexpr const T&    operator[](std::size_t i) const { return data_[i]; }
    constexpr T&          operator[](std::size_t i) { return data_[i]; }
    constexpr const T*    begin() const { return data_; }
    constexpr const T*    end() const { return data_ + size_; }
    constexpr T*          begin() { return data_; }
    constexpr T*          end() { return data_ + size_; }
    constexpr std::span<const T> view() const { return {data_, size_}; }

    friend constexpr bool operator==(const list& a, const list& b) {
        if (a.size_ != b.size_) return false;
        for (std::size_t i = 0; i < a.size_; ++i)
            if (!(a.data_[i] == b.data_[i])) return false;
        return true;
    }

private:
    T         data_[N] = {};
    size_type size_ = 0;
};

// One of a fixed set of labels, stored as its index: a settings enum whose labels are what a document carries
// ("low-pass", "high-pass-tpdf"), which a C++ enumerator cannot spell. It stands in for rfl::Literal, whose interface
// it keeps (value() is the label's index, make<"label">() and value_of<"label">() name one at compile time), and is
// read and written as the same JSON string with the same schema; it holds one byte (two past 255 labels) and no
// string.
template <fixed_string... Labels>
class choice {
public:
    static_assert(sizeof...(Labels) > 0 && sizeof...(Labels) < 65536, "a choice has 1 to 65535 labels");
    using value_type = std::conditional_t<(sizeof...(Labels) <= 255), std::uint8_t, std::uint16_t>;

    static constexpr std::size_t size() { return sizeof...(Labels); }
    // Label i, in declaration order.
    static constexpr std::string_view label(std::size_t i) {
        constexpr std::string_view kLabels[] = {Labels.view()...};
        return kLabels[i];
    }
    // The index of `name`, or size() when it is none of the labels.
    static constexpr std::size_t index_of(std::string_view name) {
        for (std::size_t i = 0; i < size(); ++i)
            if (label(i) == name) return i;
        return size();
    }
    template <fixed_string Name>
    static consteval value_type value_of() {
        if (index_of(Name.view()) == size()) detail::notALabel();
        return static_cast<value_type>(index_of(Name.view()));
    }
    template <fixed_string Name>
    static consteval choice make() {
        choice c;
        c.value_ = value_of<Name>();
        return c;
    }

    constexpr choice() = default;  // the first label

    constexpr value_type       value() const { return value_; }
    constexpr std::string_view name() const { return label(value_); }

    // Set by label. False, with the value unchanged, when `name` is none of the labels.
    constexpr bool assign(std::string_view name) {
        const std::size_t i = index_of(name);
        if (i == size()) return false;
        value_ = static_cast<value_type>(i);
        return true;
    }
    // Set by index. False, with the value unchanged, past the last label.
    constexpr bool assign_index(std::size_t i) {
        if (i >= size()) return false;
        value_ = static_cast<value_type>(i);
        return true;
    }

    friend constexpr bool operator==(const choice& a, const choice& b) { return a.value_ == b.value_; }

private:
    value_type value_ = 0;
};

// A TOP-LEVEL settings field that names a resource: the document writes "res:N" and the node reads the bytes without
// ever touching the filesystem (docs/modular-graph-phase-c3-design.md section 4). The authoring layer takes the pin
// when it creates the instance and drops it when the instance is destroyed, both on the control thread (the same pin
// docs/modular-graph-design.md section 2.5 gives a node that references a resource from its settings). A field whose
// ref moved is a REBUILD, which node.hpp decides itself: the fresh instance pins what it names and the old pin drops
// with the instance the last plan ran. So a resource changes under a node only by a rebuild, and no pin is ever taken
// or dropped on the audio thread.
//
// Create pins before any hook that sees the instance's settings runs, so `on_settings`, `activate`, `process`, `step`
// and `finish` read a resolved handle (and a prepare that keeps the refs copies the instance's pins into the settings
// object it installs). The hooks that run before create has pinned anything (`validate`, `adjust_layout`,
// `rebuild_key`, `extra_memory`, `query_memory`) see only `id` (and `empty()`): a node must not read `bytes()` or
// `audio()` there. `needs_rebuild` sees handles in both objects, and is skipped for a ref that moved, which is a
// rebuild on its own.
//
//   struct Settings { rp::resource_ref<"rom"> rom; };   // JSON: {"rom": "res:7"}; absent: the node names none
//
// The type is the resource type an id must have (the host's putResource makes "rom", "sram", "state"); a resource
// of another type, an unknown or dropped id, or a host with no table refuses the create with the field's name.
template <fixed_string Type>
struct resource_ref {
    // Read and written as the plain string the document carries, never the handle it resolves to (settings_json.hpp
    // on the desktop, settings_msgpack.hpp on either build). The id is held as the number, so a settings object owns
    // nothing on the heap for it.
    static constexpr std::string_view resource_type() { return Type.view(); }

    std::uint32_t      id = 0;              // N of the document's "res:N" (0: the document names none)
    const rp_resource* resource = nullptr;  // resolved by the host layer at create, and again at each prepare

    bool empty() const { return id == 0; }
#ifndef RP_DEVICE
    // The document's spelling of the id, for an error message: "res:N", or empty for none.
    std::string ref() const { return id ? "res:" + std::to_string(id) : std::string(); }
#endif

    // The payload of a bytes resource (every type but "audio-buffer"), or null when the field names none.
    const rp_bytes* bytes() const {
        return resource && resource->payload && std::strcmp(resource->type, "audio-buffer") != 0
                   ? static_cast<const rp_bytes*>(resource->payload)
                   : nullptr;
    }
    // The payload of an "audio-buffer" resource, or null when the field names none or names another type.
    const rp_audio_buffer* audio() const {
        return resource && resource->payload && std::strcmp(resource->type, "audio-buffer") == 0
                   ? static_cast<const rp_audio_buffer*>(resource->payload)
                   : nullptr;
    }
};

RP_NAMESPACE_END
