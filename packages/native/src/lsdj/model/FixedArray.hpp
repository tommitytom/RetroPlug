#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include <rfl/internal/has_reflector.hpp> // declares the rfl::Reflector primary template

// A fixed-length array that authors leniently from JSON: a short or omitted JSON
// array is padded to its full on-disk length with default elements, so a sav
// fixture only has to specify the cells it cares about (e.g. `rows: [{chains:[0]}]`
// instead of all 256 rows). The codec still sees a full std::array-shaped buffer,
// and serialization always emits the full length, so on-disk encoding and JSON
// round-trips are unchanged.
//
// The leniency lives in the rfl::Reflector below: it reflects as `std::vector<T>`
// (any length 0..N), which reflect-cpp parses element-by-element through T's own
// parser (so std::optional<T>, the Command enum, nested FixedArray, and the
// Instrument TaggedUnion all pad recursively) and which also relaxes the
// generated JSON Schema / zod to an unbounded array.
namespace rp::lsdj::model {

template <typename T, std::size_t N>
struct FixedArray {
    std::array<T, N> arr{}; // value-initialised: N default-constructed Ts

    using value_type = T;

    constexpr std::size_t size() const noexcept { return N; }

    T&       operator[](std::size_t i)       noexcept { return arr[i]; }
    const T& operator[](std::size_t i) const noexcept { return arr[i]; }

    T*       data()       noexcept { return arr.data(); }
    const T* data() const noexcept { return arr.data(); }

    auto begin()       noexcept { return arr.begin(); }
    auto end()         noexcept { return arr.end(); }
    auto begin() const noexcept { return arr.begin(); }
    auto end()   const noexcept { return arr.end(); }

    bool operator==(const FixedArray&) const = default;
};

} // namespace rp::lsdj::model

namespace rfl {

template <class T, std::size_t N>
struct Reflector<rp::lsdj::model::FixedArray<T, N>> {
    using ReflType = std::vector<T>;

    static rp::lsdj::model::FixedArray<T, N> to(const ReflType& v) {
        if (v.size() > N) {
            throw std::runtime_error("Expected at most " + std::to_string(N) +
                                     " elements, got " + std::to_string(v.size()) + ".");
        }
        rp::lsdj::model::FixedArray<T, N> out{}; // tail stays default (T{})
        for (std::size_t i = 0; i < v.size(); ++i) { out[i] = v[i]; }
        return out;
    }

    static ReflType from(const rp::lsdj::model::FixedArray<T, N>& a) {
        return ReflType(a.begin(), a.end());
    }
};

} // namespace rfl
