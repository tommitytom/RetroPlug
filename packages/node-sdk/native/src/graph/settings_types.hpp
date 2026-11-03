#pragma once

// Fixed-capacity settings types (docs/modular-graph-phase-e1-design.md section 5). A node meant for the device keeps
// every settings field inline: no allocation to read, copy or destroy one. node.hpp includes this header, so a node
// names them as rp::text and rp::list.
//
//   struct Settings {
//       rp::text<16>                      layout = "stereo";   // JSON: "stereo"
//       rp::list<std::uint32_t, 16>       counts = {1, 1};     // JSON: [1, 1]
//   };
//
// Both are read and written as the JSON the std::string and std::vector they stand in for were, and their JSON Schema
// is that type's (a string, an array of the element's schema), so a descriptor does not change when a field moves to
// them. A JSON value over the bound is a settings error; reflect-cpp names the field it was reading in the message.
// Reading builds a std::string or std::vector first, which is why the JSON side is a desktop one: the device reads
// the same fields from msgpack (design section 6), and the types themselves allocate nothing.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <rfl/Result.hpp>
#include <rfl/json.hpp>
#include <rfl/parsing/Parser.hpp>

namespace rp {

namespace detail {
// Called by the consteval constructors when a literal or a braced list is over its type's bound. It is not constexpr,
// so evaluating it is a compile error where the default is written; a run-time value goes through assign /
// push_back, which answer false instead.
[[noreturn]] inline void capacityExceeded() { std::abort(); }
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
    std::string           str() const { return std::string(view()); }

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

}  // namespace rp

// A text is the JSON string it holds, and its schema is the string's.
template <class R, class W, std::size_t N, class ProcessorsType>
    requires rfl::parsing::AreReaderAndWriter<R, W, rp::text<N>>
struct rfl::parsing::Parser<R, W, rp::text<N>, ProcessorsType> {
    using InputVarType = typename R::InputVarType;
    using String = Parser<R, W, std::string, ProcessorsType>;

    static Result<rp::text<N>> read(const R& r, const InputVarType& var) noexcept {
        auto s = String::read(r, var);
        if (!s) return rfl::Unexpected(s.error());
        rp::text<N> out;
        if (!out.assign(*s))
            return rfl::Unexpected(rfl::Error("a string of " + std::to_string(s->size()) + " bytes is over the " +
                                              std::to_string(N) + " the field holds"));
        return out;
    }
    template <class P>
    static void write(const W& w, const rp::text<N>& field, const P& parent) {
        String::write(w, field.str(), parent);
    }
    static schema::Type to_schema(std::map<std::string, schema::Type>* definitions) {
        return String::to_schema(definitions);
    }
};

// A list is the JSON array its elements make, and its schema is the array's. The elements are read as a std::vector
// would be, so each one reports its own error exactly as it did when the field was one.
template <class R, class W, class T, std::size_t N, class ProcessorsType>
    requires rfl::parsing::AreReaderAndWriter<R, W, rp::list<T, N>>
struct rfl::parsing::Parser<R, W, rp::list<T, N>, ProcessorsType> {
    using InputVarType = typename R::InputVarType;
    using Vector = Parser<R, W, std::vector<T>, ProcessorsType>;

    static Result<rp::list<T, N>> read(const R& r, const InputVarType& var) noexcept {
        auto v = Vector::read(r, var);
        if (!v) return rfl::Unexpected(v.error());
        if (v->size() > N)
            return rfl::Unexpected(rfl::Error("an array of " + std::to_string(v->size()) + " elements is over the " +
                                              std::to_string(N) + " the field holds"));
        rp::list<T, N> out;
        for (const T& e : *v) out.push_back(e);
        return out;
    }
    template <class P>
    static void write(const W& w, const rp::list<T, N>& field, const P& parent) {
        Vector::write(w, std::vector<T>(field.begin(), field.end()), parent);
    }
    static schema::Type to_schema(std::map<std::string, schema::Type>* definitions) {
        return Vector::to_schema(definitions);
    }
};
