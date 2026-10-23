#include "graph/ResourceTable.hpp"

#include <cassert>
#include <cmath>
#include <vector>

namespace rp::graph {

struct ResourceTable::Entry {
    Entry() = default;
    Entry(const Entry&) = delete;  // rp_resource* handles point into it: it never moves
    Entry& operator=(const Entry&) = delete;

    Id                        id = 0;
    bool                      owned = true;
    std::uint32_t             pins = 0;
    std::string               type;
    rp_resource               resource{};
    // audio-buffer payload
    std::vector<float>        samples;
    std::vector<float*>       channelPointers;
    rp_audio_buffer           audio{};
    // byte payload
    std::vector<std::uint8_t> bytes;
    rp_bytes                  bytesView{};
};

namespace {

ResourceTable::Id refused(std::string* error, std::string why) {
    if (error) *error = std::move(why);
    return 0;
}

}  // namespace

ResourceTable::ResourceTable() = default;

ResourceTable::~ResourceTable() {
#ifndef NDEBUG
    for (const auto& entry : entries_) assert(entry.second->pins == 0 && "a plan still pins a resource the table is freeing");
#endif
}

ResourceTable::Id ResourceTable::insert(std::unique_ptr<Entry> e, std::string* error) {
    if (next_ == 0) return refused(error, "resource ids are exhausted");  // they would wrap onto live ones
    e->id = next_++;
    const Id id = e->id;
    entries_.emplace(id, std::move(e));
    return id;
}

ResourceTable::Id ResourceTable::createAudioBuffer(std::uint32_t channels, std::uint32_t frames, double sampleRate,
                                                   std::string* error) {
    if (channels == 0 || frames == 0) return refused(error, "an audio buffer needs at least one channel and one frame");
    if (!(sampleRate > 0.0) || !std::isfinite(sampleRate))
        return refused(error, "an audio buffer's sample rate must be a positive number");
    if (std::uint64_t(channels) * frames > kMaxAudioSamples)
        return refused(error, "an audio buffer of " + std::to_string(channels) + " x " + std::to_string(frames) +
                                  " samples is larger than " + std::to_string(kMaxAudioSamples));
    auto e = std::make_unique<Entry>();
    e->type = "audio-buffer";
    e->samples.assign(static_cast<std::size_t>(channels) * frames, 0.0f);
    for (std::uint32_t c = 0; c < channels; ++c) e->channelPointers.push_back(e->samples.data() + std::size_t(c) * frames);
    e->audio = rp_audio_buffer{channels, frames, sampleRate, e->channelPointers.data()};
    e->resource = rp_resource{e->type.c_str(), &e->audio};
    return insert(std::move(e), error);
}

ResourceTable::Id ResourceTable::createBytes(std::string type, std::vector<std::uint8_t> bytes, std::string* error) {
    if (type.empty()) return refused(error, "a resource needs a type");
    for (const char c : type)
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            return refused(error, "resource type \"" + type + "\" must not contain whitespace");
    if (type == "audio-buffer") return refused(error, "an \"audio-buffer\" is made with createAudioBuffer, not from bytes");
    auto e = std::make_unique<Entry>();
    e->type = std::move(type);
    e->bytes = std::move(bytes);
    e->bytesView = rp_bytes{e->bytes.data(), e->bytes.size()};
    e->resource = rp_resource{e->type.c_str(), &e->bytesView};
    return insert(std::move(e), error);
}

rp_resource* ResourceTable::get(Id id) const {
    const auto it = entries_.find(id);
    return it != entries_.end() ? &it->second->resource : nullptr;
}

void ResourceTable::freeIfUnused(Id id) {
    const auto it = entries_.find(id);
    if (it != entries_.end() && !it->second->owned && it->second->pins == 0) entries_.erase(it);
}

bool ResourceTable::drop(Id id) {
    const auto it = entries_.find(id);
    if (it == entries_.end() || !it->second->owned) return false;
    it->second->owned = false;
    freeIfUnused(id);
    return true;
}

bool ResourceTable::pin(Id id) {
    const auto it = entries_.find(id);
    if (it == entries_.end()) return false;
    ++it->second->pins;
    return true;
}

bool ResourceTable::unpin(Id id) {
    const auto it = entries_.find(id);
    if (it == entries_.end() || it->second->pins == 0) return false;
    --it->second->pins;
    freeIfUnused(id);
    return true;
}

std::uint32_t ResourceTable::pinCount(Id id) const {
    const auto it = entries_.find(id);
    return it != entries_.end() ? it->second->pins : 0;
}

}  // namespace rp::graph
