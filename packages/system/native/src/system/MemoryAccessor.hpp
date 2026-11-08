#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "system/MemoryType.hpp"

namespace rp {

// Read/write view of one emulator memory region. Construction-time bindings:
//   - `data` + `size`: backing storage (the live emulator's region pointer).
//     `data` == nullptr / `size` == 0 means "this region is not supported by
//     this system" — callers branch on `valid()`.
//   - `type`: the region tag (for diagnostics / patch records).
//   - `access`: caller's intent. `Read` callers MUST NOT call write(); the
//     accessor returns false from write() in that case rather than touching
//     the buffer.
//   - `patches`: optional sink for write records. When non-null, every
//     successful write appends a MemoryPatch describing what changed.
//     Unused by LSDJ kit patching (that role persists via its own config),
//     but exposed for future roles that want a generic change log.
//
// Lifetime: the backing pointer must outlive the accessor. The accessor
// itself is a cheap value type; pass it around by value or rvalue.
//
// Thread-safety: the accessor does not lock. Snapshot publication uses the
// MemorySnapshotTriple transport; cold-path reads from the UI thread accept
// torn reads of live emulator memory.
class MemoryAccessor {
public:
    MemoryAccessor() = default;

    MemoryAccessor(MemoryType                  type,
                   AccessType                  access,
                   std::uint8_t*               data,
                   std::size_t                 size,
                   std::vector<MemoryPatch>*   patches = nullptr)
        : data_(data),
          size_(size),
          patches_(patches),
          type_(type),
          access_(access) {}

    bool                valid()  const { return data_ != nullptr && size_ != 0; }
    std::size_t         size()   const { return size_; }
    MemoryType          type()   const { return type_; }
    AccessType          access() const { return access_; }

    const std::uint8_t* data()  const { return data_; }
    std::uint8_t*       data()        { return data_; }

    std::uint8_t operator[](std::size_t idx) const {
        return idx < size_ ? data_[idx] : std::uint8_t{0};
    }

    // Single-byte write. Returns false if read-only / out-of-range / null.
    bool write(std::size_t offset, std::uint8_t value) {
        if (!valid() || access_ == AccessType::Read) return false;
        if (offset >= size_) return false;
        data_[offset] = value;
        if (patches_) patches_->push_back(MemoryPatch{offset, {value}});
        return true;
    }

    // Bulk write. Returns false if read-only / out-of-range / null / bytes
    // empty. Patch records carry a copy of the written bytes — fine for the
    // sub-kilobyte writes the LSDJ roles do; reconsider if a future caller
    // wants to log multi-MB writes.
    bool write(std::size_t offset, const std::uint8_t* bytes, std::size_t count) {
        if (!valid() || access_ == AccessType::Read) return false;
        if (bytes == nullptr || count == 0) return false;
        if (offset > size_ || size_ - offset < count) return false;
        for (std::size_t i = 0; i < count; ++i) data_[offset + i] = bytes[i];
        if (patches_) {
            MemoryPatch p;
            p.offset = offset;
            p.bytes.assign(bytes, bytes + count);
            patches_->push_back(std::move(p));
        }
        return true;
    }

    bool clear(std::uint8_t value = 0) {
        if (!valid() || access_ == AccessType::Read) return false;
        for (std::size_t i = 0; i < size_; ++i) data_[i] = value;
        if (patches_) {
            MemoryPatch p;
            p.offset = 0;
            p.bytes.assign(size_, value);
            patches_->push_back(std::move(p));
        }
        return true;
    }

private:
    std::uint8_t*               data_    = nullptr;
    std::size_t                 size_    = 0;
    std::vector<MemoryPatch>*   patches_ = nullptr;
    MemoryType                  type_    = MemoryType::Ram;
    AccessType                  access_  = AccessType::Read;
};

} // namespace rp
