#pragma once

#include <string>
#include <string_view>

#include <rfl/Generic.hpp>
#include <rfl/Result.hpp>

namespace rp::graph {

// The one JSON spelling descriptors, layouts and settings comparisons use, so a C++ node and a TS node
// of the same shape produce the same bytes. It is exactly what packages/retroplug/src/graph/canonicalJson.ts
// writes: object keys sorted, no whitespace, strings escaped as JSON.stringify escapes them, and numbers
// spelled as JavaScript's Number.prototype.toString spells them (so -60.0 is "-60", 1e-7 is "1e-7").
std::string canonicalJson(const rfl::Generic& value);

// Parse any JSON text, then re-spell it canonically. Used to compare settings without key order or
// number spelling counting as a change, and to fold a generated JSON Schema into a descriptor.
rfl::Result<std::string> canonicalizeJson(std::string_view json);

// JavaScript's Number.prototype.toString for a finite double; "null" for NaN / infinity, as
// JSON.stringify does.
std::string jsNumber(double value);

}  // namespace rp::graph
