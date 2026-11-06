#include "graph/PackageMemory.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace rp::graph {

namespace {

void writeError(rp_error* err, const char* name, const char* what, const char* why) {
    if (err) std::snprintf(err->message, sizeof err->message, "the \"%.48s\" pack%s%.160s", name, what, why);
}

}  // namespace

void* PackageMemory::memory(void* ctx, const rp_package* package, rp_error* err) {
    auto* self = static_cast<PackageMemory*>(ctx);
    if (!package || !package->memory) {
        if (err) std::snprintf(err->message, sizeof err->message, "%s", "the package has no memory of its own");
        return nullptr;
    }
    const rp_package_memory& hooks = *package->memory;
    const char* name = hooks.name ? hooks.name : "unnamed";
    const std::lock_guard<std::mutex> lock(self->mutex_);
    for (const Block& b : self->blocks_)
        if (b.package == package) return b.memory;
    if (!hooks.query || !hooks.setup || !hooks.teardown) {
        writeError(err, name, " has package memory without its query, setup and teardown", "");
        return nullptr;
    }
    rp_memory_req req{};
    rp_error why{};
    if (!hooks.query(package, &req, &why)) {
        writeError(err, name, "'s memory cannot be sized: ", why.message);
        return nullptr;
    }
    std::size_t align = req.align ? req.align : alignof(std::max_align_t);
    if ((align & (align - 1)) != 0) {
        writeError(err, name, " asked for an alignment that is not a power of two", "");
        return nullptr;
    }
    const std::size_t size = req.size ? req.size : 1;
    // calloc's zeroes, where it can align the block: a fresh mapping's pages, never written here.
    const bool zeroed = align <= alignof(std::max_align_t);
    void* block = zeroed ? std::calloc(1, size) : ::operator new(size, std::align_val_t(align), std::nothrow);
    if (!block) {
        writeError(err, name, "'s memory could not be allocated", "");
        return nullptr;
    }
    if (!zeroed) std::memset(block, 0, size);
    const Block made{package, block, zeroed ? 0 : align};
    if (!hooks.setup(package, block, &why)) {
        release(made);
        writeError(err, name, " would not set up in its memory: ", why.message);
        return nullptr;
    }
    self->blocks_.push_back(made);
    return block;
}

void PackageMemory::release(const Block& b) {
    if (b.align == 0) std::free(b.memory);
    else ::operator delete(b.memory, std::align_val_t(b.align));
}

void PackageMemory::clear() {
    const std::lock_guard<std::mutex> lock(mutex_);
    while (!blocks_.empty()) {
        const Block b = blocks_.back();
        blocks_.pop_back();
        b.package->memory->teardown(b.package, b.memory);
        release(b);
    }
}

std::size_t PackageMemory::size() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return blocks_.size();
}

}  // namespace rp::graph
