#pragma once

#include <cstdint>
#include <cstring>
#include <span>

// Byte + sub-byte cursors over a flat LSDj buffer (a 0x8000 song body or the
// 128 KiB sav image). The system-wide MemoryAccessor is byte-only; the codec
// needs bit-field access, so the bit primitive lives here and ONLY here.
//
// Bit semantics match liblsdj exactly (bytes.c): a field value at
// (byte, position, count) is `(byte >> position) & mask(count)` on read
// (get_instrument_bits = get_bits(...) >> position), and on write the field is
// spliced in via `(byte & ~(mask<<pos)) | ((value & mask) << pos)` (copy_bits).
namespace rp::lsdj::codec {

constexpr std::uint8_t bitMask(std::uint8_t count) {
    return count >= 8 ? std::uint8_t(0xFF) : std::uint8_t((1u << count) - 1u);
}

// Read-only view.
class SavView {
public:
    SavView(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
    explicit SavView(std::span<const std::uint8_t> s) : data_(s.data()), size_(s.size()) {}

    std::size_t size() const { return size_; }
    bool inBounds(std::size_t off, std::size_t len = 1) const {
        return off <= size_ && len <= size_ - off; // overflow-safe
    }

    std::uint8_t u8(std::size_t off) const { return off < size_ ? data_[off] : 0; }
    std::uint8_t bits(std::size_t off, std::uint8_t pos, std::uint8_t count) const {
        return std::uint8_t((u8(off) >> pos) & bitMask(count));
    }
    std::uint16_t u16le(std::size_t off) const {
        return std::uint16_t(u8(off) | (std::uint16_t(u8(off + 1)) << 8));
    }
    std::span<const std::uint8_t> slice(std::size_t off, std::size_t len) const {
        return inBounds(off, len) ? std::span<const std::uint8_t>(data_ + off, len)
                                  : std::span<const std::uint8_t>();
    }

private:
    const std::uint8_t* data_;
    std::size_t         size_;
};

// Mutable view (also supports reads, e.g. read-modify-write of a shared byte).
class SavWriter {
public:
    SavWriter(std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
    explicit SavWriter(std::span<std::uint8_t> s) : data_(s.data()), size_(s.size()) {}

    std::size_t size() const { return size_; }
    bool inBounds(std::size_t off, std::size_t len = 1) const {
        return off <= size_ && len <= size_ - off;
    }

    std::uint8_t u8(std::size_t off) const { return off < size_ ? data_[off] : 0; }
    std::uint8_t bits(std::size_t off, std::uint8_t pos, std::uint8_t count) const {
        return std::uint8_t((u8(off) >> pos) & bitMask(count));
    }

    void setU8(std::size_t off, std::uint8_t v) { if (off < size_) data_[off] = v; }
    void setBits(std::size_t off, std::uint8_t pos, std::uint8_t count, std::uint8_t v) {
        if (off >= size_) return;
        const std::uint8_t mask = bitMask(count);
        data_[off] = std::uint8_t((data_[off] & ~(mask << pos)) | ((v & mask) << pos));
    }
    void setU16le(std::size_t off, std::uint16_t v) {
        setU8(off, std::uint8_t(v));
        setU8(off + 1, std::uint8_t(v >> 8));
    }
    void copyIn(std::size_t off, std::span<const std::uint8_t> src) {
        if (inBounds(off, src.size()) && !src.empty())
            std::memcpy(data_ + off, src.data(), src.size());
    }

    SavView reader() const { return SavView(data_, size_); }

private:
    std::uint8_t* data_;
    std::size_t   size_;
};

} // namespace rp::lsdj::codec
