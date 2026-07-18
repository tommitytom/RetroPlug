#include "system/mesen/roles/NesN8MidiRole.hpp"

#include <cstdio>

#include "Core/NES/NesConsole.h"
#include "Core/NES/NesMemoryManager.h"

NesN8MidiRole::NesN8MidiRole() = default;
NesN8MidiRole::~NesN8MidiRole() = default;

void NesN8MidiRole::onAttach(NesConsole& console) {
    auto* memMgr = console.GetMemoryManager();
    if (!memMgr) {
        std::fprintf(stderr, "[NesN8MidiRole] no memory manager available; attach failed\n");
        return;
    }
    memMgr->RegisterIODevice(&fifo_);
    std::fprintf(stderr, "[NesN8MidiRole] FIFO attached at $40F0/$40F1\n");
}

void NesN8MidiRole::onMidi(const ::MidiEvent* events, std::uint32_t count) {
    if (events == nullptr || count == 0) return;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto& ev = events[i];
        const std::uint32_t size = ev.size > ::MidiEvent::kDataSize ? ::MidiEvent::kDataSize : ev.size;
        // Queue (don't deliver) each byte tagged with the event's intra-block offset. All bytes of one
        // event share ev.frame and stay contiguous, so pumpUntil releases the whole message together and
        // in order once its offset is reached.
        for (std::uint32_t b = 0; b < size; ++b) {
            pending_.push_back({ ev.frame, ev.data[b] });
        }
    }
}

void NesN8MidiRole::pumpUntil(std::uint32_t sampleOffset) {
    while (!pending_.empty() && pending_.front().offset <= sampleOffset) {
        fifo_.pushByte(pending_.front().byte);
        pending_.pop_front();
    }
}

void NesN8MidiRole::rebase(std::uint32_t frames) {
    for (auto& e : pending_) {
        e.offset = (e.offset > frames) ? e.offset - frames : 0;
    }
}
