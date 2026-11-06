#pragma once

#include <cstddef>
#include <mutex>
#include <vector>

#include "graph/rp_node.h"

namespace rp::graph {

// Package memory as a desktop host gives it (rp_package_memory, docs/modular-graph-phase-e4-design.md section 5): a
// block of the host's own for each package with memory that one of its nodes asks for (rp.host.packages/1, which a
// node's create asks), sized, allocated and set up at that first ask, and torn down and freed when the host goes, after
// its nodes. Where the device's loader sets every package of a plan up before its first node (graph/PlanLoader.hpp),
// a desktop host plans document after document, so a package is set up when the first node that needs it is created,
// and kept for the host's life. Desktop only: it allocates. A block is handed to its setup zeroed by the allocator
// (calloc), so a large one (a script pack's context, 1 MiB on the desktop) costs only the pages its package touches.
//
// One per host: GraphHost holds one, and the exporter one for the plan it makes. The host answers
// RP_HOST_PACKAGES_INTERFACE with service(), and destroys every node before clear() (or the destructor) tears the
// packages down. memory() is the control thread's; a mutex keeps two control threads of one host from setting one
// package up twice.
class PackageMemory {
public:
    PackageMemory() = default;
    ~PackageMemory() { clear(); }
    PackageMemory(const PackageMemory&) = delete;
    PackageMemory& operator=(const PackageMemory&) = delete;

    // What the host answers rp.host.packages/1 with.
    const rp_host_packages* service() const { return &service_; }
    // Every package torn down, the last set up first, and its block freed: once no node made under them is left.
    void clear();
    // The packages set up so far.
    std::size_t size() const;

private:
    struct Block {
        const rp_package* package = nullptr;
        void*             memory = nullptr;
        std::size_t       align = 0;  // 0: calloc's, freed with free; else operator new's, at this alignment
    };
    static void* memory(void* ctx, const rp_package* package, rp_error* err);
    static void  release(const Block& b);

    mutable std::mutex mutex_;
    std::vector<Block> blocks_;
    rp_host_packages   service_{this, &PackageMemory::memory};
};

}  // namespace rp::graph
