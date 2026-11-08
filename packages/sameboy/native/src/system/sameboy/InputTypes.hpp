#pragma once

#include <cstdint>

// The Game Boy's button identifiers. Values match SameBoy's GB_key_t (deps/sameboy/Core/joypad.h) so SameBoySystem
// passes them through without a translation layer. Position-aligned with the other consoles' tables (Right=0, ...,
// Start=7), which is what lets the CLI's script parser and the JS bridge use one name table and each console
// reinterpret the byte.
enum class GameboyButton : std::uint8_t {
    Right  = 0,
    Left   = 1,
    Up     = 2,
    Down   = 3,
    A      = 4,
    B      = 5,
    Select = 6,
    Start  = 7,
    Count  = 8,
};
