#pragma once

// A msgpack reader that allocates nothing and throws nothing: a cursor over a caller's bytes, which a settings decode
// (settings_msgpack.hpp) walks once, on either build of node.hpp. It reads every msgpack format a settings blob can
// carry (nil, bools, every int width, float 32 and 64, str, bin, array, map) and skips ext. Each read answers false,
// leaving the cursor where it was, when the next value is not of the asked type or runs past the end; a value that
// runs past the end makes the blob malformed, which `truncated` reports.
//
// It is not reflect-cpp's msgpack reader (rfl/msgpack), which unpacks with msgpack-c into a msgpack_zone (a heap
// arena), reports through rfl::Error (a std::string) and includes <istream>; msgpack-c is not a dependency of this
// tree either.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace rp::graph::msgpack {

enum class Type : std::uint8_t { Nil, Bool, Int, Float, Str, Bin, Array, Map, Ext, Invalid };

// An integer as msgpack carries it: non-negative ones as their unsigned value, negative ones as their signed value.
struct Int {
    bool          negative = false;
    std::uint64_t u = 0;  // when !negative
    std::int64_t  i = 0;  // when negative
};

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : p_(data), end_(data + size) {}

    bool        atEnd() const { return p_ == end_; }
    std::size_t remaining() const { return static_cast<std::size_t>(end_ - p_); }
    // Set when a read found a value whose length runs past the end of the blob.
    bool        truncated() const { return truncated_; }

    // The type of the next value, without reading it (Invalid at the end, and for 0xc1, which msgpack never uses).
    Type peek() const {
        if (p_ == end_) return Type::Invalid;
        const std::uint8_t b = *p_;
        if (b <= 0x7f || b >= 0xe0) return Type::Int;  // positive and negative fixint
        if (b <= 0x8f) return Type::Map;
        if (b <= 0x9f) return Type::Array;
        if (b <= 0xbf) return Type::Str;
        switch (b) {
            case 0xc0: return Type::Nil;
            case 0xc2:
            case 0xc3: return Type::Bool;
            case 0xc4: case 0xc5: case 0xc6: return Type::Bin;
            case 0xc7: case 0xc8: case 0xc9: return Type::Ext;
            case 0xca: case 0xcb: return Type::Float;
            case 0xcc: case 0xcd: case 0xce: case 0xcf:
            case 0xd0: case 0xd1: case 0xd2: case 0xd3: return Type::Int;
            case 0xd4: case 0xd5: case 0xd6: case 0xd7: case 0xd8: return Type::Ext;
            case 0xd9: case 0xda: case 0xdb: return Type::Str;
            case 0xdc: case 0xdd: return Type::Array;
            case 0xde: case 0xdf: return Type::Map;
            default: return Type::Invalid;
        }
    }

    bool readNil() {
        if (peek() != Type::Nil) return false;
        ++p_;
        return true;
    }

    bool readBool(bool& out) {
        if (peek() != Type::Bool) return false;
        out = *p_++ == 0xc3;
        return true;
    }

    bool readInt(Int& out) {
        if (peek() != Type::Int) return false;
        const std::uint8_t b = *p_;
        if (b <= 0x7f) {
            out = Int{false, b, 0};
            ++p_;
            return true;
        }
        if (b >= 0xe0) {
            out = Int{true, 0, static_cast<std::int8_t>(b)};
            ++p_;
            return true;
        }
        const std::size_t n = std::size_t{1} << ((b - 0xcc) & 3);  // 0xcc..0xcf, 0xd0..0xd3: 1, 2, 4, 8 bytes
        if (!has(1 + n)) return false;
        const std::uint64_t raw = be(p_ + 1, n);
        if (b <= 0xcf) {
            out = Int{false, raw, 0};
        } else {
            // Sign-extend the n-byte two's complement value.
            const std::int64_t v = n == 8 ? static_cast<std::int64_t>(raw)
                                          : static_cast<std::int64_t>(raw << (64 - 8 * n)) >> (64 - 8 * n);
            out = v < 0 ? Int{true, 0, v} : Int{false, static_cast<std::uint64_t>(v), 0};
        }
        p_ += 1 + n;
        return true;
    }

    bool readFloat(double& out) {
        if (peek() != Type::Float) return false;
        const bool wide = *p_ == 0xcb;
        if (!has(wide ? 9 : 5)) return false;
        if (wide) {
            const std::uint64_t bits = be(p_ + 1, 8);
            std::memcpy(&out, &bits, sizeof out);
            p_ += 9;
        } else {
            const auto bits = static_cast<std::uint32_t>(be(p_ + 1, 4));
            float f;
            std::memcpy(&f, &bits, sizeof f);
            out = f;
            p_ += 5;
        }
        return true;
    }

    // A str's bytes, a view into the blob.
    bool readStr(std::string_view& out) {
        if (peek() != Type::Str) return false;
        const std::uint8_t b = *p_;
        std::size_t head = 1, len = 0;
        if (b >= 0xa0 && b <= 0xbf) len = b & 0x1f;
        else {
            head = 1 + (b == 0xd9 ? 1 : b == 0xda ? 2 : 4);
            if (!has(head)) return false;
            len = static_cast<std::size_t>(be(p_ + 1, head - 1));
        }
        if (!has(head, len)) return false;
        out = std::string_view(reinterpret_cast<const char*>(p_ + head), len);
        p_ += head + len;
        return true;
    }

    bool readArray(std::uint32_t& count) { return readContainer(Type::Array, 0x90, 0xdc, count); }
    bool readMap(std::uint32_t& count) { return readContainer(Type::Map, 0x80, 0xde, count); }

    // Step over the next value, whatever it is and whatever it holds. No recursion: a container adds its elements to
    // the count still to step over, so a deeply nested value costs no stack.
    bool skip() {
        std::uint64_t pending = 1;
        while (pending > 0) {
            --pending;
            std::uint32_t n = 0;
            Int i;
            double d;
            bool v;
            std::string_view s;
            switch (peek()) {
                case Type::Nil: ++p_; break;
                case Type::Bool: if (!readBool(v)) return false; break;
                case Type::Int: if (!readInt(i)) return false; break;
                case Type::Float: if (!readFloat(d)) return false; break;
                case Type::Str: if (!readStr(s)) return false; break;
                case Type::Bin: if (!skipSized(0xc4)) return false; break;
                case Type::Ext: if (!skipExt()) return false; break;
                case Type::Array:
                    if (!readArray(n)) return false;
                    pending += n;
                    break;
                case Type::Map:
                    if (!readMap(n)) return false;
                    pending += std::uint64_t{n} * 2;
                    break;
                case Type::Invalid: return false;
            }
        }
        return true;
    }

