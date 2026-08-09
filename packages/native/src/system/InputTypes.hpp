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
// (see deps/mesen/Core/NES/Input/NesController.h) so MesenNesSystem can pass
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

// GBA buttons. The shared 8 names keep the position-aligned wire bytes used
// across all kinds (Right=0..Start=7) so the CLI script parser, JS bridge,
// and any other "name → button" path can stay system-agnostic. L and R are
// GBA-only and live at the end of the table.
//
// Note: Mesen's own GbaController::Buttons enum has a different order
// (Up=0, Down=1, Left=2, Right=3, Start=4, Select=5, B=6, A=7, L=8, R=9 —
// see deps/mesen/Core/GBA/Input/GbaController.h). MesenGbaSystem::pressButton
// does the explicit remap from this wire byte to Mesen's native enum.
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

// Master System / Game Gear buttons. Position-aligned with the other kinds
// (Right=0..Start=7) so the shared name table keeps working, but the pad only
// has six of them: a d-pad plus two face buttons. Two consequences worth
// knowing before wiring anything to this enum:
//
//   Select has no hardware equivalent at all. MesenSmsSystem::pressButton
//   drops it rather than folding it onto a face button, or a Select tap would
//   spuriously fire button 2.
//
//   Start is not a pad button on either machine. On Master System it is the
//   console's Pause switch, which drives the Z80 NMI (deps/mesen/Core/SMS/
//   SmsVdp.cpp:600-602); on Game Gear the same bit reads as Start at port $00
//   bit 7 (SmsMemoryManager.cpp:456-464). One wire byte, two behaviours - the
//   settings UI wants different labels per platform even though this enum is
//   shared.
//
// Mesen's own SmsController::Buttons is ordered {Up=0, Down, Left, Right, B,
// A, Pause} (deps/mesen/Core/SMS/Input/SmsController.h:58), so the remap in
// MesenSmsSystem is an explicit switch, not a cast.
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
