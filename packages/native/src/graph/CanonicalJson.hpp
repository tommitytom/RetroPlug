#pragma once

#include <string>
#include <string_view>

#include <rfl/Generic.hpp>
#include <rfl/Result.hpp>

namespace rp::graph {

// The one JSON spelling descriptors, layouts and settings comparisons use, so a C++ node and a TS node
// of the same shape produce the same bytes. It is what packages/retroplug/src/graph/canonicalJson.ts
// writes for the same value: no whitespace; strings escaped as JSON.stringify escapes them; numbers
// spelled as JavaScript spells them (so -60.0 is "-60", 1e-7 is "1e-7", and an integer beyond 2^53 is
// rounded to the nearest double, as JS would hold it); object keys sorted by UTF-16 code unit (JS's
// default sort, not byte order); and a key that appears twice keeps its LAST value, as JSON.parse does.
//
// Strings must be valid UTF-8; they are copied through, not validated.
std::string canonicalJson(const rfl::Generic& value);

// Parse JSON text the way JavaScript's JSON.parse sees it: an integer that a double holds exactly stays an
// int64, every other number becomes a double (never a wrapped or truncated integer), and a key that
// appears twice keeps its last value. Use this, not rfl::json::read<rfl::Generic>, to read JSON as a
// Generic: reflect-cpp's reader turns an unsigned value above INT64_MAX into a negative int64. (To read a
// typed struct with integer fields, rfl::json::read<T> is still the right call: it range-checks them,
// and rfl::from_generic<T> does not.) Refuses, with a reason, what JSON.parse would turn into Infinity
// (1e400), lone-surrogate escapes, and nesting deeper than 512 levels.
rfl::Result<rfl::Generic> parseJson(std::string_view json);

// parseJson, then re-spell canonically. Used to compare settings without key order or number spelling
// counting as a change, and to fold a generated JSON Schema into a descriptor.
rfl::Result<std::string> canonicalizeJson(std::string_view json);

// JavaScript's spelling of a number, as JSON.stringify writes it: Number.prototype.toString for a finite
// double, "null" for NaN or infinity.
std::string jsNumber(double value);

}  // namespace rp::graph
