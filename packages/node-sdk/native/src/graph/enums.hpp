#pragma once

// A C++ enum's enumerators, names and values, at compile time and with no heap: what a device-mode build reads an enum
// settings field and an enum_param by (settings_msgpack.hpp, node_core.hpp). Both builds use it; the desktop's
// description layer holds it to reflect-cpp's own enum reflection (node_desc.hpp's enumMatchesReflect), so a name here
// is the one rfl::json reads and writes for the same enumerator.
//
// reflect-cpp's enum reflection is enchantum's, behind rfl/enums.hpp, which includes <sstream> and <string>; this is
// the same technique in a few lines: every value in the scanned range is a template argument of a function whose own
// name spells it, an enumerator by its identifier and any other value as a cast ("(Kind)5"), and the values whose
// spelling ends in an identifier are the enumerators. The range is enchantum's default: -256 to 256, clipped to the
// underlying type, and 0 up for an unsigned one. An enum whose underlying type is not fixed (an unscoped enum with no
// `: type`) cannot be scanned (a value outside its enumerators is not a constant there), and a flag enum (one with
// operator|, which reflect-cpp reads as names joined by '|') is not one this reads either: kScannableEnum says which
// are.

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace rp {
namespace detail {

template <class E>
concept FixedEnum = std::is_enum_v<E> && requires { E{0}; };

template <class E>
concept FlagEnum = std::is_enum_v<E> && requires(E e) {
    { e | e } -> std::same_as<E>;
};

// Whether E's enumerators can be read here: an enum with a fixed underlying type that is not a flag enum.
template <class E>
inline constexpr bool kScannableEnum = FixedEnum<E> && !FlagEnum<E>;

template <class E>
struct EnumRange {
    using U = std::underlying_type_t<E>;
    // Each bound compared where it cannot wrap: a signed type's least value fits a long long, and any type's greatest
    // fits an unsigned long long, where a 64-bit unsigned one would turn negative as a long long.
    static constexpr long long kLeast =
        std::is_signed_v<U> ? static_cast<long long>((std::numeric_limits<U>::min)()) : 0;
    static constexpr unsigned long long kMost = static_cast<unsigned long long>((std::numeric_limits<U>::max)());
    static constexpr long long lo = kLeast > -256 ? kLeast : -256;
    static constexpr long long hi = kMost < 256 ? static_cast<long long>(kMost) : 256;
    static constexpr std::size_t size = static_cast<std::size_t>(hi - lo + 1);
};

// V's spelling, the identifier this function's own name ends in, or empty when V is no enumerator. The return type is
// deduced, so GCC's spelling ends at V (a named one would add a "[with ...]" note after it), and the function is
// noexcept, which MSVC's spelling then ends with.
template <class E, E V>
consteval auto enumNameFromSignature() noexcept {
#if defined(__clang__) || defined(__GNUC__)
    const std::string_view name{__PRETTY_FUNCTION__, sizeof(__PRETTY_FUNCTION__) - 2};  // up to the closing ']'
#elif defined(_MSC_VER)
    const std::string_view name{__FUNCSIG__, sizeof(__FUNCSIG__) - 17};  // up to ">(void) noexcept"
#else
    static_assert(sizeof(E) == 0, "enum names are read for GCC, Clang and MSVC");
#endif
    std::size_t start = name.size();
    while (start > 0) {
        const char c = name[start - 1];
        const bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!word) break;
        --start;
    }
    const std::string_view tail(name.data() + start, name.size() - start);
    const bool identifier = !tail.empty() && !(tail[0] >= '0' && tail[0] <= '9');
    return identifier ? tail : std::string_view();
}

// One value's name, copied out of the signature into storage of its own.
template <std::size_t N>
struct EnumNameText {
    char v[N + 1] = {};
};
template <class E, E V>
struct EnumName {
    static constexpr std::string_view kRaw = enumNameFromSignature<E, V>();
    static constexpr EnumNameText<kRaw.size()> kText = [] {
        EnumNameText<kRaw.size()> t;
        for (std::size_t i = 0; i < kRaw.size(); ++i) t.v[i] = kRaw[i];
        return t;
    }();
    static constexpr std::string_view view() { return {kText.v, kRaw.size()}; }
};

template <class E, std::size_t I>
constexpr E kScanned = static_cast<E>(EnumRange<E>::lo + static_cast<long long>(I));

template <class T, std::size_t N>
struct EnumArray {
    T v[N > 0 ? N : 1] = {};
};

// Which scanned positions are enumerators. A pack expanded into a list, not a fold: Clang caps a fold's nesting at 256,
// and the range is 513 values.
template <class E>
inline constexpr auto kEnumValid = []<std::size_t... I>(std::index_sequence<I...>) {
    return EnumArray<bool, EnumRange<E>::size>{{!EnumName<E, kScanned<E, I>>::kRaw.empty()...}};
}(std::make_index_sequence<EnumRange<E>::size>{});

// How many enumerators E has in the scanned range.
template <class E>
inline constexpr std::size_t kEnumCount = [] {
    std::size_t n = 0;
    for (std::size_t i = 0; i < EnumRange<E>::size; ++i) n += kEnumValid<E>.v[i] ? 1 : 0;
    return n;
}();

// The scanned positions that are enumerators, in order.
template <class E>
inline constexpr auto kEnumPositions = [] {
    EnumArray<std::size_t, kEnumCount<E>> p;
    std::size_t                           n = 0;
    for (std::size_t i = 0; i < EnumRange<E>::size; ++i)
        if (kEnumValid<E>.v[i]) p.v[n++] = i;
    return p;
}();

template <class E>
inline constexpr auto kEnumValues = []<std::size_t... K>(std::index_sequence<K...>) {
    return EnumArray<E, kEnumCount<E>>{{static_cast<E>(EnumRange<E>::lo +
                                                       static_cast<long long>(kEnumPositions<E>.v[K]))...}};
}(std::make_index_sequence<kEnumCount<E>>{});

template <class E>
inline constexpr auto kEnumNames = []<std::size_t... K>(std::index_sequence<K...>) {
    return EnumArray<std::string_view, kEnumCount<E>>{{EnumName<E, kEnumValues<E>.v[K]>::view()...}};
}(std::make_index_sequence<kEnumCount<E>>{});

// E's enumerators in ascending order of value, as reflect-cpp lists them: count, value(i) and name(i).
template <class E>
struct Enumerators {
    static_assert(kScannableEnum<E>, "an enum read at compile time has a fixed underlying type and no operator|");
    static constexpr std::size_t count = kEnumCount<E>;
    static constexpr E                value(std::size_t i) { return kEnumValues<E>.v[i]; }
    static constexpr std::string_view name(std::size_t i) { return kEnumNames<E>.v[i]; }

    // The enumerator called `name`, or false.
    static constexpr bool fromName(std::string_view want, E& out) {
        for (std::size_t i = 0; i < count; ++i)
            if (name(i) == want) {
                out = value(i);
                return true;
            }
        return false;
    }
};

}  // namespace detail
}  // namespace rp
