#pragma once

#include <cstdint>

// The Mega Drive's button identifiers, keyed by the wire byte SystemBase::pressButton takes. Position-aligned with every
// other console's table (Right=0 ... Start=7), and the 6-button pad's extra buttons after them, exactly as the GBA's L
// and R are.
//
//   Select (6) is the Mega Drive's C button: the pad has A, B and C, and the shared table's sixth position is a face
//   button on every console whose pad has a third one. That is a choice, and it is the one the JS bridge's name table
//   makes unavoidable; the UI labels it per platform.
//
// BlastEm's own enum is {BUTTON_INVALID, DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT, BUTTON_A, BUTTON_B, BUTTON_C,
// BUTTON_START, BUTTON_X, BUTTON_Y, BUTTON_Z, BUTTON_MODE} (io.h:129-142, NUM_GAMEPAD_BUTTONS last), and
// io_gamepad_down takes that ordering, so BlastemMdSystem maps this table onto it explicitly rather than casting.
enum class MdButton : std::uint8_t {
    Right  = 0,
    Left   = 1,
    Up     = 2,
    Down   = 3,
    A      = 4,
    B      = 5,
    C      = 6,  // the shared table's Select slot
    Start  = 7,
    X      = 8,
    Y      = 9,
    Z      = 10,
    Mode   = 11,
    Count  = 12,
};
