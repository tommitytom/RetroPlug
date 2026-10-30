#pragma once

#include <cstdint>
#include <span>
#include <string_view>

// ROMs baked into the binary at build time (see cmake bin2c step + the
// generated build/generated/roms/mgb_rom_data.c). Exposed through one accessor so every path that puts an embedded ROM
// into a resource (EngineRpcService::putResource's `embedded` marker) reads the same array, linked once rather than
// duplicated per translation unit.

extern "C" {
    extern const unsigned char mgb_rom[];
    extern const unsigned int  mgb_rom_len;
}

namespace rp {

// Bytes of the embedded mGB Game Boy MIDI-synth ROM.
inline std::span<const std::uint8_t> embeddedMgbRom() {
    return { reinterpret_cast<const std::uint8_t*>(mgb_rom), mgb_rom_len };
}

// Look up an embedded ROM by id, as putResource's `embedded` spec names it. Empty span for unknown ids.
inline std::span<const std::uint8_t> embeddedRom(std::string_view id) {
    if (id == "mgb") return embeddedMgbRom();
    return {};
}

} // namespace rp
