#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "graph/noinline.hpp"

namespace rp::graph {

// A sentence built into a fixed buffer, cut where it no longer fits: an rp_error's text, or any other a portable piece
// of the runtime writes (the plan loader's refusals, the MicroQuickJS host's faults), with no heap. Its writes are out
// of line: every refusal builds one, and a device keeps one copy of each rather than one a refusal.
struct Text {
    char*       out;
    std::size_t capacity;
    std::size_t length = 0;

    Text(char* buffer, std::size_t size) : out(buffer), capacity(size) { out[0] = '\0'; }
    RP_NOINLINE Text& put(const char* s, std::size_t n) {
        for (std::size_t i = 0; i < n && length + 1 < capacity; ++i) out[length++] = s[i];
        out[length] = '\0';
        return *this;
    }
    Text& operator<<(const char* s) { return put(s, std::strlen(s)); }
    Text& operator<<(std::string_view s) { return put(s.data(), s.size()); }
    RP_NOINLINE Text& operator<<(std::uint64_t v) {
        char digits[20];
        std::size_t n = 0;
        do {
            digits[n++] = static_cast<char>('0' + v % 10);
            v /= 10;
        } while (v);
        while (n) put(&digits[--n], 1);
        return *this;
    }
    Text& hex(std::uint32_t v) {
        char digits[10] = {'0', 'x'};
        for (int i = 0; i < 8; ++i) digits[2 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 0xF];
        return put(digits, 10);
    }
    // A string from outside, quoted: a type id or a node id, which may hold anything the document did.
    RP_NOINLINE Text& quoted(std::string_view s) { return *this << "\"" << s << "\""; }
};

}  // namespace rp::graph
