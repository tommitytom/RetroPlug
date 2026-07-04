#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "miniz.h"

// Minimal RAII wrappers around miniz's in-memory zip APIs. Project files use
// these to bundle `project.json` plus the per-system binary entries (ROMs,
// SRAM, savestates) into a single deflate-compressed PKZIP blob — same blob
// shape used for `.rplg` files on disk and DPF `getState`/`setState`.
//
// Scope intentionally narrow: heap writer + memory reader. No file-path
// constructors (miniz's stdio APIs are disabled by txiki's `MINIZ_NO_STDIO`
// define), no LZMA. Compression level defaults to MZ_BEST_COMPRESSION because
// projects are saved rarely and the file-size win is the whole point.

class MinizWriter {
public:
    MinizWriter() {
        std::memset(&zip_, 0, sizeof(zip_));
        valid_ = mz_zip_writer_init_heap(&zip_, 0, /*initial_alloc=*/0) != 0;
    }

    ~MinizWriter() {
        if (initialized()) {
            mz_zip_writer_end(&zip_);
        }
    }

    MinizWriter(const MinizWriter&) = delete;
    MinizWriter& operator=(const MinizWriter&) = delete;

    bool valid() const noexcept { return valid_; }

    bool add(std::string_view name,
             const void* data,
             std::size_t size,
             int level = MZ_BEST_COMPRESSION) {
        if (!valid_) return false;
        const std::string nameZ(name); // miniz wants NUL-terminated
        const mz_bool ok = mz_zip_writer_add_mem(&zip_,
                                                 nameZ.c_str(),
                                                 data,
                                                 size,
                                                 static_cast<mz_uint>(level));
        if (!ok) valid_ = false;
        return ok != 0;
    }

    bool add(std::string_view name,
             std::span<const std::uint8_t> bytes,
             int level = MZ_BEST_COMPRESSION) {
        return add(name, bytes.data(), bytes.size(), level);
    }

    bool add(std::string_view name,
             std::string_view text,
             int level = MZ_BEST_COMPRESSION) {
        return add(name, text.data(), text.size(), level);
    }

    // Finalize the archive and return the resulting bytes. After this the
    // writer is consumed; subsequent calls fail.
    std::vector<std::uint8_t> finish() {
        if (!valid_) return {};
        void* heap = nullptr;
        std::size_t heapSize = 0;
        const mz_bool ok = mz_zip_writer_finalize_heap_archive(&zip_, &heap, &heapSize);
        std::vector<std::uint8_t> out;
        if (ok && heap && heapSize > 0) {
            const auto* p = static_cast<const std::uint8_t*>(heap);
            out.assign(p, p + heapSize);
        }
        if (heap) {
            // miniz allocated via its allocator (malloc by default).
            std::free(heap);
        }
        valid_ = false;
        mz_zip_writer_end(&zip_);
        return out;
    }

private:
    bool initialized() const noexcept {
        return zip_.m_pState != nullptr;
    }

    mz_zip_archive zip_{};
    bool valid_ = false;
};

class MinizReader {
public:
    explicit MinizReader(std::span<const std::uint8_t> blob) {
        std::memset(&zip_, 0, sizeof(zip_));
        if (blob.empty()) return;
        valid_ = mz_zip_reader_init_mem(&zip_,
                                        blob.data(),
                                        blob.size(),
                                        /*flags=*/0) != 0;
    }

    ~MinizReader() {
        if (zip_.m_pState != nullptr) {
            mz_zip_reader_end(&zip_);
        }
    }

    MinizReader(const MinizReader&) = delete;
    MinizReader& operator=(const MinizReader&) = delete;

    bool valid() const noexcept { return valid_; }

    bool has(std::string_view name) const {
        if (!valid_) return false;
        const std::string nameZ(name);
        return mz_zip_reader_locate_file(const_cast<mz_zip_archive*>(&zip_),
                                         nameZ.c_str(),
                                         nullptr,
                                         /*flags=*/0) >= 0;
    }

    // Returns the entry contents, or an empty vector when missing / on error.
    // Distinguish missing entries from empty-but-present ones via `has()`.
    std::vector<std::uint8_t> read(std::string_view name) const {
        if (!valid_) return {};
        const std::string nameZ(name);
        const int index = mz_zip_reader_locate_file(
            const_cast<mz_zip_archive*>(&zip_),
            nameZ.c_str(),
            nullptr,
            /*flags=*/0);
        if (index < 0) return {};

        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(const_cast<mz_zip_archive*>(&zip_),
                                     static_cast<mz_uint>(index),
                                     &stat)) {
            return {};
        }
        if (stat.m_uncomp_size == 0) return {};

        std::vector<std::uint8_t> out(static_cast<std::size_t>(stat.m_uncomp_size));
        const mz_bool ok = mz_zip_reader_extract_to_mem(
            const_cast<mz_zip_archive*>(&zip_),
            static_cast<mz_uint>(index),
            out.data(),
            out.size(),
            /*flags=*/0);
        if (!ok) return {};
        return out;
    }

    std::string readString(std::string_view name) const {
        const auto bytes = read(name);
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    // Enumerate every entry name in the archive, in stored order. Lets a caller
    // unzip to a full {name, bytes} list without knowing the keys up front.
    std::vector<std::string> names() const {
        std::vector<std::string> out;
        if (!valid_) return out;
        auto* z = const_cast<mz_zip_archive*>(&zip_);
        const mz_uint n = mz_zip_reader_get_num_files(z);
        out.reserve(n);
        for (mz_uint i = 0; i < n; ++i) {
            const mz_uint sz = mz_zip_reader_get_filename(z, i, nullptr, 0); // incl NUL
            if (sz == 0) continue;
            std::string name(sz, '\0');
            mz_zip_reader_get_filename(z, i, name.data(), sz);
            name.resize(sz - 1); // drop the trailing NUL miniz writes
            out.push_back(std::move(name));
        }
        return out;
    }

private:
    mz_zip_archive zip_{};
    bool valid_ = false;
};
