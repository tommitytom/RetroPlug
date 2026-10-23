#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "graph/rp_node.h"

namespace rp::graph {

// Resources: typed blocks of data that live outside the wires (section 2.5). Created, retained, released
// and freed on the CONTROL thread only. The audio thread only ever sees rp_resource pointers that a plan
// has pinned (retained), so it can pass them around but never holds the last reference.
class ResourceTable {
public:
    using Id = std::uint32_t;

    ResourceTable();
    ResourceTable(const ResourceTable&) = delete;
    ResourceTable& operator=(const ResourceTable&) = delete;
    ~ResourceTable();

    // A zeroed "audio-buffer" (mutable: a recorder may write it). Starts with one reference, the caller's.
    Id createAudioBuffer(std::uint32_t channels, std::uint32_t frames, double sampleRate);
    // An immutable byte resource of the given type ("bytes", "string", or a package type).
    Id createBytes(std::string type, std::vector<std::uint8_t> bytes);

    rp_resource* get(Id id) const;
    void         retain(Id id);
    void         release(Id id);  // frees at zero
    std::size_t  liveCount() const;

private:
    struct Entry;
    Entry* find(Id id) const;
    std::vector<std::unique_ptr<Entry>> entries_;
    Id                                  next_ = 1;
};

}  // namespace rp::graph
