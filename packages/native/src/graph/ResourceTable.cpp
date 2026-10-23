#include "graph/ResourceTable.hpp"

#include <algorithm>

namespace rp::graph {

struct ResourceTable::Entry {
    Id                        id = 0;
    int                       refs = 1;
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

ResourceTable::ResourceTable() = default;
ResourceTable::~ResourceTable() = default;

ResourceTable::Id ResourceTable::createAudioBuffer(std::uint32_t channels, std::uint32_t frames, double sampleRate) {
    auto e = std::make_unique<Entry>();
    e->id = next_++;
    e->type = "audio-buffer";
    e->samples.assign(static_cast<std::size_t>(channels) * frames, 0.0f);
    for (std::uint32_t c = 0; c < channels; ++c) e->channelPointers.push_back(e->samples.data() + std::size_t(c) * frames);
    e->audio = rp_audio_buffer{channels, frames, sampleRate, e->channelPointers.data()};
    e->resource = rp_resource{e->type.c_str(), &e->audio};
    const Id id = e->id;
    entries_.push_back(std::move(e));
    return id;
}

ResourceTable::Id ResourceTable::createBytes(std::string type, std::vector<std::uint8_t> bytes) {
    auto e = std::make_unique<Entry>();
    e->id = next_++;
    e->type = std::move(type);
    e->bytes = std::move(bytes);
    e->bytesView = rp_bytes{e->bytes.data(), e->bytes.size()};
    e->resource = rp_resource{e->type.c_str(), &e->bytesView};
    const Id id = e->id;
    entries_.push_back(std::move(e));
    return id;
}

ResourceTable::Entry* ResourceTable::find(Id id) const {
    for (const auto& e : entries_)
        if (e->id == id) return e.get();
    return nullptr;
}

rp_resource* ResourceTable::get(Id id) const {
    Entry* e = find(id);
    return e ? &e->resource : nullptr;
}

void ResourceTable::retain(Id id) {
    if (Entry* e = find(id)) ++e->refs;
}

void ResourceTable::release(Id id) {
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const auto& e) { return e->id == id; });
    if (it == entries_.end()) return;
    if (--(*it)->refs <= 0) entries_.erase(it);
}

std::size_t ResourceTable::liveCount() const { return entries_.size(); }

}  // namespace rp::graph
