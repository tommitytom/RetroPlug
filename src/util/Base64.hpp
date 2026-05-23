#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Tiny base64 codec. Only needed at the DPF state boundary: DPF's
// `getState`/`setState` take NUL-terminated UTF-8 strings, so the
// project's binary zip blob has to be wrapped in base64 on the way through
// the DAW. Everything else (project files on disk, RPC payloads) carries
// raw bytes.

namespace base64 {

namespace detail {
inline constexpr const char* kTable =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

inline int charValue(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+')             return 62;
    if (c == '/')             return 63;
    return -1;
}
} // namespace detail

inline std::string encode(std::span<const std::uint8_t> in) {
    std::string out;
    const std::size_t n = in.size();
    out.reserve(((n + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 3 <= n; i += 3) {
        const std::uint32_t triple =
            (std::uint32_t(in[i])     << 16) |
            (std::uint32_t(in[i + 1]) << 8)  |
             std::uint32_t(in[i + 2]);
        out.push_back(detail::kTable[(triple >> 18) & 0x3F]);
        out.push_back(detail::kTable[(triple >> 12) & 0x3F]);
        out.push_back(detail::kTable[(triple >> 6)  & 0x3F]);
        out.push_back(detail::kTable[ triple        & 0x3F]);
    }
    if (i < n) {
        std::uint32_t triple = std::uint32_t(in[i]) << 16;
        if (i + 1 < n) triple |= std::uint32_t(in[i + 1]) << 8;
        out.push_back(detail::kTable[(triple >> 18) & 0x3F]);
        out.push_back(detail::kTable[(triple >> 12) & 0x3F]);
        out.push_back(i + 1 < n ? detail::kTable[(triple >> 6) & 0x3F] : '=');
        out.push_back('=');
    }
    return out;
}

inline std::vector<std::uint8_t> decode(std::string_view s) {
    std::vector<std::uint8_t> out;
    out.reserve((s.size() / 4) * 3);
    std::uint32_t buf = 0;
    int bits = 0;
    for (char c : s) {
        if (c == '=' || c == '\0') break;
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        const int v = detail::charValue(c);
        if (v < 0) continue; // skip stray chars
        buf = (buf << 6) | std::uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace base64
