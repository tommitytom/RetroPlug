#include "SnapshotRegistry.hpp"

#include <cstdio>
#include <cstring>

#include "project/Project.hpp"
#include "system/SystemBase.hpp"
#include "system/MemoryType.hpp"   // rp::MemoryType::Sram

SnapshotRegistry::Slot* SnapshotRegistry::find(SystemId id) {
    for (auto& s : slots_)
        if (s.id.load(std::memory_order_acquire) == id) return &s;
    return nullptr;
}

SnapshotRegistry::Slot* SnapshotRegistry::findFree() {
    for (auto& s : slots_)
        if (s.id.load(std::memory_order_acquire) == 0) return &s;
    return nullptr;
}

bool SnapshotRegistry::claim(SystemId id, SystemBase& sys) {
    Slot* s = findFree();
    if (!s) {
        std::fprintf(stderr, "[greenfield] snapshot registry full (%zu slots); construct dropped\n", kMaxSlots);
        return false;
    }

    // Reset the slot; it may be a reused free one.
    s->width = s->height = 0;
    s->frame.reset();
    s->state.reset();
    s->sram.reset();
    s->sramOffset = 0;
    s->sampleAccum = 0;

    // Frame: sized from the core's framebuffer (absent → non-video system). Left UNpublished — the
    // core hasn't rendered yet, so getFrame reports published:false until publishAll copies frame 1.
    if (FrameBufferTriple* fb = sys.framebuffer()) {
        s->width = fb->width();
        s->height = fb->height();
        s->frame = std::make_unique<FrameBufferTriple>(s->width, s->height);
    }

    // State + SRAM: seed from the live savestate so a read right after construct (no block yet)
    // returns real bytes. saveStateBytes() and the per-block readStateSnapshot() are the same
    // GB_save_state_to_buffer layout, so stateRegions()' SRAM offset slices both identically.
    const std::vector<std::uint8_t> savestate = sys.saveStateBytes();
    const std::size_t stateSize = savestate.size();
    if (stateSize > 0 && stateSize <= kMaxStateBytes) {
        s->state = std::make_unique<MemorySnapshotTriple>(stateSize);
        std::memcpy(s->state->writeSlot(), savestate.data(), stateSize);
        s->state->publish();

        const auto& reg = sys.stateRegions()[static_cast<std::size_t>(rp::MemoryType::Sram)];
        if (reg.size > 0 && static_cast<std::size_t>(reg.offset) + reg.size <= savestate.size()) {
            s->sramOffset = reg.offset;
            s->sram = std::make_unique<MemorySnapshotTriple>(reg.size);
            std::memcpy(s->sram->writeSlot(), savestate.data() + reg.offset, reg.size);
            s->sram->publish();
        }
    }

    s->id.store(id, std::memory_order_release);   // publish the slot LAST — the block thread may now match it
    return true;
}

void SnapshotRegistry::publishAll(const Project& project, std::uint32_t frames, double sampleRate) {
    const std::uint64_t interval = sampleRate > 0.0
        ? static_cast<std::uint64_t>(kStateIntervalSec * sampleRate)
        : 0;

    for (const auto& up : project.systems()) {
        SystemBase* sys = up.get();
        if (!sys) continue;
        Slot* s = find(sys->id());
        if (!s) continue;   // e.g. a system built before this registry existed / pool was full

        // Frame every block (cheap): copy the core's latest published frame into the owned triple.
        if (s->frame) {
            if (FrameBufferTriple* fb = sys->framebuffer()) {
                const std::uint32_t px = s->width * s->height;
                if (fb->readInto(s->frame->writeSlot(), px))
                    s->frame->publish();
            }
        }

        // State + SRAM on the coarse interval — the core only republishes its savestate every ~0.5s,
        // so per-block copies would be redundant. Matches publishStateSnapshot's cadence.
        s->sampleAccum += frames;
        if (interval == 0 || s->sampleAccum < interval) continue;
        s->sampleAccum = 0;

        if (s->state && sys->readStateSnapshot(publishScratch_) && !publishScratch_.empty()) {
            if (publishScratch_.size() == s->state->size()) {
                std::memcpy(s->state->writeSlot(), publishScratch_.data(), publishScratch_.size());
                s->state->publish();
            }
            if (s->sram &&
                static_cast<std::size_t>(s->sramOffset) + s->sram->size() <= publishScratch_.size()) {
                std::memcpy(s->sram->writeSlot(), publishScratch_.data() + s->sramOffset, s->sram->size());
                s->sram->publish();
            }
        }
    }
}

SnapshotRegistry::Frame SnapshotRegistry::readFrame(SystemId id) {
    Frame out;
    Slot* s = find(id);
    if (!s || !s->frame) return out;   // unknown / non-video → width 0
    out.width = s->width;
    out.height = s->height;
    const std::size_t px = static_cast<std::size_t>(s->width) * s->height;
    std::vector<std::uint32_t> tmp(px);
    out.published = s->frame->readInto(tmp.data(), static_cast<std::uint32_t>(px));
    if (out.published) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(tmp.data());
        out.data.assign(p, p + px * 4);
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> SnapshotRegistry::readState(SystemId id) {
    Slot* s = find(id);
    if (!s || !s->state) return std::nullopt;
    std::vector<std::uint8_t> out;
    if (!s->state->readInto(out)) return std::nullopt;
    return out;
}

std::optional<std::vector<std::uint8_t>> SnapshotRegistry::readSram(SystemId id) {
    Slot* s = find(id);
    if (!s || !s->sram) return std::nullopt;
    std::vector<std::uint8_t> out;
    if (!s->sram->readInto(out)) return std::nullopt;
    return out;
}

void SnapshotRegistry::release(SystemId id) {
    if (id == 0) return;
    Slot* s = find(id);
    if (!s) return;
    // Clear the id FIRST so a stray block-thread scan can't match this slot, then free the buffers.
    // release only runs once the system is out of project.systems() (audio no longer publishes to
    // it), so this can't race an in-flight publishAll.
    s->id.store(0, std::memory_order_release);
    s->frame.reset();
    s->state.reset();
    s->sram.reset();
    s->width = s->height = 0;
    s->sramOffset = 0;
    s->sampleAccum = 0;
}
