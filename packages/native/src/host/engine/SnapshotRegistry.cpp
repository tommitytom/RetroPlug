#include "host/engine/SnapshotRegistry.hpp"

#include <cstdio>
#include <cstring>

#include "project/Project.hpp"
#include "system/SystemBase.hpp"
#include "system/MemoryType.hpp"   // rp::MemoryType::Sram

// Write [len:4 LE][payload] into a state triple's next slot and publish it. `payload`/`len` must fit
// the triple (prefix + len <= triple size) — checked by the callers. Matches SystemBase's own layout.
void SnapshotRegistry::writeState(MemorySnapshotTriple& triple, const std::uint8_t* payload, std::size_t len) {
    std::uint8_t* slot = triple.writeSlot();
    const std::uint32_t len32 = static_cast<std::uint32_t>(len);
    std::memcpy(slot, &len32, sizeof(len32));
    std::memcpy(slot + kStateLenPrefix, payload, len);
    triple.publish();
}

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
        std::fprintf(stderr, "[retroplug] snapshot registry full (%zu slots); construct dropped\n", kMaxSlots);
        return false;
    }

    // Reset the slot; it may be a reused free one.
    s->width = s->height = 0;
    s->frame.reset();
    s->state.reset();
    s->sram.reset();
    s->sramOffset = 0;
    s->sramFromCore = false;
    s->sampleAccum = 0;

    // Frame: sized from the core's framebuffer (absent → non-video system). Left UNpublished — the
    // core hasn't rendered yet, so getFrame reports published:false until publishAll copies frame 1.
    if (FrameBufferTriple* fb = sys.framebuffer()) {
        s->width = fb->width();
        s->height = fb->height();
        s->frame = std::make_unique<FrameBufferTriple>(s->width, s->height);
    }

    // State + SRAM: seed from the live savestate so a read right after construct (no block yet)
    // returns real bytes. The slot is sized to the live snapshot triple's PAYLOAD CAPACITY (which
    // carries per-core headroom — Mesen savestates grow within a session) plus the length prefix, so
    // the coarse-interval republish never overflows it. `sys` must already have enableStateSnapshot()'d.
    const std::vector<std::uint8_t> savestate = sys.saveStateBytes();
    const std::size_t bootLen = savestate.size();
    const std::size_t cap     = sys.stateSnapshotCapacity();
    if (bootLen > 0 && cap >= bootLen && cap <= kMaxStateBytes) {
        s->state = std::make_unique<MemorySnapshotTriple>(kStateLenPrefix + cap);
        writeState(*s->state, savestate.data(), bootLen);
    }

    // SRAM has two sources. SameBoy exposes its cart RAM as a region WITHIN the savestate (same layout
    // live + seeded), so slice it out — cheap, no extra core read. A core whose savestate exposes no
    // SRAM region (Mesen's streamed format leaves stateRegions() empty) publishes its battery straight
    // from the live core via saveSramBytes() — sourced HERE on the control thread, republished on the
    // coarse interval. A no-battery cart reports empty → no slot → readSram stays null (correct).
    const auto& reg = sys.stateRegions()[static_cast<std::size_t>(rp::MemoryType::Sram)];
    if (reg.size > 0 && static_cast<std::size_t>(reg.offset) + reg.size <= bootLen) {
        s->sramOffset = reg.offset;
        s->sram = std::make_unique<MemorySnapshotTriple>(reg.size);
        std::memcpy(s->sram->writeSlot(), savestate.data() + reg.offset, reg.size);
        s->sram->publish();
    } else if (reg.size == 0) {
        const std::vector<std::uint8_t> sram = sys.saveSramBytes();
        if (!sram.empty() && sram.size() <= kMaxSramBytes) {
            s->sramFromCore = true;
            s->sram = std::make_unique<MemorySnapshotTriple>(sram.size());
            std::memcpy(s->sram->writeSlot(), sram.data(), sram.size());
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
            // Republish the fresh savestate. The slot carries headroom (prefix + capacity), so a
            // grown Mesen savestate still fits; only a capture that would overflow it is skipped —
            // NOT the old exact-size (==) match, which froze every variable-size republish.
            if (publishScratch_.size() + kStateLenPrefix <= s->state->size())
                writeState(*s->state, publishScratch_.data(), publishScratch_.size());
            // SameBoy SRAM slice (payload-relative offset within the fresh savestate).
            if (s->sram && !s->sramFromCore &&
                static_cast<std::size_t>(s->sramOffset) + s->sram->size() <= publishScratch_.size()) {
                std::memcpy(s->sram->writeSlot(), publishScratch_.data() + s->sramOffset, s->sram->size());
                s->sram->publish();
            }
        }

        // Core-sourced SRAM (Mesen): read the live battery straight off the core — safe here, the block
        // thread owns it and every system has finished its block (publishAll is the tail of processBlock,
        // like captureStateSnapshot). SRAM size is fixed for a cart, so a mismatch just skips. Independent
        // of the savestate read above (a no-savestate core could still carry a battery).
        if (s->sram && s->sramFromCore) {
            sramScratch_ = sys->saveSramBytes();
            if (sramScratch_.size() == s->sram->size()) {
                std::memcpy(s->sram->writeSlot(), sramScratch_.data(), sramScratch_.size());
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
    const std::size_t slotSize = s->state->size();
    if (stateReadScratch_.size() < slotSize) stateReadScratch_.resize(slotSize);
    if (!s->state->readInto(stateReadScratch_.data(), slotSize)) return std::nullopt;
    std::uint32_t len = 0;
    std::memcpy(&len, stateReadScratch_.data(), sizeof(len));
    if (len == 0 || static_cast<std::size_t>(len) + kStateLenPrefix > slotSize) return std::nullopt;
    return std::vector<std::uint8_t>(stateReadScratch_.begin() + kStateLenPrefix,
                                     stateReadScratch_.begin() + kStateLenPrefix + len);
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
    s->sramFromCore = false;
    s->sampleAccum = 0;
}
