#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Newtype that owns a vector<uint8_t> but reflectcpp-serialises as a base64
// string. Keeps saved JSON ~1.36x the raw size instead of the ~4x cost of
// reflectcpp's default vector-of-uint8 (decimal numbers). Intentionally
// standalone — no DPF dependency — so unit tests can include it.
class Base64Bytes {
public:
    using ReflectionType = std::string;

    Base64Bytes() = default;
    Base64Bytes(const Base64Bytes&) = default;
    Base64Bytes(Base64Bytes&&) noexcept = default;
    Base64Bytes& operator=(const Base64Bytes&) = default;
    Base64Bytes& operator=(Base64Bytes&&) noexcept = default;

    explicit Base64Bytes(std::vector<std::uint8_t> bytes) noexcept
        : bytes_(std::move(bytes)) {}

    // Reflectcpp constructs from a base64 string on parse.
    Base64Bytes(const std::string& encoded)
        : bytes_(decode(encoded)) {}

    std::string reflection() const { return encode(bytes_); }

    const std::vector<std::uint8_t>& bytes() const noexcept { return bytes_; }
    std::vector<std::uint8_t>&       bytes()       noexcept { return bytes_; }

    bool        empty() const noexcept { return bytes_.empty(); }
    std::size_t size()  const noexcept { return bytes_.size(); }

    bool operator==(const Base64Bytes& other) const noexcept {
        return bytes_ == other.bytes_;
    }

private:
    static constexpr const char* kTable =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    static std::string encode(const std::vector<std::uint8_t>& in) {
        std::string out;
        const std::size_t n = in.size();
        out.reserve(((n + 2) / 3) * 4);
        std::size_t i = 0;
        for (; i + 3 <= n; i += 3) {
            const std::uint32_t triple =
                (std::uint32_t(in[i])     << 16) |
                (std::uint32_t(in[i + 1]) << 8)  |
                 std::uint32_t(in[i + 2]);
            out.push_back(kTable[(triple >> 18) & 0x3F]);
            out.push_back(kTable[(triple >> 12) & 0x3F]);
            out.push_back(kTable[(triple >> 6)  & 0x3F]);
            out.push_back(kTable[ triple        & 0x3F]);
        }
        if (i < n) {
            std::uint32_t triple = std::uint32_t(in[i]) << 16;
            if (i + 1 < n) triple |= std::uint32_t(in[i + 1]) << 8;
            out.push_back(kTable[(triple >> 18) & 0x3F]);
            out.push_back(kTable[(triple >> 12) & 0x3F]);
            out.push_back(i + 1 < n ? kTable[(triple >> 6) & 0x3F] : '=');
            out.push_back('=');
        }
        return out;
    }

    static int charValue(char c) {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+')             return 62;
        if (c == '/')             return 63;
        return -1;
    }

    static std::vector<std::uint8_t> decode(const std::string& s) {
        std::vector<std::uint8_t> out;
        out.reserve((s.size() / 4) * 3);
        std::uint32_t buf = 0;
        int bits = 0;
        for (char c : s) {
            if (c == '=' || c == '\0') break;
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
            const int v = charValue(c);
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

    std::vector<std::uint8_t> bytes_;
};
