// PS/2 scancode tables for LSDj's keyboard input mode (KEYBD in PROJECT → SYNC), used by the
// lsdj-sync `KeyboardMidi` behaviour to translate incoming MIDI notes into the scancodes LSDj reads
// over the link cable as if a PS/2 keyboard were attached. Verbatim port of the native
// packages/native/src/system/sameboy/roles/LsdjKeyboardMap.hpp.
//
//   NOTE_START .. +24  → NOTE_MAP        (2 octaves of note keys)
//   LOW_START  .. +12  → LOW_OCTAVE_MAP  (mute / cursor / enter / table)
//
// Cursor keys must be preceded by the 0xE0 "extended" prefix — isExtendedScancode() flags them.

export const KEYBOARD_NOTE_START = 48; // MIDI C-3
export const KEYBOARD_LOW_START = 36; // MIDI C-2

export const KEYBOARD_NOTE_MAP: readonly number[] = [
  0x1a, 0x1b, 0x22, 0x23, 0x21, 0x2a, 0x34, 0x32, 0x33, 0x31, 0x3b, 0x3a,
  0x15, 0x1e, 0x1d, 0x26, 0x24, 0x2d, 0x2e, 0x2c, 0x36, 0x35, 0x3d, 0x3c,
];

export const KEYBOARD_LOW_OCTAVE_MAP: readonly number[] = [
  0x01, // Mute1
  0x09, // Mute2
  0x78, // Mute3
  0x07, // Mute4
  0x68, // Cursor Left
  0x74, // Cursor Right
  0x75, // Cursor Up
  0x72, // Cursor Down
  0x5a, // Enter
  0x7a, // Table Up
  0x7d, // Table Down
  0x29, // Table Cue
];

export const KEYBOARD_OCT_DN = 0x05;
export const KEYBOARD_OCT_UP = 0x06;

/** True for the four cursor scancodes that require the 0xE0 "extended" prefix before them. */
export function isExtendedScancode(scancode: number): boolean {
  return scancode === 0x68 || scancode === 0x72 || scancode === 0x74 || scancode === 0x75;
}
