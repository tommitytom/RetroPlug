#pragma once

// The one place a struct's fields are walked (the main design's 4.7 fields_of seam): the authoring layer reflects a
// node's `inputs`, `outputs` and `params` structs and its Settings through these, on both builds of node.hpp.
//
// The walk is reflect-cpp's compile-time core and nothing above it: num_fields (aggregate initialisation counts the
// fields), bind_to_tuple (a structured binding gives a pointer to each) and the fake object a field's name is read
// from. Those headers are portable (they include <tuple>, <array> and <utility>, and allocate and throw nothing),
// where reflect-cpp's public face is not: rfl/to_view.hpp includes <iostream> and <stdexcept>, and
// rfl/named_tuple_t.hpp and the field names behind it (rfl::Literal) include <string> and <vector>. So the name is
// read here as reflect-cpp reads it, from the compiler's spelling of a function whose template argument points at the
// field, and a name is the one rfl::named_tuple_t gives the same field: the descriptors (port and param ids) do not
// move. graph-test's [fields] case holds fieldName to rfl::fields<S>()[I].name() over every shape of struct the layer
// walks, so a reflect-cpp upgrade that spells a name differently fails there by name.
//
// What reflect-cpp's own walk does and this one does not: rfl::Rename (a field read under another key) and
// rfl::Flatten (a struct's fields read as the parent's). A walked struct holding either is refused at compile time
// (IsRenameOrFlatten below; the desktop's description layer, which can see those types, marks them), since its keys
// here would not be the ones its JSON has.

#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>
#if __has_include(<source_location>)
#include <source_location>
#endif

#include <rfl/internal/bind_to_tuple.hpp>
#include <rfl/internal/cpp20/get_ith_field_from_fake_object.hpp>
#include <rfl/internal/cpp20/num_fields.hpp>

// Under C++26 reflection reflect-cpp's bind_to_tuple switches to its cpp26 walk while the count and the names here stay
// cpp20's: the two could disagree on a field's index.
#ifdef REFLECTCPP_USE_CPP26_REFLECTION
#error "fields.hpp reads reflect-cpp's cpp20 reflection core; REFLECTCPP_USE_CPP26_REFLECTION needs it reworked"
#endif

namespace rp {
namespace detail {

// Whether a field's type is an rfl::Rename or an rfl::Flatten. False here, where neither is visible (rfl/Rename.hpp
// includes <string>, so a device-mode build never has one); node_desc.hpp specialises it true for both.
template <class F>
struct IsRenameOrFlatten : std::false_type {};

template <class S> inline constexpr std::size_t kFieldCount = rfl::internal::cpp20::num_fields<S>;

// A pointer to each field of `s`, as a tuple (rfl::get<I> reads one).
template <class S>
auto fieldPointers(S& s) {
    return rfl::internal::bind_to_tuple(s);
}

// The type of field I.
template <class S, std::size_t I>
using FieldType = std::remove_cvref_t<std::remove_pointer_t<
    std::remove_cvref_t<decltype(rfl::get<static_cast<int>(I)>(fieldPointers(std::declval<S&>())))>>>;

// Clang wants the field pointer wrapped before it is a template argument, as reflect-cpp does it.
template <class T>
struct FieldPtr {
    T v;
};
template <class T>
FieldPtr(T) -> FieldPtr<T>;
template <class T>
constexpr auto wrapFieldPtr(const T& p) noexcept {
    return FieldPtr{p};
}

// The field Ptr points at, by name: the tail of this function's own name, which spells its template argument
// (reflect-cpp's get_field_name_str_view, compiler for compiler). Its return type is deduced on purpose: a named one
// (std::string_view) adds a "[with ... std::string_view = ...]" note to GCC's spelling, after the field's name.
template <class S, auto Ptr>
consteval auto fieldNameFromSignature() {
#if __cpp_lib_source_location >= 201907L
    const auto name = std::string_view{std::source_location::current().function_name()};
#elif defined(_MSC_VER)
    const auto name = std::string_view{__FUNCSIG__};
#else
    const auto name = std::string_view{__PRETTY_FUNCTION__};
#endif
#if defined(__clang__)
    const auto split = name.substr(0, name.size() - 2);
    return split.substr(split.find_last_of(":.") + 1);
#elif defined(__GNUC__)
    const auto split = name.substr(0, name.size() - 2);
    return split.substr(split.find_last_of(":") + 1);
#elif defined(_MSC_VER)
    const auto split = name.substr(0, name.size() - 7);
    return split.substr(split.rfind("->") + 2);
#else
    static_assert(sizeof(S) == 0, "field names are read for GCC, Clang and MSVC");
#endif
}

template <class S, std::size_t I>
consteval std::string_view fieldNameView() {
    using rfl::internal::cpp20::get_ith_field_from_fake_object;
#if defined(__clang__)
    return fieldNameFromSignature<S, wrapFieldPtr(get_ith_field_from_fake_object<S, static_cast<int>(I)>())>();
#else
    return fieldNameFromSignature<S, get_ith_field_from_fake_object<S, static_cast<int>(I)>()>();
#endif
}

// The name copied out of the function signature into storage of its own, so it is a constant any build can keep.
template <std::size_t N>
struct FieldNameText {
    char v[N + 1] = {};
};
template <class S, std::size_t I>
struct FieldName {
    static constexpr std::string_view kRaw = fieldNameView<S, I>();
    static constexpr FieldNameText<kRaw.size()> kText = [] {
        FieldNameText<kRaw.size()> t;
        for (std::size_t i = 0; i < kRaw.size(); ++i) t.v[i] = kRaw[i];
        return t;
    }();
};

// Field I's name, the key it has in a JSON or msgpack settings object.
template <class S, std::size_t I>
constexpr std::string_view fieldName() {
    return {FieldName<S, I>::kText.v, FieldName<S, I>::kRaw.size()};
}

// No field of S is an rfl::Rename or an rfl::Flatten (the header comment says why). Checked by both walks below.
template <class S>
consteval bool plainFields() {
    return []<std::size_t... I>(std::index_sequence<I...>) {
        return (!IsRenameOrFlatten<FieldType<S, I>>::value && ...);
    }(std::make_index_sequence<kFieldCount<S>>{});
}

// f(field) for each field of `s`, in declaration order.
template <class S, class F>
void forEachField(S& s, F&& f) noexcept {
    if constexpr (kFieldCount<S> > 0) {
        static_assert(plainFields<S>(), "a node's ports, params and settings hold no rfl::Rename or rfl::Flatten");
        auto ptrs = fieldPointers(s);
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (f(*rfl::get<static_cast<int>(I)>(ptrs)), ...);
        }(std::make_index_sequence<kFieldCount<S>>{});
    }
}

// f(name, field) for each field of `s`, in declaration order. Not `noexcept`, unlike forEachField, whose bodies only
// bind ports and params: the authoring layer's own callers here (pinning and resolving resources, a refusal written
// into a fixed ErrorText) throw nothing, but a desktop caller's callback may, and what it throws has to reach the
// caller's catch rather than terminate (graph-test's "the named field walk lets what its callers build throw out of
// it" holds that).
template <class S, class F>
void forEachNamedField(S& s, F&& f) {
    if constexpr (kFieldCount<S> > 0) {
        static_assert(plainFields<S>(), "a node's ports, params and settings hold no rfl::Rename or rfl::Flatten");
        auto ptrs = fieldPointers(s);
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (f(fieldName<S, I>(), *rfl::get<static_cast<int>(I)>(ptrs)), ...);
        }(std::make_index_sequence<kFieldCount<S>>{});
    }
}

}  // namespace detail
}  // namespace rp
