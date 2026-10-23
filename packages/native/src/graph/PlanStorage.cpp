#include "graph/PlanStorage.hpp"

#include <cassert>
#include <new>

namespace rp::graph {

StreamStore::StreamStore(std::uint32_t capacity, double start) : changes(capacity) {
    queue.start = start;
    queue.changes = changes.data();
    queue.capacity = capacity;
}

QueueStore::QueueStore(std::uint32_t itemCapacity, std::uint32_t byteCapacity)
    : items(itemCapacity), bytes(byteCapacity) {
    queue.items = items.data();
    queue.capacity = itemCapacity;
    queue.bytes = bytes.data();
    queue.bytes_capacity = byteCapacity;
}

Instance::~Instance() {
    if (node) {
        if (active) node->vt->deactivate(node);
        entry->type->destroy(node);
    }
    if (memory) ::operator delete(memory, std::align_val_t(align));  // also when create failed
}

PlanStorage::~PlanStorage() {
    for (auto& c : commits) {
        if (c.committed) {
            if (c.old) c.node->vt->release_settings(c.node, c.old);
        } else if (c.prepared) {
            c.node->vt->release_settings(c.node, c.prepared);
        }
    }
    if (resources)
        for (const auto id : pins) {
            [[maybe_unused]] const bool held = resources->unpin(id);
            assert(held && "a plan unpinned a resource it never pinned");
        }
}

void PlanStorage::adopt() {
    for (auto& c : commits) {
        c.node->vt->commit_settings(c.node, c.prepared, &c.old);
        c.committed = true;
    }
    for (auto& p : peers) p.link->set_peers(p.node, p.peers.data(), static_cast<std::uint32_t>(p.peers.size()));
    for (auto& p : portsChanged)
        p.node->vt->ports_changed(p.node, p.mask.data(), static_cast<std::uint32_t>(p.mask.size()));
    for (auto& s : paramSets) s.queue->start = s.value;
}

}  // namespace rp::graph
