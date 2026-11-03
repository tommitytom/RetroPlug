// The node SDK's field walk (graph/fields.hpp) held to reflect-cpp's own. The walk reads a field's name the way
// reflect-cpp does, over reflect-cpp's compile-time internals, and every JSON key a node's settings are read by is
// reflect-cpp's, so the two must name every field alike: a port or param id, a settings key a msgpack blob is matched
// by, a resource field a refusal names. Held here over each shape of struct the authoring layer walks (a named
// namespace's, an anonymous namespace's, a class template's, the unnamed member struct a node's `inputs` is) and over a
// field of every kind a settings struct or a port struct holds, so a reflect-cpp upgrade that spells a name otherwise
// fails here by name rather than as a moved descriptor golden. The enum scan a device reads an enum's names with
// (graph/enums.hpp) is held to reflect-cpp's enumerator list the same way, over every underlying type it takes.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <rfl/enums.hpp>
#include <rfl/fields.hpp>

#include "graph/node.hpp"

namespace fields_test {

enum class Mode : std::uint8_t { Off, On };

// One enum per underlying type the scan takes, with enumerators at the ends of its range where the type reaches them.
enum class U8 : std::uint8_t { Zero, Gap = 7, Top = 255 };
enum class S8 : std::int8_t { Bottom = -128, Minus = -1, Top = 127 };
enum class S16 : std::int16_t { Least = -256, Most = 256, Past = 300 };  // Past is outside the scanned range
enum class U16 : std::uint16_t { A = 1, B = 256 };
enum class S32 { Neg = -5, Zero, Big = 200 };
enum class S64 : std::int64_t { A = -3, B = 3 };
enum class U64 : std::uint64_t { First, Second = 2, Last = 256 };
enum class ULong : unsigned long { One = 1, Two };  // 64 bits on LP64 Linux and macOS, 32 on Windows
enum Plain : int { PlainA = 4, PlainB = 9 };       // unscoped, with a fixed type
enum class Bool : bool { No, Yes };
enum class Char : char { A = 'a', Z = 'z' };

struct Kinds {
    bool                              flag = false;
    std::uint8_t                      small = 0;
    std::int64_t                      wide = 0;
    float                             level = 0.0f;
    double                            precise = 0.0;
    Mode                              mode = Mode::Off;
    rp::choice<"one", "two">          pick;
    rp::text<8>                       name;
    rp::list<std::uint32_t, 4>        counts;
    rp::resource_ref<"rom">           rom;
    int                               x_1 = 0;  // an underscore and a digit
    int                               lastFieldWithALongerName = 0;
};

template <class T>
struct Boxed {
    T             value{};
    std::uint32_t count = 0;
};

struct Node {
    struct {
        rp::audio_in<rp::any> in;
        rp::events_in         events;
        rp::control_in        control;
    } inputs;
    struct { rp::audio_out<rp::match<"in">> out; } outputs;
    struct {
        rp::float_param<"Gain"> gain;
        rp::enum_param<Mode, "Mode", Mode::On> mode;
    } params;
};

}  // namespace fields_test

namespace {

struct Local {
    int   a = 0;
    float b = 0.0f;
};

template <class S>
std::vector<std::string> ours() {
    return []<std::size_t... I>(std::index_sequence<I...>) {
        return std::vector<std::string>{std::string(rp::detail::fieldName<S, I>())...};
    }(std::make_index_sequence<rp::detail::kFieldCount<S>>{});
}

template <class S>
std::vector<std::string> reflectCpps() {
    std::vector<std::string> names;
    for (const auto& f : rfl::fields<S>()) names.push_back(f.name());
    return names;
}

template <class S>
void same(const char* what) {
    INFO(what);
    CHECK(ours<S>() == reflectCpps<S>());
    CHECK(rp::detail::kFieldCount<S> == rfl::fields<S>().size());
}

}  // namespace

template <class E>
void sameEnum(const char* what) {
    INFO(what);
    constexpr auto kReflected = rfl::get_enumerator_array<E>();
    using Ours = rp::detail::Enumerators<E>;
    REQUIRE(Ours::count == kReflected.size());
    for (std::size_t i = 0; i < kReflected.size(); ++i) {
        INFO(i);
        CHECK(Ours::name(i) == kReflected[i].first);
        CHECK(Ours::value(i) == kReflected[i].second);
    }
    CHECK(Ours::count > 0);
    static_assert(rp::detail::enumMatchesReflect<E>());
}

TEST_CASE("the enum scan lists every enum's enumerators as reflect-cpp does", "[fields][enums]") {
    using namespace fields_test;
    sameEnum<U8>("uint8_t, with a gap and the type's top");
    sameEnum<S8>("int8_t, both of the type's ends");
    sameEnum<S16>("int16_t, the scanned range's ends and one past them");
    sameEnum<U16>("uint16_t");
    sameEnum<S32>("int, negative and past the int8 range");
    sameEnum<S64>("int64_t");
    sameEnum<U64>("uint64_t, whose greatest value is no long long");
    sameEnum<ULong>("unsigned long");
    sameEnum<Plain>("an unscoped enum with a fixed type");
    sameEnum<Bool>("bool");
    sameEnum<Char>("char");
    CHECK(rp::detail::Enumerators<U64>::count == 3);
    CHECK(rp::detail::Enumerators<ULong>::count == 2);
}

TEST_CASE("the field walk names every field as reflect-cpp does", "[fields]") {
    same<fields_test::Kinds>("every settings kind, in a named namespace");
    same<Local>("an anonymous namespace's struct");
    same<fields_test::Boxed<rp::text<4>>>("a class template");
    same<fields_test::Boxed<fields_test::Mode>>("a class template over an enum");
    same<decltype(fields_test::Node::inputs)>("a node's unnamed inputs struct");
    same<decltype(fields_test::Node::outputs)>("a node's unnamed outputs struct");
    same<decltype(fields_test::Node::params)>("a node's unnamed params struct");

    CHECK(ours<fields_test::Kinds>().front() == "flag");
    CHECK(ours<decltype(fields_test::Node::params)>() == std::vector<std::string>{"gain", "mode"});
}
