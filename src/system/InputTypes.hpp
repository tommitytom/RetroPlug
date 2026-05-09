#pragma once

#include <cstdint>

// Game Boy button identifiers. Values match SameBoy's GB_key_t (see
// deps/sameboy/Core/joypad.h) so SameBoySystem can pass them through without a
// translation layer. When other system kinds (Mesen, etc.) land they get their
// own button enums; the per-system command variant routes the right type to
// the right system.
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
