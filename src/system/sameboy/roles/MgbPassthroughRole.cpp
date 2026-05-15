#include "system/sameboy/roles/MgbPassthroughRole.hpp"

#include "system/sameboy/SameBoySystem.hpp"

void MgbPassthroughRole::onMidi(SameBoySystem& system,
                                const ::MidiEvent* events,
                                std::uint32_t      count) {
    if (events == nullptr || count == 0) return;
    for (std::uint32_t i = 0; i < count; ++i) {
        const ::MidiEvent& ev = events[i];
        // SysEx (size > kDataSize) is carried via dataExt and not meaningful
        // to the GB serial port without per-byte chunking; skip it.
        if (ev.size == 0 || ev.size > ::MidiEvent::kDataSize) continue;
        for (std::uint32_t b = 0; b < ev.size; ++b) {
            system.serialIn_.push_back(ev.data[b]);
        }
    }
}
