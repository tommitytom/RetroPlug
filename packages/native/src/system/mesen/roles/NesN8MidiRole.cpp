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
        for (std::uint32_t b = 0; b < size; ++b) {
            fifo_.pushByte(ev.data[b]);
        }
    }
}
