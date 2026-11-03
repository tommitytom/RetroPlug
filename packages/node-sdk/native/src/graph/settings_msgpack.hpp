#pragma once

// A node's Settings read from msgpack (RP_SETTINGS_MSGPACK, docs/modular-graph-phase-e1-design.md section 6): the
// decode a device runs at plan load, and the desktop's create and query_memory run too, so the device's decode is the
// one the desktop exercises. It allocates nothing and throws nothing, and is written over the compile-time field list
// (fields.hpp) rather than reflect-cpp's msgpack reader (msgpack_reader.hpp says why).
//
// It reads as rfl::json::read<Settings, rfl::DefaultIfMissing> reads JSON: the blob is a map; a field is matched by
// name; a field the map does not name keeps its default; a key that names no field is stepped over, so a blob a newer
// desktop wrote still loads the fields this build knows. A field's value is taken as the JSON reader takes it: an
// unsigned field reads a non-negative integer and a signed one any integer in its range, neither of them a float
// (2.0 is refused, as JSON's 2.0 is); a float field reads an integer or a float; a bool reads a bool; text, a choice,
// a C++ enum and a resource_ref read a str (an enum its enumerator's name, or a number in the string as std::stoi
// reads one; a resource_ref "res:N" or "", the string the canonical JSON carries); a list reads an array of its
// element. A text or list over its bound is refused naming the field, as is a value of the wrong type, and a blob that
// is not one well-formed map is refused whole. A refusal is worded as the JSON reader words the same one.
//
// What a Settings may hold to be read this way is kPortableSettings's list: numbers, bools, C++ enums with a fixed
// underlying type and no operator| (enums.hpp reads their names), rp::choice, rp::text, rp::list of those scalars,
// and rp::resource_ref, in a struct that is trivially copyable. An enum is written by its enumerator's name, as
// reflect-cpp writes it; rp::choice is the kind whose labels a document spells in kebab-case ("high-pass").

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

#include "graph/ResourceId.hpp"
#include "graph/enums.hpp"
#include "graph/fields.hpp"
#include "graph/msgpack_reader.hpp"
#include "graph/rp_node.h"
#include "graph/settings_types.hpp"

namespace rp {
namespace detail {

namespace msgpack = graph::msgpack;

// A reason built in place, the size of rp_error's message, for the paths that must not allocate: the msgpack decode
// and a device-mode create. Appends past the end are cut off.
struct ErrorText {
    char        text[sizeof(rp_error::message)] = {};
    std::size_t size = 0;

    ErrorText& operator<<(std::string_view s) {
        for (const char c : s) {
            if (size + 1 >= sizeof text) break;
            text[size++] = c;
        }
        text[size] = '\0';
        return *this;
    }
    ErrorText& operator<<(std::uint64_t n) {
        char digits[20];
        std::size_t k = 0;
        do {
            digits[k++] = static_cast<char>('0' + n % 10);
            n /= 10;
        } while (n != 0);
        while (k > 0) *this << std::string_view(&digits[--k], 1);
        return *this;
    }
    std::string_view view() const { return {text, size}; }
};

// ---- what a portable Settings holds --------------------------------------------------------------------------------

template <class T> struct IsText : std::false_type {};
template <std::size_t N> struct IsText<text<N>> : std::true_type {};
template <class T> struct IsList : std::false_type {};
template <class T, std::size_t N> struct IsList<list<T, N>> : std::true_type {};
template <class T> struct IsChoice : std::false_type {};
template <fixed_string... L> struct IsChoice<choice<L...>> : std::true_type {};
template <class T> struct IsResourceRef : std::false_type {};
template <fixed_string T> struct IsResourceRef<resource_ref<T>> : std::true_type {};

// A number, a bool, a choice, or a C++ enum enums.hpp can read (a fixed underlying type, no operator|).
template <class F>
inline constexpr bool kPortableScalar = std::is_arithmetic_v<F> || IsChoice<F>::value || kScannableEnum<F>;

template <class F>
consteval bool portableField() {
    if constexpr (IsList<F>::value) return kPortableScalar<typename F::value_type>;
    else return kPortableScalar<F> || IsText<F>::value || IsResourceRef<F>::value;
}

template <class S>
consteval bool portableSettings() {
    if constexpr (!std::is_class_v<S> || !std::is_aggregate_v<S> || !std::is_trivially_copyable_v<S>) {
        return false;
    } else {
        return []<std::size_t... I>(std::index_sequence<I...>) {
            return (portableField<FieldType<S, I>>() && ...);
        }(std::make_index_sequence<kFieldCount<S>>{});
    }
}

// Whether S is read from msgpack: the list in this file's header comment.
template <class S>
inline constexpr bool kPortableSettings = portableSettings<S>();

// ---- the decode ----------------------------------------------------------------------------------------------------

inline constexpr std::string_view kNotMsgpack = "settings are not valid msgpack";
inline constexpr std::string_view kOutOfRange = "Integer is out of range for the target type.";

// Write a refusal's reason, in parts, and answer false: `return fail(why, "a string of ", n, " bytes ...");`.
template <class... Parts>
bool fail(ErrorText& why, const Parts&... parts) {
    (why << ... << parts);
    return false;
}

// A decimal number at the start of `s`, as std::stoi reads one, which is how reflect-cpp's JSON reader takes an enum
// given as a number in a string ("7", " -1", "7 slow"): leading white space, a sign, at least one digit, and nothing
// after the digits read. False for no digits, or a value past int.
constexpr bool stoiLike(std::string_view s, int& out) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || (s[i] >= '\t' && s[i] <= '\r'))) ++i;
    const bool negative = i < s.size() && s[i] == '-';
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
    long long value = 0;
    std::size_t digits = 0;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i, ++digits) {
        value = value * 10 + (s[i] - '0');
        if (value > 2147483648LL) return false;
    }
    if (digits == 0) return false;
    if (negative) value = -value;
    if (value < -2147483648LL || value > 2147483647LL) return false;
    out = static_cast<int>(value);
    return true;
}