private:
    const std::uint8_t* p_;
    const std::uint8_t* end_;
    bool                truncated_ = false;

    bool has(std::size_t n) {
        if (remaining() >= n) return true;
        truncated_ = true;
        return false;
    }
    // `head` bytes and then `len` more, checked without adding the two (a 32-bit length near 4 GiB would wrap a
    // 32-bit size_t).
    bool has(std::size_t head, std::size_t len) {
        if (!has(head)) return false;
        if (remaining() - head >= len) return true;
        truncated_ = true;
        return false;
    }

    static std::uint64_t be(const std::uint8_t* at, std::size_t n) {
        std::uint64_t v = 0;
        for (std::size_t k = 0; k < n; ++k) v = (v << 8) | at[k];
        return v;
    }

    bool readContainer(Type type, std::uint8_t fixBase, std::uint8_t wide16, std::uint32_t& count) {
        if (peek() != type) return false;
        const std::uint8_t b = *p_;
        if (b >= fixBase && b <= fixBase + 0x0f) {
            count = b & 0x0f;
            ++p_;
            return true;
        }
        const std::size_t n = b == wide16 ? 2 : 4;
        if (!has(1 + n)) return false;
        count = static_cast<std::uint32_t>(be(p_ + 1, n));
        p_ += 1 + n;
        return true;
    }

    // bin 8 / 16 / 32: a length of 1, 2 or 4 bytes, then that many bytes.
    bool skipSized(std::uint8_t base) {
        const std::size_t n = std::size_t{1} << (*p_ - base);
        if (!has(1 + n)) return false;
        const auto len = static_cast<std::size_t>(be(p_ + 1, n));
        if (!has(1 + n, len)) return false;
        p_ += 1 + n + len;
        return true;
    }

    // fixext 1 / 2 / 4 / 8 / 16 (a type byte and 2^k data bytes), ext 8 / 16 / 32 (a length, a type byte, the data).
    bool skipExt() {
        const std::uint8_t b = *p_;
        if (b >= 0xd4) {
            const std::size_t len = std::size_t{1} << (b - 0xd4);
            if (!has(2 + len)) return false;
            p_ += 2 + len;
            return true;
        }
        const std::size_t n = std::size_t{1} << (b - 0xc7);
        if (!has(1 + n)) return false;
        const auto len = static_cast<std::size_t>(be(p_ + 1, n));
        if (!has(2 + n, len)) return false;
        p_ += 2 + n + len;
        return true;
    }
};

}  // namespace rp::graph::msgpack
