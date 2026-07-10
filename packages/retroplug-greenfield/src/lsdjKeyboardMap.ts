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
  0x6b, // Cursor Left  (textbook PS/2 left-arrow make code; jkotlinski/keyjazz confirms 0x6b, not 0x68)
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
  return scancode === 0x6b || scancode === 0x72 || scancode === 0x74 || scancode === 0x75;
}

/** LSDj reads a PS/2 keyboard over the link cable in EXTERNAL-clock mode, and the GB serial mangles
 *  each incoming PS/2 scancode: it keeps the low 7 bits and reverses their order (per the LSDj author's
 *  jkotlinski/keyjazz — "transferred backwards … loses some bits"). So what LSDj actually decodes is
 *  this reversed-low-7-bits form, NOT the textbook PS/2 code. Every scancode we push over serial must
 *  be pre-mangled to that form (e.g. Enter 0x5A → 0x2D, the extended prefix 0xE0 → 0x03, octave-up 0x06
 *  → 0x30 — all matching keyjazz's tables). */
export function toGbSerialByte(scancode: number): number {
  let r = 0;
  for (let i = 0; i < 7; i++) r = (r << 1) | ((scancode >> i) & 1);
  return r;
}
