#pragma once

// A document's spelling of a resource id, "res:N", read with no allocation: the desktop's JSON settings reader and its
// graph document parser (parseResourceRef, GraphDocument.hpp) and the device's msgpack settings decode all read it
// here, so the three accept exactly the same spellings.

#include <cstdint>
#include <string_view>

#include "graph/sdk_namespace.hpp"

namespace rp::graph {

// "res:12" -> 12, true. False for anything else, "res:0", a leading zero ("res:007"), a sign and an id past 32 bits
// among them: one spelling per id.
constexpr bool parseResourceId(std::string_view text, std::uint32_t& id) noexcept {
    constexpr std::string_view kPrefix = "res:";
    // No substr or compare(pos, ...): each has a throwing range check, which a device build must not reach.
    if (text.size() <= kPrefix.size() || std::string_view(text.data(), kPrefix.size()) != kPrefix) return false;
    text.remove_prefix(kPrefix.size());
    if (text[0] == '0') return false;
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
        if (value > 0xffffffffu) return false;
    }
    id = static_cast<std::uint32_t>(value);
    return true;
}

}  // namespace rp::graph
