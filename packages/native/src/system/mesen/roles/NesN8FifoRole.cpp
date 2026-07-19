#include "system/mesen/roles/NesN8FifoRole.hpp"

#include <algorithm>
#include <cstdio>

#include "Core/NES/NesConsole.h"
#include "Core/NES/NesMemoryManager.h"

NesN8FifoRole::NesN8FifoRole() = default;
NesN8FifoRole::~NesN8FifoRole() = default;

void NesN8FifoRole::onAttach(NesConsole& console) {
    auto* memMgr = console.GetMemoryManager();
    if (!memMgr) {
        std::fprintf(stderr, "[NesN8FifoRole] no memory manager available; attach failed\n");
        return;
    }
    memMgr->RegisterIODevice(&fifo_);
    std::fprintf(stderr, "[NesN8FifoRole] FIFO attached at $40F0/$40F1\n");
}

void NesN8FifoRole::pushBytes(std::uint32_t offset, const std::uint8_t* data, std::size_t count) {
    if (data == nullptr || count == 0) return;
    // Queue (don't deliver) each byte at `offset`. Contiguous + ordered, so pumpUntil releases the whole
    // run together once its offset is reached. No framing/cap — the raw twin of onMidi.
    for (std::size_t b = 0; b < count; ++b) {
        pending_.push_back({ offset, data[b] });
    }
    needsSort_ = true;  // a second feed may enqueue after MIDI with an earlier offset — re-sort before draining
}

void NesN8FifoRole::onMidi(const ::MidiEvent* events, std::uint32_t count) {
    if (events == nullptr || count == 0) return;
    // MIDI-framed adapter over pushBytes: each event's bytes share ev.frame (clamped to the inline size).
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto& ev = events[i];
        const std::uint32_t size = ev.size > ::MidiEvent::kDataSize ? ::MidiEvent::kDataSize : ev.size;
        pushBytes(ev.frame, ev.data, size);
    }
}

void NesN8FifoRole::pumpUntil(std::uint32_t sampleOffset) {
    if (needsSort_) {
        // Stable so bytes of one message (same offset, contiguous) stay together + in order; runs once per
        // block (all enqueuing precedes the step loop), after which this is a plain front-drain.
        std::stable_sort(pending_.begin(), pending_.end(),
                         [](const PendingByte& a, const PendingByte& b) { return a.offset < b.offset; });
        needsSort_ = false;
    }
    while (!pending_.empty() && pending_.front().offset <= sampleOffset) {
        fifo_.pushByte(pending_.front().byte);
        pending_.pop_front();
    }
}

void NesN8FifoRole::rebase(std::uint32_t frames) {
    for (auto& e : pending_) {
        e.offset = (e.offset > frames) ? e.offset - frames : 0;
    }
}
