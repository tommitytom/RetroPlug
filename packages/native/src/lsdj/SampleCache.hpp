#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Decoded-audio cache used by the LSDJ kit compiler. Loads source files
// via miniaudio (WAV / MP3 / FLAC), keeps them in memory as mono float32,
// and dedupes by content hash (so two paths to identical bytes share one
// entry, and editing a file in place re-loads on cache miss).

namespace rp::lsdj {

struct SampleData {
    std::vector<float> buffer;       // mono, decoded at the source's native rate
    std::uint32_t      sampleRate = 0;
    std::uint64_t      contentHash = 0;  // FNV-64 of the source bytes
};

class SampleCache {
public:
    SampleCache() = default;
    SampleCache(const SampleCache&)            = delete;
    SampleCache& operator=(const SampleCache&) = delete;

    // Load (or fetch cached) sample by filesystem path. Returns nullptr
    // when reading or decoding fails. Thread-safe: cache lookups + inserts
    // are guarded by an internal mutex.
    const SampleData* getOrLoad(const std::string& path);

    // Drop an entry by path. Useful for "file changed on disk" UX or to
    // reclaim memory after a kit recompile.
    void erase(const std::string& path);

    // Compile-time helper: hash a byte buffer the same way the cache hashes
    // file contents (FNV-1a 64-bit). Public so the rpc layer can compute
    // hashes for dirty-tracking without re-loading.
    static std::uint64_t hashBytes(const std::uint8_t* data, std::size_t size);

private:
    struct Slot {
        std::string path;
        SampleData  data;
    };

    std::unordered_map<std::string, Slot> cache_;
    std::mutex                            mutex_;
};

} // namespace rp::lsdj
