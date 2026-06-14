#pragma once

#include <cstdint>

// PS/2 scancode tables used by LSDJ's keyboard input modes (KEYBD in PROJECT
// → SYNC). Verbatim port of the tables at
// `old/src/lsdj/LsdjAudioHooks.cpp:10-38`. LSDJ reads these scancodes via the
// link cable and treats them as if a PS/2 keyboard were attached.
//
// Layout:
//   kKeyboardNoteStart .. +24  → kKeyboardNoteMap (2 octaves, note keys)
//   kKeyboardLowStart  .. +12  → kKeyboardLowOctaveMap (mute/cursor/enter/table)
//
// The "extended" scancode prefix (0xE0) is required before cursor keys —
// see the helper that consumers can call before emitting cursor commands.
namespace lsdj {

inline constexpr std::uint8_t kKeyboardNoteStart = 48; // MIDI C-3
inline constexpr std::uint8_t kKeyboardLowStart  = 36; // MIDI C-2

inline constexpr std::uint8_t kKeyboardNoteMap[24] = {
    0x1A, 0x1B, 0x22, 0x23, 0x21, 0x2A, 0x34, 0x32, 0x33, 0x31, 0x3B, 0x3A,
    0x15, 0x1E, 0x1D, 0x26, 0x24, 0x2D, 0x2E, 0x2C, 0x36, 0x35, 0x3D, 0x3C,
};

inline constexpr std::uint8_t kKeyboardLowOctaveMap[12] = {
    0x01, // Mute1
    0x09, // Mute2
    0x78, // Mute3
    0x07, // Mute4
    0x68, // Cursor Left
    0x74, // Cursor Right
    0x75, // Cursor Up
    0x72, // Cursor Down
    0x5A, // Enter
    0x7A, // Table Up
    0x7D, // Table Down
    0x29, // Table Cue
};

inline constexpr std::uint8_t kKeyboardOctDn = 0x05;
inline constexpr std::uint8_t kKeyboardOctUp = 0x06;
inline constexpr std::uint8_t kKeyboardInsDn = 0x04;
inline constexpr std::uint8_t kKeyboardInsUp = 0x0C;
inline constexpr std::uint8_t kKeyboardTblDn = 0x03;
inline constexpr std::uint8_t kKeyboardTblUp = 0x0B;

// Returns true if `scancode` is one of the cursor codes that requires the
// 0xE0 "extended" prefix to be sent before it. Matches the four cursor
// entries in kKeyboardLowOctaveMap.
inline constexpr bool isExtendedScancode(std::uint8_t scancode) {
    return scancode == 0x68 || scancode == 0x72 || scancode == 0x74 || scancode == 0x75;
}

} // namespace lsdj
