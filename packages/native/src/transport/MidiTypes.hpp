#pragma once

#include <cstdint>

// Shell-side MIDI event. Mirrors DPF's DISTRHO::MidiEvent shape exactly so the
// audio-thread boundary in PluginDSP::run can do a field-by-field copy with
// no transformation. Defined here (rather than reusing the DPF type) so the
// Project / SystemBase interfaces don't pull in DistrhoDetails.hpp,
// which keeps the unit tests free of the DPF link dependency.
struct MidiEvent {
    static constexpr std::uint32_t kDataSize = 4;

    std::uint32_t  frame    = 0;            // sample offset within the block
    std::uint32_t  size     = 0;            // number of MIDI bytes used
    std::uint8_t   data[kDataSize] = {0};   // inline bytes when size <= kDataSize
    const std::uint8_t* dataExt = nullptr;  // extended bytes when size > kDataSize
};
