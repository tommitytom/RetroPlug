#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <rfl/Result.hpp>

namespace rp::graph {

// A node's settings, from the JSON a document carries to the msgpack a device reads (RP_SETTINGS_MSGPACK,
// docs/modular-graph-phase-e1-design.md section 6): the desktop half of the encoding, which E3's plan export writes
// its blobs with. Any JSON value converts, an object becoming a map with its keys in the order the JSON has them (the
// decode matches by name, so the order is irrelevant) and a repeated key written twice (the decode keeps the last, as
// JSON.parse does).
//
// A number stays the kind of number the JSON text spells: an integer literal is a msgpack int in its smallest form, and
// anything with a fraction or an exponent is a float64, 2.0 included. That is what keeps the two encodings refusing
// the same settings: the JSON reader takes only an integer literal for an integer field (2.0 is refused there), and the
// msgpack decode takes only an int for one. An integer past 64 bits is the double yyjson reads it as. Strings are str,
// never bin; null is nil. Refused: text that is not JSON, and nesting deeper than 512 levels.
rfl::Result<std::vector<std::uint8_t>> settingsToMsgpack(std::string_view json);

}  // namespace rp::graph