// One value into a field of type F. False with the reason (the JSON reader's words, where it has some) in `why`.
template <class F>
bool readField(msgpack::Reader& r, F& out, ErrorText& why) {
    if constexpr (std::is_same_v<F, bool>) {
        if (!r.readBool(out)) return fail(why, "Could not cast to boolean.");
    } else if constexpr (std::is_integral_v<F> && std::is_unsigned_v<F>) {
        msgpack::Int v;
        if (!r.readInt(v) || v.negative) return fail(why, "Could not cast to uint.");
        if constexpr (sizeof(F) < sizeof(std::uint64_t))
            if (v.u > std::numeric_limits<F>::max()) return fail(why, kOutOfRange);
        out = static_cast<F>(v.u);
    } else if constexpr (std::is_integral_v<F>) {
        msgpack::Int v;
        if (!r.readInt(v)) return fail(why, "Could not cast to int.");
        constexpr auto kMin = static_cast<std::int64_t>(std::numeric_limits<F>::min());
        constexpr auto kMax = static_cast<std::uint64_t>(std::numeric_limits<F>::max());
        if (v.negative ? v.i < kMin : v.u > kMax) return fail(why, kOutOfRange);
        out = v.negative ? static_cast<F>(v.i) : static_cast<F>(v.u);
    } else if constexpr (std::is_floating_point_v<F>) {
        // As the JSON reader takes a number: an integer or a float, through double whatever the field's own width.
        msgpack::Int v;
        double       d = 0.0;
        if (r.readInt(v)) d = v.negative ? static_cast<double>(v.i) : static_cast<double>(v.u);
        else if (!r.readFloat(d)) return fail(why, "Could not cast to double.");
        out = static_cast<F>(d);
    } else if constexpr (std::is_enum_v<F>) {
        // The enumerator's name, or a number in the string, as reflect-cpp's string_to_enum reads one.
        std::string_view s;
        if (!r.readStr(s)) return fail(why, "Could not cast to string.");
        int number = 0;
        if (Enumerators<F>::fromName(s, out)) return true;
        if (stoiLike(s, number)) {
            out = static_cast<F>(number);
            return true;
        }
        why << "Invalid enum value: '" << s << "'. Must be one of [";
        for (std::size_t i = 0; i < Enumerators<F>::count; ++i) why << (i ? ", " : "") << Enumerators<F>::name(i);
        return fail(why, "].");
    } else if constexpr (IsChoice<F>::value) {
        // The words rfl::Literal refuses with, which the desktop's JSON reading of a choice is.
        std::string_view s;
        if (!r.readStr(s)) return fail(why, "Could not cast to string.");
        if (out.assign(s)) return true;
        why << "Literal does not support string '" << s << "'. The following strings are supported: ";
        for (std::size_t i = 0; i < F::size(); ++i) why << (i ? ", '" : "'") << F::label(i) << "'";
        return fail(why, ".");
    } else if constexpr (IsText<F>::value) {
        std::string_view s;
        if (!r.readStr(s)) return fail(why, "Could not cast to string.");
        if (!out.assign(s))
            return fail(why, "a string of ", std::uint64_t{s.size()}, " bytes is over the ",
                        std::uint64_t{F::capacity()}, " the field holds");
    } else if constexpr (IsList<F>::value) {
        std::uint32_t n = 0;
        if (!r.readArray(n)) return fail(why, "Could not cast to array.");
        if (n > F::capacity())
            return fail(why, "an array of ", std::uint64_t{n}, " elements is over the ", std::uint64_t{F::capacity()},
                        " the field holds");
        out.clear();
        for (std::uint32_t k = 0; k < n; ++k) {
            typename F::value_type element{};
            ErrorText              inner;
            if (!readField(r, element, inner)) return fail(why, "element ", std::uint64_t{k}, ": ", inner.view());
            out.push_back(element);
        }
    } else if constexpr (IsResourceRef<F>::value) {
        std::string_view s;
        if (!r.readStr(s)) return fail(why, "Could not cast to string.");
        out.id = 0;
        if (!s.empty() && !graph::parseResourceId(s, out.id))
            return fail(why, "\"", s, "\" is not a resource reference (res:N)");
    } else {
        static_assert(sizeof(F) == 0, "a field kPortableSettings refuses");
    }
    return true;
}

