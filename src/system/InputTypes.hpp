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

// NES controller buttons. Values match Mesen's NesController::Buttons enum
// (see deps/mesen/Core/NES/Input/NesController.h) so MesenSystem can pass
// them through without a translation layer. SystemBase::pressButton takes
// a uint8_t so the same command queue can carry either enum; each system
// casts back to its own kind in the override.
//
// Position-aligned with GameboyButton (Right=0, …, Start=7) so the CLI
// script parser, JS bridge, and any other "name → button" path can use a
// single name table and let each system reinterpret the byte.
enum class NesButton : std::uint8_t {
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
