#include "lsdj/SampleCache.hpp"

#include <cstdio>
#include <fstream>
#include <ios>

#include "util/Hash.hpp"

// miniaudio's decoder API only — no playback / device init. Defining the
// minimum set of MA flags keeps the implementation translation unit small.
#define MA_NO_DEVICE_IO
#define MA_NO_THREADING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_NULL
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace rp::lsdj {

namespace {

bool readFile(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0) {
        out.clear();
        return true;
    }
    out.resize(static_cast<std::size_t>(size));
    return static_cast<bool>(in.read(reinterpret_cast<char*>(out.data()), size));
}

SampleData decode(const std::vector<std::uint8_t>& fileData) {
    SampleData out;

    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 0);
    ma_decoder decoder;
    if (ma_decoder_init_memory(fileData.data(), fileData.size(), &cfg, &decoder) != MA_SUCCESS) {
        return {};
    }
    out.sampleRate = decoder.outputSampleRate;

    constexpr std::size_t kBlockFrames = 24000;
    std::size_t offset = 0;
    while (true) {
        out.buffer.resize(out.buffer.size() + kBlockFrames);
        ma_uint64 framesRead = 0;
        ma_decoder_read_pcm_frames(&decoder, out.buffer.data() + offset,
                                   kBlockFrames, &framesRead);
        offset += static_cast<std::size_t>(framesRead);
        if (framesRead < kBlockFrames) {
            out.buffer.resize(offset);
            break;
        }
    }

    ma_decoder_uninit(&decoder);
    return out;
}

} // namespace

std::uint64_t SampleCache::hashBytes(const std::uint8_t* data, std::size_t size) {
    return rp::hash::fnv1a64(data, size);
}

const SampleData* SampleCache::getOrLoad(const std::string& path) {
    // Fast path: existing entry. Cheap to take the lock here since callers
    // are typically background kit-compile workers.
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto it = cache_.find(path);
        if (it != cache_.end()) return &it->second.data;
    }

    std::vector<std::uint8_t> fileData;
    if (!readFile(path, fileData) || fileData.empty()) {
        std::fprintf(stderr, "[SampleCache] failed to read %s\n", path.c_str());
        return nullptr;
    }

    SampleData decoded = decode(fileData);
    if (decoded.buffer.empty()) {
        std::fprintf(stderr, "[SampleCache] failed to decode %s\n", path.c_str());
        return nullptr;
    }
    decoded.contentHash = hashBytes(fileData.data(), fileData.size());

    std::lock_guard<std::mutex> guard(mutex_);
    auto [it, inserted] = cache_.try_emplace(path);
    if (inserted) {
        it->second.path = path;
        it->second.data = std::move(decoded);
    }
    return &it->second.data;
}

void SampleCache::erase(const std::string& path) {
    std::lock_guard<std::mutex> guard(mutex_);
    cache_.erase(path);
}

} // namespace rp::lsdj