// One named field, its refusal worded as the JSON reader words one ("Failed to parse field 'layout': ...").
template <class F>
bool readNamedField(msgpack::Reader& r, std::string_view name, F& field, ErrorText& why) {
    // No value where one must be (the end, or a byte msgpack never uses), or a value that runs past the end, is a
    // malformed blob rather than a wrong value.
    if (r.peek() == msgpack::Type::Invalid) return fail(why, kNotMsgpack);
    ErrorText reason;
    if (readField(r, field, reason)) return true;
    if (r.truncated()) return fail(why, kNotMsgpack);
    return fail(why, "Failed to parse field '", name, "': ", reason.view());
}

// The field of S called `key`, read from the value the reader is at: true with *known set when S has one (*read says
// whether its value was taken), *known false when it has none.
template <class S>
bool readKnownField(msgpack::Reader& r, std::string_view key, S& out, ErrorText& why, bool* known) {
    *known = false;
    bool read = true;
    if constexpr (kFieldCount<S> > 0) {
        auto fields = fieldPointers(out);
        // Each field in turn, until one's name is the key: that one reads the value.
        auto tryField = [&]<std::size_t I>(std::integral_constant<std::size_t, I>) {
            if (key != fieldName<S, I>()) return false;
            *known = true;
            read = readNamedField(r, fieldName<S, I>(), *rfl::get<static_cast<int>(I)>(fields), why);
            return true;
        };
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (void)(tryField(std::integral_constant<std::size_t, I>{}) || ...);
        }(std::make_index_sequence<kFieldCount<S>>{});
    }
    return read;
}

// Read `size` bytes of msgpack into `out`, which holds the defaults beforehand: the fields the map names are replaced,
// the others keep them. False, with the reason in `why` (and `out` partly written), for a blob that is refused; an
// empty blob is the empty map.
template <class S>
bool decodeSettings(const void* data, std::size_t size, S& out, ErrorText& why) {
    static_assert(kPortableSettings<S>, "only a portable Settings is read from msgpack (settings_msgpack.hpp)");
    if (size == 0) return true;
    msgpack::Reader r(static_cast<const std::uint8_t*>(data), size);
    std::uint32_t   count = 0;
    if (!r.readMap(count)) return fail(why, r.truncated() ? kNotMsgpack : "settings must be a msgpack map");
    for (std::uint32_t k = 0; k < count; ++k) {
        std::string_view key;
        if (!r.readStr(key)) {
            const bool malformed = r.truncated() || r.peek() == msgpack::Type::Invalid;
            return fail(why, malformed ? kNotMsgpack : "a settings key must be a string");
        }
        bool known = false;
        if (!readKnownField(r, key, out, why, &known)) return false;
        if (!known && !r.skip()) return fail(why, kNotMsgpack);  // a key S has no field for: stepped over
    }
    if (!r.atEnd()) return fail(why, "settings hold bytes after their map");
    return true;
}

// A settings-less node's blob: any map, whose keys it ignores, as the JSON side takes any object.
inline bool decodeNoSettings(const void* data, std::size_t size, ErrorText& why) {
    if (size == 0) return true;
    msgpack::Reader r(static_cast<const std::uint8_t*>(data), size);
    if (r.peek() != msgpack::Type::Map) return fail(why, r.atEnd() ? kNotMsgpack : "settings must be a msgpack map");
    if (!r.skip()) return fail(why, kNotMsgpack);
    if (!r.atEnd()) return fail(why, "settings hold bytes after their map");
    return true;
}

}  // namespace detail
}  // namespace rp
