#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "graph/rp_node.h"

namespace rp::graph {

// Resources: typed blocks of data that live outside the wires (section 2.5). Created, pinned, unpinned and
// freed on the CONTROL thread only; one table belongs to one host's control thread and is not shared.
//
// A resource has one owner, its creator (TS, through putResource), and any number of pins, one per plan
// binding it. It is freed once the owner has dropped it AND the last pin is gone. The two are counted
// apart so a duplicate drop (a dispose run twice, a retried RPC) cannot take a plan's pin with it: the
// audio thread only ever sees rp_resource pointers a plan has pinned, so it never holds the last one.
//
// Ids count up from 1 and are never reused, so a stale "res:N" is refused as unknown rather than binding
// whatever took its place. Id 0 is never a resource.
class ResourceTable {
public:
    using Id = std::uint32_t;

    ResourceTable();
    ResourceTable(const ResourceTable&) = delete;
    ResourceTable& operator=(const ResourceTable&) = delete;
    ~ResourceTable();  // asserts (debug builds) that every pin has dropped

    // A zeroed "audio-buffer" (mutable: a recorder may write it). Returns 0, with a reason, for zero
    // channels or frames, a sample rate that is not a positive number, or a buffer beyond kMaxAudioSamples.
    Id createAudioBuffer(std::uint32_t channels, std::uint32_t frames, double sampleRate, std::string* error = nullptr);
    // An immutable byte resource of the given type ("bytes", "string", or a package's type). Returns 0, with
    // a reason, for a type that is not a plain id (see isPlainId) or is "audio-buffer" (not a bytes payload).
    Id createBytes(std::string type, std::vector<std::uint8_t> bytes, std::string* error = nullptr);

    // Null unless the resource exists and its owner still holds it. A dropped resource stays alive while
    // plans pin it, but a new document cannot bind it: whether that binding planned would otherwise depend
    // on when the old plan happened to be reclaimed.
    rp_resource* get(Id id) const;
    // The owner's release. Idempotent: false (and nothing changes) for an unknown or already dropped id.
    bool drop(Id id);
    // A plan's hold. pin is false for an id get() would refuse; unpin is false for an id that holds no pin.
    bool          pin(Id id);
    bool          unpin(Id id);
    std::uint32_t pinCount(Id id) const;
    std::size_t   liveCount() const { return entries_.size(); }

    static constexpr std::uint64_t kMaxAudioSamples = std::uint64_t(1) << 28;  // 1 GiB of float

private:
    struct Entry;
    Id   insert(std::unique_ptr<Entry> e, std::string* error);
    void freeIfUnused(Id id);
    std::unordered_map<Id, std::unique_ptr<Entry>> entries_;
    Id                                             next_ = 1;
};

}  // namespace rp::graph
