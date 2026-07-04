#pragma once

#include <cstdint>
#include <span>
#include <string_view>

// ROMs baked into the binary at build time (see cmake bin2c step + the
// generated build/generated/roms/mgb_rom_data.c). Exposed through one accessor
// so both the menu loader (PluginRpcService::constructSystem, embeddedRom="mgb")
// and the project loader
// (Project::addSystem, re-supplying bytes a thin .rplg stripped) read the same
// array — linked once, not duplicated per translation unit.

extern "C" {
    extern const unsigned char mgb_rom[];
    extern const unsigned int  mgb_rom_len;
}

namespace rp {

// Bytes of the embedded mGB Game Boy MIDI-synth ROM.
inline std::span<const std::uint8_t> embeddedMgbRom() {
    return { reinterpret_cast<const std::uint8_t*>(mgb_rom), mgb_rom_len };
}

// Look up an embedded ROM by id. Empty span for unknown ids. The id is stored
// in SameBoyConfig::embeddedRom so a saved project can re-supply the bytes.
inline std::span<const std::uint8_t> embeddedRom(std::string_view id) {
    if (id == "mgb") return embeddedMgbRom();
    return {};
}

} // namespace rp
