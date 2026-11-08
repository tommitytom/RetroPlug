#pragma once

#include <cstdint>

// The Mesen consoles' button identifiers, one table per console, each keyed by the wire byte SystemBase::pressButton
// takes. Every table is position-aligned with the others (Right=0, ..., Start=7) so the CLI's script parser and the JS
// bridge use one name table and each console reinterpret the byte; a byte outside a table drives no bit at all rather
// than falling through onto one.

// NES controller buttons. Mesen's own NesController::Buttons is ordered {Up=0, Down, Left, Right, Start, Select, B, A,
// Microphone} (deps/mesen/Core/NES/Input/NesController.h:80), so the remap in MesenNesSystem is an explicit switch
// rather than a cast.
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

// GBA buttons. L and R are the Game Boy Advance's alone and live at the end of the table. Mesen's own
// GbaController::Buttons has a different order (Up=0, Down=1, Left=2, Right=3, Start=4, Select=5, B=6, A=7, L=8, R=9:
// deps/mesen/Core/GBA/Input/GbaController.h), so MesenGbaSystem does the explicit remap from this wire byte as it
// applies its queue.
enum class GbaButton : std::uint8_t {
    Right  = 0,
    Left   = 1,
    Up     = 2,
    Down   = 3,
    A      = 4,
    B      = 5,
    Select = 6,
    Start  = 7,
    L      = 8,
    R      = 9,
    Count  = 10,
};

// Master System / Game Gear buttons: a d-pad and two face buttons, so the shared table's last two bytes carry two
// consequences:
//
//   Select has no hardware equivalent at all. MesenSmsSystem::pressButton drops it rather than folding it onto a face
//   button, or a Select tap would spuriously fire button 2.
//
//   Start is not a pad button on either machine. On Master System it is the console's Pause switch, which drives the
//   Z80 NMI (deps/mesen/Core/SMS/SmsVdp.cpp:600-602); on Game Gear the same bit reads as Start at port $00 bit 7
//   (SmsMemoryManager.cpp:456-464). One wire byte, two behaviours, which is why the settings UI labels it per platform.
//
// Mesen's own SmsController::Buttons is ordered {Up=0, Down, Left, Right, B, A, Pause}
// (deps/mesen/Core/SMS/Input/SmsController.h:58), so the remap in MesenSmsSystem is an explicit switch, not a cast.
enum class SmsButton : std::uint8_t {
    Right  = 0,
    Left   = 1,
    Up     = 2,
    Down   = 3,
    A      = 4,
    B      = 5,
    Select = 6,   // no SMS/GG equivalent; dropped at apply time
    Start  = 7,   // -> Pause (SMS: NMI; GG: Start)
    Count  = 8,
};
